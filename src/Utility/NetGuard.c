#include "NetGuard.h"
#include "../Proxy/Proxy.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int v4_blocked(uint32_t addr_be) {
  const uint8_t *b = (const uint8_t *)&addr_be;
  if (b[0] == 0)
    return 1;
  if (b[0] == 10)
    return 1;
  if (b[0] == 100 && b[1] >= 64 && b[1] <= 127)
    return 1;
  if (b[0] == 127)
    return 1;
  if (b[0] == 169 && b[1] == 254)
    return 1;
  if (b[0] == 172 && b[1] >= 16 && b[1] <= 31)
    return 1;
  if (b[0] == 192 && b[1] == 0 && b[2] == 0)
    return 1;
  if (b[0] == 192 && b[1] == 0 && b[2] == 2)
    return 1;
  if (b[0] == 192 && b[1] == 168)
    return 1;
  if (b[0] == 198 && (b[1] == 18 || b[1] == 19))
    return 1;
  if (b[0] == 198 && b[1] == 51 && b[2] == 100)
    return 1;
  if (b[0] == 203 && b[1] == 0 && b[2] == 113)
    return 1;
  if (b[0] >= 224)
    return 1;
  return 0;
}

static int v6_blocked(const struct in6_addr *a) {
  const uint8_t *s = a->s6_addr;
  int all_zero = 1;
  for (int i = 0; i < 15; i++) {
    if (s[i] != 0) {
      all_zero = 0;
      break;
    }
  }
  if (all_zero)
    return 1; /* :: and ::1 */
  if (s[0] == 0xfe && (s[1] & 0xc0) == 0x80)
    return 1; /* fe80::/10 */
  if (s[0] == 0xfe && (s[1] & 0xc0) == 0xc0)
    return 1; /* fec0::/10 */
  if ((s[0] & 0xfe) == 0xfc)
    return 1; /* fc00::/7 */
  if (s[0] == 0xff)
    return 1; /* ff00::/8 */
  if (s[0] == 0x20 && s[1] == 0x01 && s[2] == 0x0d && s[3] == 0xb8)
    return 1; /* 2001:db8::/32 */
  if (s[0] == 0x20 && s[1] == 0x02)
    return 1; /* 2002::/16 6to4 */
  if (s[0] == 0x20 && s[1] == 0x01 && s[2] == 0x00 && s[3] == 0x00)
    return 1; /* 2001::/32 teredo */
  if (s[0] == 0x00 && s[1] == 0x64 && s[2] == 0xff && s[3] == 0x9b)
    return 1; /* 64:ff9b::/96 NAT64 */
  int head_zero = 1;
  for (int i = 0; i < 10; i++) {
    if (s[i] != 0) {
      head_zero = 0;
      break;
    }
  }
  if (head_zero) {
    /* v4-mapped ::ffff:a.b.c.d and v4-compatible ::a.b.c.d */
    uint32_t v4;
    memcpy(&v4, s + 12, sizeof(v4));
    return v4_blocked(v4);
  }
  return 0;
}

int netguard_addr_blocked(const struct sockaddr *sa) {
  if (!sa)
    return 1;
  if (sa->sa_family == AF_INET) {
    const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
    return v4_blocked(in->sin_addr.s_addr);
  }
  if (sa->sa_family == AF_INET6) {
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
    return v6_blocked(&in6->sin6_addr);
  }
  return 1;
}

static int parse_numeric_addr(const char *host, struct sockaddr_storage *out) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST;
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, NULL, &hints, &res) == 0 && res &&
      res->ai_addrlen <= sizeof(*out)) {
    memcpy(out, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    return 0;
  }
  if (res)
    freeaddrinfo(res);
  /* non canonical numeric forms like 2130706433 or 0x7f.1 only parse
     through inet_aton */
  struct in_addr v4;
  if (inet_aton(host, &v4)) {
    struct sockaddr_in *in = (struct sockaddr_in *)out;
    memset(in, 0, sizeof(*in));
    in->sin_family = AF_INET;
    in->sin_addr = v4;
    return 0;
  }
  return -1;
}

static int name_has_blocked_suffix(const char *host) {
  static const char *suffixes[] = {
      ".localhost", ".local", ".internal", ".lan", ".home.arpa", NULL};
  size_t hl = strlen(host);
  for (int i = 0; suffixes[i]; i++) {
    size_t sl = strlen(suffixes[i]);
    if (hl > sl && strcasecmp(host + hl - sl, suffixes[i]) == 0)
      return 1;
  }
  return 0;
}

int netguard_host_is_public_literal_or_name(const char *host) {
  if (!host || !host[0] || strlen(host) > 253)
    return 0;

  char h[256];
  if (host[0] == '[') {
    size_t n = strlen(host);
    if (n < 3 || host[n - 1] != ']' || n - 2 >= sizeof(h))
      return 0;
    memcpy(h, host + 1, n - 2);
    h[n - 2] = '\0';
    host = h;
  }

  for (const char *p = host; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (!(isalnum(c) || c == '.' || c == '-' || c == ':' || c == '_'))
      return 0;
  }

  struct sockaddr_storage ss;
  if (parse_numeric_addr(host, &ss) == 0)
    return !netguard_addr_blocked((const struct sockaddr *)&ss);

  if (strchr(host, ':'))
    return 0; /* a colon outside a valid v6 literal is a port or worse */
  if (strcasecmp(host, "localhost") == 0)
    return 0;
  if (name_has_blocked_suffix(host))
    return 0;
  if (!strchr(host, '.'))
    return 0; /* single label names resolve inside the local net */
  return 1;
}

int netguard_url_host(const char *url, char *host, size_t host_sz) {
  if (!url || !host || host_sz == 0)
    return -1;
  CURLU *u = curl_url();
  if (!u)
    return -1;
  int rc = -1;
  char *h = NULL;
  if (curl_url_set(u, CURLUPART_URL, url, 0) == CURLUE_OK &&
      curl_url_get(u, CURLUPART_HOST, &h, 0) == CURLUE_OK && h) {
    size_t n = strlen(h);
    if (n > 0 && n < host_sz) {
      memcpy(host, h, n + 1);
      rc = 0;
    }
  }
  curl_free(h);
  curl_url_cleanup(u);
  return rc;
}

static int proxy_configured(void) {
  return proxy_url[0] != '\0' || proxy_count > 0;
}

static int noproxy_match(const char *host) {
  if (!proxy_noproxy[0])
    return 0;
  char list[256];
  snprintf(list, sizeof(list), "%s", proxy_noproxy);
  size_t hl = strlen(host);
  for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
    while (*tok == ' ' || *tok == '\t')
      tok++;
    if (tok[0] == '.')
      tok++;
    if (!tok[0])
      continue;
    if (strcmp(tok, "*") == 0)
      return 1;
    size_t tl = strlen(tok);
    if (hl == tl && strcasecmp(host, tok) == 0)
      return 1;
    if (hl > tl + 1 && host[hl - tl - 1] == '.' &&
        strcasecmp(host + hl - tl, tok) == 0)
      return 1;
  }
  return 0;
}

/* resolve the host and pin the vetted addresses so a DNS rebinding cannot
   swap a public name for a private address between the check and connect */
static CURLcode vet_and_pin(const char *host, const char *port,
                            struct curl_slist **out) {
  *out = NULL;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
    return CURLE_COULDNT_RESOLVE_HOST;

  char addr_list[1024];
  addr_list[0] = '\0';
  size_t used = 0;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    if (netguard_addr_blocked(ai->ai_addr)) {
      freeaddrinfo(res);
      return CURLE_REMOTE_ACCESS_DENIED;
    }
    char abuf[INET6_ADDRSTRLEN];
    const void *addr = NULL;
    if (ai->ai_family == AF_INET)
      addr = &((struct sockaddr_in *)ai->ai_addr)->sin_addr;
    else if (ai->ai_family == AF_INET6)
      addr = &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr;
    else
      continue;
    if (!inet_ntop(ai->ai_family, addr, abuf, sizeof(abuf)))
      continue;
    size_t al = strlen(abuf);
    if (used + al + 2 >= sizeof(addr_list))
      break;
    addr_list[used++] = ',';
    memcpy(addr_list + used, abuf, al);
    used += al;
    addr_list[used] = '\0';
  }
  freeaddrinfo(res);
  if (used == 0)
    return CURLE_COULDNT_RESOLVE_HOST;

  char entry[1400];
  int n = snprintf(entry, sizeof(entry), "%s:%s:%s", host, port, addr_list);
  if (n < 0 || (size_t)n >= sizeof(entry))
    return CURLE_REMOTE_ACCESS_DENIED;
  *out = curl_slist_append(NULL, entry);
  if (!*out)
    return CURLE_OUT_OF_MEMORY;
  return CURLE_OK;
}

CURLcode netguard_perform(CURL *curl, const char *url, int max_redirs,
                          void (*reset)(void *), void *reset_arg) {
  char next[4096];
  snprintf(next, sizeof(next), "%s", url ? url : "");

  int proxied = proxy_configured();

  for (int hop = 0; hop <= max_redirs; hop++) {
    CURLU *u = curl_url();
    if (!u)
      return CURLE_FAILED_INIT;
    CURLcode rc = CURLE_OK;
    struct curl_slist *pinned = NULL;
    char *scheme = NULL, *host = NULL, *port_s = NULL, *user = NULL,
         *pass = NULL;
    do {
      if (curl_url_set(u, CURLUPART_URL, next, 0) != CURLUE_OK) {
        rc = CURLE_URL_MALFORMAT;
        break;
      }
      curl_url_get(u, CURLUPART_SCHEME, &scheme, 0);
      curl_url_get(u, CURLUPART_HOST, &host, 0);
      curl_url_get(u, CURLUPART_PORT, &port_s, CURLU_DEFAULT_PORT);
      curl_url_get(u, CURLUPART_USER, &user, 0);
      curl_url_get(u, CURLUPART_PASSWORD, &pass, 0);
      if (!scheme ||
          (strcasecmp(scheme, "http") && strcasecmp(scheme, "https")) ||
          !host || !host[0] || (user && user[0]) || (pass && pass[0]) ||
          !port_s || !port_s[0]) {
        rc = CURLE_URL_MALFORMAT;
        break;
      }
      if (!netguard_host_is_public_literal_or_name(host)) {
        rc = CURLE_REMOTE_ACCESS_DENIED;
        break;
      }
      /* a proxied hop leaves resolution to the proxy - a local lookup here
         would leak queries the SOCKS proxy is meant to hide */
      if (!proxied || noproxy_match(host))
        rc = vet_and_pin(host, port_s, &pinned);
    } while (0);

    curl_free(scheme);
    curl_free(host);
    curl_free(port_s);
    curl_free(user);
    curl_free(pass);
    curl_url_cleanup(u);
    if (rc != CURLE_OK) {
      curl_slist_free_all(pinned);
      return rc;
    }

    if (reset)
      reset(reset_arg);
    curl_easy_setopt(curl, CURLOPT_URL, next);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_RESOLVE, pinned);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(pinned);
    curl_easy_setopt(curl, CURLOPT_RESOLVE, NULL);
    if (res != CURLE_OK)
      return res;

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (code == 301 || code == 302 || code == 303 || code == 307 ||
        code == 308) {
      char *redir = NULL;
      curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redir);
      if (!redir || !redir[0])
        return CURLE_OK;
      snprintf(next, sizeof(next), "%s", redir);
      continue;
    }
    return CURLE_OK;
  }
  return CURLE_TOO_MANY_REDIRECTS;
}
