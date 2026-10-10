#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace oralb {

Icon icon_from_wmo(int c) {
  if (c == 0 || c == 1) return Icon::CLEAR;
  if (c == 2) return Icon::PARTLY;
  if (c == 3) return Icon::CLOUDY;
  if (c == 45 || c == 48) return Icon::FOG;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return Icon::RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return Icon::SNOW;
  if (c >= 95 && c <= 99) return Icon::STORM;
  return Icon::CLOUDY;
}

namespace {

// value following `"key":` inside [from, ...); nullptr if missing
const char* find_value(const char* from, const char* key) {
  char pat[40];
  snprintf(pat, sizeof pat, "\"%s\":", key);
  const char* p = strstr(from, pat);
  return p ? p + strlen(pat) : nullptr;
}

// first element of a string array: `["2026-10-10T06:58"]` -> minutes since midnight
bool first_time_of_day(const char* daily, const char* key, int& minutes) {
  const char* v = find_value(daily, key);
  if (!v || *v != '[') return false;
  const char* t = strchr(v, 'T');
  const char* end = strchr(v, ']');
  if (!t || !end || t > end) return false;
  char* e;
  long h = strtol(t + 1, &e, 10);
  if (e == t + 1 || *e != ':') return false;
  long m = strtol(e + 1, &e, 10);
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  minutes = (int)(h * 60 + m);
  return true;
}

}  // namespace

bool parse_open_meteo(const char* json, WeatherData& out) {
  const char* cur = strstr(json, "\"current\":");
  if (!cur) return false;
  const char* t = find_value(cur, "temperature_2m");
  const char* w = find_value(cur, "weather_code");
  if (!t || !w) return false;
  char* end;
  float tv = strtof(t, &end);
  if (end == t) return false;
  long wv = strtol(w, &end, 10);
  if (end == w) return false;
  WeatherData d;
  d.temp_c = tv;
  d.code = (int)wv;
  if (const char* id = find_value(cur, "is_day")) {
    long v = strtol(id, &end, 10);
    if (end != id) {
      d.has_is_day = true;
      d.is_day = v != 0;
    }
  }
  if (const char* daily = strstr(json, "\"daily\":")) {
    int rise, set;
    if (first_time_of_day(daily, "sunrise", rise) && first_time_of_day(daily, "sunset", set)) {
      d.has_sun = true;
      d.sunrise_min = rise;
      d.sunset_min = set;
    }
  }
  out = d;
  return true;
}

bool is_daytime(const WeatherData& w, int now_min, bool clock_valid) {
  if (clock_valid && w.has_sun) return now_min >= w.sunrise_min && now_min < w.sunset_min;
  if (w.has_is_day) return w.is_day;
  if (clock_valid) return now_min >= 7 * 60 && now_min < 19 * 60;
  return true;
}

Icon weather_icon(const WeatherData& w, int now_min, bool clock_valid) {
  Icon i = icon_from_wmo(w.code);
  if (is_daytime(w, now_min, clock_valid)) return i;
  if (i == Icon::CLEAR) return Icon::CLEAR_NIGHT;
  if (i == Icon::PARTLY) return Icon::PARTLY_NIGHT;
  return i;
}

}  // namespace oralb
