#include "settings_store.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "diag.h"

using namespace oralb;

namespace {

constexpr uint32_t BLOB_VERSION = 1;  // bump when Settings changes layout (stored blob is then ignored)
struct Blob {
  uint32_t version;
  Settings s;
};

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
    if (p.getBytesLength("settings") == sizeof b && p.getBytes("settings", &b, sizeof b) == sizeof b &&
        b.version == BLOB_VERSION) {
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
