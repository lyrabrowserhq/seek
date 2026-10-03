#include "beaker.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char buf[4096];
  size_t n = size < sizeof(buf) - 64 ? size : sizeof(buf) - 64;
  memcpy(buf, data, n);
  buf[n] = '\0';

  char line[4200];
  snprintf(line, sizeof(line), "GET %s HTTP/1.1", buf);

  UrlParams params;
  char *path = parse_request_url(line, &params);
  free(path);
  return 0;
}
