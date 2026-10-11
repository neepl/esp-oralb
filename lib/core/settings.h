// Runtime settings (web UI): plain struct, defaults from config.h, parsing/validation, JSON (no Arduino).
// The device keeps one instance behind a lock (src/settings_store.cpp) and hands out copies.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace oralb {

struct Settings {
  float lat, lon;            // weather location
  char loc_name[24];         // display-only name of the location (UTF-8, up to 23 bytes)
  char alert_region[8];      // siren.pp.ua region id ("31" = Kyiv city)
  char tz[48];               // POSIX TZ string
  int night_start, night_end, night_wake_s;
  int ldr_bright, ldr_dark, bl_min, bl_max;
  int led_max;
  bool show_weather, show_alert, show_zones;
};

Settings settings_defaults();  // values from config.h

// Keys accepted by settings_set() and used in the JSON / form fields.
extern const char* const SETTINGS_KEYS[];
extern const int SETTINGS_KEY_COUNT;

// Parse and range-check one field. On failure returns false and `*err` points to a static message.
bool settings_set(Settings& s, const char* key, const char* value, const char** err);
// Cross-field rules (backlight min <= max, sensor points differ). Call after a batch of settings_set().
bool settings_check(const Settings& s, const char** err);
// Every field passes settings_set() and settings_check() (used on the blob read back from NVS).
bool settings_valid(const Settings& s);
// Compact JSON object with every setting.
bool settings_to_json(const Settings& s, char* buf, size_t cap);

// Night hours: start == end means never; start < end is a same-day window; otherwise it wraps midnight (23 -> 7).
bool is_night_hour(int hour, int start, int end);

}  // namespace oralb
