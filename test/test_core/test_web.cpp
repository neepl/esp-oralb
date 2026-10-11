#include <string.h>
#include <unity.h>

#include "config.h"
#include "feedback.h"
#include "jsonw.h"
#include "settings.h"
#include "timesrc.h"

using namespace oralb;

static void test_defaults_match_config() {
  Settings s = settings_defaults();
  TEST_ASSERT_EQUAL_FLOAT(WEATHER_LAT, s.lat);
  TEST_ASSERT_EQUAL_FLOAT(WEATHER_LON, s.lon);
  TEST_ASSERT_EQUAL_STRING(ALERT_REGION_ID, s.alert_region);
  TEST_ASSERT_EQUAL_STRING(TZ_POSIX, s.tz);
  TEST_ASSERT_EQUAL_INT(23, s.night_start);
  TEST_ASSERT_EQUAL_INT(7, s.night_end);
  TEST_ASSERT_EQUAL_INT(8, s.night_wake_s);
  TEST_ASSERT_EQUAL_INT(LED_MAX, s.led_max);
  TEST_ASSERT_TRUE(s.show_weather && s.show_alert && s.show_zones);
  TEST_ASSERT_EQUAL_INT(LANG_UK, s.lang);
  const char* e = nullptr;
  TEST_ASSERT_TRUE(settings_check(s, &e));
}

static void test_settings_set_valid() {
  Settings s = settings_defaults();
  const char* e = nullptr;
  TEST_ASSERT_TRUE(settings_set(s, "lat", "48.8566", &e));
  TEST_ASSERT_TRUE(settings_set(s, "lon", "-2.35", &e));
  TEST_ASSERT_TRUE(settings_set(s, "loc", "\xD0\x9B\xD1\x8C\xD0\xB2\xD1\x96\xD0\xB2", &e));  // Львів
  TEST_ASSERT_TRUE(settings_set(s, "region", "14", &e));
  TEST_ASSERT_TRUE(settings_set(s, "night_start", "0", &e));
  TEST_ASSERT_TRUE(settings_set(s, "show_alert", "off", &e));
  TEST_ASSERT_TRUE(settings_set(s, "show_zones", "0", &e));
  TEST_ASSERT_TRUE(settings_set(s, "tz", "<+03>-3", &e));
  TEST_ASSERT_EQUAL_FLOAT(48.8566f, s.lat);
  TEST_ASSERT_EQUAL_FLOAT(-2.35f, s.lon);
  TEST_ASSERT_EQUAL_STRING("14", s.alert_region);
  TEST_ASSERT_EQUAL_INT(0, s.night_start);
  TEST_ASSERT_FALSE(s.show_alert);
  TEST_ASSERT_FALSE(s.show_zones);
  TEST_ASSERT_TRUE(s.show_weather);
}

static void test_settings_lang() {
  Settings s = settings_defaults();
  const char* e = nullptr;
  TEST_ASSERT_TRUE(settings_set(s, "lang", "en", &e));
  TEST_ASSERT_EQUAL_INT(LANG_EN, s.lang);
  TEST_ASSERT_TRUE(settings_valid(s));
  char json[640];
  TEST_ASSERT_TRUE(settings_to_json(s, json, sizeof json));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"lang\":\"en\"}"));
  TEST_ASSERT_TRUE(settings_set(s, "lang", "uk", &e));
  TEST_ASSERT_EQUAL_INT(LANG_UK, s.lang);
  TEST_ASSERT_TRUE(settings_to_json(s, json, sizeof json));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"lang\":\"uk\"}"));
  const char* bad[] = {"", "EN", "de", "english", "1"};
  for (const char* b : bad) TEST_ASSERT_FALSE_MESSAGE(settings_set(s, "lang", b, &e), b);
  TEST_ASSERT_EQUAL_INT(LANG_UK, s.lang);  // unchanged by the rejected values
  s.lang = 9;  // garbage byte from a corrupt blob
  TEST_ASSERT_FALSE(settings_valid(s));
}

static void test_settings_set_invalid() {
  Settings s = settings_defaults(), ref = settings_defaults();
  const char* bad[][2] = {{"lat", "91"},        {"lat", "abc"},       {"lat", ""},         {"lat", "nan"},
                          {"lon", "180.5"},     {"loc", "this name is far too long for it"},
                          {"loc", "bad\x01"},   {"loc", "\xC3"},      {"region", ""},      {"region", "12345678"},
                          {"region", "a/b"},    {"tz", "x"},          {"tz", "EET;rm"},    {"night_start", "24"},
                          {"night_end", "-1"},  {"night_wake", "0"},  {"night_wake", "601"}, {"ldr_bright", "4096"},
                          {"bl_max", "256"},    {"led_max", "1.5"},   {"led_max", "12x"},  {"show_alert", "maybe"},
                          {"nope", "1"}};
  for (auto& b : bad) {
    const char* e = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(settings_set(s, b[0], b[1], &e), b[0]);
    TEST_ASSERT_NOT_NULL(e);
  }
  TEST_ASSERT_EQUAL_MEMORY(&ref, &s, sizeof s);  // nothing changed
}

static void test_settings_valid() {
  Settings s = settings_defaults();
  TEST_ASSERT_TRUE(settings_valid(s));
  s.lat = 123;
  TEST_ASSERT_FALSE(settings_valid(s));
  s = settings_defaults();
  memset(s.tz, 'A', sizeof s.tz);  // not terminated
  TEST_ASSERT_FALSE(settings_valid(s));
  s = settings_defaults();
  s.night_wake_s = -3;
  TEST_ASSERT_FALSE(settings_valid(s));
  s = settings_defaults();
  *(unsigned char*)&s.show_alert = 7;
  TEST_ASSERT_FALSE(settings_valid(s));
}

static void test_settings_check_cross() {
  Settings s = settings_defaults();
  const char* e = nullptr;
  s.bl_min = 200;
  s.bl_max = 100;
  TEST_ASSERT_FALSE(settings_check(s, &e));
  s = settings_defaults();
  s.ldr_dark = s.ldr_bright;
  TEST_ASSERT_FALSE(settings_check(s, &e));
}

static void test_settings_keys_all_accepted() {
  Settings s = settings_defaults();
  char json[512];
  TEST_ASSERT_TRUE(settings_to_json(s, json, sizeof json));
  for (int i = 0; i < SETTINGS_KEY_COUNT; i++) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\":", SETTINGS_KEYS[i]);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json, pat), SETTINGS_KEYS[i]);
  }
}

static void test_settings_json() {
  Settings s = settings_defaults();
  strcpy(s.loc_name, "a\"b\\c");
  char json[512];
  TEST_ASSERT_TRUE(settings_to_json(s, json, sizeof json));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"loc\":\"a\\\"b\\\\c\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"lat\":50.4500"));
  TEST_ASSERT_NOT_NULL(strstr(json, "\"show_zones\":true,\"lang\":\"uk\"}"));
  TEST_ASSERT_FALSE(settings_to_json(s, json, 40));  // too small: reported, still terminated
}

static void test_json_writer() {
  char b[200];
  JsonW j(b, sizeof b);
  j.begin().str("a", "x").i64("n", -5).boolean("t", false).begin("sub").num("f", 1.5, 1).end().str("z", "\n\x01").end();
  TEST_ASSERT_TRUE(j.ok());
  TEST_ASSERT_EQUAL_STRING("{\"a\":\"x\",\"n\":-5,\"t\":false,\"sub\":{\"f\":1.5},\"z\":\"\\n\\u0001\"}", b);
  char e[8];
  TEST_ASSERT_FALSE(json_escape("0123456789", e, sizeof e));
  char u[16];
  TEST_ASSERT_TRUE(json_escape("\xD0\x96", u, sizeof u));  // UTF-8 passes through
  TEST_ASSERT_EQUAL_STRING("\xD0\x96", u);
}

static void test_night_hours() {
  TEST_ASSERT_TRUE(is_night_hour(23, 23, 7));
  TEST_ASSERT_TRUE(is_night_hour(0, 23, 7));
  TEST_ASSERT_TRUE(is_night_hour(6, 23, 7));
  TEST_ASSERT_FALSE(is_night_hour(7, 23, 7));
  TEST_ASSERT_FALSE(is_night_hour(22, 23, 7));
  TEST_ASSERT_TRUE(is_night_hour(2, 1, 5));   // same-day window
  TEST_ASSERT_FALSE(is_night_hour(5, 1, 5));
  TEST_ASSERT_FALSE(is_night_hour(0, 1, 5));
  TEST_ASSERT_FALSE(is_night_hour(3, 4, 4));  // start == end: never
}

static void test_boot_time_decision() {
  const int64_t t = 1760000000;
  BootTime b = decide_boot_time(t, t - 600);  // system time kept (software reset)
  TEST_ASSERT_EQUAL_INT((int)TimeSource::RTC, (int)b.source);
  TEST_ASSERT_EQUAL_INT64(0, b.set_epoch);
  b = decide_boot_time(42, t);  // power-on: uptime only, restore from NVS
  TEST_ASSERT_EQUAL_INT((int)TimeSource::RESTORED, (int)b.source);
  TEST_ASSERT_EQUAL_INT64(t, b.set_epoch);
  b = decide_boot_time(42, 0);  // nothing at all
  TEST_ASSERT_EQUAL_INT((int)TimeSource::NONE, (int)b.source);
  TEST_ASSERT_EQUAL_INT64(0, b.set_epoch);
  b = decide_boot_time(t, 0);  // valid system time, never saved
  TEST_ASSERT_EQUAL_INT((int)TimeSource::RTC, (int)b.source);
  b = decide_boot_time(1700000100, t);  // system time days behind the saved value: distrust it
  TEST_ASSERT_EQUAL_INT((int)TimeSource::RESTORED, (int)b.source);
  b = decide_boot_time(t - 3600, t);  // an hour behind (NTP step back): still fine
  TEST_ASSERT_EQUAL_INT((int)TimeSource::RTC, (int)b.source);
  b = decide_boot_time(42, 5000000000LL);  // saved value implausible (garbage)
  TEST_ASSERT_EQUAL_INT((int)TimeSource::NONE, (int)b.source);
  TEST_ASSERT_EQUAL_STRING("restored", time_source_name(TimeSource::RESTORED));
  TEST_ASSERT_TRUE(TimeSource::NONE < TimeSource::RESTORED && TimeSource::RESTORED < TimeSource::RTC &&
                   TimeSource::RTC < TimeSource::MANUAL && TimeSource::MANUAL < TimeSource::NTP);
}

static void test_interval_elapsed() {
  TEST_ASSERT_TRUE(interval_elapsed(700000, 100000, 600000));
  TEST_ASSERT_FALSE(interval_elapsed(699999, 100000, 600000));
  TEST_ASSERT_TRUE(interval_elapsed(5, 0xFFFFFFF0u, 20));  // millis() wrap
}

static void test_parse_local_datetime() {
  LocalDateTime d;
  TEST_ASSERT_TRUE(parse_local_datetime("2026-10-11T14:30", d));
  TEST_ASSERT_TRUE(d.year == 2026 && d.month == 10 && d.day == 11 && d.hour == 14 && d.min == 30 && d.sec == 0);
  TEST_ASSERT_TRUE(parse_local_datetime("2028-02-29 23:59:58", d));
  TEST_ASSERT_EQUAL_INT(58, d.sec);
  TEST_ASSERT_FALSE(parse_local_datetime("2027-02-29T10:00", d));
  TEST_ASSERT_FALSE(parse_local_datetime("2026-13-01T10:00", d));
  TEST_ASSERT_FALSE(parse_local_datetime("2026-10-11T24:00", d));
  TEST_ASSERT_FALSE(parse_local_datetime("2026-10-11", d));
  TEST_ASSERT_FALSE(parse_local_datetime("2026-10-11T10:00Z", d));
  TEST_ASSERT_FALSE(parse_local_datetime("2010-01-01T10:00", d));
  TEST_ASSERT_FALSE(parse_local_datetime(nullptr, d));
}

static void test_backlight_set_params() {
  BacklightControl c(150, 1500, 20, 255);
  for (int i = 0; i < 100; i++) c.update(100);
  TEST_ASSERT_EQUAL_INT(255, c.level());
  c.set_params(150, 1500, 20, 100);  // lower max: slews down to the new target
  for (int i = 0; i < 100; i++) c.update(100);
  TEST_ASSERT_EQUAL_INT(100, c.level());
}

void run_web_tests() {
  RUN_TEST(test_defaults_match_config);
  RUN_TEST(test_settings_set_valid);
  RUN_TEST(test_settings_set_invalid);
  RUN_TEST(test_settings_lang);
  RUN_TEST(test_settings_valid);
  RUN_TEST(test_settings_check_cross);
  RUN_TEST(test_settings_keys_all_accepted);
  RUN_TEST(test_settings_json);
  RUN_TEST(test_json_writer);
  RUN_TEST(test_night_hours);
  RUN_TEST(test_boot_time_decision);
  RUN_TEST(test_interval_elapsed);
  RUN_TEST(test_parse_local_datetime);
  RUN_TEST(test_backlight_set_params);
}
