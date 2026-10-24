#include "Cache.h"
#include "Config.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CACHE_LOCK_STALE_SEC 120

static char cache_dir[BUFFER_SIZE_MEDIUM] = {0};
static int cache_ttl_search_val = DEFAULT_CACHE_TTL_SEARCH;
static int cache_ttl_infobox_val = DEFAULT_CACHE_TTL_INFOBOX;
static int cache_ttl_image_val = DEFAULT_CACHE_TTL_IMAGE;

void set_cache_ttl_search(int ttl) { cache_ttl_search_val = ttl; }

void set_cache_ttl_infobox(int ttl) { cache_ttl_infobox_val = ttl; }

void set_cache_ttl_image(int ttl) { cache_ttl_image_val = ttl; }

int get_cache_ttl_search(void) { return cache_ttl_search_val; }

int get_cache_ttl_infobox(void) { return cache_ttl_infobox_val; }

int get_cache_ttl_image(void) { return cache_ttl_image_val; }

static void md5_hash(const char *str, char *output) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  if (!ctx)
    return;
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
  EVP_DigestUpdate(ctx, str, strlen(str));
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);
  for (unsigned int i = 0; i < hash_len; i++) {
    sprintf(output + (i * 2), "%02x", hash[i]);
  }
}

static time_t get_file_mtime(const char *filepath) {
  struct stat st;
  if (stat(filepath, &st) == 0) {
    return st.st_mtime;
  }
  return 0;
}

int cache_init(const char *dir) {
  if (!dir || strlen(dir) == 0) {
    strncpy(cache_dir, DEFAULT_CACHE_DIR, sizeof(cache_dir) - 1);
    cache_dir[sizeof(cache_dir) - 1] = '\0';
  } else {
    strncpy(cache_dir, dir, sizeof(cache_dir) - 1);
    cache_dir[sizeof(cache_dir) - 1] = '\0';
  }

  struct stat st;
  if (stat(cache_dir, &st) != 0) {
    if (mkdir(cache_dir, 0755) != 0) {
      fprintf(stderr, "[ERROR] Failed to create cache directory: %s\n",
              cache_dir);
      return -1;
    }
  } else if (!S_ISDIR(st.st_mode)) {
    fprintf(stderr, "[ERROR] Cache path exists but is not a directory: %s\n",
            cache_dir);
    return -1;
  }

  for (int i = 0; HEX_CHARS[i]; i++) {
    char subdir_path[BUFFER_SIZE_LARGE];
    snprintf(subdir_path, sizeof(subdir_path), "%s/%c", cache_dir,
             HEX_CHARS[i]);
    if (stat(subdir_path, &st) != 0) {
      mkdir(subdir_path, 0755);
    }
  }

  return 0;
}

const char *cache_dir_path(void) { return cache_dir; }

void cache_shutdown(void) { cache_dir[0] = '\0'; }

char *cache_compute_key(const char *query, int page, const char *engine_name) {
  char key_buffer[BUFFER_SIZE_LARGE];
  snprintf(key_buffer, sizeof(key_buffer), "%s_%d_%s", query ? query : "", page,
           engine_name ? engine_name : "");

  char *hash = malloc(MD5_HASH_LEN + 1);
  if (!hash) {
    return NULL;
  }
  md5_hash(key_buffer, hash);
  return hash;
}

int cache_get(const char *key, time_t max_age, char **out_data,
              size_t *out_size) {
  if (!key || !out_data || !out_size || cache_dir[0] == '\0') {
    return -1;
  }

  char filepath[BUFFER_SIZE_LARGE];
  snprintf(filepath, sizeof(filepath), "%s/%c/%s.cache", cache_dir, key[0],
           key);

  time_t file_mtime = get_file_mtime(filepath);
  if (file_mtime == 0) {
    return -1;
  }

  time_t now = time(NULL);
  if (max_age > 0 && (now - file_mtime) > max_age) {
    remove(filepath);
    return -1;
  }

  FILE *fp = fopen(filepath, "rb");
  if (!fp) {
    return -1;
  }

  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size <= 0) {
    fclose(fp);
    return -1;
  }

  char *data = malloc(size + 1);
  if (!data) {
    fclose(fp);
    return -1;
  }

  size_t read_size = fread(data, 1, size, fp);
  fclose(fp);

  if (read_size != (size_t)size) {
    free(data);
    return -1;
  }

  data[size] = '\0';
  *out_data = data;
  *out_size = size;
  return 0;
}

int cache_set(const char *key, const char *data, size_t size) {
  if (!key || !data || size == 0 || cache_dir[0] == '\0') {
    return -1;
  }

  char filepath[BUFFER_SIZE_LARGE];
  snprintf(filepath, sizeof(filepath), "%s/%c/%s.cache", cache_dir, key[0],
           key);

  FILE *fp = fopen(filepath, "wb");
  if (!fp) {
    return -1;
  }

  size_t written = fwrite(data, 1, size, fp);
  fclose(fp);

  if (written != size) {
    remove(filepath);
    return -1;
  }

  return 0;
}

static void cache_lock_path(const char *key, char *out, size_t outsz) {
  snprintf(out, outsz, "%s/%c/%s.lock", cache_dir, key[0], key);
}

int cache_lock_acquire(const char *key) {
  if (!key || cache_dir[0] == '\0')
    return -1;

  char path[BUFFER_SIZE_LARGE];
  cache_lock_path(key, path, sizeof(path));

  struct stat st;
  if (stat(path, &st) == 0) {
    time_t now = time(NULL);
    if (now - st.st_mtime > CACHE_LOCK_STALE_SEC)
      unlink(path);
  }

  int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
  if (fd >= 0) {
    close(fd);
    return 0;
  }
  if (errno == EEXIST)
    return -1;
  return -1;
}

void cache_lock_release(const char *key) {
  if (!key || cache_dir[0] == '\0')
    return;

  char path[BUFFER_SIZE_LARGE];
  cache_lock_path(key, path, sizeof(path));
  unlink(path);
}

void cache_cleanup(time_t max_age) {
  if (cache_dir[0] == '\0' || max_age <= 0) {
    return;
  }

  time_t now = time(NULL);
  time_t cutoff = now - max_age;

  for (int d = 0; HEX_CHARS[d]; d++) {
    char subdir_path[BUFFER_SIZE_LARGE];
    snprintf(subdir_path, sizeof(subdir_path), "%s/%c", cache_dir,
             HEX_CHARS[d]);

    DIR *dir = opendir(subdir_path);
    if (!dir)
      continue;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      size_t len = strlen(entry->d_name);
      if (len > 7 && strcmp(entry->d_name + len - 7, ".cache") == 0) {
        char filepath[BUFFER_SIZE_XLARGE];
        snprintf(filepath, sizeof(filepath), "%s/%s", subdir_path,
                 entry->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0 && st.st_mtime < cutoff) {
          remove(filepath);
        }
      }
    }
    closedir(dir);
  }
}

/* background reaper: sweeps expired entries every 5 minutes and enforces a
   hard byte cap by evicting oldest files first, so the cache cannot grow
   without bound under sustained load */

typedef struct {
  time_t ttl_search;
} ReaperCfg;

#define CACHE_REAPER_INTERVAL_SEC 300
#define CACHE_MAX_BYTES (256L * 1024 * 1024)

typedef struct {
  char path[BUFFER_SIZE_XLARGE];
  time_t mtime;
  long size;
} EvictEnt;

static int evict_cmp(const void *a, const void *b) {
  const EvictEnt *ea = (const EvictEnt *)a;
  const EvictEnt *eb = (const EvictEnt *)b;
  if (ea->mtime < eb->mtime)
    return -1;
  if (ea->mtime > eb->mtime)
    return 1;
  return 0;
}

static long cache_total_bytes(EvictEnt **out_entries, int *out_count) {
  /* gather every cache file with mtime+size for eviction ordering */
  long total = 0;
  int cap = 256, n = 0;
  EvictEnt *ents = malloc(sizeof(EvictEnt) * cap);
  if (!ents)
    return 0;

  for (int d = 0; HEX_CHARS[d]; d++) {
    char subdir[BUFFER_SIZE_LARGE];
    snprintf(subdir, sizeof(subdir), "%s/%c", cache_dir, HEX_CHARS[d]);
    DIR *dir = opendir(subdir);
    if (!dir)
      continue;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      size_t len = strlen(entry->d_name);
      if (len <= 7 || strcmp(entry->d_name + len - 7, ".cache") != 0)
        continue;
      struct stat st;
      if (n == cap) {
        cap *= 2;
        EvictEnt *grown = realloc(ents, sizeof(EvictEnt) * cap);
        if (!grown) {
          closedir(dir);
          free(ents);
          *out_entries = NULL;
          *out_count = 0;
          return total;
        }
        ents = grown;
      }
      EvictEnt *e = &ents[n];
      snprintf(e->path, sizeof(e->path), "%s/%s", subdir, entry->d_name);
      if (stat(e->path, &st) != 0)
        continue;
      e->mtime = st.st_mtime;
      e->size = st.st_size;
      total += st.st_size;
      n++;
    }
    closedir(dir);
  }
  *out_entries = ents;
  *out_count = n;
  return total;
}

static void *cache_reaper_thread(void *arg) {
  time_t ttl = *(time_t *)arg;
  free(arg);
  for (;;) {
    sleep(CACHE_REAPER_INTERVAL_SEC);
    if (cache_dir[0] == '\0')
      break;
    cache_cleanup(ttl);

    /* evict oldest entries while over the byte cap */
    EvictEnt *ents = NULL;
    int n = 0;
    long total = cache_total_bytes(&ents, &n);
    if (ents && total > CACHE_MAX_BYTES) {
      qsort(ents, n, sizeof(EvictEnt), evict_cmp);
      long need = total - CACHE_MAX_BYTES;
      for (int i = 0; i < n && need > 0; i++) {
        if (remove(ents[i].path) == 0)
          need -= ents[i].size;
      }
      fprintf(stderr, "[INFO] cache reaper evicted to cap %ld bytes\n",
              (long)CACHE_MAX_BYTES);
    }
    free(ents);
  }
  return NULL;
}

int cache_start_reaper(time_t ttl_search) {
  if (cache_dir[0] == '\0')
    return -1;
  time_t *arg = malloc(sizeof(time_t));
  if (!arg)
    return -1;
  *arg = ttl_search;
  pthread_t t;
  if (pthread_create(&t, NULL, cache_reaper_thread, arg) != 0) {
    free(arg);
    return -1;
  }
  pthread_detach(t);
  return 0;
}
