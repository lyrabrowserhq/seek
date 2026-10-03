#include "DevTools.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void trim_cpy(const char *in, char *out, size_t cap) {
  if (!in || cap == 0) {
    if (cap > 0)
      out[0] = '\0';
    return;
  }
  while (*in && isspace((unsigned char)*in))
    in++;
  size_t len = strlen(in);
  while (len > 0 && isspace((unsigned char)in[len - 1]))
    len--;
  if (len >= cap)
    len = cap - 1;
  memcpy(out, in, len);
  out[len] = '\0';
}

static int starts_ci(const char *s, const char *p) {
  size_t n = strlen(p);
  return strncasecmp(s, p, n) == 0;
}

static void sha256_hex(const char *data, size_t len, char *out, size_t outcap) {
  unsigned char md[SHA256_DIGEST_LENGTH];
  SHA256((const unsigned char *)data, len, md);
  static const char *hex = "0123456789abcdef";
  if (outcap < SHA256_DIGEST_LENGTH * 2 + 1) {
    if (outcap)
      out[0] = '\0';
    return;
  }
  for (size_t i = 0; i < SHA256_DIGEST_LENGTH; i++) {
    out[i * 2] = hex[md[i] >> 4];
    out[i * 2 + 1] = hex[md[i] & 0xf];
  }
  out[SHA256_DIGEST_LENGTH * 2] = '\0';
}

int is_devtools_query(const char *query) {
  if (!query)
    return 0;
  char q[768];
  trim_cpy(query, q, sizeof(q));
  if (q[0] == '\0')
    return 0;

  if (strcasecmp(q, "uuid") == 0 || strcasecmp(q, "random uuid") == 0 ||
      strcasecmp(q, "devtools") == 0 || strcasecmp(q, "developer tools") == 0)
    return 1;

  if (starts_ci(q, "base64 encode ") && strlen(q) > 14)
    return 1;
  if (starts_ci(q, "base64 decode ") && strlen(q) > 14)
    return 1;
  if (starts_ci(q, "url encode ") && strlen(q) > 11)
    return 1;
  if (starts_ci(q, "url decode ") && strlen(q) > 11)
    return 1;
  if (starts_ci(q, "percent encode ") && strlen(q) > 15)
    return 1;
  if (starts_ci(q, "percent decode ") && strlen(q) > 15)
    return 1;
  if (starts_ci(q, "jwt decode ") && strlen(q) > 11)
    return 1;
  if (starts_ci(q, "sha256 ") && strlen(q) > 7)
    return 1;
  if (starts_ci(q, "sha256 hash ") && strlen(q) > 12)
    return 1;

  return 0;
}

static char *wrap_devtools(const char *mode, const char *initial, const char *noscript) {
  char em[32], ei[4096], en[8192];
  html_escape_attr(mode ? mode : "", em, sizeof(em));
  html_escape_attr(initial ? initial : "", ei, sizeof(ei));
  html_escape_text(noscript ? noscript : "", en, sizeof(en));
  char *html = malloc(16384);
  if (!html)
    return NULL;
  snprintf(html, 16384,
           "<div class=\"widget widget-devtools\" data-widget=\"devtools\" "
           "data-mode=\"%s\" data-initial=\"%s\">"
           "<div class=\"widget-devtools-root\"></div>"
           "<noscript><pre class=\"widget-fallback\">%s</pre></noscript></div>",
           em, ei, en);
  return html;
}

InfoBox fetch_devtools_data(char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  if (!query)
    return info;

  char q[768];
  trim_cpy(query, q, sizeof(q));

  char initial[4096] = "";
  const char *mode = "menu";
  char noscript[8192] = "";

  if (strcasecmp(q, "devtools") == 0 || strcasecmp(q, "developer tools") == 0) {
    mode = "menu";
    snprintf(noscript, sizeof(noscript), "Enable JavaScript for developer tools.");
  } else if (strcasecmp(q, "uuid") == 0 || strcasecmp(q, "random uuid") == 0) {
    mode = "uuid";
    snprintf(noscript, sizeof(noscript), "UUID: (generate in browser with JS enabled)");
  } else if (starts_ci(q, "base64 encode ")) {
    mode = "base64encode";
    trim_cpy(q + 14, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "base64 decode ")) {
    mode = "base64decode";
    trim_cpy(q + 14, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "url encode ")) {
    mode = "urlencode";
    trim_cpy(q + 11, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "url decode ")) {
    mode = "urldecode";
    trim_cpy(q + 11, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "percent encode ")) {
    mode = "urlencode";
    trim_cpy(q + 15, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "percent decode ")) {
    mode = "urldecode";
    trim_cpy(q + 15, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "jwt decode ")) {
    mode = "jwt";
    trim_cpy(q + 11, initial, sizeof(initial));
    snprintf(noscript, sizeof(noscript), "%s", initial);
  } else if (starts_ci(q, "sha256 hash ")) {
    mode = "sha256";
    trim_cpy(q + 12, initial, sizeof(initial));
    char hex[SHA256_DIGEST_LENGTH * 2 + 4];
    sha256_hex(initial, strlen(initial), hex, sizeof(hex));
    snprintf(noscript, sizeof(noscript), "%s", hex);
  } else if (starts_ci(q, "sha256 ")) {
    mode = "sha256";
    trim_cpy(q + 7, initial, sizeof(initial));
    char hex[SHA256_DIGEST_LENGTH * 2 + 4];
    sha256_hex(initial, strlen(initial), hex, sizeof(hex));
    snprintf(noscript, sizeof(noscript), "%s", hex);
  } else {
    return info;
  }

  char *html = wrap_devtools(mode, initial, noscript);
  if (!html)
    return info;

  info.title = strdup("Developer tools");
  info.extract = html;
  info.thumbnail_url = strdup("/static/calculation.svg");
  info.url = strdup("#");
  return info;
}
