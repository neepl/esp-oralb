#pragma once
#include <Arduino.h>

struct BleLink {
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  char addr[20] = "";
  uint8_t addr_type = 0;
  double seen_at = -1e9;           // last advert from the brush
  volatile uint8_t brush_state = 0;  // state byte of that advert (3 = running)
  volatile bool connected = false; // GATT client connected (or connecting)
};
extern BleLink link_state;

volatile extern bool ble_paused;
void ble_scan_start(bool fast);  // (re)start scanning at the light or fast duty
bool ble_scan_is_fast();
void ble_link_start();  // starts the zone-link task
