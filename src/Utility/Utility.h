#ifndef UTILITY_H
#define UTILITY_H

#define LINK_FIELD_COUNT 3

typedef struct {
  char ***rows;
  int *field_counts;
  int count;
  int capacity;
} StringMatrix;

int hex_to_int(char c);

void set_default_locale(const char *locale);
char *get_locale(const char *default_locale);

int is_engine_id_enabled(const char *engine_id);
int get_user_engines(char ***out_ids, int *out_count);
int user_engines_contains(const char *engine_id, char **ids, int count);
char *get_default_search_engine(const char *config_default);

void free_string_matrix(char ***matrix, int *inner_counts, int row_count);

void string_matrix_init(StringMatrix *matrix);
int string_matrix_append(StringMatrix *matrix, const char *const *values,
                         int field_count);
void string_matrix_free(StringMatrix *matrix);

void snippet_strip_feed_meta(char *s);

#endif
