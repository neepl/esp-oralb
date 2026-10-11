// Diagnostics: one line each, to Serial (115200) and to the ring buffer served over telnet.
// ESP-IDF log output (E (123) wifi: ...) is captured into the ring too (diag_install()).
#pragma once
#include <Arduino.h>
#include <stdint.h>

void diag_install();  // hook esp_log (still forwarded to the UART); call first thing in setup()
void diag_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define DIAG(fmt, ...) diag_printf("[%7.1f] " fmt, millis() / 1000.0, ##__VA_ARGS__)

// Ring access for the telnet server (thread safe)
uint32_t diag_ring_oldest();

// Why the firmware restarted itself: kept in RTC memory across the software reset, logged at the next boot.
void diag_note_restart(const char* why);
const char* diag_take_restart_note();  // the note left by the previous run ("" if none); clears it
// next line for a reader cursor: length, or -1 if nothing new; *skipped = lines the reader missed
int diag_ring_next(uint32_t& cursor, char* out, size_t cap, uint32_t* skipped);
