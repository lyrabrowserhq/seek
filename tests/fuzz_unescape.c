#include "../src/Utility/Unescape.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char buf[4096];
  size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
  memcpy(buf, data, n);
  buf[n] = '\0';

  char *a = unescape_search_url(buf);
  free(a);
  char *b = url_decode_query(buf);
  free(b);
  return 0;
}
