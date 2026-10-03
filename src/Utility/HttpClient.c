#include "HttpClient.h"
#include "../Cache/Cache.h"
#include "../Proxy/Proxy.h"
#include "Config.h"
#include <stdlib.h>
#include <string.h>

size_t http_response_append_chunk(void *contents, size_t size, size_t nmemb,
                                  void *userp) {
  size_t realsize = size * nmemb;
  HttpResponse *mem = (HttpResponse *)userp;

  if (mem->size + realsize + 1 > mem->capacity) {
    size_t new_cap =
        mem->capacity == 0 ? INITIAL_BUFFER_SIZE : mem->capacity * 2;
    while (new_cap < mem->size + realsize + 1)
      new_cap *= 2;

    char *ptr = realloc(mem->memory, new_cap);
    if (!ptr) {
      return 0;
    }
    mem->memory = ptr;
    mem->capacity = new_cap;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

HttpResponse http_get(const char *url, const char *user_agent) {
  HttpResponse resp = {.memory = NULL, .size = 0, .capacity = 0};

  if (!url) {
    return resp;
  }

  resp.memory = malloc(INITIAL_BUFFER_SIZE);
  if (!resp.memory) {
    return resp;
  }
  resp.capacity = INITIAL_BUFFER_SIZE;

  CURL *curl = curl_easy_init();
  if (!curl) {
    free(resp.memory);
    resp.memory = NULL;
    return resp;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_response_append_chunk);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   user_agent ? user_agent : SEEK_HTTP_UA);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)g_http_timeout_sec);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)g_http_connect_timeout_sec);
  apply_proxy_settings(curl);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    free(resp.memory);
    resp.memory = NULL;
    resp.size = 0;
    resp.capacity = 0;
  }

  return resp;
}

HttpResponse http_post_json(const char *url, const char *user_agent,
                            const char *json, long timeout_sec) {
  HttpResponse resp = {.memory = NULL, .size = 0, .capacity = 0};
  if (!url || !json)
    return resp;

  resp.memory = malloc(INITIAL_BUFFER_SIZE);
  if (!resp.memory)
    return resp;
  resp.capacity = INITIAL_BUFFER_SIZE;

  CURL *curl = curl_easy_init();
  if (!curl) {
    free(resp.memory);
    resp.memory = NULL;
    return resp;
  }

  struct curl_slist *hdrs = NULL;
  hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
  hdrs = curl_slist_append(hdrs, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(json));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_response_append_chunk);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   user_agent ? user_agent : "Seek/1.0");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                   timeout_sec > 0 ? timeout_sec : 2L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
  apply_proxy_settings(curl);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    free(resp.memory);
    resp.memory = NULL;
    resp.size = 0;
    resp.capacity = 0;
  }
  return resp;
}

void http_response_free(HttpResponse *resp) {
  if (!resp) {
    return;
  }
  free(resp->memory);
  resp->memory = NULL;
  resp->size = 0;
  resp->capacity = 0;
}

void http_get_many(HttpGetJob *jobs, int n, long timeout_sec) {
  if (!jobs || n <= 0)
    return;

  CURLM *multi = curl_multi_init();
  if (!multi) {
    for (int i = 0; i < n; i++)
      jobs[i].resp = http_get(jobs[i].url, jobs[i].user_agent);
    return;
  }

  curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 12L);
  curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, 4L);
#ifdef CURLPIPE_MULTIPLEX
  curl_multi_setopt(multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif

  long timeout = timeout_sec > 0 ? timeout_sec : 4L;
  for (int i = 0; i < n; i++) {
    jobs[i].handle = NULL;
    jobs[i].resp = (HttpResponse){0};
    if (!jobs[i].url || !jobs[i].url[0])
      continue;
    jobs[i].resp.memory = malloc(INITIAL_BUFFER_SIZE);
    if (!jobs[i].resp.memory)
      continue;
    jobs[i].resp.capacity = INITIAL_BUFFER_SIZE;
    CURL *curl = curl_easy_init();
    if (!curl) {
      free(jobs[i].resp.memory);
      jobs[i].resp.memory = NULL;
      jobs[i].resp.capacity = 0;
      continue;
    }
    jobs[i].handle = curl;
    curl_easy_setopt(curl, CURLOPT_URL, jobs[i].url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_response_append_chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &jobs[i].resp);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     jobs[i].user_agent ? jobs[i].user_agent : "Seek/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#endif
    curl_easy_setopt(curl, CURLOPT_PRIVATE, &jobs[i]);
    apply_proxy_settings(curl);
    if (curl_multi_add_handle(multi, curl) != CURLM_OK) {
      curl_easy_cleanup(curl);
      jobs[i].handle = NULL;
      http_response_free(&jobs[i].resp);
    }
  }

  int still = 0;
  CURLMcode mc = curl_multi_perform(multi, &still);
  while (mc == CURLM_OK && still) {
    int numfds = 0;
    mc = curl_multi_wait(multi, NULL, 0, 1000, &numfds);
    if (mc != CURLM_OK)
      break;
    mc = curl_multi_perform(multi, &still);
  }

  CURLMsg *msg;
  int left = 0;
  while ((msg = curl_multi_info_read(multi, &left))) {
    if (msg->msg != CURLMSG_DONE)
      continue;
    CURL *h = msg->easy_handle;
    char *priv = NULL;
    curl_easy_getinfo(h, CURLINFO_PRIVATE, &priv);
    HttpGetJob *job = (HttpGetJob *)priv;
    long code = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
    if (msg->data.result != CURLE_OK || (code != 0 && code != 200)) {
      if (job)
        http_response_free(&job->resp);
    }
    curl_multi_remove_handle(multi, h);
    curl_easy_cleanup(h);
    if (job)
      job->handle = NULL;
  }

  for (int i = 0; i < n; i++) {
    if (!jobs[i].handle)
      continue;
    curl_multi_remove_handle(multi, jobs[i].handle);
    curl_easy_cleanup(jobs[i].handle);
    jobs[i].handle = NULL;
    http_response_free(&jobs[i].resp);
  }
  curl_multi_cleanup(multi);
}

CachedHttpResponse cached_http_get(const char *url, const char *user_agent,
                                   const char *cache_key, time_t cache_ttl,
                                   XmlParserFn parser) {
  CachedHttpResponse result = {
      .memory = NULL, .size = 0, .parsed_result = NULL, .success = 0};

  if (!url || !parser) {
    return result;
  }

  if (cache_key && cache_ttl > 0) {
    char *cached_data = NULL;
    size_t cached_size = 0;
    if (cache_get(cache_key, cache_ttl, &cached_data, &cached_size) == 0 &&
        cached_data && cached_size > 0) {
      xmlDocPtr doc = parser(cached_data, cached_size, url);
      if (doc) {
        result.parsed_result = doc;
        result.success = 1;
      }
      free(cached_data);
      return result;
    }
    free(cached_data);
  }

  HttpResponse resp = http_get(url, user_agent);
  if (resp.memory && resp.size > 0) {
    if (cache_key && cache_ttl > 0) {
      cache_set(cache_key, resp.memory, resp.size);
    }

    xmlDocPtr doc = parser(resp.memory, resp.size, url);
    if (doc) {
      result.parsed_result = doc;
      result.success = 1;
    }
  }

  result.memory = resp.memory;
  result.size = resp.size;
  return result;
}
