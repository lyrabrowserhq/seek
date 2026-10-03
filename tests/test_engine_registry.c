#include "../src/Scraping/Scraping.h"
#include "test_harness.h"
#include <string.h>

static void test_ids_unique_and_wired(void) {
  TEST_ASSERT(ENGINE_COUNT > 10);
  TEST_ASSERT(ENGINE_COUNT <= MAX_BUILTIN_ENGINES);
  for (int i = 0; i < ENGINE_COUNT; i++) {
    TEST_ASSERT(ENGINE_REGISTRY[i].id[0] != '\0');
    if (ENGINE_REGISTRY[i].is_json)
      TEST_ASSERT_NOT_NULL(ENGINE_REGISTRY[i].json_parser);
    else
      TEST_ASSERT_NOT_NULL(ENGINE_REGISTRY[i].parser);
    for (int j = i + 1; j < ENGINE_COUNT; j++)
      TEST_ASSERT(strcmp(ENGINE_REGISTRY[i].id, ENGINE_REGISTRY[j].id) != 0);
  }
}

static void test_defaults(void) {
  int wiki = 0;
  int hn = 0;
  int yahoo = 0;
  int books = 0;
  int lemmy = 0;
  int so = 0;
  for (int i = 0; i < ENGINE_COUNT; i++) {
    if (!strcmp(ENGINE_REGISTRY[i].id, "wikipedia") && ENGINE_REGISTRY[i].enabled)
      wiki = 1;
    if (!strcmp(ENGINE_REGISTRY[i].id, "hn") && ENGINE_REGISTRY[i].enabled)
      hn = 1;
    if (!strcmp(ENGINE_REGISTRY[i].id, "yahoo") && ENGINE_REGISTRY[i].enabled)
      yahoo = 1;
    if (!strcmp(ENGINE_REGISTRY[i].id, "wikibooks"))
      books = 1;
    if (!strcmp(ENGINE_REGISTRY[i].id, "lemmy"))
      lemmy = 1;
    if (!strcmp(ENGINE_REGISTRY[i].id, "stackoverflow"))
      so = 1;
  }
  TEST_ASSERT_EQ(wiki, 1);
  TEST_ASSERT_EQ(hn, 1);
  TEST_ASSERT_EQ(yahoo, 0);
  TEST_ASSERT_EQ(books, 1);
  TEST_ASSERT_EQ(lemmy, 1);
  TEST_ASSERT_EQ(so, 1);
}

static void test_lyra_keeps_port(void) {
  configure_lyra_engine("http://lyra-index:8091");
  int found = 0;
  for (int i = 0; i < ENGINE_COUNT; i++) {
    if (strcmp(ENGINE_REGISTRY[i].id, "lyra") != 0)
      continue;
    TEST_ASSERT(strstr(ENGINE_REGISTRY[i].base_url, "lyra-index:8091") != NULL);
    TEST_ASSERT(strstr(ENGINE_REGISTRY[i].host_header, "8091") != NULL);
    found = 1;
  }
  TEST_ASSERT_EQ(found, 1);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_ids_unique_and_wired();
  test_defaults();
  test_lyra_keeps_port();
  TEST_MAIN_RETURN();
}
