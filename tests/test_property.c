#include "../src/Utility/JsonHelper.h"
#include "../src/Utility/Unescape.h"
#include "../src/Utility/Utility.h"
#include "test_harness.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t rng_state = 0xC0FFEEu;

static uint32_t rnd_u32(void) {
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state;
}

static void fill_random_string(char *buf, size_t cap) {
  if (cap == 0)
    return;
  size_t len = (size_t)(rnd_u32() % cap);
  for (size_t i = 0; i < len; i++) {
    uint32_t r = rnd_u32();
    buf[i] = (char)(32 + (r % 95));
  }
  buf[len] = '\0';
}

static void property_hex_to_int(void) {
  for (int c = 0; c < 256; c++) {
    int h = hex_to_int((char)c);
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F')) {
      TEST_ASSERT(h >= 0 && h <= 15);
    } else {
      TEST_ASSERT_EQ(h, -1);
    }
  }
}

static void property_url_decode_query_roundtrip_space_plus(void) {
  for (int i = 0; i < 2000; i++) {
    char in[128];
    fill_random_string(in, sizeof(in) - 1);
    for (size_t k = 0; in[k]; k++) {
      if (in[k] == '%')
        in[k] = 'x';
    }
    char *out = url_decode_query(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT(strlen(out) <= strlen(in));
    free(out);
  }
}

static void property_unescape_no_crash(void) {
  for (int i = 0; i < 8000; i++) {
    char in[256];
    fill_random_string(in, sizeof(in) - 1);
    char *u = unescape_search_url(in);
    free(u);
    char *d = url_decode_query(in);
    free(d);
  }
}

static void property_json_helpers_no_crash(void) {
  char buf[512];
  for (int i = 0; i < 3000; i++) {
    size_t n = rnd_u32() % (sizeof(buf) - 1);
    for (size_t k = 0; k < n; k++)
      buf[k] = (char)(rnd_u32() & 0x7F);
    buf[n] = '\0';
    JsonFloatMap m;
    json_parse_float_map(buf, "\"x\"", &m);
    (void)json_get_float(buf, "k");
    (void)json_get_string(buf, "k");
  }
}

static void property_decode_preserves_ascii_letters(void) {
  char *d = url_decode_query("ABCDEF");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT(strcmp(d, "ABCDEF") == 0);
  free(d);
}

int main(void) {
  rng_state = (uint32_t)time(NULL) ^ 0xA5A5A5A5u;
  test_failures = 0;
  test_runs = 0;
  property_hex_to_int();
  property_url_decode_query_roundtrip_space_plus();
  property_unescape_no_crash();
  property_json_helpers_no_crash();
  property_decode_preserves_ascii_letters();
  TEST_MAIN_RETURN();
}
