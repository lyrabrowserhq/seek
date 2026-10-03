#include "Favicon.h"
#include "../Cache/Cache.h"
#include "../Proxy/Proxy.h"
#include "../Utility/HttpClient.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FAVICON_MAX (64 * 1024)
#define FAVICON_HTML_MAX (24 * 1024)
#define FAVICON_TTL 604800

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} MemBuf;

static const char PLACEHOLDER_SVG[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\" "
    "width=\"16\" height=\"16\">"
    "<rect width=\"16\" height=\"16\" rx=\"3\" fill=\"#3f3f46\"/>"
    "<circle cx=\"8\" cy=\"8\" r=\"4.2\" fill=\"none\" stroke=\"#a1a1aa\" "
    "stroke-width=\"1.2\"/>"
    "<ellipse cx=\"8\" cy=\"8\" rx=\"1.8\" ry=\"4.2\" fill=\"none\" "
    "stroke=\"#a1a1aa\" stroke-width=\"1.2\"/>"
    "<path d=\"M3.8 8h8.4\" fill=\"none\" stroke=\"#a1a1aa\" "
    "stroke-width=\"1.2\"/>"
    "</svg>";

static const char MISS_MAP[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static size_t fav_write(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t n = size * nmemb;
  MemBuf *b = userp;
  if (b->size + n > FAVICON_MAX)
    return 0;
  if (b->size + n > b->capacity) {
    size_t cap = b->capacity ? b->capacity * 2 : 4096;
    while (cap < b->size + n)
      cap *= 2;
    char *p = realloc(b->data, cap);
    if (!p)
      return 0;
    b->data = p;
    b->capacity = cap;
  }
  memcpy(b->data + b->size, contents, n);
  b->size += n;
  return n;
}

static int sha256_hex(const unsigned char *data, size_t n, char *out,
                      size_t out_sz) {
  if (out_sz < 65)
    return -1;
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hlen = 0;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx)
    return -1;
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
      EVP_DigestUpdate(ctx, data, n) != 1 ||
      EVP_DigestFinal_ex(ctx, hash, &hlen) != 1) {
    EVP_MD_CTX_free(ctx);
    return -1;
  }
  EVP_MD_CTX_free(ctx);
  for (unsigned int i = 0; i < hlen && (i * 2 + 2) < out_sz; i++)
    sprintf(out + (i * 2), "%02x", hash[i]);
  out[hlen * 2] = '\0';
  return 0;
}

int favicon_host_hash(const char *host, char *out, size_t out_sz) {
  if (!host || !out)
    return -1;
  char lower[256];
  size_t n = 0;
  for (; host[n] && n + 1 < sizeof(lower); n++)
    lower[n] = (char)tolower((unsigned char)host[n]);
  lower[n] = '\0';
  return sha256_hex((const unsigned char *)lower, n, out, out_sz);
}

int favicon_parent_host(const char *host, char *out, size_t out_sz) {
  if (!host || !out || out_sz < 4)
    return -1;
  const char *dot = strchr(host, '.');
  if (!dot || !dot[1] || !strchr(dot + 1, '.'))
    return -1;
  snprintf(out, out_sz, "%s", dot + 1);
  return 0;
}

static int extract_host(const char *url, char *host, size_t host_sz) {
  const char *p = url;
  if (!p)
    return -1;
  if (!strncasecmp(p, "http://", 7))
    p += 7;
  else if (!strncasecmp(p, "https://", 8))
    p += 8;
  else
    return -1;
  const char *at = strchr(p, '@');
  if (at)
    p = at + 1;
  const char *end = p;
  while (*end && *end != '/' && *end != '?' && *end != '#' && *end != ':')
    end++;
  size_t n = (size_t)(end - p);
  if (n == 0 || n >= host_sz)
    return -1;
  memcpy(host, p, n);
  host[n] = '\0';
  return 0;
}

static int host_blocked(const char *host) {
  if (!host || !host[0])
    return 1;
  if (!strcasecmp(host, "localhost") || !strcmp(host, "127.0.0.1") ||
      !strcmp(host, "::1"))
    return 1;
  struct in_addr v4;
  if (inet_pton(AF_INET, host, &v4) == 1) {
    unsigned char *b = (unsigned char *)&v4.s_addr;
    if (b[0] == 10 || b[0] == 127)
      return 1;
    if (b[0] == 192 && b[1] == 168)
      return 1;
    if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
      return 1;
  }
  return 0;
}

char *favicon_proxy_url_for_host(const char *host) {
  if (!host || !host[0])
    return strdup("/static/icon-placeholder.svg");
  CURL *c = curl_easy_init();
  if (!c)
    return strdup("/static/icon-placeholder.svg");
  char *enc = curl_easy_escape(c, host, 0);
  curl_easy_cleanup(c);
  if (!enc)
    return strdup("/static/icon-placeholder.svg");
  size_t n = strlen(enc) + 24;
  char *out = malloc(n);
  if (out)
    snprintf(out, n, "/favicon?host=%s", enc);
  curl_free(enc);
  return out ? out : strdup("/static/icon-placeholder.svg");
}

char *favicon_proxy_url_for_page(const char *page_url) {
  char host[256];
  if (extract_host(page_url, host, sizeof(host)) != 0)
    return strdup("/static/icon-placeholder.svg");
  return favicon_proxy_url_for_host(host);
}

char *favicon_url_for_engine_id(const char *engine_id) {
  static const struct {
    const char *id;
    const char *host;
  } hosts[] = {
      {"ddg", "duckduckgo.com"},
      {"brave", "brave.com"},
      {"yahoo", "yahoo.com"},
      {"startpage", "startpage.com"},
      {"wikipedia", "wikipedia.org"},
      {"wiktionary", "wiktionary.org"},
      {"archwiki", "archlinux.org"},
      {"debianwiki", "debian.org"},
      {"nixos", "nixos.org"},
      {"gentoo", "gentoo.org"},
      {"mediawiki", "mediawiki.org"},
      {"mwmbl", "mwmbl.org"},
      {"hn", "news.ycombinator.com"},
      {"reddit", "reddit.com"},
      {"lobsters", "lobste.rs"},
      {"qwant", "qwant.com"},
      {"mojeek", "mojeek.com"},
      {"wiby", "wiby.me"},
      {"yacy", "yacy.net"},
      {"wikibooks", "wikibooks.org"},
      {"wikiquote", "wikiquote.org"},
      {"lemmy", "lemmy.world"},
      {"stackoverflow", "stackoverflow.com"},
  };
  size_t i;

  if (!engine_id || !engine_id[0])
    return strdup("/static/icon-placeholder.svg");
  if (strcmp(engine_id, "lyra") == 0)
    return strdup("/static/engines/lyra.svg");
  for (i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
    if (strcmp(engine_id, hosts[i].id) == 0)
      return favicon_proxy_url_for_host(hosts[i].host);
  }
  return strdup("/static/icon-placeholder.svg");
}

static int mkdir_p(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0)
    return S_ISDIR(st.st_mode) ? 0 : -1;
  return mkdir(path, 0755);
}

static void fav_paths(const char *hash, char *host_path, size_t hsz,
                      char *blob_dir, size_t bsz) {
  const char *root = cache_dir_path();
  snprintf(blob_dir, bsz, "%s/fav", root && root[0] ? root : "/tmp/seek_cache");
  mkdir_p(blob_dir);
  char shard_path[768];
  snprintf(shard_path, sizeof(shard_path), "%s/%.2s", blob_dir, hash);
  mkdir_p(shard_path);
  snprintf(host_path, hsz, "%s/%s.map", shard_path, hash);
}

static int read_file(const char *path, char **data, size_t *n) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  rewind(f);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[got] = '\0';
  *data = buf;
  *n = got;
  return 0;
}

static int write_file(const char *path, const char *data, size_t n) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  size_t w = fwrite(data, 1, n, f);
  fclose(f);
  return w == n ? 0 : -1;
}

static int fetch_url_limit(const char *url, MemBuf *buf, size_t cap, int allow_html) {
  CURL *curl = curl_easy_init();
  if (!curl)
    return -1;
  buf->data = malloc(4096);
  buf->size = 0;
  buf->capacity = buf->data ? 4096 : 0;
  if (!buf->data) {
    curl_easy_cleanup(curl);
    return -1;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fav_write);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT,
                   "Seek/1.0 (https://seek.lyrabrowser.com)");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#endif
  apply_proxy_settings(curl);
  CURLcode res = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK || code != 200 || buf->size < 8)
    return -1;
  if (buf->size > cap)
    buf->size = cap;
  if (!allow_html && buf->data[0] == '{')
    return -1;
  if (!allow_html && buf->data[0] == '<') {
    int svg = 0;
    if (buf->size >= 4 && strncasecmp(buf->data, "<svg", 4) == 0)
      svg = 1;
    else if (buf->size >= 5 && strncmp(buf->data, "<?xml", 5) == 0 &&
             (strcasestr(buf->data, "<svg") != NULL))
      svg = 1;
    if (!svg)
      return -1;
  }
  return 0;
}

static int fetch_url(const char *url, MemBuf *buf) {
  return fetch_url_limit(url, buf, FAVICON_MAX, 0);
}

static int attr_ci_has(const char *attrs, const char *key, const char *want) {
  const char *p = attrs;
  size_t klen = strlen(key);
  while (*p) {
    while (*p && *p != key[0] && *p != (char)toupper((unsigned char)key[0]))
      p++;
    if (!*p)
      return 0;
    if (strncasecmp(p, key, klen) == 0) {
      const char *q = p + klen;
      while (*q == ' ' || *q == '\t')
        q++;
      if (*q == '=') {
        q++;
        while (*q == ' ' || *q == '\t')
          q++;
        char quote = 0;
        if (*q == '"' || *q == '\'') {
          quote = *q;
          q++;
        }
        char val[256];
        size_t i = 0;
        while (*q && i + 1 < sizeof(val) &&
               ((quote && *q != quote) || (!quote && *q != ' ' && *q != '>'))) {
          val[i++] = *q++;
        }
        val[i] = '\0';
        if (strcasestr(val, want))
          return 1;
      }
    }
    p++;
  }
  return 0;
}

static int attr_href(const char *attrs, char *out, size_t out_sz) {
  const char *p = strcasestr(attrs, "href");
  if (!p)
    return -1;
  p += 4;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != '=')
    return -1;
  p++;
  while (*p == ' ' || *p == '\t')
    p++;
  char quote = 0;
  if (*p == '"' || *p == '\'') {
    quote = *p;
    p++;
  }
  size_t i = 0;
  while (*p && i + 1 < out_sz &&
         ((quote && *p != quote) || (!quote && *p != ' ' && *p != '>'))) {
    out[i++] = *p++;
  }
  out[i] = '\0';
  return i > 0 ? 0 : -1;
}

int favicon_href_from_html(const char *html, const char *host, char *out,
                           size_t out_sz) {
  if (!html || !host || !out || out_sz < 16)
    return -1;
  const char *p = html;
  char href[512];
  href[0] = '\0';
  while ((p = strcasestr(p, "<link"))) {
    const char *end = strchr(p, '>');
    if (!end)
      break;
    size_t alen = (size_t)(end - p);
    if (alen > 1023)
      alen = 1023;
    char attrs[1024];
    memcpy(attrs, p, alen);
    attrs[alen] = '\0';
    p = end + 1;
    if (!attr_ci_has(attrs, "rel", "icon"))
      continue;
    if (attr_href(attrs, href, sizeof(href)) == 0)
      break;
    href[0] = '\0';
  }
  if (!href[0])
    return -1;
  if (!strncmp(href, "data:", 5))
    return -1;
  if (!strncmp(href, "//", 2)) {
    snprintf(out, out_sz, "https:%s", href);
    return 0;
  }
  if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8)) {
    snprintf(out, out_sz, "%s", href);
    return 0;
  }
  if (href[0] == '/') {
    snprintf(out, out_sz, "https://%s%s", host, href);
    return 0;
  }
  snprintf(out, out_sz, "https://%s/%s", host, href);
  return 0;
}

static int fetch_favicon_for_host(const char *host, MemBuf *buf) {
  char url[768];
  const char *paths[] = {"/favicon.ico", "/favicon.png", "/favicon.svg",
                         "/apple-touch-icon.png",
                         "/apple-touch-icon-precomposed.png", NULL};
  for (int i = 0; paths[i]; i++) {
    snprintf(url, sizeof(url), "https://%s%s", host, paths[i]);
    if (fetch_url(url, buf) == 0)
      return 0;
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
  }

  snprintf(url, sizeof(url), "https://%s/", host);
  MemBuf html = {0};
  if (fetch_url_limit(url, &html, FAVICON_HTML_MAX, 1) == 0 && html.data) {
    if (html.size >= html.capacity) {
      char *grow = realloc(html.data, html.size + 1);
      if (!grow) {
        free(html.data);
        html.data = NULL;
      } else {
        html.data = grow;
        html.capacity = html.size + 1;
      }
    }
    if (html.data)
      html.data[html.size] = '\0';
    char found[768];
    if (favicon_href_from_html(html.data, host, found, sizeof(found)) == 0) {
      free(html.data);
      if (fetch_url(found, buf) == 0)
        return 0;
      free(buf->data);
      memset(buf, 0, sizeof(*buf));
    } else {
      free(html.data);
    }
  } else {
    free(html.data);
  }

  snprintf(url, sizeof(url), "http://%s/favicon.ico", host);
  if (fetch_url(url, buf) == 0)
    return 0;
  free(buf->data);
  memset(buf, 0, sizeof(*buf));

  char parent[256];
  if (favicon_parent_host(host, parent, sizeof(parent)) == 0 &&
      strcmp(parent, host) != 0) {
    snprintf(url, sizeof(url), "https://%s/favicon.ico", parent);
    if (fetch_url(url, buf) == 0)
      return 0;
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
    snprintf(url, sizeof(url), "https://www.%s/favicon.ico", parent);
    if (fetch_url(url, buf) == 0)
      return 0;
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
  }
  return -1;
}

static const char *mime_from_bytes(const char *d, size_t n) {
  if (!d || n < 3)
    return "image/x-icon";
  if ((n >= 4 && strncasecmp(d, "<svg", 4) == 0) ||
      (n >= 5 && strncmp(d, "<?xml", 5) == 0))
    return "image/svg+xml";
  if (n >= 8 && (unsigned char)d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' &&
      d[3] == 'G')
    return "image/png";
  if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F')
    return "image/gif";
  if ((unsigned char)d[0] == 0xff && (unsigned char)d[1] == 0xd8)
    return "image/jpeg";
  if (n >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' &&
      d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P')
    return "image/webp";
  return "image/x-icon";
}

static void serve_empty(void) {
  serve_data((char *)PLACEHOLDER_SVG, sizeof(PLACEHOLDER_SVG) - 1,
             "image/svg+xml");
}

static int favicon_cached_fresh(const char *host) {
  char hash[65];
  if (favicon_host_hash(host, hash, sizeof(hash)) != 0)
    return 0;
  char host_path[1024];
  char blob_dir[512];
  fav_paths(hash, host_path, sizeof(host_path), blob_dir, sizeof(blob_dir));
  struct stat st;
  if (stat(host_path, &st) != 0)
    return 0;
  return time(NULL) - st.st_mtime < FAVICON_TTL;
}

static int store_favicon_bytes(const char *host, const char *data, size_t n) {
  if (!host || !data || n < 8)
    return -1;
  if (data[0] == '<' && strncasecmp(data, "<svg", 4) != 0 &&
      strncmp(data, "<?xml", 5) != 0)
    return -1;
  char host_hash[65];
  char content_hash[65];
  if (favicon_host_hash(host, host_hash, sizeof(host_hash)) != 0)
    return -1;
  if (sha256_hex((const unsigned char *)data, n, content_hash,
                 sizeof(content_hash)) != 0)
    return -1;
  char host_path[1024];
  char blob_dir[512];
  fav_paths(host_hash, host_path, sizeof(host_path), blob_dir, sizeof(blob_dir));
  char blob[768];
  snprintf(blob, sizeof(blob), "%s/%.2s/%s.bin", blob_dir, content_hash,
           content_hash);
  char shard[768];
  snprintf(shard, sizeof(shard), "%s/%.2s", blob_dir, content_hash);
  mkdir_p(shard);
  if (access(blob, F_OK) != 0)
    write_file(blob, data, n);
  write_file(host_path, content_hash, 64);
  return 0;
}

void favicon_prime_pages(const char **page_urls, int n) {
  if (!page_urls || n <= 0)
    return;
  if (n > 64)
    n = 64;
  char hosts[64][256];
  char urls[64][320];
  int hn = 0;
  for (int i = 0; i < n; i++) {
    char host[256];
    if (extract_host(page_urls[i], host, sizeof(host)) != 0)
      continue;
    if (host_blocked(host))
      continue;
    if (favicon_cached_fresh(host))
      continue;
    int dup = 0;
    for (int j = 0; j < hn; j++) {
      if (strcasecmp(hosts[j], host) == 0) {
        dup = 1;
        break;
      }
    }
    if (dup)
      continue;
    snprintf(hosts[hn], sizeof(hosts[hn]), "%s", host);
    snprintf(urls[hn], sizeof(urls[hn]), "https://%s/favicon.ico", host);
    hn++;
  }
  if (hn == 0)
    return;

  HttpGetJob *jobs = calloc((size_t)hn, sizeof(HttpGetJob));
  if (!jobs)
    return;
  for (int i = 0; i < hn; i++) {
    jobs[i].url = urls[i];
    jobs[i].user_agent = SEEK_HTTP_UA;
  }
  http_get_many(jobs, hn, 1);
  for (int i = 0; i < hn; i++) {
    if (jobs[i].resp.memory && jobs[i].resp.size >= 8)
      store_favicon_bytes(hosts[i], jobs[i].resp.memory, jobs[i].resp.size);
    http_response_free(&jobs[i].resp);
  }
  free(jobs);
}

int favicon_handler(UrlParams *params) {
  const char *hash = NULL;
  const char *host_q = NULL;
  for (int i = 0; i < params->count; i++) {
    if (!strcmp(params->params[i].key, "h"))
      hash = params->params[i].value;
    else if (!strcmp(params->params[i].key, "host"))
      host_q = params->params[i].value;
  }

  char computed[65];
  if ((!hash || strlen(hash) != 64) && host_q && host_q[0]) {
    if (host_blocked(host_q) || favicon_host_hash(host_q, computed, sizeof(computed)) != 0) {
      serve_empty();
      return 0;
    }
    hash = computed;
  }
  if (!hash || strlen(hash) != 64) {
    serve_empty();
    return 0;
  }
  for (int i = 0; i < 64; i++) {
    char c = hash[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      serve_empty();
      return 0;
    }
  }

  char host_path[1024];
  char blob_dir[512];
  fav_paths(hash, host_path, sizeof(host_path), blob_dir, sizeof(blob_dir));

  char *map = NULL;
  size_t mapn = 0;
  if (read_file(host_path, &map, &mapn) == 0 && mapn >= 64) {
    struct stat st;
    if (stat(host_path, &st) == 0 && time(NULL) - st.st_mtime < FAVICON_TTL) {
      if (memcmp(map, MISS_MAP, 64) == 0) {
        free(map);
        serve_empty();
        return 0;
      }
      char blob[640];
      char bhash[65];
      memcpy(bhash, map, 64);
      bhash[64] = '\0';
      snprintf(blob, sizeof(blob), "%s/%.2s/%s.bin", blob_dir, bhash, bhash);
      char *data = NULL;
      size_t n = 0;
      if (read_file(blob, &data, &n) == 0) {
        serve_data(data, n, mime_from_bytes(data, n));
        free(data);
        free(map);
        return 0;
      }
      free(data);
    }
  }
  free(map);

  if (!host_q || host_blocked(host_q)) {
    serve_empty();
    return 0;
  }

  MemBuf buf = {0};
  if (fetch_favicon_for_host(host_q, &buf) != 0) {
    free(buf.data);
    write_file(host_path, MISS_MAP, 64);
    serve_empty();
    return 0;
  }

  char content_hash[65];
  if (sha256_hex((unsigned char *)buf.data, buf.size, content_hash,
                 sizeof(content_hash)) != 0) {
    free(buf.data);
    serve_empty();
    return 0;
  }
  char blob[768];
  snprintf(blob, sizeof(blob), "%s/%.2s/%s.bin", blob_dir, content_hash,
           content_hash);
  char shard[768];
  snprintf(shard, sizeof(shard), "%s/%.2s", blob_dir, content_hash);
  mkdir_p(shard);
  if (access(blob, F_OK) != 0)
    write_file(blob, buf.data, buf.size);
  write_file(host_path, content_hash, 64);
  serve_data(buf.data, buf.size, mime_from_bytes(buf.data, buf.size));
  free(buf.data);
  return 0;
}
