// Screen state machine: CLOCK -> BRUSHING -> RESULT -> CLOCK (port of session.py).
#pragma once
#include <stdint.h>
#include <string.h>

#include "protocol.h"

namespace oralb {

constexpr int TARGET_SECONDS = 120;
constexpr double STALE_AFTER = 3.0;
constexpr double LOST_AFTER = 10.0;
constexpr double RESULT_HOLD = 60.0;
constexpr int MIN_SESSION = 5;

enum class Screen : uint8_t { CLOCK, BRUSHING, RESULT };

struct LastSession {
  int seconds = 0;
  double ended_at = 0;  // wall clock (unix seconds, 0 if unknown)
  int high_pressure_hits = 0;
};

class Session {
 public:
  Screen screen = Screen::CLOCK;
  bool has_frame = false;
  BrushFrame frame;
  char address[20] = "";
  int rssi = 0;
  bool has_rssi = false;
  double last_rx = 0;
  double result_since = 0;
  int pressure_hits = 0;
  uint32_t session_id = 0;  // bumps whenever a new brushing session starts
  int brush_seconds = 0;    // timer from the last *running* frame
  bool has_last = false;
  LastSession last;

  void on_frame(const BrushFrame& f, double now, double wall, const char* addr = "", const int* rssi = nullptr);
  void tick(double now, double wall);
  bool stale(double now) const { return now - last_rx > STALE_AFTER; }
  bool seen_recently(double now, double window = 30.0) const { return has_frame && now - last_rx < window; }

 private:
  bool was_high_ = false;
  void new_session();
  void end(double now, double wall);
};

}  // namespace oralb
