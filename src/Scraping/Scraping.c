#include "Scraping.h"
#include "../Cache/Cache.h"
#include "../Proxy/Proxy.h"
#include "../Utility/XmlHelper.h"
#include "Config.h"
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int response_contains(const char *response, const char *needle) {
  return response && needle && strstr(response, needle) != NULL;
}

static int is_startpage_job(const ScrapeJob *job) {
  return job && job->engine && strcmp(job->engine->name, "Startpage") == 0;
}

static int response_is_startpage_captcha(const ScrapeJob *job,
                                         const char *response) {
  if (!is_startpage_job(job))
    return 0;

  return response_contains(response, "<title>Startpage Captcha</title>") ||
         response_contains(response, "Startpage Captcha") ||
         response_contains(response, "/static-pages-assets/page-data/captcha/") ||
         response_contains(response, ">Startpage Blocked</title>");
}

static int response_looks_like_results_page(const ScrapeJob *job,
                                            const char *response) {
  if (!job || !job->engine || !response)
    return 0;

  if (strcmp(job->engine->name, "DuckDuckGo Lite") == 0) {
    return response_contains(response, "result-link") ||
           response_contains(response, "result-snippet");
  }

  if (strcmp(job->engine->name, "Startpage") == 0) {
    return response_contains(response, "<title>Startpage Search Results</title>") ||
           response_contains(response, "class=\"w-gl") ||
           response_contains(response, "data-testid=\"gl-title-link\"");
  }

  if (strcmp(job->engine->name, "Brave") == 0) {
    return response_contains(response, "result-wrapper") ||
           response_contains(response, "search-snippet-title");
  }

  if (strcmp(job->engine->name, "Yahoo") == 0) {
    return response_contains(response, "algo-sr") ||
           response_contains(response, "compTitle") ||
           response_contains(response, "compText");
  }

  if (strcmp(job->engine->name, "Mojeek") == 0) {
    return response_contains(response, "class=\"results-standard\"") ||
           response_contains(response, "Mojeek Search");
  }

  if (strcmp(job->engine->name, "Wiby") == 0) {
    return response_contains(response, "class=\"tlink\"") ||
           response_contains(response, "wiby.me");
  }

  if (strcmp(job->engine->name, "YaCy") == 0) {
    return response_contains(response, "<item") ||
           response_contains(response, "<rss");
  }

  if (strcmp(job->engine->name, "Lyra Index") == 0) {
    return response_contains(response, "<item") ||
           response_contains(response, "<rss");
  }

  if (job->engine->is_json) {
    return response_contains(response, "\"title\"") ||
           response_contains(response, "\"hits\"") ||
           response_contains(response, "\"search\"");
  }

  return 0;
}

static void classify_job_response(ScrapeJob *job, const char *response,
                                  size_t response_size) {
  job->results_count = 0;

  if (!response || response_size == 0) {
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    return;
  }

  if (response_is_startpage_captcha(job, response)) {
    job->status = SCRAPE_STATUS_BLOCKED;
    return;
  }

  if (job->engine->is_json && job->engine->json_parser) {
    job->results_count = job->engine->json_parser(
        job->engine->id, response, job->out_results, job->max_results);
    if (job->results_count > 0) {
      job->status = SCRAPE_STATUS_OK;
      return;
    }
    if (job->http_status >= 400) {
      job->status = SCRAPE_STATUS_FETCH_ERROR;
      return;
    }
    if (response_contains(response, "\"title\"") ||
        response_contains(response, "\"hits\"") ||
        response_contains(response, "\"search\"")) {
      job->status = SCRAPE_STATUS_PARSE_MISMATCH;
      return;
    }
    job->status = SCRAPE_STATUS_EMPTY;
    return;
  }

  xmlDocPtr doc;
  if (job->engine->is_xml) {
    doc = xmlReadMemory(response, response_size, NULL, NULL,
                        XML_PARSE_RECOVER | XML_PARSE_NOERROR |
                            XML_PARSE_NOWARNING);
  } else {
    doc = htmlReadMemory(response, response_size, NULL, NULL,
                         HTML_PARSE_RECOVER | HTML_PARSE_NOERROR |
                             HTML_PARSE_NOWARNING);
  }

  if (!doc) {
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    return;
  }

  job->results_count =
      job->engine->parser(job->engine->name, doc, job->out_results,
                          job->max_results);
  xmlFreeDoc(doc);

  if (job->results_count > 0) {
    job->status = SCRAPE_STATUS_OK;
    return;
  }

  if (job->http_status >= 400) {
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    return;
  }

  if (response_looks_like_results_page(job, response)) {
    job->status = SCRAPE_STATUS_PARSE_MISMATCH;
    return;
  }

  job->status = SCRAPE_STATUS_EMPTY;
}

int check_cache_for_job(ScrapeJob *job) {
  if (get_cache_ttl_search() <= 0)
    return 0;

  free(job->cache_key);
  job->cache_key = cache_compute_key(job->query, job->page, job->engine->name);
  if (!job->cache_key)
    return 0;

  char *cached_data = NULL;
  size_t cached_size = 0;

  if (cache_get(job->cache_key, (time_t)get_cache_ttl_search(), &cached_data,
                &cached_size) == 0 &&
      cached_data && cached_size > 0) {
    classify_job_response(job, cached_data, cached_size);

    if (job->status == SCRAPE_STATUS_BLOCKED) {
      free(cached_data);
      free(job->cache_key);
      job->cache_key = NULL;
      return 0;
    }

    free(cached_data);

    if (job->results_count == 0) {
      free(job->cache_key);
      job->cache_key = NULL;
      return 0;
    }

    free(job->cache_key);
    job->cache_key = NULL;
    return 1;
  }

  free(cached_data);
  return 0;
}

void parse_and_cache_response(ScrapeJob *job) {
  if (job->response.size == 0) {
    job->results_count = 0;
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    return;
  }

  classify_job_response(job, job->response.memory, job->response.size);

  if (job->status == SCRAPE_STATUS_OK || job->status == SCRAPE_STATUS_EMPTY) {
    if (job->cache_key && get_cache_ttl_search() > 0)
      cache_set(job->cache_key, job->response.memory, job->response.size);
  }
}

static void cleanup_job_results(ScrapeJob *job) {
  if (!job || !job->out_results)
    return;

  xml_result_free(*job->out_results, job->results_count);
  *job->out_results = NULL;
  job->results_count = 0;
}

static void cleanup_job_handle(ScrapeJob *job, CURL *handle) {
  struct curl_slist *headers = NULL;
  if (handle)
    curl_easy_getinfo(handle, CURLINFO_PRIVATE, &headers);
  if (headers)
    curl_slist_free_all(headers);

  if (job->cache_lock_held && job->cache_key) {
    cache_lock_release(job->cache_key);
    job->cache_lock_held = 0;
  }
  free(job->cache_key);
  job->cache_key = NULL;

  free(job->response.memory);
  job->response.memory = NULL;
  job->response.size = 0;
  job->response.capacity = 0;
}

void process_response(ScrapeJob *job, CURL *handle, CURLMsg *msg) {
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &job->http_status);

  if (msg->data.result == CURLE_OK ||
      (job->response.size > 0 && job->http_status >= 200 &&
       job->http_status < 400))
    parse_and_cache_response(job);
  else {
    job->results_count = 0;
    job->status = SCRAPE_STATUS_FETCH_ERROR;
  }

  cleanup_job_handle(job, handle);
}

static void scrape_job_abort_cache(ScrapeJob *job) {
  if (job->cache_lock_held && job->cache_key) {
    cache_lock_release(job->cache_key);
    job->cache_lock_held = 0;
  }
  free(job->cache_key);
  job->cache_key = NULL;
}

int setup_job(ScrapeJob *job, CURLM *multi_handle) {
  if (job->handle) {
    cleanup_job_handle(job, job->handle);
    curl_easy_cleanup(job->handle);
    job->handle = NULL;
  } else if (job->response.memory) {
    free(job->response.memory);
    job->response.memory = NULL;
    job->response.size = 0;
    job->response.capacity = 0;
  }

  cleanup_job_results(job);
  job->http_status = 0;
  job->status = SCRAPE_STATUS_PENDING;
  job->cache_lock_held = 0;

  if (check_cache_for_job(job))
    return 0;

  if (job->cache_key && get_cache_ttl_search() > 0) {
    if (cache_lock_acquire(job->cache_key) == 0) {
      job->cache_lock_held = 1;
    } else {
      int attempts = 0;
      while (attempts < g_cache_stampede_max_attempts) {
        usleep((useconds_t)g_cache_stampede_wait_ms * 1000);
        char *cached_data = NULL;
        size_t cached_size = 0;
        if (cache_get(job->cache_key, (time_t)get_cache_ttl_search(),
                      &cached_data, &cached_size) == 0 &&
            cached_data && cached_size > 0) {
          classify_job_response(job, cached_data, cached_size);
          free(cached_data);
          if (job->status != SCRAPE_STATUS_BLOCKED && job->results_count > 0) {
            free(job->cache_key);
            job->cache_key = NULL;
            return 0;
          }
        } else {
          free(cached_data);
        }
        if (cache_lock_acquire(job->cache_key) == 0) {
          job->cache_lock_held = 1;
          break;
        }
        attempts++;
      }
    }
  }

  cleanup_job_results(job);

  char *encoded_query = curl_easy_escape(NULL, job->query, 0);
  if (!encoded_query) {
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    scrape_job_abort_cache(job);
    return -1;
  }

  char *read = encoded_query;
  char *write = encoded_query;
  while (*read) {
    if (read[0] == '%' && read[1] == '2' && read[2] == '0') {
      *write++ = '+';
      read += 3;
    } else {
      *write++ = *read++;
    }
  }
  *write = '\0';

  char *full_url =
      build_search_url(job->engine->base_url, job->engine->page_param,
                       job->engine->page_multiplier, job->engine->page_base,
                       encoded_query, job->page);
  free(encoded_query);

  if (!full_url) {
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    scrape_job_abort_cache(job);
    return -1;
  }

  job->handle = curl_easy_init();
  if (!job->handle) {
    free(full_url);
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    scrape_job_abort_cache(job);
    return -1;
  }

  job->response.memory = (char *)malloc(INITIAL_BUFFER_SIZE);
  job->response.size = 0;
  job->response.capacity = INITIAL_BUFFER_SIZE;
  if (!job->response.memory) {
    curl_easy_cleanup(job->handle);
    job->handle = NULL;
    job->response.capacity = 0;
    free(full_url);
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    scrape_job_abort_cache(job);
    return -1;
  }

  struct curl_slist *headers =
      build_request_headers(job->engine->host_header, job->engine->referer);

  configure_curl_handle(job->handle, full_url, &job->response, headers);
  if (job->engine && job->engine->is_json) {
    long to = strcmp(job->engine->id, "lyra") == 0 ? 8L : 4L;
    curl_easy_setopt(job->handle, CURLOPT_TIMEOUT, to);
    curl_easy_setopt(job->handle, CURLOPT_CONNECTTIMEOUT, 2L);
  }
  curl_easy_setopt(job->handle, CURLOPT_PRIVATE, headers);

  free(full_url);
  CURLMcode add_result = curl_multi_add_handle(multi_handle, job->handle);
  if (add_result != CURLM_OK) {
    cleanup_job_handle(job, job->handle);
    curl_easy_cleanup(job->handle);
    job->handle = NULL;
    job->status = SCRAPE_STATUS_FETCH_ERROR;
    scrape_job_abort_cache(job);
    return -1;
  }
  return 0;
}

static void cleanup_unfinished_jobs(CURLM *multi_handle, ScrapeJob *jobs,
                                    int num_jobs) {
  for (int i = 0; i < num_jobs; i++) {
    if (!jobs[i].handle)
      continue;

    curl_multi_remove_handle(multi_handle, jobs[i].handle);
    cleanup_job_handle(&jobs[i], jobs[i].handle);
    curl_easy_cleanup(jobs[i].handle);
    jobs[i].handle = NULL;

    if (jobs[i].status == SCRAPE_STATUS_PENDING)
      jobs[i].status = SCRAPE_STATUS_FETCH_ERROR;
  }
}

int handle_responses(CURLM *multi_handle, ScrapeJob *jobs, int num_jobs) {
  CURLMsg *msg;
  int msgs_left;

  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg != CURLMSG_DONE)
      continue;

    CURL *handle = msg->easy_handle;

    for (int i = 0; i < num_jobs; i++) {
      if (jobs[i].handle && jobs[i].handle == handle) {
        process_response(&jobs[i], handle, msg);
        curl_multi_remove_handle(multi_handle, handle);
        curl_easy_cleanup(handle);
        jobs[i].handle = NULL;
        break;
      }
    }
  }

  return 0;
}

int should_retry(ScrapeJob *jobs, int num_jobs) {
  if (proxy_count <= 0 && proxy_url[0] == '\0')
    return 0;

  int need = 0;
  for (int i = 0; i < num_jobs; i++) {
    if (jobs[i].status == SCRAPE_STATUS_FETCH_ERROR ||
        jobs[i].status == SCRAPE_STATUS_BLOCKED)
      need = 1;
  }
  if (need)
    proxy_heal();
  return need;
}

static int scrape_one_multi(ScrapeJob *jobs, int num_jobs) {
  int retries = 0;

retry:
  ;
  CURLM *multi_handle = curl_multi_init();
  if (!multi_handle)
    return -1;

  long maxconn = (long)g_max_concurrent_fetches;
  if (maxconn < 1L)
    maxconn = 1L;
  curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, maxconn);
  curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 4L);
#ifdef CURLPIPE_MULTIPLEX
  curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif

  for (int i = 0; i < num_jobs; i++) {
    setup_job(&jobs[i], multi_handle);
  }

  int still_running = 0;
  CURLMcode mc = curl_multi_perform(multi_handle, &still_running);

  while (mc == CURLM_OK && still_running) {
    int numfds = 0;
    mc = curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
    if (mc != CURLM_OK)
      break;
    mc = curl_multi_perform(multi_handle, &still_running);
  }

  handle_responses(multi_handle, jobs, num_jobs);
  cleanup_unfinished_jobs(multi_handle, jobs, num_jobs);
  curl_multi_cleanup(multi_handle);

  if (retries < max_proxy_retries && should_retry(jobs, num_jobs)) {
    retries++;
    if (g_retry_backoff_ms > 0) {
      int shift = retries > 8 ? 8 : retries;
      usleep((useconds_t)g_retry_backoff_ms * 1000U << (unsigned)shift);
    }
    goto retry;
  }

  return 0;
}

int scrape_engines_parallel(ScrapeJob *jobs, int num_jobs) {
  if (num_jobs <= 0)
    return 0;
  return scrape_one_multi(jobs, num_jobs);
}

int scrape_engine(const SearchEngine *engine, const char *query,
                  SearchResult **out_results, int max_results) {
  ScrapeJob job = {.engine = engine,
                   .query = (char *)query,
                   .out_results = out_results,
                   .max_results = max_results,
                   .results_count = 0,
                   .page = 1,
                   .http_status = 0,
                   .status = SCRAPE_STATUS_PENDING,
                   .cache_key = NULL,
                   .cache_lock_held = 0};

  scrape_engines_parallel(&job, 1);
  return job.results_count;
}
