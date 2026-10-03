#ifndef BEAKER_GZIP_H
#define BEAKER_GZIP_H

#include <stddef.h>

#define GZIP_MIN_SIZE 1024

int beaker_client_accepts_gzip(void);
unsigned char *beaker_gzip_compress(const unsigned char *data, size_t size,
                                    size_t *out_size);

#endif
