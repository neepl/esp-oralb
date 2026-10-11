#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <esp_sntp.h>
#include <time.h>

#include "app_state.h"
#include "ble.h"
#include "config.h"
#include "diag.h"
#include "isrg_root_x1.h"
#include "netsvc.h"

namespace {

volatile NetState state = NetState::CONNECTING;
volatile bool synced = false;
volatile bool portal_requested = false;  // BOOT button long press

// latest weather (written by the net task, read by loop(); single-word fields)
portMUX_TYPE weather_mux = portMUX_INITIALIZER_UNLOCKED;
bool weather_have = false;           // a fetch has succeeded at least once
oralb::WeatherData weather_data;
uint32_t weather_fetched_ms = 0;     // millis() of the last successful fetch
volatile bool weather_last_ok = false;  // result of the most recent attempt

bool wait_sntp(uint32_t timeout_ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeout_ms) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) return true;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  return false;
}

[[maybe_unused]] void wifi_off() {
  // Order matters on Arduino-ESP32 2.0.x: stop the AP (portal) and STA first, give the event
  // loop time to drain, then de-init; otherwise "wifi:timeout when WiFi un-init" is logged.
  esp_sntp_stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, false);
  vTaskDelay(pdMS_TO_TICKS(300));
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(200));
  DIAG("wifi off");
}

// Wi-Fi is up: sync the clock, then switch it off again to leave RAM and radio time to BLE.
[[maybe_unused]] bool sync_clock() {
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTzTime(TZ_POSIX, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  bool ok = wait_sntp(20000);
  DIAG("ntp %s", ok ? "synced" : "timeout");
  if (ok) synced = true;
  return ok;
}

// Open-Meteo over plain HTTP (no TLS heap cost; the reply is a few hundred bytes).
bool fetch_weather_impl();
bool fetch_weather() {  // heap-delta diagnostics (leak hunt)
  uint32_t h0 = ESP.getFreeHeap();
  bool ok = fetch_weather_impl();
  DIAG("weather: heap %u -> %u (%+d)", (unsigned)h0, (unsigned)ESP.getFreeHeap(), (int)ESP.getFreeHeap() - (int)h0);
  return ok;
}
bool fetch_weather_impl() {
  char url[200];
  snprintf(url, sizeof url,
           "http://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m,weather_code,is_day"
           "&daily=sunrise,sunset&timezone=auto&forecast_days=1",
           (double)WEATHER_LAT, (double)WEATHER_LON);
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    weather_last_ok = false;
    return false;
  }
  int status = http.GET();
  bool ok = false;
  if (status == 200) {
    String body = http.getString();
    oralb::WeatherData w;
    if (oralb::parse_open_meteo(body.c_str(), w)) {
      portENTER_CRITICAL(&weather_mux);
      weather_data = w;
      weather_fetched_ms = millis();
      weather_have = true;
      portEXIT_CRITICAL(&weather_mux);
      ok = true;
      DIAG("weather: %d %.1f", w.code, w.temp_c);
    } else {
      DIAG("weather: parse error");
    }
  } else {
    DIAG("weather: http %d", status);
  }
  http.end();
  weather_last_ok = ok;
  return ok;
}

// ---- air-raid alert (siren.pp.ua API v3, HTTPS only, pinned ISRG Root X1) --------------------
portMUX_TYPE alert_mux = portMUX_INITIALIZER_UNLOCKED;
bool alert_have = false;
oralb::AlertLevel alert_level = oralb::AlertLevel::UNKNOWN;
char alert_since[8] = "";
uint32_t alert_ok_ms = 0;  // millis() of the last successful poll

const char* alert_name(oralb::AlertLevel l) {
  return l == oralb::AlertLevel::RED ? "red" : (l == oralb::AlertLevel::YELLOW ? "yellow" : "none");
}

// One poll: handshake, GET, close (no connection kept open: TLS buffers are ~40 KB of heap).
bool fetch_alert_impl();
bool fetch_alert() {  // heap-delta diagnostics around the real fetch (leak hunt)
  uint32_t h0 = ESP.getFreeHeap();
  bool ok = fetch_alert_impl();
  static uint32_t n = 0;
  if (++n % 4 == 0) DIAG("alert: heap %u -> %u (%+d)", (unsigned)h0, (unsigned)ESP.getFreeHeap(), (int)ESP.getFreeHeap() - (int)h0);
  return ok;
}
bool fetch_alert_impl() {
  WiFiClientSecure client;
#if ALERT_TLS_VERIFY
  client.setCACert(ISRG_ROOT_X1_PEM);  // bundle: ISRG Root X1 + ISRG Root YR
#else
  client.setInsecure();  // no certificate check (see ALERT_TLS_VERIFY in config.h)
#endif
  client.setTimeout(8);  // seconds
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  int code = -1;
  bool ok = false;
  if (http.begin(client, "https://siren.pp.ua/api/v3/alerts/" ALERT_REGION_ID)) {
    http.addHeader("accept", "application/json");
    code = http.GET();
    if (code == 200) {
      String body = http.getString();
      oralb::AlertResult r;
      if (oralb::parse_alerts(body.c_str(), r)) {
        char since[8] = "";
        if (r.has_since) {
          time_t t = (time_t)r.since_utc;
          struct tm lt;
          localtime_r(&t, &lt);  // UTC -> local time through TZ
          strftime(since, sizeof since, "%H:%M", &lt);
        }
        bool changed;
        portENTER_CRITICAL(&alert_mux);
        changed = !alert_have || alert_level != r.level || strcmp(alert_since, since) != 0;
        alert_level = r.level;
        strncpy(alert_since, since, sizeof alert_since - 1);
        alert_have = true;
        alert_ok_ms = millis();
        portEXIT_CRITICAL(&alert_mux);
        if (changed) DIAG("alert: %s since %s", alert_name(r.level), since[0] ? since : "-");
        ok = true;
      } else {
        code = -1000;  // reply was not the expected shape
      }
    }
  }
  if (!ok) {
    char tls[96] = "";
    int tls_err = client.lastError(tls, sizeof tls);  // mbedTLS reason behind HTTPClient's generic -1
    DIAG("alert: fetch failed (%d), tls %d %s, heap free %u largest %u", code, tls_err, tls, (unsigned)ESP.getFreeHeap(),
         (unsigned)ESP.getMaxAllocHeap());
  }
  http.end();
  return ok;
}

// One short Wi-Fi session with the saved credentials: NTP and/or weather, then Wi-Fi off again.
[[maybe_unused]] bool online_session(bool want_ntp, bool want_weather) {
  WiFi.mode(WIFI_STA);
  WiFi.begin();  // stored credentials
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) vTaskDelay(pdMS_TO_TICKS(250));
  bool ok = WiFi.status() == WL_CONNECTED;
  bool ntp_ok = true, weather_ok_now = true;
  if (ok && want_ntp) ntp_ok = sync_clock();
  if (ok && want_weather) weather_ok_now = fetch_weather();
  wifi_off();
  return ok && ntp_ok && weather_ok_now;
}

// The setup portal (blue LED). With a timeout of 0 it stays open until a network is saved.
// Saved credentials are kept unless the user enters new ones.
bool run_portal(int timeout_s) {
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(timeout_s);
  state = NetState::PORTAL;
  // BLE scanning takes ~90% of the shared radio and makes the portal pages crawl; nobody brushes
  // while configuring Wi-Fi, so pause it (as during OTA).
  ble_pause(true);
  DIAG("wifi: setup portal open (%s), BLE paused", WIFI_AP_NAME);
  bool ok = wm.startConfigPortal(WIFI_AP_NAME);
  DIAG("wifi: setup portal closed (%s)", ok ? "network saved" : "timeout");
  if (ok) {
    // The portal's web server leaves the heap fragmented (largest block ~20 KB vs ~37 KB after
    // a clean boot), and the BLE link needs headroom: restart with the new network.
    DIAG("wifi: restarting with the new network");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
  }
  ble_pause(false);
  WiFi.mode(WIFI_STA);  // drop the portal's AP side
  return ok;
}

// Boot: connect with the saved network, never falling back to the portal on its own (a weak
// signal or a slow start must not look like "forget my Wi-Fi"). Only an empty config opens it.
bool connect_or_setup() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFiManager wm;
  if (!wm.getWiFiIsSaved()) {
    DIAG("wifi: no saved network");
    run_portal(0);
  } else {
    DIAG("wifi: saved network '%s'", wm.getWiFiSSID(true).c_str());
  }
  WiFi.begin();
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_S * 1000UL) vTaskDelay(pdMS_TO_TICKS(250));
  bool ok = WiFi.status() == WL_CONNECTED;
  if (!ok) DIAG("wifi: not connected after %d s, retrying in the background", WIFI_CONNECT_TIMEOUT_S);
  return ok;
}

// BOOT button: open the portal on demand (keeps the saved network unless a new one is entered).
void handle_portal_request() {
  if (!portal_requested) return;
  portal_requested = false;
  run_portal(WIFI_PORTAL_TIMEOUT_S);
  WiFi.begin();
  state = synced ? NetState::SYNCED : NetState::OFFLINE;
}

[[maybe_unused]] void net_task_sessions() {
  uint32_t last_ntp = millis(), last_weather = millis();
  {
    bool connected = connect_or_setup();
    if (connected && sync_clock()) state = NetState::SYNCED;
    else state = NetState::OFFLINE;
    if (connected) fetch_weather();  // same Wi-Fi session as the first NTP sync
    last_ntp = last_weather = millis();
    wifi_off();
  }
  int quick_retries = 0;  // after a failure: a few retries 30 s apart (Wi-Fi often isn't ready yet), then 10 min
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    if (portal_requested) {
      handle_portal_request();
      wifi_off();
    }
    uint32_t ms = millis();
    uint32_t retry_ms = quick_retries < QUICK_RETRY_COUNT ? QUICK_RETRY_MS : NTP_RETRY_MS;
    bool ntp_due = ms - last_ntp >= (synced ? NTP_RESYNC_MS : retry_ms);
    bool weather_due = ms - last_weather >= (weather_last_ok ? WEATHER_REFRESH_MIN * 60000UL : retry_ms);
    if (!ntp_due && !weather_due) continue;
    {
      Lock l;
      if (app.session.screen != oralb::Screen::CLOCK) continue;  // never while brushing
    }
    // when both are (nearly) due, do them in one session
    bool do_ntp = ntp_due, do_weather = weather_due || ntp_due;
    state = NetState::CONNECTING;
    bool ok = online_session(do_ntp, do_weather);
    last_ntp = do_ntp ? millis() : last_ntp;
    if (do_weather) last_weather = millis();
    quick_retries = (synced && weather_last_ok) ? 0 : quick_retries + 1;
    state = (synced || ok) ? NetState::SYNCED : NetState::OFFLINE;
  }
}

void log_wifi_event(arduino_event_id_t ev, arduino_event_info_t info) {
  switch (ev) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      DIAG("wifi: connected %s rssi %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      DIAG("wifi: disconnected (reason %d)", (int)info.wifi_sta_disconnected.reason);
      break;
    default: break;
  }
}

#if WIFI_ALWAYS_ON
// Stay connected: SNTP runs continuously, weather/alerts are fetched in place, services run in netsvc.
void net_task_always_on() {
  uint32_t last_weather = millis(), last_begin = millis();
  {
    bool connected = connect_or_setup();  // default modem sleep stays on (needed for BLE coexistence)
    if (connected) {
      sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
      configTzTime(TZ_POSIX, "pool.ntp.org", "time.google.com", "time.cloudflare.com");  // keeps syncing hourly
      synced = wait_sntp(20000);
      DIAG("ntp %s", synced ? "synced" : "timeout (SNTP keeps trying)");
      fetch_weather();
      netsvc_start();
    }
    state = synced ? NetState::SYNCED : NetState::OFFLINE;
    last_weather = millis();
  }
  int quick_retries = 0;
  uint32_t last_alert = 0;
  bool prev_clock = true, alert_now = true;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    handle_portal_request();
    uint32_t ms = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (state != NetState::PORTAL) state = synced ? NetState::SYNCED : NetState::OFFLINE;
      if (ms - last_begin >= QUICK_RETRY_MS) {  // auto-reconnect handles drops; this covers "never connected"
        last_begin = ms;
        WiFi.mode(WIFI_STA);
        WiFi.begin();
      }
      continue;
    }
    netsvc_start();  // idempotent: first time we are connected
    if (!synced && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      synced = true;
      DIAG("ntp synced");
    }
    if (synced) state = NetState::SYNCED;
    // No fetches during a firmware update: a TLS handshake every 15 s stalled the OTA transfer
    // ("ota: error 4" at 60%) and every 30 s update overlapped one.
    if (app.ota_active) continue;
    bool clock_screen;
    {
      Lock l;
      clock_screen = app.session.screen == oralb::Screen::CLOCK;  // never poll while brushing
    }
    if (clock_screen && !prev_clock) alert_now = true;  // back on the clock after a session: poll right away
    prev_clock = clock_screen;
    if (clock_screen && synced && (alert_now || ms - last_alert >= ALERT_POLL_S * 1000UL)) {  // TLS needs a valid clock
      alert_now = false;
      fetch_alert();
      last_alert = millis();
    }
    uint32_t retry_ms = quick_retries < QUICK_RETRY_COUNT ? QUICK_RETRY_MS : NTP_RETRY_MS;
    if (clock_screen && ms - last_weather >= (weather_last_ok ? WEATHER_REFRESH_MIN * 60000UL : retry_ms)) {
      fetch_weather();
      last_weather = millis();
      quick_retries = weather_last_ok ? 0 : quick_retries + 1;
    }
  }
}
#endif

void net_task(void*) {
  WiFi.onEvent(log_wifi_event);
#if WIFI_ALWAYS_ON
  net_task_always_on();
#else
  net_task_sessions();
#endif
}

}  // namespace

void net_begin() { xTaskCreatePinnedToCore(net_task, "net", 10240, nullptr, 1, nullptr, 1); }
NetState net_state() { return state; }
bool net_time_valid() { return synced && time(nullptr) > 1700000000; }

void net_request_portal() { portal_requested = true; }

bool net_weather(oralb::WeatherData& w, uint32_t& age_ms) {
  bool have;
  portENTER_CRITICAL(&weather_mux);
  have = weather_have;
  w = weather_data;
  age_ms = millis() - weather_fetched_ms;
  portEXIT_CRITICAL(&weather_mux);
  return have;
}

bool net_alert(oralb::AlertLevel& level, char since[8], uint32_t& age_ms) {
  bool have;
  portENTER_CRITICAL(&alert_mux);
  have = alert_have;
  level = alert_level;
  memcpy(since, alert_since, 8);
  age_ms = millis() - alert_ok_ms;
  portEXIT_CRITICAL(&alert_mux);
  return have;
}
