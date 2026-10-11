// Minimal CST816S touch reader (bit-banged I2C, "is the screen touched?" only).
// Why not LovyanGFX's touch driver: on Arduino-ESP32 3.x it pulls in the IDF I2C master driver, whose
// IRAM code pushes the classic-ESP32 IRAM segment (already full with Wi-Fi + BLE) over its limit.
#pragma once

void touch_begin();   // reset pulse + chip setup (call after the display is up)
bool touch_down();    // true while a finger is on the screen (rate-limited, cheap to poll)
