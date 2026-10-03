#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>
#include <time.h>

int cache_init(const char *cache_dir);
const char *cache_dir_path(void);
void cache_shutdown(void);

int cache_get(const char *key, time_t max_age, char **out_data,
              size_t *out_size);
int cache_set(const char *key, const char *data, size_t size);

int cache_lock_acquire(const char *key);
void cache_lock_release(const char *key);

void cache_cleanup(time_t max_age);

char *cache_compute_key(const char *query, int page, const char *engine_name);

void set_cache_ttl_search(int ttl);
void set_cache_ttl_infobox(int ttl);
int get_cache_ttl_search(void);
int get_cache_ttl_infobox(void);
void set_cache_ttl_image(int ttl);
int get_cache_ttl_image(void);

#endif
