#ifndef SCRAPING_H
#define SCRAPING_H

#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>

typedef struct {
  char *url;
  char *title;
  char *snippet;
} SearchResult;

typedef int (*ParserFunc)(const char *engine_name, xmlDocPtr doc,
                          SearchResult **out_results, int max_results);
typedef int (*JsonParserFunc)(const char *engine_id, const char *json,
                              SearchResult **out_results, int max_results);

#define ENGINE_ID_MAX 32
#define ENGINE_NAME_MAX 64
#define ENGINE_URL_MAX 512
#define ENGINE_HOST_MAX 128
#define ENGINE_REFERER_MAX 256
#define ENGINE_PAGE_PARAM_MAX 16
#define MAX_BUILTIN_ENGINES 28
#define MAX_EXTRA_ENGINES 16
#define MAX_ENGINE_JOBS (MAX_BUILTIN_ENGINES + MAX_EXTRA_ENGINES)

typedef struct {
  char id[ENGINE_ID_MAX];
  char name[ENGINE_NAME_MAX];
  char base_url[ENGINE_URL_MAX];
  char host_header[ENGINE_HOST_MAX];
  char referer[ENGINE_REFERER_MAX];
  char page_param[ENGINE_PAGE_PARAM_MAX];
  int page_multiplier;
  int page_base;
  ParserFunc parser;
  JsonParserFunc json_parser;
  int enabled;
  int is_xml;
  int is_json;
} SearchEngine;

typedef struct {
  char *memory;
  size_t size;
  size_t capacity;
} MemoryBuffer;

typedef enum {
  SCRAPE_STATUS_PENDING,
  SCRAPE_STATUS_OK,
  SCRAPE_STATUS_EMPTY,
  SCRAPE_STATUS_FETCH_ERROR,
  SCRAPE_STATUS_PARSE_MISMATCH,
  SCRAPE_STATUS_BLOCKED,
} ScrapeStatus;

typedef struct {
  const SearchEngine *engine;
  char *query;
  SearchResult **out_results;
  int max_results;
  int page;
  CURL *handle;
  MemoryBuffer response;
  int results_count;
  long http_status;
  ScrapeStatus status;
  char *cache_key;
  int cache_lock_held;
} ScrapeJob;

extern SearchEngine ENGINE_REGISTRY[];
extern const int ENGINE_COUNT;
extern SearchEngine EXTRA_ENGINES[];
extern int extra_engine_count;

int engines_total(void);
const SearchEngine *engine_at(int idx);
SearchEngine *engine_mutable_at(int idx);
int scraping_load_engines_file(const char *path);
void apply_engines_config(const char *engines_str);
void configure_yacy_engine(const char *instance);
void configure_lyra_engine(const char *instance);

size_t write_memory_callback(void *contents, size_t size, size_t nmemb,
                             void *userp);
const char *get_random_user_agent(void);
void configure_curl_handle(CURL *curl, const char *full_url,
                           MemoryBuffer *chunk, struct curl_slist *headers);
char *build_search_url(const char *base_url, const char *page_param,
                       int page_multiplier, int page_base,
                       const char *encoded_query, int page);
struct curl_slist *build_request_headers(const char *host_header,
                                         const char *referer);
void http_delay(void);

xmlXPathContextPtr create_xpath_context(xmlDocPtr doc);
void free_xpath_objects(xmlXPathContextPtr ctx, xmlXPathObjectPtr obj);
SearchResult *alloc_results_array(int capacity, int max_results);
void assign_result(SearchResult *result, char *url, char *title, char *snippet,
                   int unescape);
void free_xml_node_list(char *title, char *url, char *snippet);

int scrape_engine(const SearchEngine *engine, const char *query,
                  SearchResult **out_results, int max_results);

int scrape_engines_parallel(ScrapeJob *jobs, int num_jobs);

#endif
