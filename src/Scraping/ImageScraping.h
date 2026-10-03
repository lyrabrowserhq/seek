#ifndef IMAGESCRAPING_H
#define IMAGESCRAPING_H

#include "ImageFilters.h"
#include <curl/curl.h>
#include <libxml/HTMLparser.h>

typedef struct {
  char *thumbnail_url;
  char *title;
  char *page_url;
  char *full_url;
} ImageResult;

int scrape_images(const char *query, int page, const ImageFilters *filters,
                  ImageResult **out_results, int *out_count);
void free_image_results(ImageResult *results, int count);

#endif
