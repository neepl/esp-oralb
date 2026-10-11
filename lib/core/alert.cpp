#include "alert.h"

#include <stdlib.h>
#include <string.h>

namespace oralb {

namespace {

struct Span {
  const char* b = nullptr;  // first char (the opening bracket)
  const char* e = nullptr;  // one past the matching closing bracket
};

// Skip a JSON string starting at the opening quote; returns the char after the closing quote.
const char* skip_string(const char* p, const char* end) {
  p++;
  while (p < end && *p != '"') p += (*p == '\\' && p + 1 < end) ? 2 : 1;
  return p < end ? p + 1 : end;
}

// Match the bracket at p ('[' or '{') honouring strings and nesting.
bool match(const char* p, const char* end, Span& s) {
  char open = *p, close = open == '[' ? ']' : '}';
  int depth = 0;
  for (const char* q = p; q < end;) {
    if (*q == '"') { q = skip_string(q, end); continue; }
    if (*q == '[' || *q == '{') depth++;
    else if (*q == ']' || *q == '}') {
      depth--;
      if (depth == 0) {
        if (*q != close) return false;
        s.b = p;
        s.e = q + 1;
        return true;
      }
    }
    q++;
  }
  return false;
}

// Find `"key":` in [b, e); returns the first char of the value (after whitespace) or nullptr.
const char* find_key(const char* b, const char* e, const char* key) {
  char pat[48];
  size_t n = strlen(key);
  if (n + 4 > sizeof pat) return nullptr;
  pat[0] = '"';
  memcpy(pat + 1, key, n);
  pat[n + 1] = '"';
  pat[n + 2] = 0;
  for (const char* p = b; p < e;) {
    if (*p == '"') {
      if ((size_t)(e - p) > n + 2 && strncmp(p, pat, n + 2) == 0) {
        const char* v = p + n + 2;
        while (v < e && (*v == ' ' || *v == ':')) v++;
        return v;
      }
      p = skip_string(p, e);
      // a string that is a value (not our key) is skipped whole, keys inside nested values are still reached
      continue;
    }
    p++;
  }
  return nullptr;
}

// Copy a string value starting at the opening quote; false if not a string.
bool read_string(const char* v, const char* e, char* out, size_t cap) {
  if (!v || v >= e || *v != '"') return false;
  const char* q = v + 1;
  size_t n = 0;
  while (q < e && *q != '"' && n + 1 < cap) out[n++] = *q++;
  out[n] = 0;
  return q < e;
}

struct Best {
  bool have = false;
  bool has_since = false;
  int64_t since = 0;
  void offer(bool hs, int64_t s) {
    if (!have || (hs && (!has_since || s < since))) {
      have = true;
      if (hs) { has_since = true; since = s; }
    }
  }
};

}  // namespace

int64_t parse_iso_utc(const char* s) {
  int y, mo, d, h, mi, sec;
  if (!s || strlen(s) < 19) return -1;
  char* e;
  y = (int)strtol(s, &e, 10);
  if (*e != '-') return -1;
  mo = (int)strtol(e + 1, &e, 10);
  if (*e != '-') return -1;
  d = (int)strtol(e + 1, &e, 10);
  if (*e != 'T') return -1;
  h = (int)strtol(e + 1, &e, 10);
  if (*e != ':') return -1;
  mi = (int)strtol(e + 1, &e, 10);
  if (*e != ':') return -1;
  sec = (int)strtol(e + 1, &e, 10);
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return -1;
  // days from civil (Howard Hinnant)
  y -= mo <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = era * 146097 + doe - 719468;
  return days * 86400 + h * 3600 + mi * 60 + sec;
}

bool parse_alerts(const char* json, AlertResult& out) {
  if (!json) return false;
  const char* end = json + strlen(json);
  const char* v = find_key(json, end, "activeAlerts");
  if (!v || *v != '[') return false;
  Span alerts;
  if (!match(v, end, alerts)) return false;

  Best red, yellow;
  for (const char* p = alerts.b + 1; p < alerts.e - 1;) {
    if (*p != '{') { p++; continue; }
    Span obj;
    if (!match(p, alerts.e - 1, obj)) return false;
    p = obj.e;

    char ts[40];
    int64_t alert_since = -1;
    if (read_string(find_key(obj.b, obj.e, "lastUpdate"), obj.e, ts, sizeof ts)) alert_since = parse_iso_utc(ts);

    const char* lv = find_key(obj.b, obj.e, "activeAlertLevels");
    Span levels;
    bool have_levels = lv && *lv == '[' && match(lv, obj.e, levels);
    bool any_level = false;
    if (have_levels) {
      for (const char* q = levels.b + 1; q < levels.e - 1;) {
        if (*q != '{') { q++; continue; }
        Span lo;
        if (!match(q, levels.e - 1, lo)) return false;
        q = lo.e;
        any_level = true;
        char name[16];
        if (!read_string(find_key(lo.b, lo.e, "alertLevel"), lo.e, name, sizeof name)) continue;
        Best* target = !strcmp(name, "Red") ? &red : (!strcmp(name, "Yellow") ? &yellow : nullptr);
        if (!target) continue;
        int64_t t = alert_since;
        bool hs = alert_since >= 0;
        if (read_string(find_key(lo.b, lo.e, "createdAt"), lo.e, ts, sizeof ts)) {
          int64_t c = parse_iso_utc(ts);
          if (c >= 0) { t = c; hs = true; }
        }
        target->offer(hs, t);
      }
    }
    if (!have_levels || !any_level) red.offer(alert_since >= 0, alert_since);  // no level info: treat as a full alert
  }

  AlertResult r;
  const Best* b = red.have ? &red : (yellow.have ? &yellow : nullptr);
  r.level = red.have ? AlertLevel::RED : (yellow.have ? AlertLevel::YELLOW : AlertLevel::NONE);
  if (b && b->has_since) {
    r.has_since = true;
    r.since_utc = b->since;
  }
  out = r;
  return true;
}

}  // namespace oralb
