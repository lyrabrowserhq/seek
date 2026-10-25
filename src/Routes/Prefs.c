#include "Prefs.h"
#include "../Utility/DomainPrefs.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* send the user back to the page they came from. referer is only trusted
 * when its host matches the request Host so /prefs/domain cannot be used
 * as an open redirect. writes a relative path+query into out. */
static void safe_return_target(char *out, size_t out_sz) {
  const char *fallback = "/";
  if (!out || out_sz < 2)
    return;
  const char *ref = beaker_get_header("Referer");
  const char *host = beaker_get_header("Host");
  if (!ref || !ref[0] || !host || !host[0])
    goto done;
  const char *scheme = strstr(ref, "://");
  if (!scheme)
    goto done;
  const char *h = scheme + 3;
  size_t hn = strlen(host);
  if (strncmp(h, host, hn) != 0)
    goto done;
  char next = h[hn];
  if (next != '/' && next != '\0' && next != '?' && next != '#')
    goto done;
  const char *path = h + hn;
  if (*path == '\0')
    path = "/";
  else if (*path == '?' || *path == '#')
    path = "/";
  /* must be a single leading slash - reject // and /\ style escapes */
  if (path[0] != '/' || path[1] == '/' || path[1] == '\\')
    goto done;
  snprintf(out, out_sz, "%s", path);
  return;
done:
  snprintf(out, out_sz, "%s", fallback);
}

static int cross_site_fetch(void) {
  const char *v = beaker_get_header("Sec-Fetch-Site");
  return v && strcmp(v, "cross-site") == 0;
}

int prefs_domain_handler(UrlParams *params) {
  if (cross_site_fetch()) {
    send_redirect("/");
    return 0;
  }
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
      char target[4096];
      safe_return_target(target, sizeof(target));
      send_redirect(target);
      return 0;
    }
  }
  send_redirect("/");
  return 0;
}

/* /prefs/lens?text=a.com,b.com replaces the custom-lens domain list.
   same-cookie storage as dprefs - ulens holds comma-separated suffixes */
int prefs_lens_handler(UrlParams *params) {
  if (cross_site_fetch()) {
    send_redirect("/");
    return 0;
  }
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
    set_cookie("ulens", clean, PREF_COOKIE_EXPIRES, "/", false, false);
    char target[4096];
    safe_return_target(target, sizeof(target));
    send_redirect(target);
    return 0;
  }
  send_redirect("/settings");
  return 0;
}
