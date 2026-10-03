#include "JsonHelper.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_whitespace_json(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int json_parse_float_map(const char *json, const char *target_key,
                         JsonFloatMap *result) {
  memset(result, 0, sizeof(JsonFloatMap));

  if (!json || !target_key)
    return 0;

  const char *obj_start = strchr(json, '{');
  if (!obj_start)
    return 0;

  const char *rates_start = strstr(obj_start, target_key);
  if (!rates_start)
    return 0;

  rates_start = strchr(rates_start, '{');
  if (!rates_start)
    return 0;

  const char *p = rates_start + 1;
  while (*p && *p != '}') {
    while (*p && (*p == ' ' || *p == '\n' || *p == '\t' || *p == ','))
      p++;

    if (*p == '}')
      break;

    if (*p != '"')
      break;
    p++;

    char key[32] = {0};
    size_t key_len = 0;
    while (*p && *p != '"' && key_len < 31) {
      key[key_len++] = *p++;
    }
    if (*p != '"')
      break;
    p++;

    while (*p && *p != ':')
      p++;
    if (*p != ':')
      break;
    p++;

    while (*p && is_whitespace_json(*p))
      p++;

    double value = 0;
    int has_digit = 0;
    while (*p >= '0' && *p <= '9') {
      value = value * 10 + (*p - '0');
      has_digit = 1;
      p++;
    }
    if (*p == '.') {
      p++;
      double frac = 0.1;
      while (*p >= '0' && *p <= '9') {
        value += (*p - '0') * frac;
        frac *= 0.1;
        has_digit = 1;
        p++;
      }
    }

    if (has_digit && key_len > 0) {
      memcpy(result->keys[result->count], key, key_len);
      result->keys[result->count][key_len] = '\0';
      result->values[result->count] = value;
      result->count++;
      if (result->count >= 256)
        break;
    }
  }

  return result->count > 0;
}

double json_get_float(const char *json, const char *key) {
  if (!json || !key)
    return 0;

  const char *key_pos = strstr(json, key);
  if (!key_pos)
    return 0;

  const char *colon = strchr(key_pos, ':');
  if (!colon)
    return 0;

  colon++;
  while (*colon && is_whitespace_json(*colon))
    colon++;

  return strtod(colon, NULL);
}

char *json_get_string(const char *json, const char *key) {
  if (!json || !key)
    return NULL;

  static char buffer[256];

  const char *key_pos = strstr(json, key);
  if (!key_pos)
    return NULL;

  const char *colon = strchr(key_pos, ':');
  if (!colon)
    return NULL;

  colon++;
  while (*colon && is_whitespace_json(*colon))
    colon++;

  if (*colon != '"')
    return NULL;
  colon++;

  size_t len = 0;
  while (*colon && *colon != '"' && len < 255) {
    buffer[len++] = *colon++;
  }
  buffer[len] = '\0';

  return buffer;
}

char *json_escape_text(const char *s) {
  size_t n = 0;
  const char *p = s ? s : "";
  for (; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '"' || c == '\\')
      n += 2;
    else if (c < 0x20)
      n += 6;
    else
      n++;
  }
  char *out = malloc(n + 1);
  if (!out)
    return NULL;
  char *w = out;
  for (p = s ? s : ""; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '"') {
      *w++ = '\\';
      *w++ = '"';
    } else if (c == '\\') {
      *w++ = '\\';
      *w++ = '\\';
    } else if (c == '\n') {
      memcpy(w, "\\n", 2);
      w += 2;
    } else if (c == '\r') {
      memcpy(w, "\\r", 2);
      w += 2;
    } else if (c == '\t') {
      memcpy(w, "\\t", 2);
      w += 2;
    } else if (c < 0x20) {
      w += sprintf(w, "\\u%04x", c);
    } else {
      *w++ = (char)c;
    }
  }
  *w = '\0';
  return out;
}

char *json_string_value(const char *s) {
  char *escaped = json_escape_text(s);
  if (!escaped)
    return strdup("\"\"");
  size_t len = strlen(escaped);
  char *out = malloc(len + 3);
  if (!out) {
    free(escaped);
    return NULL;
  }
  snprintf(out, len + 3, "\"%s\"", escaped);
  free(escaped);
  return out;
}
