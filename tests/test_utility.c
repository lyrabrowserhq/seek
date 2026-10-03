#include "../src/Utility/Utility.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static void test_hex(void) {
  TEST_ASSERT_EQ(hex_to_int('0'), 0);
  TEST_ASSERT_EQ(hex_to_int('9'), 9);
  TEST_ASSERT_EQ(hex_to_int('a'), 10);
  TEST_ASSERT_EQ(hex_to_int('f'), 15);
  TEST_ASSERT_EQ(hex_to_int('A'), 10);
  TEST_ASSERT_EQ(hex_to_int('F'), 15);
  TEST_ASSERT_EQ(hex_to_int('x'), -1);
}

static void test_hn_snippet(void) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "Article URL: https://example.com\nComments URL: "
           "https://news.ycombinator.com/item?id=1\nPoints: 11 # Comments: 3");
  snippet_strip_feed_meta(buf);
  TEST_ASSERT_EQ((int)strlen(buf), 0);

  snprintf(buf, sizeof(buf),
           "A real comment about Orion.\nArticle URL: https://x.test\nPoints: "
           "4");
  snippet_strip_feed_meta(buf);
  TEST_ASSERT(strstr(buf, "Article URL") == NULL);
  TEST_ASSERT(strstr(buf, "Orion") != NULL);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_hex();
  test_hn_snippet();
  TEST_MAIN_RETURN();
}
