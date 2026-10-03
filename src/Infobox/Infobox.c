#include "Infobox.h"
#include <stdlib.h>

void free_infobox(InfoBox *info) {
  if (info->title)
    free(info->title);
  if (info->thumbnail_url)
    free(info->thumbnail_url);
  if (info->extract)
    free(info->extract);
  if (info->url)
    free(info->url);
}
