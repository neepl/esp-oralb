#include "hw_feedback.h"

#include <Arduino.h>

#include "board.h"
#include "config.h"

using namespace oralb;

namespace {
// LEDC channels: 0-2 LED (timers 0/1); the backlight uses channel 7 (timer 3).
constexpr int CH_R = 0, CH_G = 1, CH_B = 2;
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
  const int ch[3] = {CH_R, CH_G, CH_B};
  for (int i = 0; i < 3; i++) {
    ledcSetup(ch[i], LED_FREQ, 8);
    ledcAttachPin(LED_PINS[i], ch[i]);
  }
  led_set({0, 0, 0});
}

void led_set(Rgb c) {
  const uint8_t v[3] = {c.r, c.g, c.b};
  const int ch[3] = {CH_R, CH_G, CH_B};
  for (int i = 0; i < 3; i++) {
    int level = v[i] * LED_MAX / 255;
    ledcWrite(ch[i], LED_ACTIVE_LOW ? 255 - level : level);
  }
}

void ldr_begin() {
  analogSetPinAttenuation(BOARD_LDR, ADC_0db);
  pinMode(BOARD_LDR, INPUT);
}

int ldr_read() {
  int sum = 0;
  for (int i = 0; i < 4; i++) sum += analogRead(BOARD_LDR);
  return sum / 4;
}
