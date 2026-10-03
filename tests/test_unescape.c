#include "../src/Utility/Unescape.h"
#include "../src/Utility/Utility.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

static void test_hex_to_int(void) {
  TEST_ASSERT_EQ(hex_to_int('0'), 0);
  TEST_ASSERT_EQ(hex_to_int('9'), 9);
  TEST_ASSERT_EQ(hex_to_int('a'), 10);
  TEST_ASSERT_EQ(hex_to_int('f'), 15);
  TEST_ASSERT_EQ(hex_to_int('A'), 10);
  TEST_ASSERT_EQ(hex_to_int('F'), 15);
  TEST_ASSERT_EQ(hex_to_int('x'), -1);
  TEST_ASSERT_EQ(hex_to_int('\0'), -1);
}

static void test_unescape_null(void) {
  TEST_ASSERT_NULL(unescape_search_url(NULL));
  TEST_ASSERT_NULL(url_decode_query(NULL));
}

static void test_unescape_plain(void) {
  char *a = unescape_search_url("/search");
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT(strcmp(a, "/search") == 0);
  free(a);

  char *b = url_decode_query("hello+world");
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT(strcmp(b, "hello world") == 0);
  free(b);
}

static void test_uddg_decode(void) {
  char *u = unescape_search_url(
      "https://duckduckgo.com/?q=test&uddg=hello%2Bworld%20x");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "hello") != NULL);
  free(u);
}

static void test_ru_branch(void) {
  char *r = unescape_search_url("x?RU=abc/def");
  TEST_ASSERT_NOT_NULL(r);
  free(r);

  char *no_slash = unescape_search_url("x?RU=onlyvalue");
  TEST_ASSERT_NOT_NULL(no_slash);
  free(no_slash);
}

static void test_url_decode_query_percent(void) {
  char *d = url_decode_query("%41%42");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT(strcmp(d, "AB") == 0);
  free(d);

  char *tail = url_decode_query("%");
  TEST_ASSERT_NOT_NULL(tail);
  free(tail);

  char *one = url_decode_query("%4");
  TEST_ASSERT_NOT_NULL(one);
  free(one);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_hex_to_int();
  test_unescape_null();
  test_unescape_plain();
  test_uddg_decode();
  test_ru_branch();
  test_url_decode_query_percent();
  TEST_MAIN_RETURN();
}
