// BLE: continuous scan for Oral-B adverts + GATT client for the IMU stream (zone tracking).
#pragma once

void ble_begin();                // init NimBLE, start scanning, start the GATT link task
void ble_pause(bool paused);     // OTA: stop scanning and drop the GATT link (true) / resume (false)
