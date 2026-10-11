#include <string.h>
#include <unity.h>

#include "alert.h"

using namespace oralb;

// Real response captured from siren.pp.ua (Kyiv, yellow level)
static const char* YELLOW =
    "[{\"regionId\":\"31\",\"regionType\":\"State\",\"regionName\":\"\xD0\xBC. \xD0\x9A\xD0\xB8\xD1\x97\xD0\xB2\","
    "\"regionEngName\":\"Kyiv\",\"lastUpdate\":\"2026-10-10T17:30:45.148116Z\",\"activeAlerts\":[{\"regionId\":\"31\","
    "\"regionType\":\"State\",\"type\":\"AIR\",\"lastUpdate\":\"2026-10-10T17:30:45.148116Z\",\"activeAlertLevels\":"
    "[{\"alertLevel\":\"Yellow\",\"reason\":\"\xD0\x94\xD1\x80\xD0\xBE\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xB0 \xD0\xB7\xD0\xB0\xD0\xB3\xD1\x80\xD0\xBE\xD0\xB7\xD0\xB0 "
    "(\xD0\xB6\xD0\xBE\xD0\xB2\xD1\x82\xD0\xB8\xD0\xB9 \xD1\x80\xD1\x96\xD0\xB2\xD0\xB5\xD0\xBD\xD1\x8C)\",\"createdAt\":\"2026-10-10T17:30:45.390209Z\"}]}]}]";

static const char* NONE =
    "[{\"regionId\":\"31\",\"regionType\":\"State\",\"regionName\":\"Kyiv\",\"regionEngName\":\"Kyiv\","
    "\"lastUpdate\":\"2026-10-10T17:40:00.000000Z\",\"activeAlerts\":[]}]";

static const char* RED =
    "[{\"regionId\":\"31\",\"activeAlerts\":[{\"regionId\":\"31\",\"type\":\"AIR\",\"lastUpdate\":\"2026-10-10T17:49:12.0Z\","
    "\"activeAlertLevels\":[{\"alertLevel\":\"Yellow\",\"reason\":\"x\",\"createdAt\":\"2026-10-10T17:30:45.390209Z\"},"
    "{\"alertLevel\":\"Red\",\"reason\":\"y [brackets] {and} \\\"quotes\\\"\",\"createdAt\":\"2026-10-10T17:49:12.000000Z\"}]}]}]";

static const char* NO_LEVELS =
    "[{\"regionId\":\"31\",\"activeAlerts\":[{\"regionId\":\"31\",\"type\":\"AIR\",\"lastUpdate\":\"2026-10-10T17:20:00.000Z\"}]}]";

static const char* EMPTY_LEVELS =
    "[{\"regionId\":\"31\",\"activeAlerts\":[{\"type\":\"AIR\",\"lastUpdate\":\"2026-10-10T17:20:00.000Z\",\"activeAlertLevels\":[]}]}]";

static const char* MULTI =
    "[{\"regionId\":\"31\",\"activeAlerts\":["
    "{\"type\":\"ARTILLERY\",\"lastUpdate\":\"2026-10-10T16:00:00Z\",\"activeAlertLevels\":[{\"alertLevel\":\"Yellow\",\"createdAt\":\"2026-10-10T16:00:00Z\"}]},"
    "{\"type\":\"AIR\",\"lastUpdate\":\"2026-10-10T17:49:12Z\",\"activeAlertLevels\":[{\"alertLevel\":\"Red\",\"createdAt\":\"2026-10-10T17:49:12Z\"}]},"
    "{\"type\":\"CHEMICAL\",\"lastUpdate\":\"2026-10-10T15:00:00Z\",\"activeAlertLevels\":[{\"alertLevel\":\"Red\",\"createdAt\":\"2026-10-10T15:00:00Z\"}]}"
    "]}]";

static void test_iso() {
  TEST_ASSERT_EQUAL_INT64(1791653445LL, parse_iso_utc("2026-10-10T17:30:45.390209Z"));
  TEST_ASSERT_EQUAL_INT64(1791652800LL, parse_iso_utc("2026-10-10T17:20:00Z"));
  TEST_ASSERT_EQUAL_INT64(0, parse_iso_utc("1970-01-01T00:00:00Z"));
  TEST_ASSERT_EQUAL_INT64(-1, parse_iso_utc("garbage"));
  TEST_ASSERT_EQUAL_INT64(-1, parse_iso_utc("2026-13-10T17:20:00Z"));
}

static void test_yellow() {
  AlertResult r;
  TEST_ASSERT_TRUE(parse_alerts(YELLOW, r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::YELLOW);
  TEST_ASSERT_TRUE(r.has_since);
  TEST_ASSERT_EQUAL_INT64(1791653445LL, r.since_utc);  // createdAt of the level
}

static void test_none() {
  AlertResult r;
  TEST_ASSERT_TRUE(parse_alerts(NONE, r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::NONE);
  TEST_ASSERT_FALSE(r.has_since);
}

static void test_red_beats_yellow_in_one_alert() {
  AlertResult r;
  TEST_ASSERT_TRUE(parse_alerts(RED, r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::RED);
  TEST_ASSERT_EQUAL_INT64(1791654552LL, r.since_utc);  // 17:49:12, strings with brackets/quotes did not confuse the walker
}

static void test_levelless_is_red() {
  AlertResult r;
  TEST_ASSERT_TRUE(parse_alerts(NO_LEVELS, r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::RED);
  TEST_ASSERT_EQUAL_INT64(1791652800LL, r.since_utc);  // falls back to the alert's lastUpdate
  TEST_ASSERT_TRUE(parse_alerts(EMPTY_LEVELS, r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::RED);
}

static void test_multiple_alerts_red_wins() {
  AlertResult r;
  TEST_ASSERT_TRUE(parse_alerts(MULTI, r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::RED);
  TEST_ASSERT_EQUAL_INT64(parse_iso_utc("2026-10-10T15:00:00Z"), r.since_utc);  // earliest red
}

static void test_malformed() {
  AlertResult r;
  TEST_ASSERT_FALSE(parse_alerts("", r));
  TEST_ASSERT_FALSE(parse_alerts(nullptr, r));
  TEST_ASSERT_FALSE(parse_alerts("{\"error\":true}", r));
  TEST_ASSERT_FALSE(parse_alerts("[]", r));
  TEST_ASSERT_FALSE(parse_alerts("[{\"activeAlerts\":[{\"type\":\"AIR\"", r));  // truncated
  TEST_ASSERT_FALSE(parse_alerts("<html>301 Moved Permanently</html>", r));
  r.level = AlertLevel::UNKNOWN;
  // unknown level names alone count as no alert
  TEST_ASSERT_TRUE(parse_alerts(
      "[{\"activeAlerts\":[{\"lastUpdate\":\"2026-10-10T16:00:00Z\",\"activeAlertLevels\":[{\"alertLevel\":\"Blue\"}]}]}]", r));
  TEST_ASSERT_TRUE(r.level == AlertLevel::NONE);
}

void run_alert_tests() {
  RUN_TEST(test_iso);
  RUN_TEST(test_yellow);
  RUN_TEST(test_none);
  RUN_TEST(test_red_beats_yellow_in_one_alert);
  RUN_TEST(test_levelless_is_red);
  RUN_TEST(test_multiple_alerts_red_wins);
  RUN_TEST(test_malformed);
}
