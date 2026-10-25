#include "ImageProxy.h"
#include "../Proxy/Proxy.h"
#include "../Limiter/RateLimit.h"
#include "../Utility/NetGuard.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_IMAGE_SIZE (10 * 1024 * 1024)
#define MAX_PROXY_TARGET_URL 4096

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} MemoryBuffer;

static int is_safe_proxy_target(const char *url) {
  if (!url)
    return 0;
  size_t n = strlen(url);
  if (n == 0 || n > MAX_PROXY_TARGET_URL)
    return 0;
  if (strncasecmp(url, "http://", 7) != 0 &&
      strncasecmp(url, "https://", 8) != 0)
    return 0;
  char host[256];
  if (netguard_url_host(url, host, sizeof(host)) != 0)
    return 0;
  return netguard_host_is_public_literal_or_name(host);
}

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             void *userp) {
  size_t realsize = size * nmemb;
  MemoryBuffer *buf = (MemoryBuffer *)userp;

  if (buf->size + realsize > MAX_IMAGE_SIZE) {
    return 0;
  }

  if (buf->size + realsize > buf->capacity) {
    size_t new_capacity = buf->capacity * 2;
    if (new_capacity < buf->size + realsize) {
      new_capacity = buf->size + realsize;
    }
    char *new_data = realloc(buf->data, new_capacity);
    if (!new_data)
      return 0;
    buf->data = new_data;
    buf->capacity = new_capacity;
  }

  memcpy(buf->data + buf->size, contents, realsize);
  buf->size += realsize;
  return realsize;
}

static void reset_buf(void *arg) {
  MemoryBuffer *buf = (MemoryBuffer *)arg;
  buf->size = 0;
}

char *proxy_wrap_image_url(const char *url) {
  if (!url || strlen(url) == 0)
    return NULL;
  if (strcmp(url, "#") == 0)
    return strdup("#");
  if (!is_safe_proxy_target(url))
    return strdup(url);

  char *out = NULL;
  CURL *curl = curl_easy_init();
  if (!curl)
    return strdup(url);

  char *encoded = curl_easy_escape(curl, (char *)url, 0);
  if (encoded) {
    size_t len = strlen("/proxy?url=") + strlen(encoded) + 1;
    out = malloc(len);
    if (out)
      snprintf(out, len, "/proxy?url=%s", encoded);
    curl_free(encoded);
  }
  curl_easy_cleanup(curl);

  return out ? out : strdup(url);
}

static const char *allowed_image_mime(const char *content_type) {
  static const char *allowed[] = {
      "image/png",     "image/jpeg",    "image/gif",  "image/webp",
      "image/avif",    "image/bmp",     "image/x-icon",
      "image/vnd.microsoft.icon", NULL};
  if (!content_type)
    return NULL;
  char ct[64];
  size_t i = 0;
  while (content_type[i] && content_type[i] != ';' && i + 1 < sizeof(ct))
    i++;
  size_t n = i;
  while (n > 0 && (content_type[n - 1] == ' ' || content_type[n - 1] == '\t'))
    n--;
  if (n == 0 || n >= sizeof(ct))
    return NULL;
  memcpy(ct, content_type, n);
  ct[n] = '\0';
  for (int j = 0; allowed[j]; j++) {
    if (strcasecmp(ct, allowed[j]) == 0)
      return allowed[j];
  }
  return NULL;
}

int image_proxy_handler(UrlParams *params) {
  {
    RateLimitConfig rl = {.max_requests = 300, .interval_seconds = 10};
    RateLimitResult rlres = rate_limit_check("proxy", &rl);
    if (rlres.limited) {
      char retry[64];
      snprintf(retry, sizeof(retry), "Retry-After: %d",
               rlres.retry_after_seconds);
      send_response_with_status("429 Too Many Requests", "<h1>429</h1>",
                                retry);
      return 0;
    }
  }
  const char *url = NULL;
  for (int i = 0; i < params->count; i++) {
    if (strcmp(params->params[i].key, "url") == 0) {
      url = params->params[i].value;
      break;
    }
  }

  if (!url || strlen(url) == 0 || strlen(url) > MAX_PROXY_TARGET_URL) {
    send_status("400 Bad Request");
    return 0;
  }

  if (!is_safe_proxy_target(url)) {
    send_status("403 Forbidden");
    return 0;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    send_status("502 Bad Gateway");
    return 0;
  }

  MemoryBuffer buf = {.data = malloc(8192), .size = 0, .capacity = 8192};

  if (!buf.data) {
    curl_easy_cleanup(curl);
    send_status("500 Internal Server Error");
    return 0;
  }

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "Seek/1.0 (https://seek.lyrabrowser.com)");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  struct curl_slist *hdrs = NULL;
  hdrs = curl_slist_append(
      hdrs, "Accept: image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
  if (strcasestr(url, "wikimedia.org") || strcasestr(url, "wikipedia.org"))
    hdrs = curl_slist_append(hdrs, "Referer: https://en.wikipedia.org/");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  apply_proxy_settings(curl);

  CURLcode res = netguard_perform(curl, url, 5, reset_buf, &buf);

  long response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  char *content_type_ptr = NULL;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type_ptr);
  const char *mime_type = allowed_image_mime(content_type_ptr);

  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK || response_code != 200) {
    free(buf.data);
    if (res == CURLE_REMOTE_ACCESS_DENIED ||
        res == CURLE_COULDNT_RESOLVE_HOST)
      send_status("403 Forbidden");
    else if (res == CURLE_URL_MALFORMAT)
      send_status("400 Bad Request");
    else
      send_status("502 Bad Gateway");
    return 0;
  }

  if (!mime_type) {
    free(buf.data);
    send_status("502 Bad Gateway");
    return 0;
  }

  serve_data(buf.data, buf.size, mime_type);
  free(buf.data);
  return 0;
}
