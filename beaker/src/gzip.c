#include "gzip.h"
#include "beaker_globals.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zlib.h>

int beaker_client_accepts_gzip(void) {
  const char *ae = beaker_get_header("Accept-Encoding");
  if (!ae)
    return 0;
  const char *p = ae;
  while (*p) {
    while (*p == ' ' || *p == ',')
      p++;
    if (strncasecmp(p, "gzip", 4) == 0 &&
        (p[4] == ',' || p[4] == ';' || p[4] == ' ' || p[4] == '\0'))
      return 1;
    if (*p == '*' &&
        (p[1] == ',' || p[1] == ';' || p[1] == ' ' || p[1] == '\0'))
      return 1;
    while (*p && *p != ',')
      p++;
  }
  return 0;
}

unsigned char *beaker_gzip_compress(const unsigned char *data, size_t size,
                                    size_t *out_size) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK)
    return NULL;
  uLong bound = deflateBound(&zs, (uLong)size);
  unsigned char *out = malloc(bound ? bound : 1);
  if (!out) {
    deflateEnd(&zs);
    return NULL;
  }
  zs.next_in = (Bytef *)data;
  zs.avail_in = (uInt)size;
  zs.next_out = out;
  zs.avail_out = (uInt)bound;
  int ret = deflate(&zs, Z_FINISH);
  if (ret != Z_STREAM_END) {
    deflateEnd(&zs);
    free(out);
    return NULL;
  }
  *out_size = zs.total_out;
  deflateEnd(&zs);
  return out;
}
