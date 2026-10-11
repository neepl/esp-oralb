#include "imu.h"

namespace oralb {

size_t decode_sensor(const uint8_t* d, size_t len, ImuSample out[4]) {
  if (len != 20) return 0;
  if (d[19] != 0x80) {  // plain motion data: 4 x (ts u16, accel xyz i8)
    for (int k = 0; k < 4; k++) {
      const uint8_t* p = d + k * 5;
      out[k].ts = (uint16_t)(p[0] | (p[1] << 8));
      out[k].has_gyro = false;
      out[k].gyro[0] = out[k].gyro[1] = out[k].gyro[2] = 0;
      for (int i = 0; i < 3; i++) out[k].accel[i] = (int8_t)p[2 + i];
    }
    return 4;
  }
  if (d[18] == 0x10) {  // 2 x 8 bytes: ts u16, gyro xyz i8, accel xyz i8
    for (int k = 0; k < 2; k++) {
      const uint8_t* p = d + k * 8;
      out[k].ts = (uint16_t)(p[0] | (p[1] << 8));
      out[k].has_gyro = true;
      for (int i = 0; i < 3; i++) {
        out[k].gyro[i] = (int8_t)p[2 + i];
        out[k].accel[i] = (int8_t)p[5 + i];
      }
    }
    return 2;
  }
  return 0;
}

bool decode_pressure(const uint8_t* d, size_t len, uint16_t& raw, uint8_t& level) {
  if (len < 5) return false;
  level = d[0];
  raw = (uint16_t)(d[3] | (d[4] << 8));
  return true;
}

}  // namespace oralb
