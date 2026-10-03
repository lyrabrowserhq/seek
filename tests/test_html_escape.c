#include "../src/Utility/HtmlEscape.h"
#include "test_harness.h"
#include <string.h>

static void test_attr_basic(void) {
  char out[64];
  html_escape_attr("a&b\"c'd", out, sizeof(out));
  TEST_ASSERT(strcmp(out, "a&amp;b&quot;c&#39;d") == 0);
}

static void test_attr_null_in(void) {
  char out[16];
  html_escape_attr(NULL, out, sizeof(out));
  TEST_ASSERT(strcmp(out, "") == 0);
}

static void test_text_basic(void) {
  char out[64];
  html_escape_text("1<2&3>4", out, sizeof(out));
  TEST_ASSERT(strcmp(out, "1&lt;2&amp;3&gt;4") == 0);
}

static void test_text_null_in(void) {
  char out[8];
  html_escape_text(NULL, out, sizeof(out));
  TEST_ASSERT(strcmp(out, "") == 0);
}

static void test_cap_truncates_safely(void) {
  char out[8];
  html_escape_text("<<<<<<<<", out, sizeof(out));
  TEST_ASSERT(strlen(out) < sizeof(out));
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_attr_basic();
  test_attr_null_in();
  test_text_basic();
  test_text_null_in();
  test_cap_truncates_safely();
  TEST_MAIN_RETURN();
}
