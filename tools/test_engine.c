#include "Scraping/Scraping.h"
#include "Proxy/Proxy.h"
#include "Cache/Cache.h"
#include <curl/curl.h>
#include <libxml/parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
  int engine_idx = 0;
  const char *query = "linux";

  if (argc >= 2) {
    engine_idx = atoi(argv[1]);
    if (engine_idx < 0 || engine_idx >= ENGINE_COUNT) {
      fprintf(stderr, "Engine index must be 0-%d\n", ENGINE_COUNT - 1);
      return 1;
    }
  }
  if (argc >= 3) {
    query = argv[2];
  }

  srand((unsigned)time(NULL));
  curl_global_init(CURL_GLOBAL_DEFAULT);
  LIBXML_TEST_VERSION;
  xmlInitParser();

  set_proxy_config("", 0, 0);
  set_cache_ttl_search(0);
  cache_init("/tmp/seeker-test-cache");

  const SearchEngine *engine = &ENGINE_REGISTRY[engine_idx];
  SearchResult *results = NULL;
  int count = scrape_engine(engine, query, &results, 10);

  printf("Engine: %s\n", engine->name);
  printf("Query: %s\n", query);
  printf("Results: %d\n", count);

  if (count > 0 && results) {
    for (int i = 0; i < count && i < 5; i++) {
      printf("  [%d] %s\n", i + 1, results[i].title ? results[i].title : "(no title)");
      printf("      %s\n", results[i].url ? results[i].url : "(no url)");
    }
    for (int i = 0; i < count; i++) {
      free(results[i].url);
      free(results[i].title);
      free(results[i].snippet);
    }
    free(results);
  }

  cache_shutdown();
  xmlCleanupParser();
  curl_global_cleanup();
  return count > 0 ? 0 : 1;
}
