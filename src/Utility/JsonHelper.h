#ifndef JSONHELPER_H
#define JSONHELPER_H

#include <stddef.h>

typedef struct {
  double values[256];
  char keys[256][32];
  size_t count;
} JsonFloatMap;

int json_parse_float_map(const char *json, const char *target_key,
                         JsonFloatMap *result);
double json_get_float(const char *json, const char *key);
char *json_get_string(const char *json, const char *key);
char *json_escape_text(const char *s);
char *json_string_value(const char *s);

#endif
