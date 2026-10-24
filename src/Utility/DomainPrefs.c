#include "DomainPrefs.h"
#include <beaker.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DPREF_COOKIE "dprefs"
#define DPREF_COOKIE_EXPIRES "Fri, 31 Dec 2038 23:59:59 GMT"

#define DPREF_MAX_ENTRIES 128
#define DPREF_HOST_MAX 128

static int host_from_url(const char *url, char *out, size_t n) {
  if (!url || !out || n == 0)
    return 0;
  const char *h = strstr(url, "://");
  h = h ? h + 3 : url;
  size_t i = 0;
  while (h[i] && h[i] != '/' && h[i] != ':' && h[i] != '?' && i + 1 < n) {
    out[i] = (char)tolower((unsigned char)h[i]);
    i++;
  }
  out[i] = '\0';
  while (i > 4 && strncmp(out, "www.", 4) == 0) {
    memmove(out, out + 4, i - 3);
    i -= 4;
  }
  return i > 0;
}

static int host_suffix_match(const char *host, const char *domain) {
  size_t hn = strlen(host);
  size_t dn = strlen(domain);
  if (dn == 0 || hn < dn)
    return 0;
  if (strcasecmp(host + hn - dn, domain) != 0)
    return 0;
  return hn == dn || host[hn - dn - 1] == '.';
}

static char level_to_code(const char *level) {
  if (!level)
    return 0;
  if (!strcmp(level, "pin"))
    return 'p';
  if (!strcmp(level, "raise"))
    return 'r';
  if (!strcmp(level, "lower"))
    return 'l';
  if (!strcmp(level, "block"))
    return 'b';
  return 0;
}

static double code_to_weight(char code) {
  switch (code) {
  case 'p':
    return 25.0;
  case 'r':
    return 4.0;
  case 'l':
    return 0.2;
  case 'b':
    return 0.0;
  default:
    return 1.0;
  }
}

static char code_to_sigil(char code, int *two) {
  *two = 0;
  switch (code) {
  case 'p':
    *two = 1;
    return '+';
  case 'r':
    return '+';
  case 'l':
    return '-';
  case 'b':
    return '!';
  default:
    return ' ';
  }
}

static int valid_host(const char *host) {
  if (!host)
    return 0;
  size_t n = strlen(host);
  if (n == 0 || n > DPREF_HOST_MAX)
    return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)host[i];
    if (!(isalnum(c) || c == '.' || c == '-'))
      return 0;
  }
  return 1;
}

/* longest matching entry wins so sub.example.com can override example.com */
static char lookup_code(const char *cookie_val, const char *host,
                        size_t *best_len) {
  char best = 0;
  size_t blen = 0;
  char buf[8192];
  snprintf(buf, sizeof(buf), "%s", cookie_val ? cookie_val : "");
  char *save = NULL;
  for (char *tok = strtok_r(buf, "+", &save); tok;
       tok = strtok_r(NULL, "+", &save)) {
    char *colon = strchr(tok, ':');
    if (!colon || colon == tok || !colon[1])
      continue;
    *colon = '\0';
    if (host_suffix_match(host, tok) && strlen(tok) > blen) {
      best = colon[1];
      blen = strlen(tok);
    }
  }
  if (best_len)
    *best_len = blen;
  return best;
}

double dprefs_url_weight(const char *url) {
  char *cookie = get_cookie(DPREF_COOKIE);
  if (!cookie || !cookie[0]) {
    free(cookie);
    return 1.0;
  }
  char host[DPREF_HOST_MAX + 1];
  if (!host_from_url(url, host, sizeof(host))) {
    free(cookie);
    return 1.0;
  }
  char code = lookup_code(cookie, host, NULL);
  free(cookie);
  if (!code)
    return 1.0;
  return code_to_weight(code);
}

int dprefs_set(const char *host, const char *level) {
  if (!valid_host(host))
    return -1;
  char code = level_to_code(level);
  if (!code && strcmp(level ? level : "", "normal") != 0)
    return -1;

  char *cookie = get_cookie(DPREF_COOKIE);
  char buf[8192];
  snprintf(buf, sizeof(buf), "%s", cookie ? cookie : "");
  free(cookie);

  char out[8192];
  out[0] = '\0';
  int done = 0;
  char *save = NULL;
  for (char *tok = strtok_r(buf, "+", &save); tok;
       tok = strtok_r(NULL, "+", &save)) {
    char *colon = strchr(tok, ':');
    if (!colon || colon == tok || !colon[1])
      continue;
    *colon = '\0';
    if (strcasecmp(tok, host) == 0) {
      if (code) {
        size_t used = strlen(out);
        snprintf(out + used, sizeof(out) - used, "%s%s:%c",
                 used ? "+" : "", tok, code);
      }
      done = 1;
      continue;
    }
    size_t used = strlen(out);
    snprintf(out + used, sizeof(out) - used, "%s%s:%c", used ? "+" : "", tok,
             colon[1]);
  }
  if (!done && code) {
    char lower[DPREF_HOST_MAX + 1];
    snprintf(lower, sizeof(lower), "%s", host);
    for (char *p = lower; *p; p++)
      *p = (char)tolower((unsigned char)*p);
    size_t used = strlen(out);
    snprintf(out + used, sizeof(out) - used, "%s%s:%c", used ? "+" : "", lower,
             code);
  }
  set_cookie(DPREF_COOKIE, out, DPREF_COOKIE_EXPIRES, "/", false, false);
  return 0;
}

/* render the cookie as a textarea-friendly list:
 * ++pin.example +raise.example -lower.example !block.example */
char *dprefs_edit_text(void) {
  char *cookie = get_cookie(DPREF_COOKIE);
  if (!cookie || !cookie[0]) {
    free(cookie);
    return strdup("");
  }
  size_t cap = strlen(cookie) * 2 + 64;
  char *out = malloc(cap);
  if (!out) {
    free(cookie);
    return NULL;
  }
  out[0] = '\0';
  char *save = NULL;
  for (char *tok = strtok_r(cookie, "+", &save); tok;
       tok = strtok_r(NULL, "+", &save)) {
    char *colon = strchr(tok, ':');
    if (!colon || colon == tok || !colon[1])
      continue;
    *colon = '\0';
    int two = 0;
    char sig = code_to_sigil(colon[1], &two);
    size_t used = strlen(out);
    snprintf(out + used, cap - used, "%s%s%s\n", two ? "++" : "",
             two ? "" : (char[]){sig, '\0'}, tok);
  }
  free(cookie);
  return out;
}

/* parse the textarea back into a cookie value. accepted line forms:
 * ++host pins, +host raises, -host lowers, !host blocks, bare host resets */
int dprefs_save_editor_text(const char *text) {
  if (!text)
    return -1;
  char buf[8192];
  snprintf(buf, sizeof(buf), "%s", text);
  char out[8192];
  out[0] = '\0';
  int entries = 0;
  char *save = NULL;
  for (char *line = strtok_r(buf, "\r\n", &save); line && entries < DPREF_MAX_ENTRIES;
       line = strtok_r(NULL, "\r\n", &save)) {
    while (*line == ' ' || *line == '\t')
      line++;
    if (!*line || *line == '#')
      continue;
    char code = 'r';
    if (line[0] == '+' && line[1] == '+') {
      code = 'p';
      line += 2;
    } else if (line[0] == '+') {
      code = 'r';
      line += 1;
    } else if (line[0] == '-') {
      code = 'l';
      line += 1;
    } else if (line[0] == '!') {
      code = 'b';
      line += 1;
    }
    char host[DPREF_HOST_MAX + 1];
    size_t hn = 0;
    while (line[hn] && !isspace((unsigned char)line[hn]) && hn < DPREF_HOST_MAX)
      hn++;
    if (hn == 0 || (line[hn] && !isspace((unsigned char)line[hn])))
      continue;
    memcpy(host, line, hn);
    host[hn] = '\0';
    for (char *p = host; *p; p++)
      *p = (char)tolower((unsigned char)*p);
    if (!valid_host(host))
      continue;
    size_t used = strlen(out);
    if (used + hn + 8 >= sizeof(out))
      break;
    snprintf(out + used, sizeof(out) - used, "%s%s:%c", used ? "+" : "", host,
             code);
    entries++;
  }
  set_cookie(DPREF_COOKIE, out, DPREF_COOKIE_EXPIRES, "/", false, false);
  return 0;
}
