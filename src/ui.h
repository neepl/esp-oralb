// Display layer: LovyanGFX config for the board and banded sprite rendering of oralb::Scene.
// Drawing itself lives in lib/core/screens.cpp (shared with the native pixel tests).
#pragma once
#include <stdint.h>

#include "screens.h"

void ui_begin();
void ui_render(const oralb::Scene& s);
void ui_set_brightness(uint8_t level);
bool ui_touch_down();
void ui_message(const char* line1, const char* line2);  // ASCII/UTF-8, pixel font
