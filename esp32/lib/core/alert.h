// Air-raid alert status (siren.pp.ua API v3, GET /api/v3/alerts/{regionId}); pure C++, no Arduino.
#pragma once
#include <stdint.h>

namespace oralb {

enum class AlertLevel : uint8_t { UNKNOWN, NONE, YELLOW, RED };

struct AlertResult {
  AlertLevel level = AlertLevel::UNKNOWN;
  bool has_since = false;
  int64_t since_utc = 0;  // unix seconds (UTC) of createdAt of the chosen level (alert lastUpdate as fallback)
};

// Parse the reply for one region: `[{"regionId":..,"activeAlerts":[{...,"activeAlertLevels":[...]}]}]`.
// No active alerts -> NONE. Any alert with a "Red" level, or without a (non-empty) level list (a full
// alert) -> RED. Otherwise any "Yellow" -> YELLOW. Levels with other names are ignored. When several
// alerts qualify, the earliest `since` wins. Returns false for malformed input (no `activeAlerts` array).
bool parse_alerts(const char* json, AlertResult& out);

// "2026-10-10T17:30:45.148116Z" (UTC) -> unix seconds; -1 if malformed.
int64_t parse_iso_utc(const char* s);

}  // namespace oralb
