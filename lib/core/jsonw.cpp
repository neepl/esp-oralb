#include "jsonw.h"

#include <string.h>

namespace oralb {

bool json_escape(const char* in, char* out, size_t cap) {
  size_t n = 0;
  bool ok = true;
  auto put = [&](const char* s) {
    size_t l = strlen(s);
    if (n + l + 1 > cap) { ok = false; return; }
    memcpy(out + n, s, l);
    n += l;
  };
  for (const unsigned char* p = (const unsigned char*)in; *p && ok; p++) {
    char t[8];
    if (*p == '"') put("\\\"");
    else if (*p == '\\') put("\\\\");
    else if (*p == '\n') put("\\n");
    else if (*p == '\r') put("\\r");
    else if (*p == '\t') put("\\t");
    else if (*p < 0x20 || *p == 0x7f) { snprintf(t, sizeof t, "\\u%04x", *p); put(t); }
    else { t[0] = (char)*p; t[1] = 0; put(t); }
  }
  if (cap) out[n < cap ? n : cap - 1] = 0;
  return ok;
}

void JsonW::put(const char* s) {
  size_t l = strlen(s);
  if (n_ + l + 1 > cap_) { bad_ = true; return; }
  memcpy(b_ + n_, s, l + 1);
  n_ += l;
}

void JsonW::key(const char* k) {
  if (!first_) put(",");
  first_ = false;
  if (k) {
    put("\"");
    put(k);
    put("\":");
  }
}

JsonW& JsonW::begin(const char* k) {
  key(k);
  put("{");
  first_ = true;
  return *this;
}

JsonW& JsonW::end() {
  put("}");
  first_ = false;
  return *this;
}

JsonW& JsonW::str(const char* k, const char* v) {
  key(k);
  char esc[200];
  if (!json_escape(v ? v : "", esc, sizeof esc)) bad_ = true;
  put("\"");
  put(esc);
  put("\"");
  return *this;
}

JsonW& JsonW::num(const char* k, double v, int decimals) {
  key(k);
  char t[40];
  snprintf(t, sizeof t, "%.*f", decimals, v);
  put(t);
  return *this;
}

JsonW& JsonW::i64(const char* k, int64_t v) {
  key(k);
  char t[24];
  snprintf(t, sizeof t, "%lld", (long long)v);
  put(t);
  return *this;
}

JsonW& JsonW::boolean(const char* k, bool v) {
  key(k);
  put(v ? "true" : "false");
  return *this;
}

}  // namespace oralb
