// Fixed-size ring of text lines with per-reader cursors (no Arduino dependency, not thread safe:
// the caller serialises access). Used for the telnet log: every reader keeps its own sequence
// number, so a slow reader just loses the oldest lines instead of blocking anyone.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace oralb {

template <size_t LINES, size_t LEN>
class LogRingT {
 public:
  // Store one line (no newline). Longer lines are truncated and end with '~'.
  void push(const char* s, size_t n) {
    char* slot = text_[total_ % LINES];
    if (n > LEN - 1) {
      memcpy(slot, s, LEN - 2);
      slot[LEN - 2] = '~';
      n = LEN - 1;
    } else {
      memcpy(slot, s, n);
    }
    slot[n] = 0;
    len_[total_ % LINES] = (uint16_t)n;
    total_++;
  }

  uint32_t total() const { return total_; }
  // Cursor that replays everything still stored.
  uint32_t oldest() const { return total_ > LINES ? total_ - LINES : 0; }

  // Next line for a reader at `cursor` (advances it). Returns the line length, or -1 when there is
  // nothing new. If the reader fell behind, *skipped is the number of overwritten lines it missed.
  int next(uint32_t& cursor, char* out, size_t cap, uint32_t* skipped = nullptr) const {
    uint32_t miss = 0;
    if (cursor < oldest()) {
      miss = oldest() - cursor;
      cursor = oldest();
    }
    if (skipped) *skipped = miss;
    if (cursor >= total_) return miss ? 0 : -1;  // report the skip even if no line follows yet
    size_t i = cursor % LINES;
    size_t n = len_[i] < cap - 1 ? len_[i] : cap - 1;
    memcpy(out, text_[i], n);
    out[n] = 0;
    cursor++;
    return (int)n;
  }

 private:
  char text_[LINES][LEN];
  uint16_t len_[LINES];
  uint32_t total_ = 0;
};

}  // namespace oralb
