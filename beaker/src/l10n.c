#include "../beaker.h"
#include "beaker_globals.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  SECTION_NONE,
  SECTION_META,
  SECTION_KEYS,
} IniSection;

static char *trim(char *str) {
  if (str == NULL)
    return NULL;
  while (*str == ' ' || *str == '\t')
    str++;
  if (*str == '\0')
    return str;
  char *end = str + strlen(str) - 1;
  while (end > str &&
         (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
    *end = '\0';
    end--;
  }
  return str;
}

static void unquote_value(char *value) {
  size_t len = strlen(value);
  if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
    memmove(value, value + 1, len - 2);
    value[len - 2] = '\0';
  }
}

static int safe_grow_capacity(int current, int hard_max) {
  int new_cap;
  if (current == 0) {
    new_cap = (hard_max < INITIAL_LOCALE_KEYS_CAPACITY)
                  ? hard_max
                  : INITIAL_LOCALE_KEYS_CAPACITY;
  } else {
    new_cap = current * 2;
    if (new_cap > hard_max)
      new_cap = hard_max;
  }
  return new_cap;
}

static int parse_ini_file(const char *path, Locale *locale) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    beaker_log("ERROR", "parse_ini_file: Could not open '%s': %s\n", path,
               strerror(errno));
    return -1;
  }

  memset(&locale->meta, 0, sizeof(locale->meta));
  locale->keys = NULL;
  locale->key_count = 0;
  locale->key_capacity = 0;
  IniSection current_section = SECTION_NONE;
  char line[BUFFER_SIZE];

  while (fgets(line, sizeof(line), fp) != NULL) {
    char *trimmed = trim(line);

    if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') {
      continue;
    }

    if (*trimmed == '[') {
      char *close = strchr(trimmed, ']');
      if (close == NULL)
        continue;
      *close = '\0';
      char *section_name = trimmed + 1;
      section_name = trim(section_name);

      if (strcmp(section_name, "Meta") == 0) {
        current_section = SECTION_META;
      } else if (strcmp(section_name, "Keys") == 0) {
        current_section = SECTION_KEYS;
      } else {
        current_section = SECTION_NONE;
      }
      continue;
    }

    char *eq = strchr(trimmed, '=');
    if (eq == NULL)
      continue;

    *eq = '\0';
    char *key = trim(trimmed);
    char *value = trim(eq + 1);
    unquote_value(value);

    if (current_section == SECTION_META) {
      if (strcmp(key, "Id") == 0) {
        strncpy(locale->meta.id, value, MAX_LOCALE_ID_LEN - 1);
        locale->meta.id[MAX_LOCALE_ID_LEN - 1] = '\0';
      } else if (strcmp(key, "Name") == 0) {
        strncpy(locale->meta.name, value, MAX_VALUE_LEN - 1);
        locale->meta.name[MAX_VALUE_LEN - 1] = '\0';
      } else if (strcmp(key, "Direction") == 0) {
        strncpy(locale->meta.direction, value,
                sizeof(locale->meta.direction) - 1);
        locale->meta.direction[sizeof(locale->meta.direction) - 1] = '\0';
      }
    } else if (current_section == SECTION_KEYS) {
      if (locale->key_count >= locale->key_capacity) {
        int new_cap =
            safe_grow_capacity(locale->key_capacity, MAX_LOCALE_KEYS_HARD);
        if (new_cap <= locale->key_count) {
          beaker_log("WARN",
                     "parse_ini_file: Hard key limit (%d) reached in '%s', "
                     "skipping key '%s'.\n",
                     MAX_LOCALE_KEYS_HARD, path, key);
          continue;
        }
        LocaleKV *new_keys =
            realloc(locale->keys, (size_t)new_cap * sizeof(LocaleKV));
        if (new_keys == NULL) {
          beaker_log(
              "ERROR",
              "parse_ini_file: Memory allocation failed for keys in '%s'.\n",
              path);
          continue;
        }
        locale->keys = new_keys;
        locale->key_capacity = new_cap;
      }
      LocaleKV *kv = &locale->keys[locale->key_count];
      strncpy(kv->key, key, MAX_KEY_LEN - 1);
      kv->key[MAX_KEY_LEN - 1] = '\0';
      strncpy(kv->value, value, MAX_LOCALE_VALUE_LEN - 1);
      kv->value[MAX_LOCALE_VALUE_LEN - 1] = '\0';
      locale->key_count++;
    }
  }

  fclose(fp);

  if (locale->meta.id[0] == '\0') {
    beaker_log("WARN", "parse_ini_file: No Id in [Meta] section of '%s'.\n",
               path);
    return -1;
  }

  return 0;
}

int beaker_load_locales(void) {
  locale_count = 0;

  DIR *dir = opendir(LOCALES_DIR);
  if (dir == NULL) {
    beaker_log("ERROR",
               "beaker_load_locales: Could not open directory '%s': %s\n",
               LOCALES_DIR, strerror(errno));
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    size_t name_len = strlen(name);

    if (name_len < 5 || strcmp(name + name_len - 4, ".ini") != 0) {
      continue;
    }

    if (locale_count >= locale_capacity) {
      int new_cap = safe_grow_capacity(locale_capacity, MAX_LOCALES_HARD);
      if (new_cap <= locale_count) {
        beaker_log("WARN",
                   "beaker_load_locales: Hard locale limit (%d) reached, "
                   "skipping '%s'.\n",
                   MAX_LOCALES_HARD, name);
        break;
      }
      Locale *new_locales = realloc(locales, (size_t)new_cap * sizeof(Locale));
      if (new_locales == NULL) {
        beaker_log(
            "ERROR",
            "beaker_load_locales: Memory allocation failed for locales.\n");
        break;
      }
      locales = new_locales;
      locale_capacity = new_cap;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s%s", LOCALES_DIR, name);

    Locale *locale = &locales[locale_count];
    if (parse_ini_file(path, locale) == 0) {
      beaker_log("DEBUG", "beaker_load_locales: Loaded locale '%s' from '%s'",
                 locale->meta.id, name);
      locale_count++;
    } else {
      free(locale->keys);
      locale->keys = NULL;
      locale->key_count = 0;
      locale->key_capacity = 0;
    }
  }

  closedir(dir);
  return locale_count;
}

void beaker_set_locale(TemplateContext *ctx, const char *locale_id) {
  if (ctx == NULL || locale_id == NULL) {
    beaker_log("ERROR",
               "beaker_set_locale: Invalid NULL input (ctx or locale_id).\n");
    return;
  }

  context_set(ctx, "__locale", locale_id);

  const LocaleMeta *meta = beaker_get_locale_meta(locale_id);
  if (meta != NULL) {
    context_set(ctx, "__locale_id", meta->id);
    context_set(ctx, "__locale_name", meta->name);
    context_set(ctx, "__locale_direction", meta->direction);
  } else {
    beaker_log("WARN",
               "beaker_set_locale: Locale '%s' not found. Context vars "
               "not set.\n",
               locale_id);
  }
}

int beaker_get_all_locales(LocaleInfo *out, int max_count) {
  if (out == NULL || max_count <= 0) {
    return 0;
  }

  int count = (locale_count < max_count) ? locale_count : max_count;
  for (int i = 0; i < count; i++) {
    out[i].meta = locales[i].meta;
  }
  return count;
}

const LocaleMeta *beaker_get_locale_meta(const char *locale_id) {
  if (locale_id == NULL) {
    return NULL;
  }
  for (int i = 0; i < locale_count; i++) {
    if (strcmp(locales[i].meta.id, locale_id) == 0) {
      return &locales[i].meta;
    }
  }
  return NULL;
}

const char *beaker_get_locale_value(const char *locale_id, const char *key) {
  if (locale_id == NULL || key == NULL) {
    return NULL;
  }
  for (int i = 0; i < locale_count; i++) {
    if (strcmp(locales[i].meta.id, locale_id) == 0) {
      for (int j = 0; j < locales[i].key_count; j++) {
        if (strcmp(locales[i].keys[j].key, key) == 0) {
          return locales[i].keys[j].value;
        }
      }
      return NULL;
    }
  }
  return NULL;
}

void beaker_free_locales(void) {
  for (int i = 0; i < locale_count; i++) {
    free(locales[i].keys);
    locales[i].keys = NULL;
    locales[i].key_count = 0;
    locales[i].key_capacity = 0;
  }
  free(locales);
  locales = NULL;
  locale_count = 0;
  locale_capacity = 0;
}
