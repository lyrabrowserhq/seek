#include "Dictionary.h"
#include "../Cache/Cache.h"
#include "../Scraping/Scraping.h"
#include "../Utility/HttpClient.h"
#include "../Utility/XmlHelper.h"
#include "../Utility/HtmlEscape.h"
#include <ctype.h>
#include <curl/curl.h>
#include "../Scraping/Hdoc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *PREFIXES[] = {"what is the definition of ",
                                 "what's the definition of ",
                                 "what is the meaning of ",
                                 "what's the meaning of ",
                                 "what does the word ",
                                 "definition of ",
                                 "meaning of ",
                                 "def of ",
                                 "define ",
                                 "definition ",
                                 "define:",
                                 "def ",
                                 "def:",
                                 "what does ",
                                 "what is ",
                                 "what's ",
                                 "whats ",
                                 "meaning ",
                                 "dictionary ",
                                 "dict ",
                                 NULL};

static const char *SUFFIXES[] = {
    " definition",  " def",      " meaning", " mean",    " means",
    " dictionary",  " dict",     " define",  " defined", " definition?",
    " def?",        " meaning?", " mean?",   " means?",  " in english",
    " in english?", NULL};

static const char *SKIP_WORDS[] = {"of ", "the ", "a ", "an ", NULL};

static const char *strcasestr_impl(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle)
    return haystack;
  size_t len = strlen(needle);
  for (const char *h = haystack; *h; h++) {
    if (strncasecmp(h, needle, len) == 0)
      return h;
  }
  return NULL;
}

static char *build_html(const char *word, const char *pron, const char *pos,
                        const char *def, const char *ex) {
  char ew[256], ep[256], eo[128], ed[1024], ee[1024];
  if (word)
    html_escape_text(word, ew, sizeof(ew));
  if (pron)
    html_escape_text(pron, ep, sizeof(ep));
  if (pos)
    html_escape_text(pos, eo, sizeof(eo));
  if (def)
    html_escape_text(def, ed, sizeof(ed));
  if (ex)
    html_escape_text(ex, ee, sizeof(ee));
  char html[4096];
  int n = snprintf(html, sizeof(html),
                   "<div class=\"widget widget-dictionary\">");
  if (word)
    n += snprintf(html + n, sizeof(html) - n,
                  "<div class=\"widget-dictionary-word\">%s</div>", ew);
  if (pron)
    n += snprintf(html + n, sizeof(html) - n,
                  "<div class=\"widget-dictionary-pron\">/%s/</div>", ep);
  if (pos)
    n += snprintf(html + n, sizeof(html) - n,
                  "<div class=\"widget-dictionary-pos\">%s</div>", eo);
  if (def)
    n += snprintf(html + n, sizeof(html) - n,
                  "<div class=\"widget-dictionary-def\">%s</div>", ed);
  if (ex)
    n += snprintf(html + n, sizeof(html) - n,
                  "<div class=\"widget-dictionary-ex\">\"%s\"</div>", ee);
  snprintf(html + n, sizeof(html) - n, "</div>");
  return strdup(html);
}

static char *extract_word(const char *query) {
  if (!query)
    return NULL;

  const char *start = query;

  for (int i = 0; PREFIXES[i]; i++) {
    size_t len = strlen(PREFIXES[i]);
    if (strncasecmp(start, PREFIXES[i], len) == 0) {
      start += len;
      break;
    }
  }

  while (*start == ' ')
    start++;
  char *word = strdup(start);
  if (!word)
    return NULL;

  int changed = 1;
  while (changed) {
    changed = 0;
    for (int i = 0; SKIP_WORDS[i]; i++) {
      size_t len = strlen(SKIP_WORDS[i]);
      if (strncasecmp(word, SKIP_WORDS[i], len) == 0) {
        memmove(word, word + len, strlen(word + len) + 1);
        changed = 1;
        break;
      }
    }
  }

  changed = 1;
  while (changed) {
    changed = 0;
    for (int i = 0; SUFFIXES[i]; i++) {
      const char *found = strcasestr_impl(word, SUFFIXES[i]);
      if (found) {
        char *pos = word + (found - word);
        *pos = '\0';
        changed = 1;
        break;
      }
    }
  }

  size_t len = strlen(word);
  while (len > 0 && (word[len - 1] == ' ' || word[len - 1] == '?' ||
                     word[len - 1] == '!' || word[len - 1] == '.')) {
    word[--len] = '\0';
  }

  if (len == 0) {
    free(word);
    return NULL;
  }

  for (size_t i = 0; i < len; i++)
    word[i] = tolower((unsigned char)word[i]);
  char *space = strchr(word, ' ');
  if (space)
    *space = '\0';

  return word;
}

int is_dictionary_query(const char *query) {
  if (!query)
    return 0;

  for (int i = 0; PREFIXES[i]; i++) {
    size_t len = strlen(PREFIXES[i]);
    if (strncasecmp(query, PREFIXES[i], len) == 0) {
      const char *after = query + len;
      while (*after == ' ')
        after++;
      if (*after != '\0')
        return 1;
    }
  }

  for (int i = 0; SUFFIXES[i]; i++) {
    const char *pos = strcasestr_impl(query, SUFFIXES[i]);
    if (pos) {
      const char *after = pos + strlen(SUFFIXES[i]);
      while (*after == ' ' || *after == '?' || *after == '!' || *after == '.')
        after++;
      if (*after == '\0' && pos > query && (pos - query) < 100)
        return 1;
    }
  }

  if (strncasecmp(query, "what is ", 8) == 0 ||
      strncasecmp(query, "what's ", 7) == 0 ||
      strncasecmp(query, "whats ", 6) == 0) {
    const char *word = query + (strncasecmp(query, "what is ", 8) == 0  ? 8
                                : strncasecmp(query, "what's ", 7) == 0 ? 7
                                                                        : 6);
    const char *articles[] = {"the ",   "your ", "my ",   "his ",  "her ",
                              "their ", "our ",  "this ", "that ", "these ",
                              "those ", "a ",    "an ",   NULL};
    for (int i = 0; articles[i]; i++) {
      if (strncasecmp(word, articles[i], strlen(articles[i])) == 0)
        return 0;
    }
    const char *space = strchr(word, ' ');
    if (!space || *(space + 1) == '\0' || *(space + 1) == '?')
      return 1;
  }

  return 0;
}

char *construct_dictionary_url(const char *query) {
  char *word = extract_word(query);
  if (!word)
    return NULL;

  char *escaped = curl_easy_escape(NULL, word, 0);
  const char *base = "https://dictionary.cambridge.org/dictionary/english/";
  char *url = malloc(strlen(base) + strlen(escaped) + 1);
  if (url) {
    strcpy(url, base);
    strcat(url, escaped);
  }

  curl_free(escaped);
  free(word);
  return url;
}

static char *find1_text(Hdoc *d, const char *css) {
  Hnode *n = hdoc_find1(d, NULL, css);
  return n ? hnode_text(n) : NULL;
}

static InfoBox dict_parse_html(const char *html, size_t len,
                               const char *page_url) {
  InfoBox info = {NULL, NULL, NULL, NULL};
  Hdoc *doc = hdoc_parse(html, len);
  if (!doc)
    return info;

  char *word = find1_text(doc, "span.hw.dhw");
  char *pron =
      find1_text(doc, "span.us.dpron-i span.ipa.dipa");
  char *pos = find1_text(doc, "span.pos.dpos");
  char *def = find1_text(doc, "div.def.ddef_d.db");
  char *ex = find1_text(doc, "span.eg.deg");

  if (word && def) {
    info.title = strdup("Dictionary");
    info.extract = build_html(word, pron, pos, def, ex);
    info.thumbnail_url = strdup("/static/dictionary.jpg");
    info.url = strdup(page_url);
  }
  free(word);
  free(pron);
  free(pos);
  free(def);
  free(ex);
  hdoc_free(doc);
  return info;
}

InfoBox fetch_dictionary_data(const char *query) {
  InfoBox info = {NULL, NULL, NULL, NULL};

  char *url = construct_dictionary_url(query);
  if (!url)
    return info;

  char *cache_key = cache_compute_key(url, 0, "dictionary");
  if (cache_key && get_cache_ttl_infobox() > 0) {
    char *cached_data = NULL;
    size_t cached_size = 0;
    if (cache_get(cache_key, (time_t)get_cache_ttl_infobox(), &cached_data,
                  &cached_size) == 0 &&
        cached_data && cached_size > 0) {
      info = dict_parse_html(cached_data, cached_size, url);
      free(cached_data);
      free(cache_key);
      free(url);
      return info;
    }
    free(cached_data);
  }
  free(cache_key);

  HttpResponse resp = http_get(url, "Mozilla/5.0");
  if (resp.memory && resp.size > 0) {
    cache_key = cache_compute_key(url, 0, "dictionary");
    if (cache_key && get_cache_ttl_infobox() > 0) {
      cache_set(cache_key, resp.memory, resp.size);
    }
    free(cache_key);

    info = dict_parse_html(resp.memory, resp.size, url);
  }

  http_response_free(&resp);
  free(url);
  return info;
}
