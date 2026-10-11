// Tiny JSON writer into a caller-provided buffer (no heap, no Arduino). Output is truncated and
// ok() turns false when the buffer is too small. Strings are escaped; UTF-8 passes through.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace oralb {

// Escapes `in` for use between JSON quotes. Returns false if `out` is too small (output is still terminated).
bool json_escape(const char* in, char* out, size_t cap);

class JsonW {
 public:
  JsonW(char* buf, size_t cap) : b_(buf), cap_(cap) { if (cap_) b_[0] = 0; }
  JsonW& begin(const char* key = nullptr);  // '{' (nested when key is given)
  JsonW& end();                             // '}'
  JsonW& str(const char* key, const char* value);
  JsonW& num(const char* key, double value, int decimals);
  JsonW& i64(const char* key, int64_t value);
  JsonW& boolean(const char* key, bool value);
  bool ok() const { return !bad_; }
  size_t size() const { return n_; }
  const char* c_str() const { return b_; }

 private:
  char* b_;
  size_t cap_, n_ = 0;
  bool bad_ = false, first_ = true;
  void put(const char* s);
  void key(const char* k);
};

}  // namespace oralb
