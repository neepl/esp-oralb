#include "settings.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "jsonw.h"

namespace oralb {

const char* const SETTINGS_KEYS[] = {"lat",      "lon",      "loc",     "region",   "tz",       "night_start",
                                     "night_end", "night_wake", "ldr_bright", "ldr_dark", "bl_min", "bl_max",
                                     "led_max",  "show_weather", "show_alert", "show_zones", "lang"};
const int SETTINGS_KEY_COUNT = sizeof SETTINGS_KEYS / sizeof SETTINGS_KEYS[0];

Settings settings_defaults() {
  Settings s;
  memset(&s, 0, sizeof s);
  s.lat = WEATHER_LAT;
  s.lon = WEATHER_LON;
  strcpy(s.loc_name, "Kyiv");
  strcpy(s.alert_region, ALERT_REGION_ID);
  strcpy(s.tz, TZ_POSIX);
  s.night_start = NIGHT_START_H;
  s.night_end = NIGHT_END_H;
  s.night_wake_s = NIGHT_WAKE_S;
  s.ldr_bright = LDR_BRIGHT_RAW;
  s.ldr_dark = LDR_DARK_RAW;
  s.bl_min = BACKLIGHT_MIN;
  s.bl_max = BACKLIGHT_MAX;
  s.led_max = LED_MAX;
  s.show_weather = s.show_alert = s.show_zones = true;
  s.lang = LANG_UK;
  return s;
}

namespace {

bool parse_int(const char* v, long lo, long hi, int& out) {
  if (!v || !*v) return false;
  char* end;
  errno = 0;
  long x = strtol(v, &end, 10);
  if (errno || *end || x < lo || x > hi) return false;
  out = (int)x;
  return true;
}

bool parse_double(const char* v, double lo, double hi, float& out) {
  if (!v || !*v) return false;
  char* end;
  errno = 0;
  double x = strtod(v, &end);
  if (errno || *end || !(x >= lo && x <= hi)) return false;  // also rejects NaN
  out = (float)x;
  return true;
}

bool parse_bool(const char* v, bool& out) {
  if (!v) return false;
  if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on")) { out = true; return true; }
  if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "off")) { out = false; return true; }
  return false;
}

// Well-formed UTF-8 without control characters.
bool valid_text(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    int extra;
    if (*p < 0x20 || *p == 0x7f) return false;
    else if (*p < 0x80) extra = 0;
    else if ((*p & 0xe0) == 0xc0 && *p >= 0xc2) extra = 1;
    else if ((*p & 0xf0) == 0xe0) extra = 2;
    else if ((*p & 0xf8) == 0xf0 && *p <= 0xf4) extra = 3;
    else return false;
    p++;
    while (extra--) {
      if ((*p & 0xc0) != 0x80) return false;
      p++;
    }
  }
  return true;
}

bool region_ok(const char* v, size_t cap) {
  size_t n = strlen(v);
  if (n == 0 || n >= cap) return false;
  for (size_t i = 0; i < n; i++)
    if (!((v[i] >= '0' && v[i] <= '9') || (v[i] >= 'a' && v[i] <= 'z') || (v[i] >= 'A' && v[i] <= 'Z') || v[i] == '-' ||
          v[i] == '_'))
      return false;
  return true;
}

// POSIX TZ ("EET-2EEST,M3.5.0/3,M10.5.0/4", "<+03>-3"): a conservative character set, starts with a name.
bool tz_ok(const char* v, size_t cap) {
  size_t n = strlen(v);
  if (n < 3 || n >= cap) return false;
  if (!((v[0] >= 'A' && v[0] <= 'Z') || (v[0] >= 'a' && v[0] <= 'z') || v[0] == '<')) return false;
  for (size_t i = 0; i < n; i++) {
    char c = v[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || strchr("+-:,./<>", c))) return false;
  }
  return true;
}

}  // namespace

bool settings_set(Settings& s, const char* k, const char* v, const char** err) {
  const char* e = nullptr;
  if (!v) v = "";
  if (!strcmp(k, "lat")) { if (!parse_double(v, -90, 90, s.lat)) e = "lat: number from -90 to 90"; }
  else if (!strcmp(k, "lon")) { if (!parse_double(v, -180, 180, s.lon)) e = "lon: number from -180 to 180"; }
  else if (!strcmp(k, "loc")) {
    if (strlen(v) >= sizeof s.loc_name || !valid_text(v)) e = "loc: up to 23 bytes of text";
    else strcpy(s.loc_name, v);
  } else if (!strcmp(k, "region")) {
    if (!region_ok(v, sizeof s.alert_region)) e = "region: 1-7 letters, digits, - or _";
    else strcpy(s.alert_region, v);
  } else if (!strcmp(k, "tz")) {
    if (!tz_ok(v, sizeof s.tz)) e = "tz: POSIX TZ string, e.g. EET-2EEST,M3.5.0/3,M10.5.0/4";
    else strcpy(s.tz, v);
  } else if (!strcmp(k, "night_start")) { if (!parse_int(v, 0, 23, s.night_start)) e = "night_start: hour 0-23"; }
  else if (!strcmp(k, "night_end")) { if (!parse_int(v, 0, 23, s.night_end)) e = "night_end: hour 0-23"; }
  else if (!strcmp(k, "night_wake")) { if (!parse_int(v, 1, 600, s.night_wake_s)) e = "night_wake: 1-600 seconds"; }
  else if (!strcmp(k, "ldr_bright")) { if (!parse_int(v, 0, 4095, s.ldr_bright)) e = "ldr_bright: 0-4095"; }
  else if (!strcmp(k, "ldr_dark")) { if (!parse_int(v, 0, 4095, s.ldr_dark)) e = "ldr_dark: 0-4095"; }
  else if (!strcmp(k, "bl_min")) { if (!parse_int(v, 0, 255, s.bl_min)) e = "bl_min: 0-255"; }
  else if (!strcmp(k, "bl_max")) { if (!parse_int(v, 0, 255, s.bl_max)) e = "bl_max: 0-255"; }
  else if (!strcmp(k, "led_max")) { if (!parse_int(v, 0, 255, s.led_max)) e = "led_max: 0-255"; }
  else if (!strcmp(k, "show_weather")) { if (!parse_bool(v, s.show_weather)) e = "show_weather: 0 or 1"; }
  else if (!strcmp(k, "show_alert")) { if (!parse_bool(v, s.show_alert)) e = "show_alert: 0 or 1"; }
  else if (!strcmp(k, "show_zones")) { if (!parse_bool(v, s.show_zones)) e = "show_zones: 0 or 1"; }
  else if (!strcmp(k, "lang")) {
    if (!strcmp(v, "uk")) s.lang = LANG_UK;
    else if (!strcmp(v, "en")) s.lang = LANG_EN;
    else e = "lang: uk or en";
  } else e = "unknown setting";
  if (e && err) *err = e;
  return !e;
}

bool settings_check(const Settings& s, const char** err) {
  const char* e = nullptr;
  if (s.bl_min > s.bl_max) e = "bl_min must not exceed bl_max";
  else if (s.ldr_bright == s.ldr_dark) e = "ldr_bright and ldr_dark must differ";
  if (e && err) *err = e;
  return !e;
}

bool settings_valid(const Settings& s) {
  Settings t = settings_defaults();
  char v[24];
  const char* e;
  if (memchr(s.loc_name, 0, sizeof s.loc_name) == nullptr || memchr(s.alert_region, 0, sizeof s.alert_region) == nullptr ||
      memchr(s.tz, 0, sizeof s.tz) == nullptr)
    return false;
  bool ok = settings_set(t, "loc", s.loc_name, &e) && settings_set(t, "region", s.alert_region, &e) &&
            settings_set(t, "tz", s.tz, &e);
  snprintf(v, sizeof v, "%.6f", (double)s.lat);
  ok = ok && settings_set(t, "lat", v, &e);
  snprintf(v, sizeof v, "%.6f", (double)s.lon);
  ok = ok && settings_set(t, "lon", v, &e);
  const struct { const char* k; int v; } ints[] = {
      {"night_start", s.night_start}, {"night_end", s.night_end}, {"night_wake", s.night_wake_s},
      {"ldr_bright", s.ldr_bright},   {"ldr_dark", s.ldr_dark},   {"bl_min", s.bl_min},
      {"bl_max", s.bl_max},           {"led_max", s.led_max}};
  for (auto& i : ints) {
    snprintf(v, sizeof v, "%d", i.v);
    ok = ok && settings_set(t, i.k, v, &e);
  }
  // bool bytes other than 0/1 (garbage) are invalid too
  const unsigned char* bools[] = {(const unsigned char*)&s.show_weather, (const unsigned char*)&s.show_alert,
                                  (const unsigned char*)&s.show_zones};
  for (auto b : bools) ok = ok && *b <= 1;
  ok = ok && s.lang <= LANG_EN;
  return ok && settings_check(s, &e);
}

bool settings_to_json(const Settings& s, char* buf, size_t cap) {
  JsonW j(buf, cap);
  j.begin();
  j.num("lat", s.lat, 4).num("lon", s.lon, 4).str("loc", s.loc_name).str("region", s.alert_region).str("tz", s.tz);
  j.i64("night_start", s.night_start).i64("night_end", s.night_end).i64("night_wake", s.night_wake_s);
  j.i64("ldr_bright", s.ldr_bright).i64("ldr_dark", s.ldr_dark).i64("bl_min", s.bl_min).i64("bl_max", s.bl_max);
  j.i64("led_max", s.led_max);
  j.boolean("show_weather", s.show_weather).boolean("show_alert", s.show_alert).boolean("show_zones", s.show_zones).str("lang", lang_code(s.lang));
  j.end();
  return j.ok();
}

const char* lang_code(uint8_t lang) { return lang == LANG_EN ? "en" : "uk"; }

bool is_night_hour(int h, int start, int end) {
  if (start == end) return false;
  if (start < end) return h >= start && h < end;
  return h >= start || h < end;
}

}  // namespace oralb
