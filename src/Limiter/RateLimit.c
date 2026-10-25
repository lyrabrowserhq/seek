#include "RateLimit.h"
#include "../Config.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

typedef struct RateLimitEntry {
  char client_key[64];
  char scope[32];
  time_t window_start;
  time_t last_seen;
  int count;
  int strikes;
  struct RateLimitEntry *next;
} RateLimitEntry;

extern __thread int current_client_socket;
extern __thread char current_request_buffer[];

static pthread_mutex_t rate_limit_mutex = PTHREAD_MUTEX_INITIALIZER;
static RateLimitEntry *rate_limit_entries = NULL;
static pthread_once_t salt_once = PTHREAD_ONCE_INIT;
static unsigned char abuse_salt[32];
static int local_warned;

static int is_blank_char(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *str_case_str(const char *haystack, const char *needle) {
  size_t nlen = strlen(needle);
  for (; *haystack; haystack++) {
    if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
      size_t i;
      for (i = 1; i < nlen; i++) {
        if (tolower((unsigned char)haystack[i]) !=
            tolower((unsigned char)needle[i]))
          break;
      }
      if (i == nlen)
        return haystack;
    }
  }
  return NULL;
}

static void trim_copy(char *dest, size_t dest_size, const char *src,
                      size_t src_len) {
  while (src_len > 0 && is_blank_char(*src)) {
    src++;
    src_len--;
  }

  while (src_len > 0 && is_blank_char(src[src_len - 1])) {
    src_len--;
  }

  if (dest_size == 0)
    return;

  if (src_len >= dest_size)
    src_len = dest_size - 1;

  memcpy(dest, src, src_len);
  dest[src_len] = '\0';
}

static void init_salt(void) {
  const char *env = getenv("SEEK_ABUSE_SALT");
  if (env && env[0]) {
    memset(abuse_salt, 0, sizeof(abuse_salt));
    size_t n = strlen(env);
    if (n > sizeof(abuse_salt))
      n = sizeof(abuse_salt);
    memcpy(abuse_salt, env, n);
    return;
  }
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) {
    size_t got = fread(abuse_salt, 1, sizeof(abuse_salt), f);
    fclose(f);
    if (got == sizeof(abuse_salt))
      return;
  }
  memset(abuse_salt, 0x5a, sizeof(abuse_salt));
}

static int ipv4_local(const struct in_addr *a) {
  unsigned long ip = ntohl(a->s_addr);
  if ((ip >> 24) == 127)
    return 1;
  if ((ip >> 24) == 10)
    return 1;
  if ((ip >> 16) == 0xa9fe)
    return 1;
  if ((ip >> 16) == 0xc0a8)
    return 1;
  if ((ip >> 20) == 0xac1)
    return 1;
  if (ip == 0)
    return 1;
  return 0;
}

int rate_limit_ip_is_local(const char *ip) {
  if (!ip || !ip[0])
    return 0;
  if (strncmp(ip, "unix:", 5) == 0)
    return 1;

  struct in_addr v4;
  if (inet_pton(AF_INET, ip, &v4) == 1)
    return ipv4_local(&v4);

  struct in6_addr v6;
  if (inet_pton(AF_INET6, ip, &v6) != 1)
    return 0;

  if (IN6_IS_ADDR_LOOPBACK(&v6) || IN6_IS_ADDR_UNSPECIFIED(&v6) ||
      IN6_IS_ADDR_LINKLOCAL(&v6) || IN6_IS_ADDR_SITELOCAL(&v6))
    return 1;
  if ((v6.s6_addr[0] & 0xfe) == 0xfc)
    return 1;
  if (IN6_IS_ADDR_V4MAPPED(&v6)) {
    struct in_addr mapped;
    memcpy(&mapped, &v6.s6_addr[12], 4);
    return ipv4_local(&mapped);
  }
  return 0;
}

static void hash_ip(const char *ip, char *out, size_t out_sz) {
  pthread_once(&salt_once, init_salt);
  if (!out || out_sz < 18)
    return;
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hlen = 0;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx) {
    snprintf(out, out_sz, "h0");
    return;
  }
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
      EVP_DigestUpdate(ctx, abuse_salt, sizeof(abuse_salt)) != 1 ||
      EVP_DigestUpdate(ctx, ip, strlen(ip)) != 1 ||
      EVP_DigestFinal_ex(ctx, hash, &hlen) != 1) {
    EVP_MD_CTX_free(ctx);
    snprintf(out, out_sz, "h0");
    return;
  }
  EVP_MD_CTX_free(ctx);
  out[0] = 'h';
  size_t n = 16;
  if (n * 2 + 2 > out_sz)
    n = (out_sz - 2) / 2;
  for (size_t i = 0; i < n && i < hlen; i++)
    sprintf(out + 1 + (i * 2), "%02x", hash[i]);
}

/* take the hop `hops` entries from the right end of X-Forwarded-For; the
   rightmost was appended by the nearest proxy. only call this when the
   socket peer is a trusted local/private proxy. */
static void parse_xff(char *out, size_t out_sz, int hops) {
  const char *header = str_case_str(current_request_buffer, "x-forwarded-for:");
  if (!header)
    return;
  header += strlen("X-Forwarded-For:");
  const char *line_end = strpbrk(header, "\r\n");
  size_t line_len = line_end ? (size_t)(line_end - header) : strlen(header);

  if (hops < 1)
    hops = 1;
  char entries[32][64];
  int n = 0;
  const char *p = header;
  const char *end = header + line_len;
  while (p < end && n < 32) {
    const char *comma = memchr(p, ',', (size_t)(end - p));
    size_t value_len = comma ? (size_t)(comma - p) : (size_t)(end - p);
    trim_copy(entries[n], sizeof(entries[n]), p, value_len);
    if (entries[n][0])
      n++;
    if (!comma)
      break;
    p = comma + 1;
  }
  if (n >= hops)
    snprintf(out, out_sz, "%s", entries[n - hops]);
}

static void get_client_key_from_socket(char *client_key,
                                       size_t client_key_size) {
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  if (getpeername(current_client_socket, (struct sockaddr *)&addr, &addr_len) !=
      0) {
    return;
  }

  if (addr.ss_family == AF_INET) {
    struct sockaddr_in *ipv4 = (struct sockaddr_in *)&addr;
    inet_ntop(AF_INET, &ipv4->sin_addr, client_key, client_key_size);
  } else if (addr.ss_family == AF_INET6) {
    struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)&addr;
    inet_ntop(AF_INET6, &ipv6->sin6_addr, client_key, client_key_size);
  } else if (addr.ss_family == AF_UNIX) {
    snprintf(client_key, client_key_size, "unix:%d", current_client_socket);
  }
}

static void warn_local_once(const char *raw) {
  if (local_warned)
    return;
  local_warned = 1;
  fprintf(stderr,
          "rate limit: address looks local or private (%s). reverse proxy is "
          "likely not forwarding the public client address.\n",
          raw && raw[0] ? raw : "unknown");
}

void rate_limit_get_client_key(char *client_key, size_t client_key_size) {
  if (!client_key || client_key_size == 0)
    return;

  client_key[0] = '\0';
  char raw[64];
  raw[0] = '\0';
  char peer[64];
  peer[0] = '\0';
  get_client_key_from_socket(peer, sizeof(peer));
  /* a client can spoof X-Forwarded-For, so only honor it when the socket
     peer is a trusted local proxy */
  if (rate_limit_ip_is_local(peer))
    parse_xff(raw, sizeof(raw), g_trusted_proxy_hops);
  if (raw[0] == '\0')
    snprintf(raw, sizeof(raw), "%s", peer);
  if (raw[0] == '\0')
    snprintf(raw, sizeof(raw), "nun");

  if (rate_limit_ip_is_local(raw)) {
    warn_local_once(raw);
    snprintf(client_key, client_key_size, "local");
    return;
  }
  hash_ip(raw, client_key, client_key_size);
}

#define RATE_LIMIT_MAX_ENTRIES 20000

static int entry_count;
static time_t last_prune;

static void prune_stale_entries(time_t now) {
  if (now == last_prune)
    return;
  last_prune = now;
  RateLimitEntry **cursor = &rate_limit_entries;

  while (*cursor) {
    RateLimitEntry *entry = *cursor;
    if (now - entry->last_seen > 9999) {
      *cursor = entry->next;
      free(entry);
      entry_count--;
      continue;
    }
    cursor = &entry->next;
  }
}

static RateLimitEntry *find_entry(const char *client_key, const char *scope) {
  for (RateLimitEntry *entry = rate_limit_entries; entry; entry = entry->next) {
    if (strcmp(entry->client_key, client_key) == 0 &&
        strcmp(entry->scope, scope) == 0) {
      return entry;
    }
  }
  return NULL;
}

static RateLimitEntry *create_entry(const char *client_key, const char *scope,
                                    time_t now) {
  RateLimitEntry *entry = (RateLimitEntry *)calloc(1, sizeof(RateLimitEntry));
  if (!entry)
    return NULL;

  snprintf(entry->client_key, sizeof(entry->client_key), "%s", client_key);
  snprintf(entry->scope, sizeof(entry->scope), "%s", scope);
  entry->window_start = now;
  entry->last_seen = now;
  entry->next = rate_limit_entries;
  rate_limit_entries = entry;
  entry_count++;
  return entry;
}

static RateLimitResult check_hashed(const char *scope,
                                    const RateLimitConfig *config,
                                    const char *hashed) {
  RateLimitResult result = {.limited = 0, .retry_after_seconds = 0};
  time_t now = time(NULL);

  pthread_mutex_lock(&rate_limit_mutex);
  prune_stale_entries(now);

  RateLimitEntry *entry = find_entry(hashed, scope);
  if (!entry) {
    if (entry_count >= RATE_LIMIT_MAX_ENTRIES) {
      result.limited = 1;
      result.retry_after_seconds = config->interval_seconds;
      pthread_mutex_unlock(&rate_limit_mutex);
      return result;
    }
    entry = create_entry(hashed, scope, now);
    if (!entry) {
      pthread_mutex_unlock(&rate_limit_mutex);
      return result;
    }
  }

  entry->last_seen = now;

  if (now - entry->window_start >= config->interval_seconds) {
    entry->window_start = now;
    entry->count = 0;
  }

  if (entry->count >= config->max_requests) {
    entry->strikes++;
    result.limited = 1;
    int wait = config->interval_seconds - (int)(now - entry->window_start);
    if (wait < 1)
      wait = 1;
    if (entry->strikes > 1)
      wait *= entry->strikes > 8 ? 8 : entry->strikes;
    result.retry_after_seconds = wait;
    pthread_mutex_unlock(&rate_limit_mutex);
    return result;
  }

  entry->count++;
  pthread_mutex_unlock(&rate_limit_mutex);
  return result;
}

RateLimitResult rate_limit_check_ip(const char *scope,
                                    const RateLimitConfig *config,
                                    const char *raw_ip) {
  RateLimitResult result = {.limited = 0, .retry_after_seconds = 0};
  if (!scope || !config || config->max_requests <= 0 ||
      config->interval_seconds <= 0)
    return result;
  if (rate_limit_ip_is_local(raw_ip))
    return result;
  char hashed[64];
  hash_ip(raw_ip ? raw_ip : "nun", hashed, sizeof(hashed));
  return check_hashed(scope, config, hashed);
}

RateLimitResult rate_limit_check(const char *scope,
                                 const RateLimitConfig *config) {
  RateLimitResult result = {.limited = 0, .retry_after_seconds = 0};

  if (!scope || !config || config->max_requests <= 0 ||
      config->interval_seconds <= 0) {
    return result;
  }

  char client_key[64];
  rate_limit_get_client_key(client_key, sizeof(client_key));
  if (strcmp(client_key, "local") == 0)
    return result;
  return check_hashed(scope, config, client_key);
}
