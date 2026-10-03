#ifndef BEAKER_SECURITY_HEADERS_H
#define BEAKER_SECURITY_HEADERS_H

#include <stddef.h>

int beaker_format_security_headers(char *buf, size_t bufsize);
int beaker_snprint_http_empty_response(char *buf, size_t bufsize,
                                       const char *status_line);
int beaker_snprint_http_html_response(char *buf, size_t bufsize,
                                      const char *status_line,
                                      const char *html);

#endif
