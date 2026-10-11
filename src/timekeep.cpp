#include "timekeep.h"

#include <Arduino.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

#include "diag.h"

using namespace oralb;

namespace {
volatile uint8_t source = (uint8_t)TimeSource::NONE;
volatile bool save_pending = false;
uint32_t last_save_ms = 0;
constexpr uint32_t SAVE_INTERVAL_MS = 10UL * 60UL * 1000UL;  // one 8-byte NVS entry, wear-levelled by NVS

void save_epoch() {
  time_t t = time(nullptr);
  if (source == (uint8_t)TimeSource::NONE || !epoch_plausible(t)) return;
  Preferences p;
  if (p.begin("cfg", false)) {
    p.putLong64("epoch", (int64_t)t);
    p.end();
  }
  last_save_ms = millis();
  save_pending = false;
}
}  // namespace

void timekeep_boot() {
  int64_t saved = 0;
  Preferences p;
  if (p.begin("cfg", true)) {
    saved = p.getLong64("epoch", 0);
    p.end();
  }
  // The IDF keeps the system time across software resets (RTC timer + boot-time offset in RTC store registers);
  // after power-on the system time is just the uptime, i.e. far below EPOCH_MIN_VALID.
  BootTime b = decide_boot_time((int64_t)time(nullptr), saved);
  if (b.set_epoch > 0) {
    struct timeval tv = {(time_t)b.set_epoch, 0};
    settimeofday(&tv, nullptr);
  }
  source = (uint8_t)b.source;
  last_save_ms = millis();
  DIAG("time: source %s, epoch %lld (saved %lld)", time_source_name(b.source), (long long)time(nullptr),
       (long long)saved);
}

TimeSource timekeep_source() { return (TimeSource)source; }

void timekeep_ntp_synced() {
  source = (uint8_t)TimeSource::NTP;  // an NTP sync always overrides
  save_pending = true;
}

bool timekeep_set_manual(int64_t epoch) {
  if (!epoch_plausible(epoch)) return false;
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  source = (uint8_t)TimeSource::MANUAL;
  save_epoch();
  DIAG("time: set manually, epoch %lld", (long long)epoch);
  return true;
}

void timekeep_tick() {
  if (save_pending || interval_elapsed(millis(), last_save_ms, SAVE_INTERVAL_MS)) save_epoch();
}

void timekeep_save_now() { save_epoch(); }
