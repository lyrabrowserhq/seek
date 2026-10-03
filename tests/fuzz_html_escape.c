#include "../src/Utility/HtmlEscape.h"
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char in[512];
  char out[4096];
  size_t n = size < sizeof(in) - 1 ? size : sizeof(in) - 1;
  memcpy(in, data, n);
  in[n] = '\0';
  html_escape_attr(in, out, sizeof(out));
  html_escape_text(in, out, sizeof(out));
  html_escape_attr(in, out, 64);
  html_escape_text(in, out, 64);
  return 0;
}
