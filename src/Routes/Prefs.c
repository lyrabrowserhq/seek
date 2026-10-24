#include "Prefs.h"
#include "../Utility/DomainPrefs.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* send the user back to the page they came from. referer is only trusted
 * when its host matches the request Host so /prefs/domain cannot be used
 * as an open redirect. */
static const char *safe_return_target(void) {
  static char ref_buf[4096];
  snprintf(ref_buf, sizeof(ref_buf), "%s", beaker_get_header("Referer"));
  const char *host = beaker_get_header("Host");
  if (!ref_buf[0] || !host || !host[0])
    return "/";
  const char *scheme = strstr(ref_buf, "://");
  if (!scheme)
    return "/";
  const char *h = scheme + 3;
  size_t hn = strlen(host);
  if (strncmp(h, host, hn) != 0)
    return "/";
  char next = h[hn];
  if (next != '/' && next != '\0' && next != '?' && next != '#')
    return "/";
  return ref_buf;
}

int prefs_domain_handler(UrlParams *params) {
  const char *host = NULL;
  const char *level = NULL;
  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "h") == 0)
        host = params->params[i].value;
      else if (strcmp(params->params[i].key, "v") == 0)
        level = params->params[i].value;
    }
  }
  if (host && level) {
    char clean[256];
    snprintf(clean, sizeof(clean), "%s", host);
    char *port = strchr(clean, ':');
    if (port)
      *port = '\0';
    int ok = dprefs_set(clean, level);
    if (ok == 0) {
      send_redirect(safe_return_target());
      return 0;
    }
  }
  send_redirect("/");
  return 0;
}

/* /prefs/lens?text=a.com,b.com replaces the custom-lens domain list.
   same-cookie storage as dprefs - ulens holds comma-separated suffixes */
int prefs_lens_handler(UrlParams *params) {
  const char *text = NULL;
  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "text") == 0)
        text = params->params[i].value;
    }
  }
  if (text) {
    /* keep host-shaped entries only - alnum, dot, dash, underscore */
    char clean[2048];
    size_t o = 0;
    for (const char *p = text; *p && o + 1 < sizeof(clean); p++) {
      char c = *p;
      if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' ||
          c == ',')
        clean[o++] = c;
    }
    clean[o] = '\0';
    set_cookie("ulens", clean, "expires=Wed, 01 Jan 2031 00:00:00 GMT", "/", false, false);
    send_redirect(safe_return_target());
    return 0;
  }
  send_redirect("/settings");
  return 0;
}
