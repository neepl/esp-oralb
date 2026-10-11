#include "settings_store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "diag.h"

using namespace oralb;

namespace {

constexpr uint32_t BLOB_VERSION = 2;  // bump when Settings changes layout; older versions below are migrated, others ignored
struct Blob {
  uint32_t version;
  Settings s;
};
// Version 1: Settings without `lang` (the fields in their old order). Loads with lang = uk.
struct SettingsV1 {
  float lat, lon;
  char loc_name[24];
  char alert_region[8];
  char tz[48];
  int night_start, night_end, night_wake_s;
  int ldr_bright, ldr_dark, bl_min, bl_max;
  int led_max;
  bool show_weather, show_alert, show_zones;
};
struct BlobV1 {
  uint32_t version;
  SettingsV1 s;
};
Settings from_v1(const SettingsV1& o) {
  Settings s = settings_defaults();  // lang = uk
  s.lat = o.lat; s.lon = o.lon;
  memcpy(s.loc_name, o.loc_name, sizeof s.loc_name);
  memcpy(s.alert_region, o.alert_region, sizeof s.alert_region);
  memcpy(s.tz, o.tz, sizeof s.tz);
  s.night_start = o.night_start; s.night_end = o.night_end; s.night_wake_s = o.night_wake_s;
  s.ldr_bright = o.ldr_bright; s.ldr_dark = o.ldr_dark; s.bl_min = o.bl_min; s.bl_max = o.bl_max;
  s.led_max = o.led_max;
  s.show_weather = o.show_weather; s.show_alert = o.show_alert; s.show_zones = o.show_zones;
  return s;
}

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Settings cur = settings_defaults();
volatile uint32_t gen = 1;

void save(const Settings& s) {
  Blob b;
  memset(&b, 0, sizeof b);
  b.version = BLOB_VERSION;
  b.s = s;
  Preferences p;
  if (p.begin("cfg", false)) {
    p.putBytes("settings", &b, sizeof b);
    p.end();
  } else {
    DIAG("settings: NVS open failed");
  }
}

}  // namespace

void settings_begin() {
  Settings loaded = settings_defaults();
  Preferences p;
  if (p.begin("cfg", true)) {
    Blob b;
    BlobV1 b1;
    size_t len = p.getBytesLength("settings");
    bool have = false;
    if (len == sizeof b && p.getBytes("settings", &b, sizeof b) == sizeof b && b.version == BLOB_VERSION) {
      have = true;
    } else if (len == sizeof b1 && p.getBytes("settings", &b1, sizeof b1) == sizeof b1 && b1.version == 1) {
      b.s = from_v1(b1.s);
      have = true;
    }
    if (have) {
      // A corrupt blob must never reach the screens or the fetch URLs.
      if (settings_valid(b.s)) loaded = b.s;
      else DIAG("settings: stored values invalid, using defaults");
    }
    p.end();
  }
  portENTER_CRITICAL(&mux);
  cur = loaded;
  gen = gen + 1;  // under the spinlock; volatile ++ is deprecated in C++20
  portEXIT_CRITICAL(&mux);
}

Settings settings_get() {
  Settings s;
  portENTER_CRITICAL(&mux);
  s = cur;
  portEXIT_CRITICAL(&mux);
  return s;
}

uint32_t settings_generation() { return gen; }

bool settings_commit(const Settings& s) {
  const char* e;
  if (!settings_check(s, &e)) return false;
  portENTER_CRITICAL(&mux);
  cur = s;
  gen = gen + 1;  // under the spinlock; volatile ++ is deprecated in C++20
  portEXIT_CRITICAL(&mux);
  save(s);  // flash write outside the critical section
  return true;
}
