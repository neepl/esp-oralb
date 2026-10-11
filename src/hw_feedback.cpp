#include "hw_feedback.h"

#include <Arduino.h>

#include "board.h"
#include "config.h"

using namespace oralb;

namespace {
constexpr int LED_FREQ = 5000;
const uint8_t LED_PINS[3] = {BOARD_LED_R, BOARD_LED_G, BOARD_LED_B};
constexpr int LED_OFF_LEVEL = LED_ACTIVE_LOW ? HIGH : LOW;

}  // namespace

void led_early_off() {
  for (uint8_t p : LED_PINS) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LED_OFF_LEVEL);
  }
}

void led_begin() {
  // Arduino-ESP32 3.x LEDC: attach by pin (channel chosen by the core), write by pin.
  for (int i = 0; i < 3; i++) ledcAttach(LED_PINS[i], LED_FREQ, 8);
  led_set({0, 0, 0});
}

void led_set(Rgb c) {
  const uint8_t v[3] = {c.r, c.g, c.b};
  for (int i = 0; i < 3; i++) {
    int level = v[i] * LED_MAX / 255;
    ledcWrite(LED_PINS[i], LED_ACTIVE_LOW ? 255 - level : level);
  }
}

void ldr_begin() {
  // Core 3.x: the pin must already be an ADC channel (first analogRead) for the attenuation to apply, and a
  // later pinMode() would detach it again (back to the 11 dB default the thresholds weren't calibrated for).
  analogRead(BOARD_LDR);
  analogSetPinAttenuation(BOARD_LDR, ADC_0db);
}

int ldr_read() {
  int sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(BOARD_LDR);
  return sum / 4;
}
