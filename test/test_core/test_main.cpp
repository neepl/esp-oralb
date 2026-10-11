#include <unity.h>

void run_protocol_tests();
void run_session_tests();
void run_real_session_tests();
void run_zone_tests();
void run_screen_tests();
void run_feedback_tests();
void run_logring_tests();
void run_alert_tests();
void run_web_tests();

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  run_protocol_tests();
  run_session_tests();
  run_real_session_tests();
  run_zone_tests();
  run_screen_tests();
  run_feedback_tests();
  run_logring_tests();
  run_alert_tests();
  run_web_tests();
  return UNITY_END();
}
