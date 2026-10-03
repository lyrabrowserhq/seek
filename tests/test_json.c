#include "../src/Utility/JsonHelper.h"
#include "test_harness.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void test_json_get_float(void) {
  const char *j = "{\"rate\": 3.14}";
  double v = json_get_float(j, "rate");
  TEST_ASSERT(fabs(v - 3.14) < 0.001);
}

static void test_json_get_float_missing(void) {
  double v = json_get_float("{}", "nope");
  TEST_ASSERT_EQ(v, 0.0);
}

static void test_json_parse_float_map(void) {
  const char *j = "{\"rates\": {\"USD\": 1.0, \"EUR\": 0.85}}";
  JsonFloatMap m;
  int ok = json_parse_float_map(j, "\"rates\"", &m);
  TEST_ASSERT(ok);
  TEST_ASSERT(m.count > 0);
}

static void test_json_get_string(void) {
  const char *j = "{\"name\": \"value\"}";
  char *s = json_get_string(j, "name");
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT(strcmp(s, "value") == 0);
}

static void test_json_null_args(void) {
  JsonFloatMap m;
  TEST_ASSERT_EQ(json_parse_float_map(NULL, "k", &m), 0);
  TEST_ASSERT_EQ(json_get_float(NULL, "k"), 0.0);
  TEST_ASSERT_NULL(json_get_string(NULL, "k"));
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_json_get_float();
  test_json_get_float_missing();
  test_json_parse_float_map();
  test_json_get_string();
  test_json_null_args();
  TEST_MAIN_RETURN();
}
