// Pure logic for keeping the clock without the internet: time sources, the boot-time decision,
// manual time input parsing (no Arduino).
#pragma once
#include <stdint.h>

namespace oralb {

// Ordered by trust: an NTP sync always overrides; a manual set overrides what the clock restored itself.
enum class TimeSource : uint8_t { NONE, RESTORED, RTC, MANUAL, NTP };
const char* time_source_name(TimeSource s);

constexpr int64_t EPOCH_MIN_VALID = 1700000000;  // 2023-11-14: anything earlier means "clock never set"
constexpr int64_t EPOCH_MAX_VALID = 4102444800;  // 2100-01-01
bool epoch_plausible(int64_t epoch);

struct BootTime {
  TimeSource source = TimeSource::NONE;
  int64_t set_epoch = 0;  // > 0: call settimeofday with this; 0: leave the system time alone
};

// Boot decision, before Wi-Fi. `sys_epoch` is time(NULL) (kept by the IDF across software resets, from the
// RTC timer plus the boot-time offset in the RTC store registers; after power-on it is just the uptime),
// `saved_epoch` the last value stored in NVS (0 if none). A valid system time wins (RTC) unless it is more than
// a day behind the saved value (lost/corrupt RTC registers); otherwise the saved epoch is restored.
BootTime decide_boot_time(int64_t sys_epoch, int64_t saved_epoch);

// Wrap-safe "interval elapsed" for millis() values.
inline bool interval_elapsed(uint32_t now_ms, uint32_t last_ms, uint32_t interval_ms) {
  return (uint32_t)(now_ms - last_ms) >= interval_ms;
}

struct LocalDateTime {
  int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
};
// "YYYY-MM-DDTHH:MM[:SS]" (or a space instead of 'T'), as sent by <input type=datetime-local>. Year 2024-2099.
bool parse_local_datetime(const char* s, LocalDateTime& out);

}  // namespace oralb
