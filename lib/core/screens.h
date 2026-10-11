// The three screens, drawn through a tiny canvas interface so the same code runs on the device
// (sprite bands) and natively (RGB buffer, compared pixel for pixel with tools/design/design.py).
// Plain C++, no Arduino dependency. Layout constants mirror screen_clock/brushing/result in design.py.
#pragma once
#include <stdint.h>

#include "alert.h"
#include "session.h"
#include "weather.h"
#include "zonemodel.h"

namespace oralb {

struct Canvas {
  virtual ~Canvas() {}
  virtual void fill_rect(int x, int y, int w, int h, uint32_t rgb) = 0;  // clipping is up to the canvas
  int width = 320, height = 240;
};

// Everything the screens need; plain data (memset to 0 before filling) so it can be memcmp'd.
struct Scene {
  Screen screen = Screen::CLOCK;
  int secs = 0;               // brushing / result time
  bool high_pressure = false;
  bool blink_on = true;       // 1 Hz blink of the current progress square
  bool zones = false;         // teeth chart available (ff0d present)
  int8_t current_zone = -1;   // model zone index or -1
  uint8_t cov[ZONE_COUNT] = {0};  // 0 none, 1 partial (> 0 s), 2 full (>= 7.5 s)
  // clock
  bool time_valid = false;
  bool colon = true;          // clock colon, blinks once per second
  uint8_t hh = 0, mm = 0, weekday = 0 /* Monday = 0 */, day = 1, month = 1;
  bool weather_valid = false;
  Icon weather = Icon::CLEAR;
  int8_t temp = 0;
  AlertLevel alert = AlertLevel::UNKNOWN;  // air-raid state of the configured region (UNKNOWN = hidden)
  char alert_since[8] = "";   // "HH:MM" local, for YELLOW/RED
  uint8_t lang = 0;           // display language index (Lang in settings.h): 0 uk, 1 en
  char status[48] = "";       // small UTF-8 line at the bottom of the clock screen
};

// coverage seconds -> Scene::cov state
inline uint8_t cov_state(double seconds) { return seconds >= ZONE_TARGET_SECONDS ? 2 : (seconds > 0 ? 1 : 0); }

void draw_scene(Canvas& c, const Scene& s);

// Primitives (exposed for tests)
int utf8_decode(const char*& p);  // next codepoint, advances p
int text_width(const char* utf8, int scale);

// Boot / notice screen: two centred lines (UTF-8, pixel font)
void draw_message(Canvas& c, const char* line1, const char* line2);

}  // namespace oralb
