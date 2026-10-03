#ifndef HTML_ESCAPE_H
#define HTML_ESCAPE_H

#include <stddef.h>

void html_escape_attr(const char *in, char *out, size_t cap);
void html_escape_text(const char *in, char *out, size_t cap);

#endif
