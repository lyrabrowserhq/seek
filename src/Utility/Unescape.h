#ifndef UNESCAPE_H
#define UNESCAPE_H

#include <stddef.h>

char *unescape_search_url(const char *input);
char *url_decode_query(const char *src);

#endif
