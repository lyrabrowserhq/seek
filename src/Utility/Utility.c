#include "Utility.h"
#include <beaker.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char global_default_locale[64] = "en_us";

void set_default_locale(const char *locale) {
  if (locale && locale[0] != '\0') {
    strncpy(global_default_locale, locale,
            sizeof(global_default_locale) - 1);
    global_default_locale[sizeof(global_default_locale) - 1] = '\0';
  }
}

static char *locale_from_accept_language(void) {
  const char *hdr = beaker_get_header("Accept-Language");
  if (!hdr || !hdr[0])
    return NULL;

  /* Best-first scan: find the highest-q tag that maps to a loaded locale.
     Tags are ordered by q; ties keep header order. */
  double best_q = -1.0;
  char best_id[64] = "";
  const char *p = hdr;
  while (*p) {
    while (*p == ' ' || *p == ',')
      p++;
    char tag[32];
    int tlen = 0;
    while (*p && *p != ';' && *p != ',' && tlen < (int)sizeof(tag) - 1)
      tag[tlen++] = *p++;
    tag[tlen] = '\0';
    double q = 1.0;
    if (*p == ';' && strncmp(p, ";q=", 3) == 0)
      q = strtod(p + 3, NULL);
    while (*p && *p != ',')
      p++;
    if (*p == ',')
      p++;
    if (tlen == 0)
      continue;
    for (int i = 0; tag[i]; i++) {
      if (tag[i] == '-')
        tag[i] = '_';
      else if (tag[i] >= 'A' && tag[i] <= 'Z')
        tag[i] += 32;
    }
    if (q > best_q && beaker_get_locale_meta(tag) != NULL) {
      best_q = q;
      snprintf(best_id, sizeof(best_id), "%s", tag);
      continue;
    }
    /* bare language like "de" -> first locale whose id starts with it */
    if (q > best_q && tlen == 2) {
      LocaleInfo infos[32];
      int n = beaker_get_all_locales(infos, 32);
      for (int i = 0; i < n; i++) {
        if (strncmp(infos[i].meta.id, tag, 2) == 0 &&
            infos[i].meta.id[2] == '_') {
          best_q = q;
          snprintf(best_id, sizeof(best_id), "%.*s",
                   (int)sizeof(best_id) - 1, infos[i].meta.id);
          break;
        }
      }
    }
  }
  return best_id[0] ? strdup(best_id) : NULL;
}

char *get_locale(const char *default_locale) {
  char *cookie = get_cookie("locale");
  if (cookie && beaker_get_locale_meta(cookie) != NULL) {
    return cookie;
  }
  free(cookie);
  char *accept = locale_from_accept_language();
  if (accept)
    return accept;
  const char *fallback =
      default_locale && default_locale[0] != '\0' ? default_locale
                                                   : global_default_locale;
  return strdup(fallback);
}

int hex_to_int(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

void free_string_matrix(char ***matrix, int *inner_counts, int row_count) {
  if (matrix) {
    for (int i = 0; i < row_count; i++) {
      int field_count = inner_counts ? inner_counts[i] : 0;
      for (int j = 0; j < field_count; j++)
        free(matrix[i][j]);
      free(matrix[i]);
    }
  }
  free(matrix);
  free(inner_counts);
}

void string_matrix_init(StringMatrix *matrix) {
  if (matrix)
    *matrix = (StringMatrix){0};
}

int string_matrix_append(StringMatrix *matrix, const char *const *values,
                         int field_count) {
  if (!matrix || !values || field_count <= 0)
    return -1;

  char **row = calloc((size_t)field_count, sizeof(*row));
  if (!row)
    return -1;

  for (int i = 0; i < field_count; i++) {
    row[i] = strdup(values[i] ? values[i] : "");
    if (!row[i]) {
      for (int j = 0; j < i; j++)
        free(row[j]);
      free(row);
      return -1;
    }
  }

  if (matrix->count == matrix->capacity) {
    int new_capacity = matrix->capacity ? matrix->capacity * 2 : 8;
    char ***new_rows = malloc(sizeof(*new_rows) * (size_t)new_capacity);
    int *new_counts = malloc(sizeof(*new_counts) * (size_t)new_capacity);
    if (!new_rows || !new_counts) {
      free(new_rows);
      free(new_counts);
      for (int i = 0; i < field_count; i++)
        free(row[i]);
      free(row);
      return -1;
    }
    if (matrix->count > 0) {
      memcpy(new_rows, matrix->rows, sizeof(*new_rows) * matrix->count);
      memcpy(new_counts, matrix->field_counts,
             sizeof(*new_counts) * matrix->count);
    }
    free(matrix->rows);
    free(matrix->field_counts);
    matrix->rows = new_rows;
    matrix->field_counts = new_counts;
    matrix->capacity = new_capacity;
  }

  matrix->rows[matrix->count] = row;
  matrix->field_counts[matrix->count] = field_count;
  matrix->count++;
  return 0;
}

void string_matrix_free(StringMatrix *matrix) {
  if (!matrix)
    return;
  free_string_matrix(matrix->rows, matrix->field_counts, matrix->count);
  *matrix = (StringMatrix){0};
}

void snippet_strip_feed_meta(char *s) {
  if (!s || !s[0])
    return;

  char *cut = strstr(s, "Article URL:");
  char *c2 = strstr(s, "Comments URL:");
  if (c2 && (!cut || c2 < cut))
    cut = c2;
  if (cut) {
    while (cut > s && (cut[-1] == ' ' || cut[-1] == '\n' || cut[-1] == '\r' ||
                       cut[-1] == '\t'))
      cut--;
    *cut = '\0';
  }

  char *pts = strstr(s, "Points:");
  if (pts) {
    char *line = pts;
    while (line > s && line[-1] != '\n')
      line--;
    if (line == pts || line[0] == 'P') {
      while (line > s && (line[-1] == ' ' || line[-1] == '\n' ||
                          line[-1] == '\r'))
        line--;
      *line = '\0';
    }
  }

  char *hashc = strstr(s, "# Comments:");
  if (hashc) {
    while (hashc > s && (hashc[-1] == ' ' || hashc[-1] == '\n' ||
                         hashc[-1] == '\r'))
      hashc--;
    *hashc = '\0';
  }

  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r')) {
    s[n - 1] = '\0';
    n--;
  }
}
