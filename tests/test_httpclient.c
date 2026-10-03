#include "../src/Utility/HttpClient.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

static xmlDocPtr stub_parser_fail(const char *d, size_t z, const char *u) {
  (void)d;
  (void)z;
  (void)u;
  return NULL;
}

static void test_append_chunk(void) {
  HttpResponse r = {0};
  size_t n = http_response_append_chunk("abc", 1, 3, &r);
  TEST_ASSERT_EQ(n, 3u);
  TEST_ASSERT_NOT_NULL(r.memory);
  TEST_ASSERT(strcmp(r.memory, "abc") == 0);
  TEST_ASSERT_EQ(r.size, 3u);
  http_response_free(&r);
}

static void test_append_growth(void) {
  HttpResponse r = {0};
  char chunk[8000];
  memset(chunk, 'B', sizeof(chunk));
  size_t got = http_response_append_chunk(chunk, sizeof(chunk), 1, &r);
  TEST_ASSERT_EQ(got, sizeof(chunk));
  TEST_ASSERT(r.capacity >= r.size + 1);
  TEST_ASSERT_EQ(r.size, sizeof(chunk));
  http_response_free(&r);
}

static void test_append_zero_bytes(void) {
  HttpResponse r = {0};
  size_t z = http_response_append_chunk("", 1, 0, &r);
  TEST_ASSERT_EQ(z, 0u);
  http_response_free(&r);
}

static void test_http_get_null_url(void) {
  HttpResponse r = http_get(NULL, NULL);
  TEST_ASSERT_NULL(r.memory);
}

static void test_cached_early_returns(void) {
  CachedHttpResponse a = cached_http_get(NULL, "ua", "k", 10, stub_parser_fail);
  TEST_ASSERT_EQ(a.success, 0);

  CachedHttpResponse b = cached_http_get("http://127.0.0.1:9/nope", "ua", NULL, 0,
                                         stub_parser_fail);
  HttpResponse hr = {.memory = b.memory, .size = b.size, .capacity = 0};
  http_response_free(&hr);
}

static void test_http_response_free_null(void) { http_response_free(NULL); }

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_append_chunk();
  test_append_growth();
  test_append_zero_bytes();
  test_http_get_null_url();
  test_cached_early_returns();
  test_http_response_free_null();
  TEST_MAIN_RETURN();
}
