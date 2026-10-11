#include "timesrc.h"

#include <stdlib.h>

namespace oralb {

const char* time_source_name(TimeSource s) {
  switch (s) {
    case TimeSource::RESTORED: return "restored";
    case TimeSource::RTC: return "rtc";
    case TimeSource::MANUAL: return "manual";
    case TimeSource::NTP: return "ntp";
    default: return "none";
  }
}

bool epoch_plausible(int64_t e) { return e >= EPOCH_MIN_VALID && e < EPOCH_MAX_VALID; }

BootTime decide_boot_time(int64_t sys, int64_t saved) {
  BootTime r;
  bool sys_ok = epoch_plausible(sys), saved_ok = epoch_plausible(saved);
  if (sys_ok && (!saved_ok || sys + 86400 >= saved)) {
    r.source = TimeSource::RTC;
  } else if (saved_ok) {
    r.source = TimeSource::RESTORED;
    r.set_epoch = saved;
  }
  return r;
}

namespace {
bool num(const char*& p, int digits, int& out) {
  int v = 0;
  for (int i = 0; i < digits; i++, p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (*p - '0');
  }
  out = v;
  return true;
}
}  // namespace

bool parse_local_datetime(const char* s, LocalDateTime& out) {
  if (!s) return false;
  const char* p = s;
  LocalDateTime t;
  if (!num(p, 4, t.year) || *p++ != '-' || !num(p, 2, t.month) || *p++ != '-' || !num(p, 2, t.day)) return false;
  if (*p != 'T' && *p != ' ') return false;
  p++;
  if (!num(p, 2, t.hour) || *p++ != ':' || !num(p, 2, t.min)) return false;
  if (*p == ':') {
    p++;
    if (!num(p, 2, t.sec)) return false;
  }
  if (*p) return false;
  if (t.year < 2024 || t.year > 2099 || t.month < 1 || t.month > 12 || t.day < 1 || t.hour > 23 || t.min > 59 ||
      t.sec > 59)
    return false;
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = dim[t.month - 1] + (t.month == 2 && t.year % 4 == 0 ? 1 : 0);  // 2024-2099: every 4th year is leap
  if (t.day > days) return false;
  out = t;
  return true;
}

}  // namespace oralb
