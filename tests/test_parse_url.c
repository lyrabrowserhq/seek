#include "beaker.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

static void test_malformed(void) {
  UrlParams p;
  TEST_ASSERT_NULL(parse_request_url("not-a-request", &p));
  TEST_ASSERT_NULL(parse_request_url("", &p));
}

static void test_simple_path(void) {
  UrlParams p;
  char *path = parse_request_url("GET / HTTP/1.1", &p);
  TEST_ASSERT_NOT_NULL(path);
  TEST_ASSERT(strcmp(path, "/") == 0);
  TEST_ASSERT_EQ(p.count, 0);
  free(path);
}

static void test_path_query(void) {
  UrlParams p;
  char *path =
      parse_request_url("GET /search?q=a&b=c HTTP/1.1", &p);
  TEST_ASSERT_NOT_NULL(path);
  TEST_ASSERT(strcmp(path, "/search") == 0);
  TEST_ASSERT(p.count >= 1);
  free(path);
}

static void test_path_traversal_blocked(void) {
  UrlParams p;
  char *bad = parse_request_url("GET /foo/../../../etc/passwd HTTP/1.1", &p);
  TEST_ASSERT_NULL(bad);
}

static void test_dot_segments(void) {
  UrlParams p;
  char *ok = parse_request_url("GET /a/./b HTTP/1.1", &p);
  TEST_ASSERT_NOT_NULL(ok);
  TEST_ASSERT(strstr(ok, "..") == NULL);
  free(ok);
}

static void test_percent_encoded_path(void) {
  UrlParams p;
  char *path = parse_request_url("GET /hello%20world HTTP/1.1", &p);
  TEST_ASSERT_NOT_NULL(path);
  free(path);
}

static void test_null_byte_encoding_rejected(void) {
  UrlParams p;
  char *n =
      parse_request_url("GET /%00evil HTTP/1.1", &p);
  TEST_ASSERT_NULL(n);
}

static void test_mime_type(void) {
  TEST_ASSERT(strcmp(get_mime_type("x.html"), "text/html") == 0);
  TEST_ASSERT(strcmp(get_mime_type("x.css"), "text/css") == 0);
  TEST_ASSERT(strcmp(get_mime_type("noext"), "application/octet-stream") == 0);
  TEST_ASSERT(strcmp(get_mime_type("a.js"), "application/javascript") == 0);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_malformed();
  test_simple_path();
  test_path_query();
  test_path_traversal_blocked();
  test_dot_segments();
  test_percent_encoded_path();
  test_null_byte_encoding_rejected();
  test_mime_type();
  TEST_MAIN_RETURN();
}
