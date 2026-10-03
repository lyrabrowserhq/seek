#include "../src/Infobox/Today.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

static void test_parse_onthisday(void) {
  const char *json =
      "{\"holidays\":[{\"text\":\"World Habitat Day\","
      "\"pages\":[{\"content_urls\":{\"desktop\":{\"page\":\"https://en.wikipedia.org/wiki/X\"}}}]}],"
      "\"selected\":[{\"year\":1957,\"text\":\"Sputnik launched.\","
      "\"pages\":[{\"content_urls\":{\"desktop\":{\"page\":\"https://en.wikipedia.org/wiki/Sputnik\"}}}]}]}";
  char **rows = NULL;
  int n = 0;
  TEST_ASSERT(today_parse_feed(json, &rows, &n, 6) >= 2);
  TEST_ASSERT(strstr((char *)((char **)rows[0])[0], "Habitat") != NULL);
  for (int i = 0; i < n; i++) {
    char **row = (char **)rows[i];
    free(row[0]);
    free(row[1]);
    free(row[2]);
    free(row);
  }
  free(rows);
}

static void test_unescape_newline(void) {
  const char *json =
      "{\"holidays\":[{\"text\":\"Feast:\\nName\","
      "\"pages\":[{\"content_urls\":{\"desktop\":{\"page\":\"https://en.wikipedia.org/wiki/X\"}}}]}]}";
  char **rows = NULL;
  int n = 0;
  TEST_ASSERT(today_parse_feed(json, &rows, &n, 6) >= 1);
  TEST_ASSERT(strstr((char *)((char **)rows[0])[0], "\\n") == NULL);
  TEST_ASSERT(strstr((char *)((char **)rows[0])[0], "Feast:") != NULL);
  for (int i = 0; i < n; i++) {
    char **row = (char **)rows[i];
    free(row[0]);
    free(row[1]);
    free(row[2]);
    free(row);
  }
  free(rows);
}

static void test_query_wants_today(void) {
  TEST_ASSERT_EQ(query_wants_today("today"), 1);
  TEST_ASSERT_EQ(query_wants_today("What is today"), 1);
  TEST_ASSERT_EQ(query_wants_today("on this day"), 1);
  TEST_ASSERT_EQ(query_wants_today("linux kernel"), 0);
  TEST_ASSERT_EQ(query_wants_today(""), 0);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_parse_onthisday();
  test_unescape_newline();
  test_query_wants_today();
  TEST_MAIN_RETURN();
}
