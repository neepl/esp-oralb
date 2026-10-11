// Hardware side of the feedback: status LED (LEDC PWM) and light sensor.
#pragma once
#include <stdint.h>

#include "feedback.h"

void led_early_off();            // call first thing in setup(): drive the LED pins to "off"
void led_begin();                // attach LEDC PWM
void led_set_max(int max);        // cap for led_set() (runtime setting, 0-255)
void led_set(oralb::Rgb c);      // logical 0-255, scaled by LED_MAX and polarity
void ldr_begin();
int ldr_read();                  // raw 12-bit ADC value (a few samples averaged)
