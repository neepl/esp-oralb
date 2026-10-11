// Live settings (web UI): one Settings instance behind a spinlock, copy-out access, persisted in NVS ("cfg").
#pragma once
#include <stdint.h>

#include "settings.h"

void settings_begin();                          // load from NVS (defaults when missing or invalid); call early in setup()
oralb::Settings settings_get();                 // consistent copy, safe from any task
uint32_t settings_generation();                 // bumps on every change; cheap way to notice updates
bool settings_commit(const oralb::Settings& s); // validate, publish, persist; false if invalid
