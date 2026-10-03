#ifndef TODAY_H
#define TODAY_H

#include <beaker.h>

typedef struct {
  char **rows;
  int *inner;
  int n;
} TodayItems;

int today_parse_feed(const char *json, char ***out_rows, int *out_n,
                     int max_items);
int today_load(TodayItems *out);
void today_items_free(TodayItems *items);
void today_apply(TemplateContext *ctx, TodayItems *items);
int query_wants_today(const char *query);

#endif
