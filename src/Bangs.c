#include "Bangs.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_BANGS 96

static BangEntry bang_table[MAX_BANGS];
static int bang_count;

static void bang_add(const char *name, const char *tmpl) {
  if (!name || !name[0] || !tmpl || !tmpl[0] || bang_count >= MAX_BANGS)
    return;
  strncpy(bang_table[bang_count].name, name, sizeof(bang_table[bang_count].name) - 1);
  bang_table[bang_count].name[sizeof(bang_table[bang_count].name) - 1] = '\0';
  strncpy(bang_table[bang_count].tmpl, tmpl, sizeof(bang_table[bang_count].tmpl) - 1);
  bang_table[bang_count].tmpl[sizeof(bang_table[bang_count].tmpl) - 1] = '\0';
  bang_count++;
}

static void bang_defaults(void) {
  bang_add("w", "https://en.wikipedia.org/wiki/Special:Search?search=%s");
  bang_add("wiki", "https://en.wikipedia.org/wiki/Special:Search?search=%s");
  bang_add("wikipedia", "https://en.wikipedia.org/wiki/Special:Search?search=%s");
  bang_add("gh", "https://github.com/search?q=%s&type=repositories");
  bang_add("github", "https://github.com/search?q=%s&type=repositories");
  bang_add("gl", "https://gitlab.com/search?search=%s");
  bang_add("gitlab", "https://gitlab.com/search?search=%s");
  bang_add("r", "https://www.reddit.com/search/?q=%s");
  bang_add("reddit", "https://www.reddit.com/search/?q=%s");
  bang_add("m", "https://www.openstreetmap.org/search?query=%s");
  bang_add("maps", "https://www.openstreetmap.org/search?query=%s");
  bang_add("osm", "https://www.openstreetmap.org/search?query=%s");
  bang_add("yt", "https://www.youtube.com/results?search_query=%s");
  bang_add("youtube", "https://www.youtube.com/results?search_query=%s");
  bang_add("so", "https://stackoverflow.com/search?q=%s");
  bang_add("aw", "https://wiki.archlinux.org/index.php?search=%s");
  bang_add("archwiki", "https://wiki.archlinux.org/index.php?search=%s");
  bang_add("tw", "https://twitter.com/search?q=%s");
  bang_add("x", "https://twitter.com/search?q=%s");
}

static int bang_override(const char *name, const char *tmpl) {
  for (int i = 0; i < bang_count; i++) {
    if (strcasecmp(bang_table[i].name, name) == 0) {
      strncpy(bang_table[i].tmpl, tmpl, sizeof(bang_table[i].tmpl) - 1);
      bang_table[i].tmpl[sizeof(bang_table[i].tmpl) - 1] = '\0';
      return 1;
    }
  }
  return 0;
}

static void bang_load_file(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "[WARN] Could not open bangs_file: %s\n", path);
    return;
  }

  char section[64] = "";
  char line[896];
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = 0;
    if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
      continue;
    if (line[0] == '[') {
      char *end = strchr(line, ']');
      if (end) {
        *end = '\0';
        size_t sl = strlen(line + 1);
        if (sl > sizeof(section) - 1)
          sl = sizeof(section) - 1;
        memcpy(section, line + 1, sl);
        section[sl] = '\0';
      }
      continue;
    }
    if (strcasecmp(section, "bangs") != 0)
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    while (*key == ' ' || *key == '\t')
      key++;
    while (*val == ' ' || *val == '\t')
      val++;
    char *ke = key + strlen(key) - 1;
    while (ke > key && (*ke == ' ' || *ke == '\t')) {
      *ke = '\0';
      ke--;
    }
    char *ve = val + strlen(val) - 1;
    while (ve > val && (*ve == ' ' || *ve == '\t' || *ve == '"' ||
                       *ve == '\'')) {
      *ve = '\0';
      ve--;
    }
    while (*val == '"' || *val == '\'')
      val++;
    if (!key[0] || !val[0])
      continue;
    if (!strstr(val, "%s")) {
      fprintf(stderr, "[WARN] Bang %s must contain %%s; skipped\n", key);
      continue;
    }
    if (!bang_override(key, val))
      bang_add(key, val);
  }
  fclose(fp);
}

void bangs_init(const char *optional_config_path) {
  bang_count = 0;
  bang_defaults();
  if (optional_config_path && optional_config_path[0])
    bang_load_file(optional_config_path);
}

char *bang_resolve_redirect_url(const char *query) {
  if (!query)
    return NULL;

  const char *p = query;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != '!')
    return NULL;
  p++;

  const char *tok_start = p;
  while (*p && *p != ' ' && *p != '\t')
    p++;
  size_t tok_len = (size_t)(p - tok_start);
  if (tok_len == 0 || tok_len >= BANG_NAME_LEN)
    return NULL;

  char token[BANG_NAME_LEN];
  memcpy(token, tok_start, tok_len);
  token[tok_len] = '\0';

  while (*p == ' ' || *p == '\t')
    p++;
  const char *rest = p;

  const char *tmpl = NULL;
  for (int i = 0; i < bang_count; i++) {
    if (strcasecmp(bang_table[i].name, token) == 0) {
      tmpl = bang_table[i].tmpl;
      break;
    }
  }
  if (!tmpl)
    return NULL;

  char *enc = curl_easy_escape(NULL, rest, 0);
  if (!enc)
    return NULL;

  char *out = malloc(BANG_TMPL_LEN + strlen(enc) + 8);
  if (!out) {
    curl_free(enc);
    return NULL;
  }
  snprintf(out, BANG_TMPL_LEN + strlen(enc) + 8, tmpl, enc);
  curl_free(enc);
  return out;
}

int bangs_count(void) { return bang_count; }

const BangEntry *bangs_at(int index) {
  if (index < 0 || index >= bang_count)
    return NULL;
  return &bang_table[index];
}
