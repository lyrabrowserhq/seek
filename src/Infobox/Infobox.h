#ifndef INFOBOX_H
#define INFOBOX_H

typedef struct {
  char *title;
  char *thumbnail_url;
  char *extract;
  char *url;
} InfoBox;

void free_infobox(InfoBox *info);

#endif
