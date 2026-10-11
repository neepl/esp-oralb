#include "web.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_state.h"
#include "config.h"
#include "diag.h"
#include "jsonw.h"
#include "net.h"
#include "settings_store.h"
#include "timekeep.h"
#include "web_page.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

using namespace oralb;

namespace {

constexpr const char* WEB_USER = "esp-oralb";

bool running = false;
bool routes_added = false;
uint32_t reboot_at_ms = 0;

WebServer& srv() {
  static WebServer s(80);  // constructed on first use; port 80 is only bound between begin() and close()
  return s;
}

// Basic auth on every request; POSTs also need a custom header, which a cross-site form cannot send
// (browsers resend cached Basic credentials to any request for this host).
bool guard(bool post) {
  if (!srv().authenticate(WEB_USER, OTA_PASSWORD)) {
    srv().requestAuthentication(BASIC_AUTH, "esp-oralb");
    return false;
  }
  if (post && !srv().hasHeader("X-Requested-With")) {
    srv().send(403, "application/json", "{\"error\":\"missing X-Requested-With header\"}");
    return false;
  }
  return true;
}

void send_json(int code, const char* body) {
  srv().sendHeader("Cache-Control", "no-store");
  srv().send(code, "application/json", body);
}

void send_error(int code, const char* msg) {
  char b[160], esc[110];
  json_escape(msg, esc, sizeof esc);
  snprintf(b, sizeof b, "{\"error\":\"%s\"}", esc);
  send_json(code, b);
}

void local_string(time_t t, char* out, size_t cap) {
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(out, cap, "%Y-%m-%d %H:%M:%S", &lt);
}

void add_time(JsonW& j) {
  time_t t = time(nullptr);
  char loc[24] = "";
  if (net_time_valid()) local_string(t, loc, sizeof loc);
  j.begin("time").i64("epoch", net_time_valid() ? (int64_t)t : 0).str("local", net_time_valid() ? loc : "not set");
  j.str("source", time_source_name(timekeep_source())).str("tz", settings_get().tz).end();
}

void handle_status() {
  if (!guard(false)) return;
  char buf[1400];
  JsonW j(buf, sizeof buf);
  j.begin();
  j.str("fw", __DATE__ " " __TIME__).str("sdk", esp_get_idf_version());
  j.i64("uptime_s", millis() / 1000);
  j.begin("heap").i64("free", ESP.getFreeHeap()).i64("largest", ESP.getMaxAllocHeap()).end();
  bool up = WiFi.status() == WL_CONNECTED;
  j.begin("wifi").str("ssid", up ? WiFi.SSID().c_str() : "").i64("rssi", up ? WiFi.RSSI() : 0);
  j.str("ip", up ? WiFi.localIP().toString().c_str() : "").end();
  add_time(j);
  j.begin("light").i64("raw", app.ldr_avg).i64("level", app.ldr_level).i64("backlight", app.backlight).end();

  const char* screen;
  int64_t seen_s = -1;
  bool has_last;
  LastSession last;
  {
    Lock l;
    screen = app.session.screen == Screen::CLOCK ? "clock" : (app.session.screen == Screen::BRUSHING ? "brushing" : "result");
    if (app.session.has_frame) seen_s = (int64_t)(now_s() - app.session.last_rx);
    has_last = app.session.has_last;
    last = app.session.last;
  }
  j.str("screen", screen);
  j.begin("brush").i64("seen_s", seen_s);
  if (has_last) {
    char ended[24] = "unknown";
    if (last.ended_at > 0) local_string((time_t)last.ended_at, ended, sizeof ended);
    j.begin("last").i64("secs", last.seconds).i64("hits", last.high_pressure_hits).str("ended", ended).end();
  }
  j.end();

  WeatherData wd;
  uint32_t age_ms = 0;
  bool have = net_weather(wd, age_ms);
  j.begin("weather").boolean("have", have).boolean("ok", net_weather_last_ok()).i64("code", have ? wd.code : -1);
  j.num("temp", have ? wd.temp_c : 0, 1).i64("age_s", have ? age_ms / 1000 : -1).i64("http", net_weather_last_code()).end();

  AlertLevel al = AlertLevel::UNKNOWN;
  char since[8] = "";
  uint32_t alert_age = 0;
  bool ahave = net_alert(al, since, alert_age);
  j.begin("alert").boolean("enabled", settings_get().show_alert).boolean("have", ahave);
  j.str("level", al == AlertLevel::RED ? "red" : (al == AlertLevel::YELLOW ? "yellow" : (al == AlertLevel::NONE ? "none" : "unknown")));
  j.str("since", since).i64("age_s", ahave ? alert_age / 1000 : -1).i64("code", net_alert_last_code()).end();
  j.end();
  if (!j.ok()) return send_error(500, "status too large");
  send_json(200, buf);
}

void send_settings() {
  char buf[640];
  if (!settings_to_json(settings_get(), buf, sizeof buf)) return send_error(500, "settings too large");
  send_json(200, buf);
}

void handle_settings_get() {
  if (!guard(false)) return;
  send_settings();
}

void handle_settings_post() {
  if (!guard(true)) return;
  Settings old = settings_get(), n = old;
  const char* err = nullptr;
  int seen = 0;
  for (int i = 0; i < SETTINGS_KEY_COUNT; i++) {
    const char* k = SETTINGS_KEYS[i];
    if (!srv().hasArg(k)) continue;
    seen++;
    String v = srv().arg(k);
    if (!settings_set(n, k, v.c_str(), &err)) return send_error(400, err);
  }
  if (!seen) return send_error(400, "no known setting in the request");
  if (!settings_check(n, &err)) return send_error(400, err);
  settings_commit(n);
  net_on_settings(old, n);
  DIAG("web: settings saved");
  send_settings();
}

void handle_time() {
  if (!guard(true)) return;
  int64_t epoch = 0;
  if (srv().hasArg("epoch")) {
    String v = srv().arg("epoch");
    char* end;
    epoch = strtoll(v.c_str(), &end, 10);
    if (v.length() == 0 || *end) return send_error(400, "epoch: whole number of seconds (UTC)");
  } else if (srv().hasArg("local")) {  // interpreted in the device's configured time zone (DST aware, via mktime)
    LocalDateTime d;
    if (!parse_local_datetime(srv().arg("local").c_str(), d)) return send_error(400, "local: YYYY-MM-DDTHH:MM[:SS], 2024-2099");
    struct tm t = {};
    t.tm_year = d.year - 1900;
    t.tm_mon = d.month - 1;
    t.tm_mday = d.day;
    t.tm_hour = d.hour;
    t.tm_min = d.min;
    t.tm_sec = d.sec;
    t.tm_isdst = -1;
    epoch = (int64_t)mktime(&t);
  } else {
    return send_error(400, "give epoch or local");
  }
  if (!timekeep_set_manual(epoch)) return send_error(400, "time out of range (2023-11 to 2100)");
  char buf[200];
  JsonW j(buf, sizeof buf);
  j.begin();
  add_time(j);
  j.end();
  send_json(200, buf);
}

void handle_reboot() {
  if (!guard(true)) return;
  send_json(200, "{\"ok\":true}");
  reboot_at_ms = millis() + 600;  // after the reply has gone out; web_poll() restarts
  if (!reboot_at_ms) reboot_at_ms = 1;
}

void handle_portal() {
  if (!guard(true)) return;
  send_json(200, "{\"ok\":true}");
  DIAG("web: Wi-Fi setup portal requested");
  net_request_portal();
}

void handle_root() {
  if (!guard(false)) return;
  srv().sendHeader("Content-Encoding", "gzip");
  srv().sendHeader("Cache-Control", "no-cache");
  srv().send_P(200, "text/html", (PGM_P)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

void handle_not_found() {
  if (!guard(false)) return;
  send_error(404, "not found");
}

void start() {
  WebServer& s = srv();
  if (!routes_added) {
    routes_added = true;
    s.on("/", HTTP_GET, handle_root);
    s.on("/api/status", HTTP_GET, handle_status);
    s.on("/api/settings", HTTP_GET, handle_settings_get);
    s.on("/api/settings", HTTP_POST, handle_settings_post);
    s.on("/api/time", HTTP_POST, handle_time);
    s.on("/api/reboot", HTTP_POST, handle_reboot);
    s.on("/api/portal", HTTP_POST, handle_portal);
    s.onNotFound(handle_not_found);
    static const char* keys[] = {"X-Requested-With"};
    s.collectHeaders(keys, 1);  // once: Authorization is always collected
  }
  s.begin();
  running = true;
  DIAG("web: up on port 80, heap free %u largest %u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

}  // namespace

void web_stop() {
  if (!running) return;
  srv().stop();
  running = false;
  DIAG("web: stopped");
}

void web_poll() {
  bool allowed = WiFi.status() == WL_CONNECTED && !app.ota_active && net_state() != NetState::PORTAL;
  if (!allowed) {
    web_stop();
    return;
  }
  if (!running) start();
  srv().handleClient();
  if (reboot_at_ms && (int32_t)(millis() - reboot_at_ms) >= 0) {
    DIAG("web: reboot requested");
    diag_note_restart("web reboot");
    timekeep_save_now();
    ESP.restart();
  }
}
