#include "feedback.h"

namespace oralb {

Rgb led_state(const LedInput& in, uint32_t now) {
  if (in.night) return {0, 0, 0};
  if (in.screen == Screen::BRUSHING) {
    if (in.high_pressure) return (now / PRESSURE_FLASH_HALF_MS) % 2 == 0 ? Rgb{255, 0, 0} : Rgb{0, 0, 0};
    if (in.goal_active) {
      uint32_t dt = now - in.goal_since_ms;
      if (dt < GOAL_BLINK_MS) return (dt % GOAL_BLINK_PERIOD_MS) < GOAL_BLINK_PERIOD_MS / 2 ? Rgb{0, 255, 0} : Rgb{0, 0, 0};
    }
    return {0, LED_DIM_GREEN, 0};
  }
  if (in.portal) return {0, 0, 255};
  return {0, 0, 0};
}

bool GoalTracker::update(Screen screen, int secs, uint32_t session_id, uint32_t now_ms) {
  if (session_id != session_) {
    session_ = session_id;
    have_prev_ = false;
    reached_ = false;
  }
  if (screen != Screen::BRUSHING) return false;
  bool fire = have_prev_ && !reached_ && prev_secs_ < TARGET_SECONDS && secs >= TARGET_SECONDS;
  have_prev_ = true;
  prev_secs_ = secs;
  if (fire) {
    reached_ = true;
    reached_ms_ = now_ms;
  }
  return fire;
}

int ldr_to_backlight(float raw, int bright_raw, int dark_raw, int bl_min, int bl_max) {
  if (bright_raw == dark_raw) return bl_max;
  float t = (raw - bright_raw) / (float)(dark_raw - bright_raw);  // 0 = bright, 1 = dark
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return (int)(bl_max + (bl_min - bl_max) * t + 0.5f);
}

int BacklightControl::update(int raw) {
  if (!init_) {
    init_ = true;
    avg_ = (float)raw;
    level_ = ldr_to_backlight(avg_, bright_, dark_, min_, max_);
    return level_;
  }
  avg_ += alpha_ * ((float)raw - avg_);
  int target = ldr_to_backlight(avg_, bright_, dark_, min_, max_);
  int d = target - level_;
  if (d >= deadband_ || d <= -deadband_ || target == min_ || target == max_) {
    if (d > max_step_) d = max_step_;
    if (d < -max_step_) d = -max_step_;
    level_ += d;
  }
  return level_;
}

}  // namespace oralb
