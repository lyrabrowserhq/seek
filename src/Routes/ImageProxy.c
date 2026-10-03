#include "ImageProxy.h"
#include "../Proxy/Proxy.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
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

static int ipv4_addr_is_blocked(uint32_t addr_be) {
  uint8_t *b = (uint8_t *)&addr_be;
  if (b[0] == 127)
    return 1;
  if (b[0] == 10)
    return 1;
  if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
    return 1;
  if (b[0] == 192 && b[1] == 168)
    return 1;
  if (b[0] == 0)
    return 1;
  if (b[0] == 169 && b[1] == 254)
    return 1;
  if (b[0] == 100 && b[1] >= 64 && b[1] <= 127)
    return 1;
  return 0;
}

static int ipv6_addr_is_blocked(const struct in6_addr *a) {
  if (a->s6_addr[0] == 0xfe && (a->s6_addr[1] & 0xc0) == 0x80)
    return 1;
  if (a->s6_addr[0] == 0xfc || a->s6_addr[0] == 0xfd)
    return 1;
  if (a->s6_addr[0] == 0xff)
    return 1;

  int all_zero = 1;
  for (int i = 0; i < 15; i++) {
    if (a->s6_addr[i] != 0) {
      all_zero = 0;
      break;
    }
  }
  if (all_zero && a->s6_addr[15] == 1)
    return 1;

  if (a->s6_addr[0] == 0x20 && a->s6_addr[1] == 0x02)
    return 1;

  if (a->s6_addr[10] == 0xff && a->s6_addr[11] == 0xff) {
    uint32_t v4;
    memcpy(&v4, a->s6_addr + 12, sizeof(v4));
    return ipv4_addr_is_blocked(v4);
  }

  return 0;
}

static int host_is_localhost(const char *host) {
  return strcasecmp(host, "localhost") == 0;
}

static int extract_host(const char *url, char *host_out, size_t host_sz) {
  const char *p = url;
  if (strncasecmp(p, "http://", 7) == 0)
    p += 7;
  else if (strncasecmp(p, "https://", 8) == 0)
    p += 8;
  else
    return -1;

  const char *at = strchr(p, '@');
  if (at)
    p = at + 1;

  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close || close <= p + 1)
      return -1;
    size_t inner = (size_t)(close - (p + 1));
    if (inner >= host_sz)
      return -1;
    memcpy(host_out, p + 1, inner);
    host_out[inner] = '\0';
    return 0;
  }

  const char *end = p;
  while (*end && *end != '/' && *end != '?' && *end != '#')
    end++;

  const char *colon = memchr(p, ':', (size_t)(end - p));
  if (colon) {
    int all_digits = 1;
    for (const char *x = colon + 1; x < end; x++) {
      if (!isdigit((unsigned char)*x)) {
        all_digits = 0;
        break;
      }
    }
    if (all_digits)
      end = colon;
  }

  size_t len = (size_t)(end - p);
  if (len == 0 || len >= host_sz)
    return -1;
  memcpy(host_out, p, len);
  host_out[len] = '\0';
  return 0;
}

static int is_safe_proxy_target(const char *url) {
  if (!url)
    return 0;
  size_t n = strlen(url);
  if (n == 0 || n > MAX_PROXY_TARGET_URL)
    return 0;

  char host[512];
  if (extract_host(url, host, sizeof(host)) != 0)
    return 0;

  if (host_is_localhost(host))
    return 0;

  struct in_addr v4;
  if (inet_pton(AF_INET, host, &v4) == 1) {
    return !ipv4_addr_is_blocked(v4.s_addr);
  }

  struct in6_addr v6;
  if (inet_pton(AF_INET6, host, &v6) == 1) {
    return !ipv6_addr_is_blocked(&v6);
  }

  return 1;
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

int image_proxy_handler(UrlParams *params) {
  const char *url = NULL;
  for (int i = 0; i < params->count; i++) {
    if (strcmp(params->params[i].key, "url") == 0) {
      url = params->params[i].value;
      break;
    }
  }

  if (!url || strlen(url) == 0) {
    send_response("Missing 'url' parameter");
    return 0;
  }

  if (!is_safe_proxy_target(url)) {
    send_response("URL not allowed");
    return 0;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    send_response("Failed to initialize curl");
    return 0;
  }

  MemoryBuffer buf = {.data = malloc(8192), .size = 0, .capacity = 8192};

  if (!buf.data) {
    curl_easy_cleanup(curl);
    send_response("Memory allocation failed");
    return 0;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "Seek/1.0 (https://seek.lyrabrowser.com)");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  {
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Accept: image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
    if (strcasestr(url, "wikimedia.org") || strcasestr(url, "wikipedia.org"))
      hdrs = curl_slist_append(hdrs, "Referer: https://en.wikipedia.org/");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#elif LIBCURL_VERSION_NUM >= 0x071304
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                     CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    apply_proxy_settings(curl);

    CURLcode res = curl_easy_perform(curl);

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    char *content_type_ptr = NULL;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type_ptr);

    char content_type[64] = {0};
    if (content_type_ptr) {
      strncpy(content_type, content_type_ptr, sizeof(content_type) - 1);
    }

    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || response_code != 200) {
      free(buf.data);
      send_response("Failed to fetch image");
      return 0;
    }

    const char *mime_type =
        strlen(content_type) > 0 ? content_type : "image/jpeg";
    if (strncmp(mime_type, "text/", 5) == 0) {
      free(buf.data);
      send_response("Failed to fetch image");
      return 0;
    }
    serve_data(buf.data, buf.size, mime_type);

    free(buf.data);
    return 0;
  }
}
