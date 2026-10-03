#include "../Proxy/Proxy.h"
#include "Config.h"
#include "Scraping.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HTTP_DELAY_MIN_US 100000
#define HTTP_DELAY_RANGE_US 100000

static const char *USER_AGENTS[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
    "like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/131.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:133.0) Gecko/20100101 "
    "Firefox/133.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/18.2 Safari/605.1.15"};

#define USER_AGENT_COUNT (sizeof(USER_AGENTS) / sizeof(USER_AGENTS[0]))

size_t write_memory_callback(void *contents, size_t size, size_t nmemb,
                             void *userp) {
  size_t realsize = size * nmemb;
  MemoryBuffer *mem = (MemoryBuffer *)userp;

  if (mem->size + realsize + 1 > mem->capacity) {
    size_t new_cap =
        mem->capacity == 0 ? INITIAL_BUFFER_SIZE : mem->capacity * 2;
    while (new_cap < mem->size + realsize + 1)
      new_cap *= 2;

    char *ptr = (char *)realloc(mem->memory, new_cap);
    if (!ptr)
      return 0;
    mem->memory = ptr;
    mem->capacity = new_cap;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

const char *get_random_user_agent(void) {
  return USER_AGENTS[rand() % USER_AGENT_COUNT];
}

void configure_curl_handle(CURL *curl, const char *full_url,
                           MemoryBuffer *chunk, struct curl_slist *headers) {
  curl_easy_setopt(curl, CURLOPT_URL, full_url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, get_random_user_agent());

  curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, CURL_DNS_TIMEOUT_SECS);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)g_http_timeout_sec);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)g_http_connect_timeout_sec);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

  apply_proxy_settings(curl);
}

char *build_search_url(const char *base_url, const char *page_param,
                       int page_multiplier, int page_base,
                       const char *encoded_query, int page) {
  int page_value = (page < 1 ? 1 : page - 1) * page_multiplier + page_base;
  char *url = malloc(BUFFER_SIZE_LARGE);
  if (!url)
    return NULL;
  /* First SERP page: omit the pagination param. Startpage (and similar engines
   * with page_base=1) return a block page when e.g. &page=1 is present. */
  if (page_value == page_base) {
    snprintf(url, BUFFER_SIZE_LARGE, "%s%s", base_url, encoded_query);
  } else {
    snprintf(url, BUFFER_SIZE_LARGE, "%s%s&%s=%d", base_url, encoded_query,
             page_param, page_value);
  }
  return url;
}

struct curl_slist *build_request_headers(const char *host_header,
                                         const char *referer) {
  struct curl_slist *headers = NULL;
  char host_buf[BUFFER_SIZE_MEDIUM], ref_buf[BUFFER_SIZE_MEDIUM];

  if (host_header && host_header[0] != '\0') {
    snprintf(host_buf, sizeof(host_buf), "Host: %s", host_header);
    headers = curl_slist_append(headers, host_buf);
  }
  if (referer && referer[0] != '\0') {
    snprintf(ref_buf, sizeof(ref_buf), "Referer: %s", referer);
    headers = curl_slist_append(headers, ref_buf);
  }
  headers = curl_slist_append(
      headers,
      "Accept: "
      "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
  headers = curl_slist_append(headers, "Accept-Language: en-US,en;q=0.5");
  headers = curl_slist_append(headers, "DNT: 1");

  return headers;
}

void http_delay(void) {
}
