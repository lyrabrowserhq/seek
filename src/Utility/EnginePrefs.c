#include "Utility.h"
#include "../Scraping/Scraping.h"
#include <beaker.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int is_engine_id_enabled(const char *engine_id) {
  if (!engine_id || !engine_id[0])
    return 0;
  for (int i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && eng->enabled && strcasecmp(eng->id, engine_id) == 0)
      return 1;
  }
  return 0;
}

int get_user_engines(char ***out_ids, int *out_count) {
  *out_ids = NULL;
  *out_count = 0;

  char *cookie = get_cookie("engines");
  if (!cookie || cookie[0] == '\0') {
    free(cookie);
    return -1;
  }

  char **ids = NULL;
  int count = 0;

  char *copy = strdup(cookie);
  if (!copy) {
    free(cookie);
    return -1;
  }

  char *saveptr;
  char *token = strtok_r(copy, ",+", &saveptr);
  while (token) {
    while (*token == ' ' || *token == '\t')
      token++;
    if (token[0] != '\0' && is_engine_id_enabled(token)) {
      char **new_ids = realloc(ids, sizeof(char *) * (count + 1));
      if (new_ids) {
        ids = new_ids;
        ids[count] = strdup(token);
        count++;
      }
    }
    token = strtok_r(NULL, ",+", &saveptr);
  }

  free(copy);
  free(cookie);

  if (count == 0) {
    free(ids);
    return -1;
  }

  *out_ids = ids;
  *out_count = count;
  return 0;
}

int user_engines_contains(const char *engine_id, char **ids, int count) {
  if (!engine_id || !ids)
    return 0;
  for (int i = 0; i < count; i++) {
    if (strcasecmp(ids[i], engine_id) == 0)
      return 1;
  }
  return 0;
}

char *get_default_search_engine(const char *config_default) {
  char *cookie = get_cookie("default_engine");
  if (cookie &&
      (strcasecmp(cookie, "all") == 0 || is_engine_id_enabled(cookie))) {
    return cookie;
  }
  free(cookie);
  return strdup(config_default && config_default[0] != '\0' ? config_default
                                                           : "all");
}

