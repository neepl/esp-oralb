// esp-oralb: live brushing display for an Oral-B iO toothbrush on the ESP32-2432S024C.
#include <Arduino.h>
#include <time.h>

#include "app_state.h"
#include "ble.h"
#include "config.h"
#include "diag.h"
#include "feedback.h"
#include "hw_feedback.h"
#include "net.h"
#include "gfx_assets.h"
#include "ui.h"

using namespace oralb;

namespace {

uint32_t last_activity_ms = 0;
uint8_t brightness = 255;
uint32_t seen_session = 0;
Scene last_drawn;
bool drawn_once = false;
uint32_t last_draw_ms = 0;
uint32_t last_heap_log_ms = 0;
Screen last_screen = Screen::CLOCK;
bool was_ota = false;
GoalTracker goal;
BacklightControl ldr_ctl(LDR_BRIGHT_RAW, LDR_DARK_RAW, BACKLIGHT_MIN, BACKLIGHT_MAX);
int ldr_level = BACKLIGHT_MAX;
uint32_t last_ldr_ms = 0, last_ldr_log_ms = 0;

void build_scene(Scene& s, double now, uint32_t ms) {
  memset((void*)&s, 0, sizeof s);  // zero padding too: Scene is compared with memcmp
  s.current_zone = -1;
  s.blink_on = true;
  s.day = 1;
  s.month = 1;
  Lock l;
  const Session& se = app.session;
  const ZoneTracker& tr = app.tracker;
  s.screen = se.screen;
  if (se.screen == Screen::BRUSHING) {
    s.secs = se.brush_seconds;
    s.high_pressure = se.has_frame && se.frame.high_pressure() && se.frame.running();
    s.blink_on = (ms / 500) % 2 == 0;  // 1 Hz blink of the current progress square
    s.zones = app.zones_linked || tr.samples > 0;
  } else if (se.screen == Screen::RESULT) {
    s.secs = se.last.seconds;
    s.zones = tr.samples > 0;  // coverage of the session that just ended (reset when the next starts)
  }
  if (s.zones) {
    s.current_zone = se.screen == Screen::BRUSHING ? (int8_t)tr.current : -1;
    for (int z = 0; z < ZONE_COUNT; z++) s.cov[z] = cov_state(tr.coverage[z]);
  }
  if (se.screen == Screen::CLOCK) {
    if (net_time_valid()) {
      time_t t = time(nullptr);
      struct tm tmv;
      localtime_r(&t, &tmv);
      s.time_valid = true;
      s.hh = tmv.tm_hour;
      s.mm = tmv.tm_min;
      s.colon = tmv.tm_sec % 2 == 0;
      s.weekday = (tmv.tm_wday + 6) % 7;  // Monday = 0
      s.day = tmv.tm_mday;
      s.month = tmv.tm_mon + 1;
    }
    WeatherData wd;
    uint32_t age_ms;
    if (net_weather(wd, age_ms) && age_ms <= WEATHER_MAX_AGE_MIN * 60000UL) {  // stale weather is hidden
      time_t t = time(nullptr);
      struct tm lt;
      localtime_r(&t, &lt);
      s.weather_valid = true;
      s.weather = weather_icon(wd, lt.tm_hour * 60 + lt.tm_min, net_time_valid());
      s.temp = (int8_t)lroundf(wd.temp_c);
    }
    AlertLevel al;
    char since[8];
    uint32_t alert_age_ms;
    if (net_alert(al, since, alert_age_ms) && alert_age_ms <= ALERT_MAX_AGE_S * 1000UL) {  // stale: hidden
      s.alert = al;
      strncpy(s.alert_since, since, sizeof s.alert_since - 1);
    }
    if (net_state() == NetState::PORTAL) snprintf(s.status, sizeof s.status, "%s", gfx::STR_WIFI_SETUP);
    else if (!se.seen_recently(now, 3600)) snprintf(s.status, sizeof s.status, "%s", gfx::STR_WAITING);
  }
}

bool is_night() {
  if (!net_time_valid()) return false;
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  return tmv.tm_hour >= NIGHT_START_H || tmv.tm_hour < NIGHT_END_H;
}

void set_backlight(uint8_t b) {
  if (b != brightness) {
    brightness = b;
    ui_set_brightness(b);
  }
}

void handle_touch(uint32_t ms) {
  bool down = ui_touch_down();
  if (down) {
    last_activity_ms = ms;  // touch only wakes the screen (never erases anything)
  }
}

// BOOT button (GPIO0, active low, external pull-up; holding it during reset enters the bootloader).
// Hold BUTTON_LONG_MS to open the Wi-Fi setup portal. (Not in board.h: its SCREEN_W/H macros clash here.)
constexpr uint8_t BOARD_BOOT_BTN = 0;
uint32_t button_since_ms = 0;
bool button_fired = false;

void handle_button(uint32_t ms) {
  if (digitalRead(BOARD_BOOT_BTN) != LOW) {
    button_since_ms = 0;
    button_fired = false;
    return;
  }
  last_activity_ms = ms;  // a press also wakes the screen
  if (!button_since_ms) button_since_ms = ms;
  if (!button_fired && ms - button_since_ms >= BUTTON_LONG_MS) {
    button_fired = true;
    DIAG("button: long press -> Wi-Fi setup portal");
    net_request_portal();
  }
}

}  // namespace

void setup() {
  diag_install();  // capture ESP-IDF log lines into the telnet ring too
  led_early_off();  // LED pins off before anything else
  pinMode(BOARD_BOOT_BTN, INPUT_PULLUP);
  Serial.begin(115200);
  led_begin();
#if LED_SELF_TEST
  if (esp_reset_reason() != ESP_RST_SW)  // not after our own heap-safety restarts
  for (oralb::Rgb c : {oralb::Rgb{255, 0, 0}, oralb::Rgb{0, 255, 0}, oralb::Rgb{0, 0, 255}}) {
    led_set(c);
    delay(250);
  }
  led_set({0, 0, 0});
#endif
  ldr_begin();
  app.mtx = xSemaphoreCreateMutex();  // before any callback can fire
  ui_begin();                         // allocate the sprite first, while the heap is unfragmented
  ui_message("esp-oralb", "starting...");
  setenv("TZ", TZ_POSIX, 1);
  tzset();
  ble_begin();
  net_begin();
  last_activity_ms = millis();
  DIAG("boot: free heap %u, largest block %u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void loop() {
  uint32_t ms = millis();
  if (app.ota_active) {  // firmware update: only this task draws (the OTA callbacks just publish state)
    static int shown = -2;
    uint32_t err = app.ota_error_ms;
    int pct = err ? -1 : app.ota_pct;
    if (!was_ota || pct != shown) {
      if (err) ui_message("\xD0\x9F\xD0\xBE\xD0\xBC\xD0\xB8\xD0\xBB\xD0\xBA\xD0\xB0 \xD0\xBE\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8F", "");  // Помилка оновлення
      else {
        char b[8];
        snprintf(b, sizeof b, "%d%%", pct);
        ui_message("\xD0\x9E\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xBD\xD1\x8F...", b);  // Оновлення...
      }
      shown = pct;
    }
    was_ota = true;
    set_backlight(BACKLIGHT_MAX);
    if (err && ms - err >= 3000) {
      app.ota_error_ms = 0;
      app.ota_active = false;  // resume normal screens
      shown = -2;
    }
    delay(50);
    return;
  }
  if (was_ota) {
    was_ota = false;
    drawn_once = false;  // redraw after a failed update
  }
  double now = now_s();
  bool active;
  {
    Lock l;
    app.session.tick(now, wall_s());
    if (app.session.session_id != seen_session) {
      seen_session = app.session.session_id;
      app.tracker.reset();
    }
    active = app.session.screen != Screen::CLOCK || (app.session.has_frame && app.session.frame.running());
  }
  {
    Lock l;
    if (app.session.screen != last_screen) {
      last_screen = app.session.screen;
      const char* n = last_screen == Screen::CLOCK ? "CLOCK" : (last_screen == Screen::BRUSHING ? "BRUSHING" : "RESULT");
      DIAG("screen -> %s (brush %ds, session %u)", n, app.session.brush_seconds, (unsigned)app.session.session_id);
    }
  }
  if (ms - last_heap_log_ms >= 60000) {
    last_heap_log_ms = ms;
    DIAG("heap free %u largest %u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  if (active) last_activity_ms = ms;
  // Heap safety net: BLE sessions and TLS fetches slowly fragment the heap until the TLS buffers no longer
  // fit. When idle on the clock with too small a largest block, restart quietly to defragment.
  // It must stay low for HEAP_RESTART_LOW_MS: a TLS fetch briefly holds ~50 KB (a snapshot mid-fetch showed
  // a 5.6 KB largest block), which must not count.
  {
    static uint32_t low_since = 0;
    bool low = !active && ms - last_activity_ms > HEAP_RESTART_IDLE_MS && ESP.getMaxAllocHeap() < HEAP_RESTART_BELOW;
    if (!low) low_since = 0;
    else if (!low_since) low_since = ms;
    else if (ms - low_since > HEAP_RESTART_LOW_MS) {
      DIAG("heap: largest block %u < %u for %u s while idle -> restart", ESP.getMaxAllocHeap(),
           (unsigned)HEAP_RESTART_BELOW, (unsigned)(HEAP_RESTART_LOW_MS / 1000));
      delay(200);
      ESP.restart();
    }
  }
  handle_touch(ms);
  handle_button(ms);
  // Light sensor: smoothed, slew-limited daytime backlight level
  if (ms - last_ldr_ms >= LDR_SAMPLE_MS) {
    last_ldr_ms = ms;
    ldr_level = ldr_ctl.update(ldr_read());
    if (ms - last_ldr_log_ms >= 10000) {
      last_ldr_log_ms = ms;
      DIAG("ldr raw %.0f -> bl %d", ldr_ctl.average(), ldr_level);
    }
  }
  bool night = is_night();
  uint32_t idle_ms = ms - last_activity_ms;
  // Night: off when idle for NIGHT_WAKE_S (a touch re-arms last_activity_ms; brushing/result keeps it on).
  // Otherwise the sensor level, with no idle dimming.
  set_backlight(night && !active && idle_ms > NIGHT_WAKE_S * 1000UL ? 0 : (uint8_t)ldr_level);

  // Goal event (2:00): the LED blinks green
  LedInput li;
  {
    Lock l;
    li.screen = app.session.screen;
    li.high_pressure = app.session.has_frame && app.session.frame.high_pressure() && app.session.frame.running();
    goal.update(app.session.screen, app.session.brush_seconds, app.session.session_id, ms);
  }
  li.portal = net_state() == NetState::PORTAL;
  li.night = night;
  li.goal_active = goal.active();
  li.goal_since_ms = goal.since_ms();
  led_set(led_state(li, ms));

  if (ms - last_draw_ms >= 100) {
    Scene s;
    build_scene(s, now, ms);
    if (!drawn_once || memcmp(&s, &last_drawn, sizeof s) != 0) {
      ui_render(s);
      last_drawn = s;
      drawn_once = true;
    }
    last_draw_ms = millis();
  }
  delay(20);
}
