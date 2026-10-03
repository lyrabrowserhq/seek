#include "../src/Cache/Cache.h"
#include "../src/Config.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_compute_key(void) {
  char *k = cache_compute_key("query", 2, "ddg");
  TEST_ASSERT_NOT_NULL(k);
  TEST_ASSERT_EQ(strlen(k), (size_t)MD5_HASH_LEN);
  for (size_t i = 0; i < strlen(k); i++) {
    char c = k[i];
    TEST_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
  free(k);

  char *k2 = cache_compute_key(NULL, 0, NULL);
  TEST_ASSERT_NOT_NULL(k2);
  free(k2);
}

static void test_set_get_roundtrip(void) {
  char dir_template[] = "/tmp/seek_cache_XXXXXX";
  char *dir = mkdtemp(dir_template);
  TEST_ASSERT_NOT_NULL(dir);

  TEST_ASSERT_EQ(cache_init(dir), 0);

  const char *key = "0123456789abcdef0123456789abcdef";
  const char *payload = "payload-bytes";
  TEST_ASSERT_EQ(cache_set(key, payload, strlen(payload)), 0);

  char *out = NULL;
  size_t sz = 0;
  TEST_ASSERT_EQ(cache_get(key, 3600, &out, &sz), 0);
  TEST_ASSERT_EQ(sz, strlen(payload));
  TEST_ASSERT(memcmp(out, payload, sz) == 0);
  free(out);

  cache_shutdown();
}

static void test_get_before_init(void) {
  cache_shutdown();
  char *out = NULL;
  size_t sz = 0;
  TEST_ASSERT_EQ(cache_get("abc", 10, &out, &sz), -1);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_compute_key();
  test_set_get_roundtrip();
  test_get_before_init();
  TEST_MAIN_RETURN();
}
