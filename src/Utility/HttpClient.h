#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <curl/curl.h>
#include <libxml/parser.h>
#include <stddef.h>
#include <time.h>

#define SEEK_HTTP_UA "Seek/1.0 (https://seek.lyrabrowser.com)"

typedef struct {
  char *memory;
  size_t size;
  size_t capacity;
} HttpResponse;

size_t http_response_append_chunk(void *contents, size_t size, size_t nmemb,
                                  void *userp);

HttpResponse http_get(const char *url, const char *user_agent);
HttpResponse http_post_json(const char *url, const char *user_agent,
                            const char *json, long timeout_sec);
void http_response_free(HttpResponse *resp);

typedef struct {
  const char *url;
  const char *user_agent;
  HttpResponse resp;
  CURL *handle;
} HttpGetJob;

void http_get_many(HttpGetJob *jobs, int n, long timeout_sec);

typedef xmlDocPtr (*XmlParserFn)(const char *data, size_t size,
                                 const char *url);

typedef struct {
  char *memory;
  size_t size;
  void *parsed_result;
  int success;
} CachedHttpResponse;

CachedHttpResponse cached_http_get(const char *url, const char *user_agent,
                                   const char *cache_key, time_t cache_ttl,
                                   XmlParserFn parser);

#endif
