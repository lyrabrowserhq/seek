#ifndef CONFIG_H
#define CONFIG_H

#define SEEKER_VERSION "1.0.0"

#define DEFAULT_HOST "0.0.0.0"
#define DEFAULT_PORT 5000
#define DEFAULT_CACHE_DIR "/tmp/seek_cache"
#define DEFAULT_INDEX_URL "https://index.lyrabrowser.com"
#define DEFAULT_CACHE_TTL_SEARCH 3600
#define DEFAULT_CACHE_TTL_INFOBOX 86400
#define DEFAULT_CACHE_TTL_IMAGE 604800
#define DEFAULT_MAX_PROXY_RETRIES 3

#define DEFAULT_YACY_INSTANCE "http://127.0.0.1:8090"

#define BUFFER_SIZE_SMALL 256
#define BUFFER_SIZE_MEDIUM 512
#define BUFFER_SIZE_LARGE 1024
#define BUFFER_SIZE_XLARGE 2048

#define INITIAL_BUFFER_SIZE 65536

#define WIKI_SUMMARY_MAX_CHARS 300

#define MD5_HASH_LEN 32
#define HEX_CHARS "0123456789abcdef"

#define INFOBOX_FIELD_COUNT 6
#define MAX_RESULTS_PER_ENGINE 15

#define CURL_TIMEOUT_SECS 5L
#define CURL_DNS_TIMEOUT_SECS 300L
#define DEFAULT_HTTP_CONNECT_TIMEOUT_SEC 2
#define DEFAULT_MAX_CONCURRENT_FETCHES 12
#define DEFAULT_CACHE_STAMPEDE_WAIT_MS 20
#define DEFAULT_CACHE_STAMPEDE_MAX_ATTEMPTS 8
#define DEFAULT_RETRY_BACKOFF_MS 100

#define BING_IMAGE_URL "https://www.bing.com/images/search"
#define IMAGE_RESULTS_PER_PAGE 32
#define IMAGE_RESULT_FIELDS 4

typedef struct {
  char host[256];
  int port;
  int hsts_max_age_sec;
  int hsts_include_subdomains;
  char domain[256];
  char default_locale[64];
  char proxy[256];
  char proxy_list_file[256];
  int max_proxy_retries;
  int randomize_username;
  int randomize_password;
  char cache_dir[512];
  int cache_ttl_search;
  int cache_ttl_infobox;
  int cache_ttl_image;
  char engines[512];
  char engines_file[512];
  char yacy_instance[512];
  char index_url[512];
  char bangs_file[512];
  int http_timeout_sec;
  int http_connect_timeout_sec;
  int max_concurrent_fetches;
  int cache_stampede_wait_ms;
  int cache_stampede_max_attempts;
  int retry_backoff_ms;
  int worker_threads;
  char default_engine[64];
  int rate_limit_search_requests;
  int rate_limit_search_interval;
  int rate_limit_images_requests;
  int rate_limit_images_interval;
  int rate_limit_news_requests;
  int rate_limit_news_interval;
} Config;

extern int g_http_timeout_sec;
extern int g_http_connect_timeout_sec;
extern int g_max_concurrent_fetches;
extern int g_cache_stampede_wait_ms;
extern int g_cache_stampede_max_attempts;
extern int g_retry_backoff_ms;

void apply_runtime_config(const Config *cfg);

int load_config(const char *filename, Config *config);

#endif
