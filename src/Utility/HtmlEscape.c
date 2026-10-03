#include "HtmlEscape.h"
#include <string.h>

void html_escape_attr(const char *in, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  if (!in) {
    out[0] = '\0';
    return;
  }
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 7 < cap; i++) {
    switch (in[i]) {
    case '&':
      memcpy(out + j, "&amp;", 5);
      j += 5;
      break;
    case '"':
      memcpy(out + j, "&quot;", 6);
      j += 6;
      break;
    case '\'':
      memcpy(out + j, "&#39;", 5);
      j += 5;
      break;
    default:
      out[j++] = in[i];
      break;
    }
  }
  out[j] = '\0';
}

void html_escape_text(const char *in, char *out, size_t cap) {
  if (!out || cap == 0)
    return;
  if (!in) {
    out[0] = '\0';
    return;
  }
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 9 < cap; i++) {
    switch (in[i]) {
    case '&':
      memcpy(out + j, "&amp;", 5);
      j += 5;
      break;
    case '<':
      memcpy(out + j, "&lt;", 4);
      j += 4;
      break;
    case '>':
      memcpy(out + j, "&gt;", 4);
      j += 4;
      break;
    default:
      out[j++] = in[i];
      break;
    }
  }
  out[j] = '\0';
}
