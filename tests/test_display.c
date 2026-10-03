#include "../src/Utility/Display.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

static void test_plain(void) {
  char *d = pretty_display_url("https://WWW.Example.COM/foo/bar/");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT(strstr(d, "example") != NULL);
  free(d);
}

static void test_null(void) {
  TEST_ASSERT_NULL(pretty_display_url(NULL));
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_plain();
  test_null();
  TEST_MAIN_RETURN();
}
