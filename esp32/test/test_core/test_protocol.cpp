#include <string.h>
#include <unity.h>

#include "imu.h"
#include "protocol.h"

using namespace oralb;

static bool P(const char* s, size_t n, BrushFrame& f) { return parse((const uint8_t*)s, n, f); }

static void test_bad_length() {
  BrushFrame f;
  uint8_t z[10] = {0};
  TEST_ASSERT_FALSE(parse(z, 10, f));
  TEST_ASSERT_FALSE(parse(z, 0, f));
}

static void test_io_series_running() {
  BrushFrame f;
  TEST_ASSERT_TRUE(P("\x06" "12\x03r\x02\x03\x00\x07\x00\x06", 11, f));
  TEST_ASSERT_EQUAL_STRING("iO Series", f.model_name());
  TEST_ASSERT_TRUE(f.running());
  TEST_ASSERT_EQUAL_INT(123, f.brush_seconds());
  TEST_ASSERT_EQUAL_STRING("daily clean", f.mode_name());
  TEST_ASSERT_EQUAL_INT(6, f.sectors());
  TEST_ASSERT_EQUAL_INT(6, f.sector());
  TEST_ASSERT_FALSE(f.high_pressure());
}

static void test_idle_sector_cleared() {
  BrushFrame f;
  TEST_ASSERT_TRUE(P("\x06" "1\x16\x08r\x00\x00\x03\x02\x00\x04", 11, f));
  TEST_ASSERT_FALSE(f.running());
  TEST_ASSERT_EQUAL_STRING("selection menu", f.state_name());
  TEST_ASSERT_EQUAL_INT(0, f.sector());
  TEST_ASSERT_EQUAL_STRING("whiten", f.mode_name());
}

static void test_high_pressure() {
  BrushFrame f;
  TEST_ASSERT_TRUE(P("\x03\"\x0c\x03\xc0\x01" "0\x07\x04<\x04", 11, f));
  TEST_ASSERT_TRUE(f.high_pressure());
  TEST_ASSERT_EQUAL_INT(108, f.brush_seconds());
  TEST_ASSERT_EQUAL_INT(4, f.sector());
  TEST_ASSERT_EQUAL_INT(60, f.sector_timer);
}

static void test_button_overrides_pressure() {
  BrushFrame f;
  TEST_ASSERT_TRUE(P("\x02\x01\x08\x03\x96\x00\x00\x01\x01\x00\x04", 11, f));
  TEST_ASSERT_TRUE(f.button_pressed());
  TEST_ASSERT_FALSE(f.high_pressure());
}

static void test_short_frame() {
  BrushFrame f;
  TEST_ASSERT_TRUE(P("\x01\x02\x05\x03\x00\x00\x08\x04\x01", 9, f));
  TEST_ASSERT_EQUAL_STRING("Triumph D36", f.model_name());
  TEST_ASSERT_FALSE(f.has_sector_info);
  TEST_ASSERT_EQUAL_INT(4, f.sectors());
  TEST_ASSERT_EQUAL_INT(1, f.sector());
  TEST_ASSERT_EQUAL_INT(8, f.brush_seconds());
}

static void test_company_id_strip() {
  // NimBLE manufacturer field: company ID 0x00DC little-endian + payload
  const uint8_t raw[] = {0xDC, 0x00, 0x06, 0x32, 0x6e, 0x03, 0x32, 0x00, 0x05, 0x08, 0x02, 0x00, 0x04};
  const uint8_t* p;
  size_t n;
  TEST_ASSERT_TRUE(strip_company_id(raw, sizeof raw, p, n));
  TEST_ASSERT_EQUAL_UINT(11, n);
  BrushFrame f;
  TEST_ASSERT_TRUE(parse(p, n, f));
  TEST_ASSERT_TRUE(f.running());
  TEST_ASSERT_EQUAL_INT(5, f.brush_seconds());
  const uint8_t other[] = {0x4C, 0x00, 0x01, 0x02};
  TEST_ASSERT_FALSE(strip_company_id(other, sizeof other, p, n));
  TEST_ASSERT_FALSE(strip_company_id(raw, 1, p, n));
}

static void test_ff0d_decode() {
  const uint8_t pkt[20] = {0xb4, 0x7e, 0x01, 0x00, 0x06, 0x1e, 0xf4, 0xf7, 0xdd, 0x7e,
                           0x09, 0xfe, 0x02, 0x1d, 0xf9, 0xfa, 0x00, 0x00, 0x10, 0x80};
  ImuSample s[4];
  TEST_ASSERT_EQUAL_UINT(2, decode_sensor(pkt, sizeof pkt, s));
  TEST_ASSERT_EQUAL_HEX16(0x7EB4, s[0].ts);
  TEST_ASSERT_TRUE(s[0].has_gyro);
  TEST_ASSERT_EQUAL_INT8(1, s[0].gyro[0]);
  TEST_ASSERT_EQUAL_INT8(0, s[0].gyro[1]);
  TEST_ASSERT_EQUAL_INT8(6, s[0].gyro[2]);
  TEST_ASSERT_EQUAL_INT8(30, s[0].accel[0]);
  TEST_ASSERT_EQUAL_INT8(-12, s[0].accel[1]);
  TEST_ASSERT_EQUAL_INT8(-9, s[0].accel[2]);
  TEST_ASSERT_EQUAL_HEX16(0x7EDD, s[1].ts);
  TEST_ASSERT_EQUAL_INT8(9, s[1].gyro[0]);
  TEST_ASSERT_EQUAL_INT8(-2, s[1].gyro[1]);
  TEST_ASSERT_EQUAL_INT8(2, s[1].gyro[2]);
  TEST_ASSERT_EQUAL_INT8(29, s[1].accel[0]);
  TEST_ASSERT_EQUAL_INT8(-7, s[1].accel[1]);
  TEST_ASSERT_EQUAL_INT8(-6, s[1].accel[2]);
  TEST_ASSERT_EQUAL_UINT(0, decode_sensor(pkt, 19, s));
  // plain motion packet: 4 x (ts u16, accel xyz)
  uint8_t plain[20];
  memset(plain, 0, sizeof plain);
  plain[0] = 1; plain[2] = 5; plain[3] = 0xfb; plain[4] = 7;
  TEST_ASSERT_EQUAL_UINT(4, decode_sensor(plain, 20, s));
  TEST_ASSERT_FALSE(s[0].has_gyro);
  TEST_ASSERT_EQUAL_INT8(-5, s[0].accel[1]);
  // 0x80 trailer without 0x10 marker: not motion
  uint8_t other[20] = {0};
  other[19] = 0x80;
  TEST_ASSERT_EQUAL_UINT(0, decode_sensor(other, 20, s));
}

static void test_pressure_decode() {
  // level 2 (too much), raw 0x1761 = 5985: shape of the user's captured ff0b notifications
  const uint8_t d[] = {0x02, 0x10, 0x80, 0x61, 0x17, 0xe8, 0x7f, 0x5d, 0x17, 0x37};
  uint16_t raw = 0;
  uint8_t level = 0;
  TEST_ASSERT_TRUE(decode_pressure(d, sizeof d, raw, level));
  TEST_ASSERT_EQUAL_UINT16(5985, raw);
  TEST_ASSERT_EQUAL_UINT8(PRESSURE_LEVEL_HIGH, level);
  TEST_ASSERT_FALSE(decode_pressure(d, 4, raw, level));
}

void run_protocol_tests() {
  RUN_TEST(test_bad_length);
  RUN_TEST(test_io_series_running);
  RUN_TEST(test_idle_sector_cleared);
  RUN_TEST(test_high_pressure);
  RUN_TEST(test_button_overrides_pressure);
  RUN_TEST(test_short_frame);
  RUN_TEST(test_company_id_strip);
  RUN_TEST(test_ff0d_decode);
  RUN_TEST(test_pressure_decode);
}
