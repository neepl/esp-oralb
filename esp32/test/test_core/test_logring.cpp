#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "logring.h"

using namespace oralb;

static void push(LogRingT<4, 16>& r, const char* s) { r.push(s, strlen(s)); }

static void test_replay_order_and_wraparound() {
  LogRingT<4, 16> r;
  push(r, "a");
  push(r, "b");
  uint32_t c = r.oldest();
  char b[32];
  TEST_ASSERT_EQUAL_INT(1, r.next(c, b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("a", b);
  TEST_ASSERT_EQUAL_INT(1, r.next(c, b, sizeof b));
  TEST_ASSERT_EQUAL_STRING("b", b);
  TEST_ASSERT_EQUAL_INT(-1, r.next(c, b, sizeof b));  // caught up
  // wrap: push 6 more (total 8), ring holds the last 4
  for (const char* s : {"c", "d", "e", "f", "g", "h"}) push(r, s);
  uint32_t fresh = r.oldest();
  const char* want[4] = {"e", "f", "g", "h"};
  for (const char* w : want) {
    TEST_ASSERT_TRUE(r.next(fresh, b, sizeof b) >= 0);
    TEST_ASSERT_EQUAL_STRING(w, b);
  }
  TEST_ASSERT_EQUAL_INT(-1, r.next(fresh, b, sizeof b));
}

static void test_slow_reader_skips_oldest() {
  LogRingT<4, 16> r;
  for (const char* s : {"1", "2"}) push(r, s);
  uint32_t c = r.oldest();  // 0
  char b[32];
  r.next(c, b, sizeof b);  // read "1"
  for (const char* s : {"3", "4", "5", "6", "7"}) push(r, s);  // total 7, holds 4..7, reader at 1
  uint32_t skipped = 0;
  TEST_ASSERT_TRUE(r.next(c, b, sizeof b, &skipped) >= 0);
  TEST_ASSERT_EQUAL_UINT32(2, skipped);  // lines "2" and "3" were overwritten
  TEST_ASSERT_EQUAL_STRING("4", b);
}

static void test_long_line_truncated() {
  LogRingT<4, 16> r;
  r.push("0123456789ABCDEFGHIJ", 20);
  uint32_t c = r.oldest();
  char b[32];
  int n = r.next(c, b, sizeof b);
  TEST_ASSERT_EQUAL_INT(15, n);
  TEST_ASSERT_EQUAL_STRING("0123456789ABCD~", b);
  // reader buffer smaller than the line: cut, still terminated
  LogRingT<4, 16> q;
  q.push("hello world", 11);
  uint32_t c2 = q.oldest();
  char small[6];
  TEST_ASSERT_EQUAL_INT(5, q.next(c2, small, sizeof small));
  TEST_ASSERT_EQUAL_STRING("hello", small);
}

static void test_two_independent_readers() {
  LogRingT<4, 16> r;
  push(r, "x");
  push(r, "y");
  uint32_t a = r.oldest(), b2 = r.oldest();
  char b[32];
  r.next(a, b, sizeof b);
  r.next(a, b, sizeof b);
  TEST_ASSERT_EQUAL_INT(-1, r.next(a, b, sizeof b));
  TEST_ASSERT_TRUE(r.next(b2, b, sizeof b) >= 0);
  TEST_ASSERT_EQUAL_STRING("x", b);
  push(r, "z");
  TEST_ASSERT_TRUE(r.next(a, b, sizeof b) >= 0);
  TEST_ASSERT_EQUAL_STRING("z", b);
}

void run_logring_tests() {
  RUN_TEST(test_replay_order_and_wraparound);
  RUN_TEST(test_slow_reader_skips_oldest);
  RUN_TEST(test_long_line_truncated);
  RUN_TEST(test_two_independent_readers);
}
