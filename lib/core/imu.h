// GATT constants and the ff0d IMU packet decoder (port of tools/python/src/esp_oralb/gatt.py).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace oralb {

// Characteristic UUIDs: a0f0XXXX-5047-4d53-8208-4f72616c2d42
#define ORALB_UUID(short_id) "a0f0" short_id "-5047-4d53-8208-4f72616c2d42"
constexpr const char* SVC_MAIN = ORALB_UUID("ff00");      // service holding the characteristics below
constexpr const char* CH_STATE = ORALB_UUID("ff04");     // [state, ?]
constexpr const char* CH_TIME = ORALB_UUID("ff08");      // [minutes, seconds]
constexpr const char* CH_PRESSURE = ORALB_UUID("ff0b");  // raw u16 at offset 3
constexpr const char* CH_SENSOR = ORALB_UUID("ff0d");    // 20-byte IMU packets

struct ImuSample {
  uint16_t ts;
  bool has_gyro;
  int8_t gyro[3];
  int8_t accel[3];
};

// Decode one ff0d notification; returns the number of samples written to out (max 4).
size_t decode_sensor(const uint8_t* data, size_t len, ImuSample out[4]);

// Pressure characteristic (ff0b): [level, ts u16, raw u16, prev ts u16, prev raw u16, 0x37].
// level is the brush's own classification: 0 light, 1 normal, 2 too much (red ring); captured
// from the user's iO 8: 1 for raw 808..2507, 2 for raw >= 2540. False if too short.
constexpr uint8_t PRESSURE_LEVEL_HIGH = 2;
bool decode_pressure(const uint8_t* data, size_t len, uint16_t& raw, uint8_t& level);

}  // namespace oralb
