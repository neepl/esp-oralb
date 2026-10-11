#include <unity.h>

#include "session.h"

using namespace oralb;

static BrushFrame fr(int state, int secs, int status = 0x32, int sector = 1) {
  uint8_t raw[11] = {6, 49, 50, (uint8_t)state, (uint8_t)status, (uint8_t)(secs / 60), (uint8_t)(secs % 60), 0,
                     (uint8_t)sector, 0, 4};
  BrushFrame f;
  parse(raw, 11, f);
  return f;
}

static void test_full_cycle() {
  Session s;
  s.on_frame(fr(2, 0), 0, 1000);
  TEST_ASSERT_TRUE(s.screen == Screen::CLOCK);
  for (int t = 0; t < 125; t++) s.on_frame(fr(3, t), t, 1000 + t);
  TEST_ASSERT_TRUE(s.screen == Screen::BRUSHING);
  s.on_frame(fr(2, 0), 125, 1125);  // idle frame already reset: real duration is kept
  TEST_ASSERT_TRUE(s.screen == Screen::RESULT);
  TEST_ASSERT_EQUAL_INT(124, s.last.seconds);
  s.tick(125 + RESULT_HOLD + 1, 0);
  TEST_ASSERT_TRUE(s.screen == Screen::CLOCK);
}

static void test_result_closes_early_on_charger() {
  Session s;
  for (int t = 0; t < 30; t++) s.on_frame(fr(3, t), t, t);
  s.on_frame(fr(2, 0), 30, 30);
  TEST_ASSERT_TRUE(s.screen == Screen::RESULT);
  s.on_frame(fr(STATE_CHARGING, 0), 31, 31);  // put straight back on the charger
  s.tick(31, 31);
  TEST_ASSERT_TRUE(s.screen == Screen::RESULT);  // still shown for a moment
  s.tick(30 + RESULT_HOLD_ON_CHARGER + 1, 0);
  TEST_ASSERT_TRUE(s.screen == Screen::CLOCK);
}

static void test_pressure_hits_per_episode() {
  Session s;
  const int seq[] = {0x32, 0xB2, 0xB2, 0x32, 0xB2, 0x32};
  for (int t = 0; t < 6; t++) s.on_frame(fr(3, t + 10, seq[t]), t, t);
  TEST_ASSERT_EQUAL_INT(2, s.pressure_hits);
}

static void test_lost_and_short_session() {
  Session s;
  for (int t = 0; t < 8; t++) s.on_frame(fr(3, t), t, t);
  s.tick(7 + LOST_AFTER + 1, 0);
  TEST_ASSERT_TRUE(s.screen == Screen::RESULT);
  Session s2;
  for (int t = 0; t < 3; t++) s2.on_frame(fr(3, t), t, t);
  s2.on_frame(fr(2, 0), 4, 4);
  TEST_ASSERT_TRUE(s2.screen == Screen::CLOCK);  // < MIN_SESSION seconds: not remembered
  TEST_ASSERT_FALSE(s2.has_last);
}

static void test_session_id_bumps() {
  Session s;
  s.on_frame(fr(3, 0), 0, 0);
  uint32_t a = s.session_id;
  for (int t = 1; t < 30; t++) s.on_frame(fr(3, t), t, t);
  TEST_ASSERT_EQUAL_UINT32(a, s.session_id);
  s.on_frame(fr(3, 3), 31, 31);  // timer restarted without idle frame
  TEST_ASSERT_EQUAL_UINT32(a + 1, s.session_id);
}

void run_session_tests() {
  RUN_TEST(test_full_cycle);
  RUN_TEST(test_result_closes_early_on_charger);
  RUN_TEST(test_pressure_hits_per_episode);
  RUN_TEST(test_lost_and_short_session);
  RUN_TEST(test_session_id_bumps);
}
