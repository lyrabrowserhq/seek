#ifndef RANK_H
#define RANK_H

int rank_query_allows_forums(const char *q);
double rank_host_weight(const char *url, int allow_forums);
int lens_is_valid(const char *lens);
int lens_forces_forums(const char *lens);
double lens_host_weight(const char *url, const char *lens);
const char *const *lens_engines(const char *lens, int *count);
int image_query_score(const char *query, const char *title, const char *page_url,
                      const char *image_url);
int image_query_has_distinctive_token(const char *query);

#endif
