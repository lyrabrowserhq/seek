#include "Today.h"
#include "../Cache/Cache.h"
#include "../Utility/HttpClient.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TODAY_MAX 6

static char *dup_json_str(const char *p) {
  if (!p)
    return NULL;
  while (*p == ' ' || *p == '\n')
    p++;
  if (*p != '"')
    return NULL;
  p++;
  const char *s = p;
  while (*p && *p != '"') {
    if (*p == '\\' && p[1])
      p += 2;
    else
      p++;
  }
  size_t n = (size_t)(p - s);
  char *out = malloc(n + 1);
  if (!out)
    return NULL;
  char *w = out;
  for (size_t i = 0; i < n; i++) {
    if (s[i] == '\\' && i + 1 < n) {
      i++;
      if (s[i] == 'n' || s[i] == 'r' || s[i] == 't')
        *w++ = ' ';
      else
        *w++ = s[i];
    } else {
      *w++ = s[i];
    }
  }
  *w = '\0';
  return out;
}

static const char *object_end(const char *obj) {
  if (!obj || *obj != '{')
    return obj;
  int depth = 0;
  int in_str = 0;
  const char *p = obj;
  while (*p) {
    if (in_str) {
      if (*p == '\\' && p[1]) {
        p += 2;
        continue;
      }
      if (*p == '"')
        in_str = 0;
      p++;
      continue;
    }
    if (*p == '"') {
      in_str = 1;
      p++;
      continue;
    }
    if (*p == '{')
      depth++;
    else if (*p == '}') {
      depth--;
      if (depth == 0)
        return p + 1;
    }
    p++;
  }
  return p;
}

static char *first_page_url(const char *obj, const char *end) {
  const char *pages = strstr(obj, "\"content_urls\"");
  if (!pages || pages >= end)
    return NULL;
  const char *page = strstr(pages, "\"page\"");
  if (!page || page >= end)
    return NULL;
  const char *colon = strchr(page, ':');
  if (!colon || colon >= end)
    return NULL;
  return dup_json_str(colon + 1);
}

static int take_array(const char *json, const char *key, const char *kind,
                      char **rows, int *n, int max_items) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *sec = strstr(json, pat);
  if (!sec)
    return 0;
  const char *br = strchr(sec, '[');
  if (!br)
    return 0;
  const char *p = br + 1;
  int added = 0;
  while (*p && *n < max_items && added < 3) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj);
    const char *tk = strstr(obj, "\"text\"");
    if (tk && tk < end) {
      const char *colon = strchr(tk, ':');
      char *text = colon && colon < end ? dup_json_str(colon + 1) : NULL;
      char *url = first_page_url(obj, end);
      const char *yk = strstr(obj, "\"year\"");
      char yearbuf[16] = "";
      if (yk && yk < end) {
        const char *yc = strchr(yk, ':');
        if (yc)
          snprintf(yearbuf, sizeof(yearbuf), "%d", atoi(yc + 1));
      }
      if (text && text[0]) {
        rows[*n] = (char *)malloc(sizeof(char *) * 3);
        char **row = (char **)rows[*n];
        if (row) {
          if (yearbuf[0]) {
            size_t m = strlen(text) + 16;
            char *t = malloc(m);
            if (t)
              snprintf(t, m, "%s. %s", yearbuf, text);
            free(text);
            text = t ? t : strdup("");
          }
          row[0] = text;
          row[1] = url ? url : strdup("https://en.wikipedia.org/wiki/Main_Page");
          row[2] = strdup(kind);
          (*n)++;
          added++;
        } else {
          free(text);
          free(url);
        }
      } else {
        free(text);
        free(url);
      }
    }
    p = end;
  }
  return added;
}

int today_parse_feed(const char *json, char ***out_rows, int *out_n,
                     int max_items) {
  *out_rows = NULL;
  *out_n = 0;
  if (!json || max_items <= 0)
    return -1;
  char **rows = calloc((size_t)max_items, sizeof(char *));
  if (!rows)
    return -1;
  int n = 0;
  take_array(json, "holidays", "holiday", rows, &n, max_items);
  take_array(json, "selected", "history", rows, &n, max_items);
  if (n == 0)
    take_array(json, "events", "history", rows, &n, max_items);
  *out_rows = rows;
  *out_n = n;
  return n;
}

void today_items_free(TodayItems *items) {
  if (!items)
    return;
  for (int i = 0; i < items->n; i++) {
    char **row = (char **)items->rows[i];
    if (row) {
      free(row[0]);
      free(row[1]);
      free(row[2]);
      free(row);
    }
  }
  free(items->rows);
  free(items->inner);
  items->rows = NULL;
  items->inner = NULL;
  items->n = 0;
}

int today_load(TodayItems *out) {
  memset(out, 0, sizeof(*out));
  time_t now = time(NULL);
  struct tm tm;
  gmtime_r(&now, &tm);
  char url[160];
  snprintf(url, sizeof(url),
           "https://en.wikipedia.org/api/rest_v1/feed/onthisday/all/%02d/%02d",
           tm.tm_mon + 1, tm.tm_mday);
  char cache_key[64];
  snprintf(cache_key, sizeof(cache_key), "today-%02d-%02d", tm.tm_mon + 1,
           tm.tm_mday);
  HttpResponse resp = {0};
  int from_cache = 0;
  if (get_cache_ttl_infobox() > 0 &&
      cache_get(cache_key, (time_t)get_cache_ttl_infobox(), &resp.memory,
                &resp.size) == 0 &&
      resp.memory)
    from_cache = 1;
  if (!from_cache) {
    resp = http_get(url, "Seek/1.0 (https://seek.lyrabrowser.com)");
    if (resp.memory && get_cache_ttl_infobox() > 0)
      cache_set(cache_key, resp.memory, resp.size);
  }
  if (!resp.memory)
    return 0;
  int n = 0;
  char **rows = NULL;
  today_parse_feed(resp.memory, &rows, &n, TODAY_MAX);
  http_response_free(&resp);
  if (n <= 0) {
    free(rows);
    return 0;
  }
  int *inner = malloc(sizeof(int) * (size_t)n);
  if (!inner) {
    TodayItems tmp = {.rows = rows, .n = n, .inner = NULL};
    today_items_free(&tmp);
    return 0;
  }
  for (int i = 0; i < n; i++)
    inner[i] = 3;
  out->rows = rows;
  out->inner = inner;
  out->n = n;
  return n;
}

void today_apply(TemplateContext *ctx, TodayItems *items) {
  if (!ctx || !items || items->n <= 0)
    return;
  context_set(ctx, "today_heading", "Today");
  context_set_array_of_arrays(ctx, "today_items", (char ***)items->rows,
                              items->n, items->inner);
}

int query_wants_today(const char *query) {
  if (!query || !query[0])
    return 0;
  char q[256];
  size_t n = 0;
  for (; query[n] && n + 1 < sizeof(q); n++)
    q[n] = (char)tolower((unsigned char)query[n]);
  q[n] = '\0';
  while (n > 0 && isspace((unsigned char)q[n - 1]))
    q[--n] = '\0';
  if (strcmp(q, "today") == 0)
    return 1;
  if (strstr(q, "on this day"))
    return 1;
  if (strstr(q, "this day in history"))
    return 1;
  if (strstr(q, "what is today") || strstr(q, "what's today") ||
      strstr(q, "whats today"))
    return 1;
  if (strstr(q, "what day is"))
    return 1;
  if (strstr(q, "today's date") || strstr(q, "todays date"))
    return 1;
  if (strstr(q, "what happened today"))
    return 1;
  if (strstr(q, "history today"))
    return 1;
  return 0;
}
