#include <unity.h>

#include "feedback.h"

using namespace oralb;

static void test_led_states() {
  LedInput in;
  Rgb c = led_state(in, 1000);
  TEST_ASSERT_TRUE(c.r == 0 && c.g == 0 && c.b == 0);  // clock: off
  in.screen = Screen::BRUSHING;
  c = led_state(in, 1000);
  TEST_ASSERT_TRUE(c.r == 0 && c.g == LED_DIM_GREEN && c.b == 0);
  in.screen = Screen::RESULT;
  c = led_state(in, 1000);
  TEST_ASSERT_TRUE(c.g == 0);
  in.screen = Screen::CLOCK;
  in.portal = true;
  c = led_state(in, 1000);
  TEST_ASSERT_TRUE(c.b == 255 && c.r == 0 && c.g == 0);
  in.night = true;  // night: always off
  c = led_state(in, 1000);
  TEST_ASSERT_TRUE(c.r == 0 && c.g == 0 && c.b == 0);
}

static void test_pressure_flash_precedence() {
  LedInput in;
  in.screen = Screen::BRUSHING;
  in.high_pressure = true;
  in.goal_active = true;
  in.goal_since_ms = 0;
  int on = 0, off = 0;
  for (uint32_t t = 0; t < 1000; t += 5) {
    Rgb c = led_state(in, t);
    TEST_ASSERT_EQUAL_UINT8(0, c.g);
    (c.r ? on : off)++;
  }
  TEST_ASSERT_TRUE(on > 80 && off > 80);  // ~50% duty
  TEST_ASSERT_EQUAL_UINT8(255, led_state(in, 0).r);
  TEST_ASSERT_EQUAL_UINT8(0, led_state(in, 125).r);   // 4 Hz: 125 ms on / 125 ms off
  TEST_ASSERT_EQUAL_UINT8(255, led_state(in, 250).r);
  in.night = true;
  TEST_ASSERT_EQUAL_UINT8(0, led_state(in, 0).r);
}

static void test_goal_blinks() {
  LedInput in;
  in.screen = Screen::BRUSHING;
  in.goal_active = true;
  in.goal_since_ms = 10000;
  // three blinks in 1.5 s: on at 0, 500, 1000; off between; dim green afterwards
  const uint32_t on_at[3] = {0, 500, 1000};
  for (uint32_t s : on_at) {
    TEST_ASSERT_EQUAL_UINT8(255, led_state(in, 10000 + s).g);
    TEST_ASSERT_EQUAL_UINT8(255, led_state(in, 10000 + s + 249).g);
    TEST_ASSERT_EQUAL_UINT8(0, led_state(in, 10000 + s + 250).g);
    TEST_ASSERT_EQUAL_UINT8(0, led_state(in, 10000 + s + 499).g);
  }
  TEST_ASSERT_EQUAL_UINT8(LED_DIM_GREEN, led_state(in, 10000 + 1500).g);
  TEST_ASSERT_EQUAL_UINT8(LED_DIM_GREEN, led_state(in, 10000 + 5000).g);
}

static void test_goal_tracker() {
  GoalTracker g;
  TEST_ASSERT_FALSE(g.update(Screen::BRUSHING, 118, 1, 100));
  TEST_ASSERT_FALSE(g.update(Screen::BRUSHING, 119, 1, 200));
  TEST_ASSERT_TRUE(g.update(Screen::BRUSHING, 120, 1, 300));
  TEST_ASSERT_TRUE(g.active());
  TEST_ASSERT_EQUAL_UINT32(300, g.since_ms());
  TEST_ASSERT_FALSE(g.update(Screen::BRUSHING, 121, 1, 400));  // once per session
  // new session re-arms
  TEST_ASSERT_FALSE(g.update(Screen::BRUSHING, 2, 2, 500));
  TEST_ASSERT_FALSE(g.active());
  TEST_ASSERT_TRUE(g.update(Screen::BRUSHING, 121, 2, 600));  // crossing detected even if the timer jumps past 120
}

static void test_goal_tracker_no_fire_on_resume() {
  GoalTracker g;
  // first observation already past 120 (e.g. reboot mid-brush): no event
  TEST_ASSERT_FALSE(g.update(Screen::BRUSHING, 130, 7, 100));
  TEST_ASSERT_FALSE(g.update(Screen::BRUSHING, 131, 7, 200));
  GoalTracker h;
  TEST_ASSERT_FALSE(h.update(Screen::CLOCK, 119, 1, 0));
  TEST_ASSERT_FALSE(h.update(Screen::RESULT, 120, 1, 0));
}

static void test_ldr_mapping() {
  // bright reading 150 -> max, dark reading 1500 -> min, linear between, clamped
  TEST_ASSERT_EQUAL_INT(255, ldr_to_backlight(0, 150, 1500, 20, 255));
  TEST_ASSERT_EQUAL_INT(255, ldr_to_backlight(150, 150, 1500, 20, 255));
  TEST_ASSERT_EQUAL_INT(20, ldr_to_backlight(1500, 150, 1500, 20, 255));
  TEST_ASSERT_EQUAL_INT(20, ldr_to_backlight(4095, 150, 1500, 20, 255));
  int mid = ldr_to_backlight(825, 150, 1500, 20, 255);
  TEST_ASSERT_INT_WITHIN(1, 138, mid);  // (255 + 20) / 2
  // reversed sensor polarity works too
  TEST_ASSERT_EQUAL_INT(255, ldr_to_backlight(3000, 3000, 200, 20, 255));
  TEST_ASSERT_EQUAL_INT(20, ldr_to_backlight(200, 3000, 200, 20, 255));
}

static void test_backlight_control() {
  BacklightControl bc(150, 1500, 20, 255);
  TEST_ASSERT_EQUAL_INT(255, bc.update(100));  // first sample: no ramp
  // sudden darkness: slews down at most 3 per step, never below min
  int prev = 255;
  for (int i = 0; i < 400; i++) {
    int l = bc.update(2000);
    TEST_ASSERT_TRUE(prev - l <= 3 && prev - l >= 0);
    TEST_ASSERT_TRUE(l >= 20);
    prev = l;
  }
  TEST_ASSERT_EQUAL_INT(20, prev);
  // small noise around a steady level does not change the output (deadband)
  BacklightControl s(150, 1500, 20, 255);
  s.update(800);
  int base = s.level();
  for (int i = 0; i < 200; i++) s.update(i % 2 ? 810 : 790);
  TEST_ASSERT_INT_WITHIN(2, base, s.level());
}

void run_feedback_tests() {
  RUN_TEST(test_led_states);
  RUN_TEST(test_pressure_flash_precedence);
  RUN_TEST(test_goal_blinks);
  RUN_TEST(test_goal_tracker);
  RUN_TEST(test_goal_tracker_no_fire_on_resume);
  RUN_TEST(test_ldr_mapping);
  RUN_TEST(test_backlight_control);
}
