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

static const char *EPMC_FIXTURE =
    "{\"resultList\":{\"result\":[{\"title\":\"CRISPR paper\","
    "\"authorString\":\"Doudna JA\",\"journalTitle\":\"Nature\","
    "\"pubYear\":\"2020\",\"citedByCount\":1500,\"doi\":\"10.1/x\","
    "\"abstractText\":\"<b>Abstract</b> text here\"},{\"title\":\"No doi\","
    "\"pmcid\":\"PMC123\",\"pubYear\":\"2019\"}]}}";

static void test_europepmc(void) {
  SearchResult *r = NULL;
  int n = parse_json_engine("europepmc", EPMC_FIXTURE, &r, 10);
  TEST_ASSERT(n == 2);
  TEST_ASSERT(strstr(r[0].url, "doi.org") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "Nature") != NULL);
  TEST_ASSERT(strstr(r[1].url, "europepmc.org/articles/PMC123") != NULL);
  free_results(r, n);
}

static const char *S2_FIXTURE =
    "{\"data\":[{\"title\":\"Attention\",\"abstract\":\"We propose.\","
    "\"url\":\"https://s2.example/p\",\"venue\":\"NeurIPS\","
    "\"year\":2017,\"citationCount\":80000,"
    "\"externalIds\":{\"DOI\":\"10.48550/arXiv.1706.03762\"},"
    "\"authors\":[{\"name\":\"Vaswani\"}]}]}";

static void test_s2(void) {
  SearchResult *r = NULL;
  int n = parse_json_engine("s2", S2_FIXTURE, &r, 10);
  TEST_ASSERT(n == 1);
  TEST_ASSERT(strstr(r[0].url, "doi.org") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "NeurIPS") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "citations") != NULL);
  TEST_ASSERT(r[0].date && strstr(r[0].date, "2017") != NULL);
  free_results(r, n);
}

static const char *OA_FIXTURE =
    "{\"results\":[{\"display_name\":\"Scaling laws\","
    "\"doi\":\"https://doi.org/10.2/x\",\"publication_year\":2020,"
    "\"cited_by_count\":300,"
    "\"abstract_inverted_index\":{\"Scaling\":[0],\"laws\":[1],"
    "\"govern\":[2],\"model\":[3]},"
    "\"primary_location\":{\"source\":{\"display_name\":\"arXiv\"}}}]}";

static void test_openalex(void) {
  SearchResult *r = NULL;
  int n = parse_json_engine("openalex", OA_FIXTURE, &r, 10);
  TEST_ASSERT(n == 1);
  TEST_ASSERT(strcmp(r[0].url, "https://doi.org/10.2/x") == 0);
  TEST_ASSERT(strstr(r[0].snippet, "Scaling laws govern model") != NULL);
  free_results(r, n);
}

static const char *OL_FIXTURE =
    "{\"docs\":[{\"title\":\"Dune\",\"key\":\"/works/OL893414W\","
    "\"author_name\":[\"Frank Herbert\"],\"first_publish_year\":1965},"
    "{\"title\":\"Dune Messiah\",\"key\":\"/works/OL893461W\","
    "\"author_name\":[\"Frank Herbert\"],\"first_publish_year\":1969}]}";

static void test_openlibrary(void) {
  SearchResult *r = NULL;
  int n = parse_json_engine("openlibrary", OL_FIXTURE, &r, 10);
  TEST_ASSERT(n == 2);
  TEST_ASSERT(strstr(r[0].url, "openlibrary.org/works/OL893414W") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "Frank Herbert") != NULL);
  TEST_ASSERT(r[0].date && strstr(r[0].date, "1965") != NULL);
  free_results(r, n);
}

static const char *ITUNES_FIXTURE =
    "{\"results\":[{\"collectionName\":\"Science Friday\","
    "\"artistName\":\"Wnyc\",\"collectionViewUrl\":\"https://podcasts.apple.com/us/podcast/x\","
    "\"primaryGenreName\":\"Science\",\"feedUrl\":\"https://feeds.example/sf\"}]}";

static void test_itunes(void) {
  SearchResult *r = NULL;
  int n = parse_json_engine("itunes", ITUNES_FIXTURE, &r, 10);
  TEST_ASSERT(n == 1);
  TEST_ASSERT(strstr(r[0].url, "podcasts.apple.com") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "Wnyc") != NULL);
  free_results(r, n);
}

static const char *MEAL_FIXTURE =
    "{\"meals\":[{\"idMeal\":\"52882\",\"strMeal\":\"Three Fish Pie\","
    "\"strCategory\":\"Seafood\",\"strArea\":\"British\","
    "\"strInstructions\":\"1) Cook fish...\",\"strSource\":\"\"}]}";

static void test_themealdb(void) {
  SearchResult *r = NULL;
  int n = parse_json_engine("themealdb", MEAL_FIXTURE, &r, 10);
  TEST_ASSERT(n == 1);
  TEST_ASSERT(strstr(r[0].url, "themealdb.com") != NULL);
  TEST_ASSERT(strstr(r[0].snippet, "British") != NULL);
  free_results(r, n);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_europepmc();
  test_s2();
  test_openalex();
  test_openlibrary();
  test_itunes();
  test_themealdb();
  test_wikipedia();
  test_hn();
  test_mwmbl();
  test_lemmy();
  test_stackoverflow();
  test_wikibooks_prefix();
  test_unknown();
  TEST_MAIN_RETURN();
}
