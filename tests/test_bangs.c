#include "../src/Bangs.h"
#include "test_harness.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

static void test_no_bang(void) {
  TEST_ASSERT_NULL(bang_resolve_redirect_url("hello"));
  TEST_ASSERT_NULL(bang_resolve_redirect_url("!"));
}

static void test_unknown_bang(void) {
  TEST_ASSERT_NULL(bang_resolve_redirect_url("!notarealbangname query"));
}

static void test_wikipedia_bang(void) {
  char *u = bang_resolve_redirect_url("!w cats and dogs");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "wikipedia.org") != NULL);
  TEST_ASSERT(strstr(u, "cats") != NULL);
  free(u);
}

static void test_github_bang(void) {
  char *u = bang_resolve_redirect_url("!gh seek-search");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "github.com") != NULL);
  free(u);
}

int main(void) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  test_failures = 0;
  test_runs = 0;
  bangs_init(NULL);
  test_no_bang();
  test_unknown_bang();
  test_wikipedia_bang();
  test_github_bang();
  curl_global_cleanup();
  TEST_MAIN_RETURN();
}
