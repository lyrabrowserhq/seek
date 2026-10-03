#include <beaker.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Cache/Cache.h"
#include "Config.h"
#include "Infobox/Wikipedia.h"
#include "Proxy/Proxy.h"
#include "Routes/Favicon.h"
#include "Routes/Home.h"
#include "Routes/ImageProxy.h"
#include "Routes/Images.h"
#include "Routes/News.h"
#include "Bangs.h"
#include "Routes/Bangs.h"
#include "Routes/Search.h"
#include "Routes/Settings.h"
#include "Routes/SettingsSave.h"
#include "Routes/Suggest.h"
#include "Scraping/Scraping.h"
#include "Utility/Utility.h"

Config global_config;

static void print_usage(FILE *out) {
  fprintf(out, "Usage: seeker [options]\n");
  fprintf(out, "Options:\n");
  fprintf(out, "  --config PATH   Load configuration from PATH\n");
  fprintf(out, "  --version, -V   Print version and exit\n");
  fprintf(out, "  --help, -h      Print this help and exit\n");
}

static int load_seeker_config(const char *explicit_path, Config *cfg) {
  if (explicit_path && explicit_path[0]) {
    if (load_config(explicit_path, cfg) == 0)
      return 0;
    fprintf(stderr, "[ERROR] Could not load config file: %s\n", explicit_path);
    return -1;
  }

  if (load_config("config.ini", cfg) == 0)
    return 0;
  if (load_config("/etc/seeker/config.ini", cfg) == 0)
    return 0;

  fprintf(stderr, "[WARN] Could not load config file, using defaults\n");
  return 0;
}

int handle_opensearch(UrlParams *params) {
  (void)params;
  extern Config global_config;
  TemplateContext ctx = new_context();
  context_set(&ctx, "domain", global_config.domain);
  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);
  free(locale);
  char *rendered = render_template("opensearch.xml", &ctx);
  serve_data(rendered, strlen(rendered), "application/opensearchdescription+xml");

  free(rendered);
  free_context(&ctx);
  return 0;
}

int handle_sw(UrlParams *params) {
  (void)params;
  serve_static_file_with_mime("sw.js", "application/javascript");
  return 0;
}

int handle_manifest(UrlParams *params) {
  (void)params;
  serve_static_file_with_mime("manifest.json", "application/manifest+json");
  return 0;
}

int handle_robots(UrlParams *params) {
  (void)params;
  serve_static_file_with_mime("robots.txt", "text/plain; charset=UTF-8");
  return 0;
}

int handle_health(UrlParams *params) {
  (void)params;
  serve_data("ok\n", 3, "text/plain; charset=UTF-8");
  return 0;
}

int main(int argc, char **argv) {
  const char *config_path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
      printf("seeker %s\n", SEEKER_VERSION);
      return EXIT_SUCCESS;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(stdout);
      return EXIT_SUCCESS;
    }
    if (strcmp(argv[i], "--config") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "[ERROR] --config requires a path argument\n");
        return EXIT_FAILURE;
      }
      config_path = argv[++i];
      continue;
    }
    fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
    print_usage(stderr);
    return EXIT_FAILURE;
  }

  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &mask, NULL);

  LIBXML_TEST_VERSION
  xmlInitParser();

  curl_global_init(CURL_GLOBAL_DEFAULT);

  Config cfg = {.host = DEFAULT_HOST,
                .port = DEFAULT_PORT,
                .hsts_max_age_sec = -1,
                .hsts_include_subdomains = 0,
                .domain = "",
                .default_locale = "en_us",
                .proxy = "",
                .proxy_list_file = "",
                .max_proxy_retries = DEFAULT_MAX_PROXY_RETRIES,
                .randomize_username = 0,
                .randomize_password = 0,
                .cache_dir = DEFAULT_CACHE_DIR,
                .cache_ttl_search = DEFAULT_CACHE_TTL_SEARCH,
                .cache_ttl_infobox = DEFAULT_CACHE_TTL_INFOBOX,
                .cache_ttl_image = DEFAULT_CACHE_TTL_IMAGE,
                .engines = "",
                .engines_file = "",
                .yacy_instance = "",
                .index_url = DEFAULT_INDEX_URL,
                .bangs_file = "",
                .http_timeout_sec = 5,
                .http_connect_timeout_sec = DEFAULT_HTTP_CONNECT_TIMEOUT_SEC,
                .max_concurrent_fetches = DEFAULT_MAX_CONCURRENT_FETCHES,
                .cache_stampede_wait_ms = DEFAULT_CACHE_STAMPEDE_WAIT_MS,
                .cache_stampede_max_attempts = DEFAULT_CACHE_STAMPEDE_MAX_ATTEMPTS,
                .retry_backoff_ms = DEFAULT_RETRY_BACKOFF_MS,
                .worker_threads = 0,
                .default_engine = "",
                .rate_limit_search_requests = 45,
                .rate_limit_search_interval = 10,
                .rate_limit_images_requests = 80,
                .rate_limit_images_interval = 10,
                .rate_limit_news_requests = 40,
                .rate_limit_news_interval = 10};

  if (load_seeker_config(config_path, &cfg) != 0) {
    curl_global_cleanup();
    xmlCleanupParser();
    return EXIT_FAILURE;
  }

  {
    const char *v;
    if ((v = getenv("INDEX_URL")) && v[0])
      snprintf(cfg.index_url, sizeof(cfg.index_url), "%s", v);
    if ((v = getenv("SEEK_DOMAIN")) && v[0])
      snprintf(cfg.domain, sizeof(cfg.domain), "%s", v);
    if ((v = getenv("SEEK_ENGINES")) && v[0])
      snprintf(cfg.engines, sizeof(cfg.engines), "%s", v);
    char env_proxy[256];
    proxy_apply_env(env_proxy, sizeof(env_proxy));
    if (env_proxy[0])
      snprintf(cfg.proxy, sizeof(cfg.proxy), "%s", env_proxy);
    if ((v = getenv("SEEK_PROXY_LIST")) && v[0])
      snprintf(cfg.proxy_list_file, sizeof(cfg.proxy_list_file), "%s", v);
  }

  apply_runtime_config(&cfg);

  if (cfg.hsts_max_age_sec != -1)
    beaker_set_hsts_max_age_sec((long)cfg.hsts_max_age_sec);
  beaker_set_hsts_include_subdomains(cfg.hsts_include_subdomains ? 1 : 0);

  if (cfg.engines_file[0] != '\0')
    scraping_load_engines_file(cfg.engines_file);

  apply_engines_config(cfg.engines);
  configure_yacy_engine(cfg.yacy_instance);
  configure_lyra_engine(cfg.index_url);

  if (cfg.yacy_instance[0] != '\0') {
    int yacy_enabled = 0;
    for (int i = 0; i < ENGINE_COUNT; i++) {
      if (strcmp(ENGINE_REGISTRY[i].id, "yacy") == 0) {
        yacy_enabled = ENGINE_REGISTRY[i].enabled;
        break;
      }
    }
    if (!yacy_enabled) {
      fprintf(stderr,
              "[INFO] YaCy instance configured but the yacy engine is not "
              "enabled (add it to the engines list, e.g. engines=\"*,yacy\")\n");
    }
  }

  bangs_init(cfg.bangs_file);

  set_default_locale(cfg.default_locale);
  int loaded_locales = beaker_load_locales();
  if (loaded_locales > 0) {
    fprintf(stderr, "[INFO] Loaded %d locales\n", loaded_locales);
  } else {
    fprintf(stderr, "[WARN] No locales loaded (run from a directory "
                    "containing locales/ to enable translations)\n");
  }

  global_config = cfg;

  if (cache_init(cfg.cache_dir) != 0) {
    fprintf(stderr,
            "[WARN] Failed to initialize cache, continuing without caching\n");
  } else {
    fprintf(stderr, "[INFO] Cache initialized at %s\n", cfg.cache_dir);
    cache_cleanup(cfg.cache_ttl_search);
  }

  set_cache_ttl_search(cfg.cache_ttl_search);
  set_cache_ttl_infobox(cfg.cache_ttl_infobox);
  set_cache_ttl_image(cfg.cache_ttl_image);

  if (cfg.proxy_list_file[0] != '\0') {
    if (load_proxy_list(cfg.proxy_list_file) < 0) {
      fprintf(stderr,
              "[WARN] Failed to load proxy list, continuing without proxies\n");
    }
  }

  max_proxy_retries = cfg.max_proxy_retries;
  set_proxy_config(cfg.proxy, cfg.randomize_username, cfg.randomize_password);

  if (proxy_url[0] != '\0') {
    fprintf(stderr, "[INFO] Using proxy: %s\n", proxy_url);
  } else if (proxy_count > 0) {
    fprintf(stderr, "[INFO] Using %d proxies from %s\n", proxy_count,
            cfg.proxy_list_file);
  }

  set_handler("/", home_handler);
  set_handler("/opensearch.xml", handle_opensearch);
  set_handler("/sw.js", handle_sw);
  set_handler("/manifest.json", handle_manifest);
  set_handler("/robots.txt", handle_robots);
  set_handler("/health", handle_health);
  set_handler("/search", results_handler);
  set_handler("/api/search", json_search_handler);
  set_handler("/rss", rss_handler);
  set_handler("/news", news_handler);
  set_handler("/images", images_handler);
  set_handler("/settings", settings_handler);
  set_handler("/save_settings", settings_save_handler);
  set_handler("/suggest", suggest_handler);
  set_handler("/bangs", bangs_handler);
  set_handler("/proxy", image_proxy_handler);
  set_handler("/favicon", favicon_handler);

  int listen_port = cfg.port;
  int result =
      beaker_run_with_threads(cfg.host, &listen_port, cfg.worker_threads);

  if (result != 0) {
    fprintf(stderr, "[ERROR] Beaker server failed to start.\n");
    curl_global_cleanup();
    xmlCleanupParser();
    return EXIT_FAILURE;
  }

  curl_global_cleanup();
  xmlCleanupParser();
  free_proxy_list();
  cache_shutdown();
  beaker_free_locales();
  return EXIT_SUCCESS;
}
