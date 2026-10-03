#include "../src/Scraping/JsonEngines.h"
#include "../src/Utility/XmlHelper.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL(f);
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  char *buf = malloc((size_t)n + 1);
  TEST_ASSERT_NOT_NULL(buf);
  fread(buf, 1, (size_t)n, f);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

static void free_results(SearchResult *r, int n) {
  xml_result_free(r, n);
}

static void test_wikipedia(void) {
  char *json = read_file("tests/fixtures/wikipedia.json");
  SearchResult *r = NULL;
  int n = parse_json_engine("wikipedia", json, &r, 10);
  TEST_ASSERT(n >= 2);
  TEST_ASSERT(strstr(r[0].url, "wikipedia.org") != NULL);
  TEST_ASSERT(strstr(r[0].title, "Privacy") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "<") == NULL);
  free_results(r, n);
  free(json);
}

static void test_hn(void) {
  char *json = read_file("tests/fixtures/hn.json");
  SearchResult *r = NULL;
  int n = parse_json_engine("hn", json, &r, 10);
  TEST_ASSERT_EQ(n, 2);
  TEST_ASSERT(strstr(r[0].url, "example.com") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "Article URL") == NULL);
  TEST_ASSERT(strstr(r[0].snippet, "frontend") != NULL);
  TEST_ASSERT(strstr(r[1].url, "news.ycombinator.com") != NULL);
  free_results(r, n);
  free(json);
}

static void test_mwmbl(void) {
  char *json = read_file("tests/fixtures/mwmbl.json");
  SearchResult *r = NULL;
  int n = parse_json_engine("mwmbl", json, &r, 10);
  TEST_ASSERT(n >= 1);
  TEST_ASSERT(strstr(r[0].title, "Privacy") != NULL);
  free_results(r, n);
  free(json);
}

static void test_unknown(void) {
  SearchResult *r = NULL;
  TEST_ASSERT_EQ(parse_json_engine("nope", "{}", &r, 10), 0);
}

static void test_lemmy(void) {
  char *json = read_file("tests/fixtures/lemmy.json");
  SearchResult *r = NULL;
  int n = parse_json_engine("lemmy", json, &r, 10);
  TEST_ASSERT_EQ(n, 1);
  TEST_ASSERT(strstr(r[0].url, "example.com") != NULL);
  TEST_ASSERT(strstr(r[0].title, "Privacy") != NULL);
  free_results(r, n);
  free(json);
}

static void test_stackoverflow(void) {
  char *json = read_file("tests/fixtures/stackoverflow.json");
  SearchResult *r = NULL;
  int n = parse_json_engine("stackoverflow", json, &r, 10);
  TEST_ASSERT_EQ(n, 1);
  TEST_ASSERT(strstr(r[0].url, "stackoverflow.com") != NULL);
  TEST_ASSERT(strstr(r[0].title, "mux") != NULL);
  free_results(r, n);
  free(json);
}

static void test_wikibooks_prefix(void) {
  char *json = read_file("tests/fixtures/wikipedia.json");
  SearchResult *r = NULL;
  int n = parse_json_engine("wikibooks", json, &r, 10);
  TEST_ASSERT(n >= 1);
  TEST_ASSERT(strstr(r[0].url, "wikibooks.org") != NULL);
  free_results(r, n);
  free(json);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_wikipedia();
  test_hn();
  test_mwmbl();
  test_lemmy();
  test_stackoverflow();
  test_wikibooks_prefix();
  test_unknown();
  TEST_MAIN_RETURN();
}
