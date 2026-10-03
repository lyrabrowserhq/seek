#include "../src/Utility/JsonHelper.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char buf[8192];
  size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
  memcpy(buf, data, n);
  buf[n] = '\0';

  JsonFloatMap m;
  json_parse_float_map(buf, "\"r\"", &m);
  (void)json_get_float(buf, "k");
  (void)json_get_string(buf, "k");
  return 0;
}
