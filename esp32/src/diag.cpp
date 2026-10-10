#include "diag.h"

#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "logring.h"

namespace {

oralb::LogRingT<LOG_RING_LINES, LOG_LINE_MAX> ring;  // static: no heap, no fragmentation
portMUX_TYPE ring_mux = portMUX_INITIALIZER_UNLOCKED;

void ring_push_text(const char* s, size_t n) {
  // split on newlines, strip CR, skip empty lines
  size_t start = 0;
  for (size_t i = 0; i <= n; i++) {
    if (i == n || s[i] == '\n') {
      size_t e = i;
      while (e > start && s[e - 1] == '\r') e--;
      if (e > start) {
        portENTER_CRITICAL(&ring_mux);
        ring.push(s + start, e - start);
        portEXIT_CRITICAL(&ring_mux);
      }
      start = i + 1;
    }
  }
}

int log_hook(const char* fmt, va_list ap) {
  if (!xPortInIsrContext()) {
    va_list ap2;
    va_copy(ap2, ap);
    char buf[LOG_LINE_MAX + 40];
    int n = vsnprintf(buf, sizeof buf, fmt, ap2);
    va_end(ap2);
    if (n > 0) ring_push_text(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
  }
  return vprintf(fmt, ap);  // keep the UART output
}

}  // namespace

void diag_install() { esp_log_set_vprintf(log_hook); }

void diag_printf(const char* fmt, ...) {
  char buf[LOG_LINE_MAX + 40];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  size_t len = (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;
  Serial.write((const uint8_t*)buf, len);
  Serial.write('\n');
  ring_push_text(buf, len);
}

uint32_t diag_ring_oldest() {
  portENTER_CRITICAL(&ring_mux);
  uint32_t v = ring.oldest();
  portEXIT_CRITICAL(&ring_mux);
  return v;
}

int diag_ring_next(uint32_t& cursor, char* out, size_t cap, uint32_t* skipped) {
  portENTER_CRITICAL(&ring_mux);
  int n = ring.next(cursor, out, cap, skipped);
  portEXIT_CRITICAL(&ring_mux);
  return n;
}
