// State shared between the BLE callbacks (NimBLE host task), the helper tasks and loop().
// Everything touching `session` / `tracker` must hold the Lock.
#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>

#include "session.h"
#include "zonemodel.h"

struct AppState {
  SemaphoreHandle_t mtx = nullptr;
  oralb::Session session;
  oralb::ZoneTracker tracker;
  double last_adv = -1e9;            // now_s() of the last Oral-B advert
  volatile bool zones_linked = false;       // GATT connected and ff0d subscribed
  volatile bool zones_unsupported = false;  // brush has no ff0d: never retry
  char zone_status[32] = "";
  volatile bool ota_active = false;  // firmware update in progress: main loop shows the update screen
  volatile int ota_pct = 0;          // progress, set by the OTA callbacks (they never draw themselves)
  volatile uint32_t ota_error_ms = 0;  // millis() of a failed update (error shown for 3 s), 0 = none
};

extern AppState app;

struct Lock {
  Lock() { xSemaphoreTake(app.mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(app.mtx); }
};

inline double now_s() { return (double)millis() / 1000.0; }
// Unix time once NTP has synced, else 0.
inline double wall_s() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (double)t : 0.0;
}
