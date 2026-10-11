#include "touch.h"

#include <Arduino.h>

#include "board.h"

namespace {

constexpr int SDA = BOARD_TOUCH_SDA, SCL = BOARD_TOUCH_SCL;
constexpr uint8_t ADDR = BOARD_TOUCH_ADDR;
constexpr int HALF_US = 5;  // ~100 kHz

// Open-drain emulation: drive low, or release (external/internal pull-up pulls high).
inline void sda_lo() { pinMode(SDA, OUTPUT); digitalWrite(SDA, LOW); }
inline void sda_hi() { pinMode(SDA, INPUT_PULLUP); }
inline void scl_lo() { pinMode(SCL, OUTPUT); digitalWrite(SCL, LOW); }
inline void scl_hi() { pinMode(SCL, INPUT_PULLUP); }
inline bool sda_read() { return digitalRead(SDA); }

void start() { sda_hi(); scl_hi(); delayMicroseconds(HALF_US); sda_lo(); delayMicroseconds(HALF_US); scl_lo(); }
void stop() { sda_lo(); delayMicroseconds(HALF_US); scl_hi(); delayMicroseconds(HALF_US); sda_hi(); delayMicroseconds(HALF_US); }

bool write_byte(uint8_t b) {  // returns true if ACKed
  for (int i = 7; i >= 0; i--) {
    if (b & (1 << i)) sda_hi(); else sda_lo();
    delayMicroseconds(HALF_US);
    scl_hi();
    delayMicroseconds(HALF_US);
    scl_lo();
  }
  sda_hi();
  delayMicroseconds(HALF_US);
  scl_hi();
  delayMicroseconds(HALF_US);
  bool ack = !sda_read();
  scl_lo();
  return ack;
}

uint8_t read_byte(bool ack) {
  uint8_t v = 0;
  sda_hi();
  for (int i = 0; i < 8; i++) {
    delayMicroseconds(HALF_US);
    scl_hi();
    delayMicroseconds(HALF_US);
    v = (uint8_t)((v << 1) | (sda_read() ? 1 : 0));
    scl_lo();
  }
  if (ack) sda_lo(); else sda_hi();
  delayMicroseconds(HALF_US);
  scl_hi();
  delayMicroseconds(HALF_US);
  scl_lo();
  sda_hi();
  return v;
}

bool write_reg(uint8_t reg, uint8_t val) {
  start();
  bool ok = write_byte(ADDR << 1) && write_byte(reg) && write_byte(val);
  stop();
  return ok;
}

bool read_reg(uint8_t reg, uint8_t* out, int n) {
  start();
  bool ok = write_byte(ADDR << 1) && write_byte(reg);
  if (ok) {
    start();  // repeated start
    ok = write_byte((ADDR << 1) | 1);
    for (int i = 0; ok && i < n; i++) out[i] = read_byte(i < n - 1);
  }
  stop();
  return ok;
}

bool inited = false;
uint32_t last_poll_ms = 0;
bool last_state = false;

}  // namespace

void touch_begin() {
  pinMode(BOARD_TOUCH_RST, OUTPUT);
  digitalWrite(BOARD_TOUCH_RST, LOW);
  delay(10);
  digitalWrite(BOARD_TOUCH_RST, HIGH);
  delay(10);
  pinMode(BOARD_TOUCH_INT, INPUT_PULLUP);
  sda_hi();
  scl_hi();
  // The chip may not answer while in power-save; setup is retried on the first successful poll.
  uint8_t id[3];
  inited = write_reg(0x00, 0x00) && read_reg(0xA7, id, 3);
  if (inited) {
    write_reg(0xFA, 0x20);  // INT pulse on change (as LovyanGFX does)
    write_reg(0xED, 20);
  }
}

bool touch_down() {
  uint32_t ms = millis();
  if (ms - last_poll_ms < 10) return last_state;  // the chip refreshes at ~10 ms
  last_poll_ms = ms;
  uint8_t b[1];
  bool ok = read_reg(0x02, b, 1);  // number of touch points
  if (ok && !inited) {
    write_reg(0xFA, 0x20);
    write_reg(0xED, 20);
    inited = true;
  }
  last_state = ok && (b[0] & 0x0F) > 0;  // any finger count wakes
  return last_state;
}
