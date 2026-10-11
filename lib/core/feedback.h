// Pure logic for the status LED, the light-sensor backlight and the goal event (no Arduino).
#pragma once
#include <stdint.h>

#include "session.h"

namespace oralb {

struct Rgb {
  uint8_t r, g, b;  // 0-255 logical brightness (the driver applies LED_MAX and polarity)
};

constexpr int LED_DIM_GREEN = 100;     // brushing
constexpr uint32_t GOAL_BLINK_MS = 1500;  // three green blinks after reaching 2:00
constexpr uint32_t GOAL_BLINK_PERIOD_MS = 500;
constexpr uint32_t PRESSURE_FLASH_HALF_MS = 125;  // 4 Hz

struct LedInput {
  Screen screen = Screen::CLOCK;
  bool high_pressure = false;
  bool portal = false;       // Wi-Fi setup portal open
  bool night = false;
  bool goal_active = false;  // a goal event happened at goal_since_ms
  uint32_t goal_since_ms = 0;
};

// Night: off. Brushing + high pressure: red flashing 4 Hz (precedence). Goal: 3 green blinks over
// 1.5 s. Brushing: dim green. Portal: blue. Otherwise off.
Rgb led_state(const LedInput& in, uint32_t now_ms);

// Detects the moment brush time reaches 120 s (once per session).
class GoalTracker {
 public:
  // True exactly once, on the update where time crosses from < 120 s to >= 120 s while BRUSHING.
  bool update(Screen screen, int secs, uint32_t session_id, uint32_t now_ms);
  bool active() const { return reached_; }
  uint32_t since_ms() const { return reached_ms_; }

 private:
  uint32_t session_ = 0;
  bool have_prev_ = false;
  int prev_secs_ = 0;
  bool reached_ = false;
  uint32_t reached_ms_ = 0;
};

// Linear map of the smoothed ADC reading to a backlight level, clamped. Works whichever of
// bright_raw / dark_raw is larger.
int ldr_to_backlight(float raw, int bright_raw, int dark_raw, int bl_min, int bl_max);

// EMA smoothing + deadband + slew limit, called at a fixed rate (~200 ms).
class BacklightControl {
 public:
  BacklightControl(int bright_raw, int dark_raw, int bl_min, int bl_max, float alpha = 0.1f, int deadband = 4,
                   int max_step = 3)
      : bright_(bright_raw), dark_(dark_raw), min_(bl_min), max_(bl_max), alpha_(alpha), deadband_(deadband),
        max_step_(max_step) {}
  int update(int raw);  // returns the backlight level to use
  // New calibration (web settings); keeps the smoothed average and level, which slew to the new target.
  void set_params(int bright_raw, int dark_raw, int bl_min, int bl_max) {
    bright_ = bright_raw; dark_ = dark_raw; min_ = bl_min; max_ = bl_max;
  }
  float average() const { return avg_; }
  int level() const { return level_; }

 private:
  int bright_, dark_, min_, max_;
  float alpha_;
  int deadband_, max_step_;
  bool init_ = false;
  float avg_ = 0;
  int level_ = 0;
};

}  // namespace oralb
