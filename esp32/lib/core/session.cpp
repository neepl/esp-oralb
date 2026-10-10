#include "session.h"

namespace oralb {

void Session::on_frame(const BrushFrame& f, double now, double wall, const char* addr, const int* rssi_) {
  frame = f;
  has_frame = true;
  strncpy(address, addr ? addr : "", sizeof address - 1);
  address[sizeof address - 1] = 0;
  has_rssi = rssi_ != nullptr;
  rssi = rssi_ ? *rssi_ : 0;
  last_rx = now;

  if (f.running()) {
    if (screen != Screen::BRUSHING) {
      // Resumed within the brush's own pause window keeps its timer, so only reset
      // counters when the brush restarted from zero.
      if (screen == Screen::CLOCK || f.brush_seconds() <= 2) new_session();
      screen = Screen::BRUSHING;
    } else if (f.brush_seconds() + 2 < brush_seconds) {
      new_session();  // timer restarted without an idle frame (seen on iO 8)
    }
    brush_seconds = f.brush_seconds();
    bool high = f.high_pressure();
    if (high && !was_high_) pressure_hits++;
    was_high_ = high;
  } else if (screen == Screen::BRUSHING) {
    end(now, wall);
  }
}

void Session::tick(double now, double wall) {
  (void)wall;
  if (screen == Screen::BRUSHING && now - last_rx > LOST_AFTER)
    end(now, wall);
  else if (screen == Screen::RESULT && now - result_since > RESULT_HOLD)
    screen = Screen::CLOCK;
}

void Session::new_session() {
  pressure_hits = 0;
  session_id++;
}

void Session::end(double now, double wall) {
  was_high_ = false;
  int secs = brush_seconds;  // last running timer value: idle frames may already be reset
  if (secs >= MIN_SESSION) {
    last.seconds = secs;
    last.ended_at = wall;
    last.high_pressure_hits = pressure_hits;
    has_last = true;
    screen = Screen::RESULT;
    result_since = now;
  } else {
    screen = Screen::CLOCK;
  }
}

}  // namespace oralb
