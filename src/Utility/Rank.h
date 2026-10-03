#ifndef RANK_H
#define RANK_H

int rank_query_allows_forums(const char *q);
double rank_host_weight(const char *url, int allow_forums);
int image_query_score(const char *query, const char *title, const char *page_url,
                      const char *image_url);
int image_query_has_distinctive_token(const char *query);

#endif
