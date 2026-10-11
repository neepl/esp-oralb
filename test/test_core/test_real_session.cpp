// Replay the real iO 8 capture (changes only) through decoder + session; same checks as
// tools/python/tests/test_real_session.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "session.h"

using namespace oralb;

struct Rec {
  int t;
  BrushFrame f;
};

static std::vector<Rec> load() {
  std::vector<Rec> out;
  FILE* fp = fopen(TEST_DATA_DIR "/io8_session.txt", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(fp, "cannot open test/data/io8_session.txt (run from the repository root)");
  char line[256];
  while (fgets(line, sizeof line, fp)) {
    int h, m, s, pos = 0;
    if (sscanf(line, "%d:%d:%d%n", &h, &m, &s, &pos) < 3) continue;
    uint8_t bytes[16];
    size_t n = 0;
    char* p = line + pos;
    while (*p && n < sizeof bytes) {
      while (*p == ' ') p++;
      if (!p[0] || !p[1] || p[0] == '\n') break;
      char hex[3] = {p[0], p[1], 0};
      bytes[n++] = (uint8_t)strtol(hex, nullptr, 16);
      p += 2;
    }
    Rec r;
    r.t = h * 3600 + m * 60 + s;
    TEST_ASSERT_TRUE(parse(bytes, n, r.f));
    out.push_back(r);
  }
  fclose(fp);
  return out;
}

static int hms(int h, int m, int s) { return h * 3600 + m * 60 + s; }

static void test_every_frame_decodes_as_io() {
  auto recs = load();
  TEST_ASSERT_TRUE(recs.size() > 50);
  for (auto& r : recs) {
    TEST_ASSERT_EQUAL_STRING("iO Series", r.f.model_name());
    TEST_ASSERT_TRUE(strcmp(r.f.mode_name(), "daily clean") == 0 || r.f.mode == 8);
  }
}

static void test_session_replay() {
  Session s;
  std::map<int, Screen> screens;
  for (auto& r : load()) {
    s.on_frame(r.f, r.t, r.t);
    screens[r.t] = s.screen;
  }
  TEST_ASSERT_TRUE(screens[hms(16, 30, 45)] == Screen::CLOCK);     // selection menu
  TEST_ASSERT_TRUE(screens[hms(16, 30, 50)] == Screen::BRUSHING);
  TEST_ASSERT_TRUE(screens[hms(16, 31, 7)] == Screen::RESULT);     // paused at 0:17
  TEST_ASSERT_TRUE(screens[hms(16, 31, 8)] == Screen::BRUSHING);   // resumed at 0:18
  // final stop after the timer restarted at 0:00 with four pressure episodes
  TEST_ASSERT_TRUE(s.screen == Screen::RESULT);
  TEST_ASSERT_EQUAL_INT(15, s.last.seconds);
  TEST_ASSERT_EQUAL_INT(4, s.last.high_pressure_hits);
}

void run_real_session_tests() {
  RUN_TEST(test_every_frame_decodes_as_io);
  RUN_TEST(test_session_replay);
}
