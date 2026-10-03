#include "security_headers.h"
#include "beaker_globals.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *g_csp_override;
static const char *g_permissions_policy_override;

static const char default_csp[] =
    "default-src 'self'; "
    "script-src 'self' 'unsafe-inline'; "
    "style-src 'self' 'unsafe-inline'; "
    "img-src 'self' data: https:; "
    "font-src 'self'; "
    "connect-src 'self' https://api.frankfurter.app "
    "https://api.open-meteo.com https://geocoding-api.open-meteo.com; "
    "frame-ancestors 'none'; "
    "base-uri 'self'; "
    "form-action 'self'";

static const char default_permissions_policy[] =
    "accelerometer=(), autoplay=(), camera=(), display-capture=(), "
    "encrypted-media=(), fullscreen=(self), geolocation=(), gyroscope=(), "
    "keyboard-map=(), magnetometer=(), microphone=(), midi=(), payment=(), "
    "picture-in-picture=(), publickey-credentials-get=(), "
    "screen-wake-lock=(), sync-xhr=(), usb=(), xr-spatial-tracking=()";

static long g_hsts_max_age_sec = -1;
static int g_hsts_include_subdomains;

#define BEAKER_HSTS_AUTO (-1L)

static int header_value_first_https(const char *value_start,
                                    const char *line_end) {
  const char *p = value_start;
  while (p < line_end) {
    while (p < line_end && (*p == ' ' || *p == '\t' || *p == ','))
      p++;
    if (p >= line_end)
      break;
    if ((size_t)(line_end - p) >= 5 && strncasecmp(p, "https", 5) == 0) {
      char c = p[5];
      if (c == '\0' || c == ',' || c == ' ' || c == '\t' || c == ';' ||
          c == '\r')
        return 1;
    }
    while (p < line_end && *p != ',')
      p++;
  }
  return 0;
}

static int header_value_is_on(const char *value_start, const char *line_end) {
  const char *p = value_start;
  while (p < line_end && (*p == ' ' || *p == '\t'))
    p++;
  if ((size_t)(line_end - p) >= 2 && strncasecmp(p, "on", 2) == 0) {
    if ((size_t)(line_end - p) == 2)
      return 1;
    char c = p[2];
    if (c == ' ' || c == '\t' || c == '\r')
      return 1;
  }
  return 0;
}

static int forwarded_line_has_proto_https(const char *line_start, size_t line_len) {
  if (line_len < 11)
    return 0;
  for (size_t i = 0; i + 11 <= line_len; i++) {
    if (strncmp(line_start + i, "proto=https", 11) == 0)
      return 1;
  }
  return 0;
}

static int request_is_https(void) {
  const char *buf = current_request_buffer;
  if (buf == NULL || buf[0] == '\0')
    return 0;

  const char *p = strstr(buf, "\r\n");
  if (p == NULL)
    return 0;
  p += 2;

  while (*p) {
    const char *line_end = strstr(p, "\r\n");
    if (line_end == NULL)
      break;
    if (line_end == p)
      break;

    size_t line_len = (size_t)(line_end - p);
    if (line_len >= 18 && strncasecmp(p, "X-Forwarded-Proto:", 18) == 0) {
      if (header_value_first_https(p + 18, line_end))
        return 1;
    }
    if (line_len >= 19 && strncasecmp(p, "X-Forwarded-Ssl:", 19) == 0) {
      if (header_value_is_on(p + 19, line_end))
        return 1;
    }
    if (line_len >= 17 && strncasecmp(p, "Front-End-Https:", 17) == 0) {
      if (header_value_is_on(p + 17, line_end))
        return 1;
    }
    if (line_len >= 10 && strncasecmp(p, "Forwarded:", 10) == 0) {
      if (forwarded_line_has_proto_https(p, line_len))
        return 1;
    }
    p = line_end + 2;
  }
  return 0;
}

void beaker_set_csp_policy(const char *policy) { g_csp_override = policy; }

void beaker_set_permissions_policy(const char *policy) {
  g_permissions_policy_override = policy;
}

void beaker_set_hsts_max_age_sec(long max_age_sec) {
  g_hsts_max_age_sec = max_age_sec;
}

void beaker_set_hsts_include_subdomains(int include) {
  g_hsts_include_subdomains = include ? 1 : 0;
}

int beaker_format_security_headers(char *buf, size_t bufsize) {
  const char *csp = g_csp_override ? g_csp_override : default_csp;
  const char *pp = g_permissions_policy_override ? g_permissions_policy_override
                                                   : default_permissions_policy;

  long hsts_age = 0;
  int send_hsts = 0;
  if (g_hsts_max_age_sec > 0) {
    hsts_age = g_hsts_max_age_sec;
    send_hsts = 1;
  } else if (g_hsts_max_age_sec == BEAKER_HSTS_AUTO && request_is_https()) {
    hsts_age = 31536000L;
    send_hsts = 1;
  }

  int n = snprintf(
      buf, bufsize,
      "Content-Security-Policy: %s\r\n"
      "X-Content-Type-Options: nosniff\r\n"
      "X-Frame-Options: DENY\r\n"
      "X-XSS-Protection: 1; mode=block\r\n"
      "Referrer-Policy: strict-origin-when-cross-origin\r\n"
      "Permissions-Policy: %s\r\n"
      "Cross-Origin-Opener-Policy: same-origin\r\n"
      "Cross-Origin-Resource-Policy: same-origin\r\n"
      "X-DNS-Prefetch-Control: off\r\n"
      "X-Permitted-Cross-Domain-Policies: none\r\n",
      csp, pp);

  if (n < 0 || (size_t)n >= bufsize)
    return -1;

  size_t off = (size_t)n;
  if (send_hsts && hsts_age > 0) {
    int m = snprintf(buf + off, bufsize - off,
                     "Strict-Transport-Security: max-age=%ld%s\r\n", hsts_age,
                     g_hsts_include_subdomains ? "; includeSubDomains" : "");
    if (m < 0 || (size_t)m >= bufsize - off)
      return -1;
    off += (size_t)m;
  }

  return (int)off;
}

int beaker_snprint_http_empty_response(char *buf, size_t bufsize,
                                       const char *status_line) {
  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;
  return snprintf(buf, bufsize,
                  "%s\r\n"
                  "%.*s"
                  "Content-Length: 0\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  status_line, seclen, sec);
}

int beaker_snprint_http_html_response(char *buf, size_t bufsize,
                                      const char *status_line,
                                      const char *html) {
  char sec[6144];
  int seclen = beaker_format_security_headers(sec, sizeof(sec));
  if (seclen < 0)
    seclen = 0;
  size_t blen = strlen(html);
  return snprintf(buf, bufsize,
                  "%s\r\n"
                  "%.*s"
                  "Content-Type: text/html; charset=UTF-8\r\n"
                  "Content-Length: %zu\r\n"
                  "Connection: close\r\n"
                  "\r\n%s",
                  status_line, seclen, sec, blen, html);
}
