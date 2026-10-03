#ifndef IMAGEFILTERS_H
#define IMAGEFILTERS_H

#include <beaker.h>
#include <stddef.h>

typedef struct {
  char region[16];
  char safe[16];
  char time[16];
  char size[16];
  char color[16];
  char type[20];
  char layout[16];
  char license[24];
} ImageFilters;

void image_filters_defaults(ImageFilters *filters);
void image_filters_from_params(ImageFilters *filters, UrlParams *params);
int image_filters_append_to_url(const ImageFilters *filters, char *url,
                                size_t url_size);
int image_filters_build_query_string(const ImageFilters *filters, char *buf,
                                     size_t bufsize);

#endif
