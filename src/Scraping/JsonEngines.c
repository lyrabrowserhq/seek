#include "JsonEngines.h"
#include "../Utility/Utility.h"
#include "../Utility/XmlHelper.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *json_unescape_range(const char *s, size_t n) {
  char *out = malloc(n + 1);
  if (!out)
    return NULL;
  char *w = out;
  for (size_t i = 0; i < n; i++) {
    if (s[i] == '\\' && i + 1 < n) {
      i++;
      switch (s[i]) {
      case 'n':
        *w++ = '\n';
        break;
      case 'r':
        *w++ = '\r';
        break;
      case 't':
        *w++ = '\t';
        break;
      case '"':
      case '\\':
      case '/':
        *w++ = s[i];
        break;
      case 'u':
        if (i + 4 < n) {
          *w++ = '?';
          i += 4;
        }
        break;
      default:
        *w++ = s[i];
        break;
      }
    } else {
      *w++ = s[i];
    }
  }
  *w = '\0';
  return out;
}

static char *strip_tags(const char *in) {
  if (!in)
    return strdup("");
  size_t n = strlen(in);
  char *out = malloc(n + 1);
  if (!out)
    return NULL;
  size_t w = 0;
  int tag = 0;
  for (size_t i = 0; i < n; i++) {
    if (in[i] == '<') {
      tag = 1;
      continue;
    }
    if (in[i] == '>') {
      tag = 0;
      continue;
    }
    if (!tag)
      out[w++] = in[i];
  }
  out[w] = '\0';
  return out;
}

static const char *find_key(const char *p, const char *end, const char *key) {
  char pat[80];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t klen = strlen(pat);
  while (p && p < end) {
    const char *hit = NULL;
    size_t room = (size_t)(end - p);
    if (room < klen)
      return NULL;
    const char *scan = p;
    while ((size_t)(end - scan) >= klen) {
      if (memcmp(scan, pat, klen) == 0) {
        hit = scan;
        break;
      }
      scan++;
    }
    if (!hit)
      return NULL;
    const char *q = hit + klen;
    while (q < end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r'))
      q++;
    if (q < end && *q == ':')
      return q + 1;
    p = hit + 1;
  }
  return NULL;
}

static char *dup_json_string_at(const char *p, const char *end) {
  if (!p)
    return NULL;
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    p++;
  if (p >= end || *p != '"')
    return NULL;
  p++;
  const char *s = p;
  while (p < end && *p) {
    if (*p == '\\' && p + 1 < end) {
      p += 2;
      continue;
    }
    if (*p == '"')
      break;
    p++;
  }
  return json_unescape_range(s, (size_t)(p - s));
}

static char *dup_key_in(const char *obj, const char *end, const char *key) {
  const char *p = find_key(obj, end, key);
  if (!p)
    return NULL;
  return dup_json_string_at(p, end);
}

static const char *object_end(const char *obj, const char *lim) {
  if (!obj || *obj != '{')
    return obj;
  int depth = 0;
  int in_str = 0;
  const char *p = obj;
  while (p < lim && *p) {
    if (in_str) {
      if (*p == '\\' && p + 1 < lim) {
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
  return lim;
}

static char *wiki_article_url(const char *prefix, const char *title) {
  if (!prefix || !title)
    return NULL;
  char *enc = NULL;
  CURL *c = curl_easy_init();
  if (c) {
    char spaced[512];
    snprintf(spaced, sizeof(spaced), "%s", title);
    for (char *p = spaced; *p; p++) {
      if (*p == ' ')
        *p = '_';
    }
    enc = curl_easy_escape(c, spaced, 0);
  }
  char url[1024];
  snprintf(url, sizeof(url), "%s%s", prefix, enc ? enc : title);
  if (enc)
    curl_free(enc);
  if (c)
    curl_easy_cleanup(c);
  return strdup(url);
}

static const char *wiki_prefix_for(const char *id) {
  if (!id)
    return "https://en.wikipedia.org/wiki/";
  if (!strcmp(id, "wikipedia"))
    return "https://en.wikipedia.org/wiki/";
  if (!strcmp(id, "wiktionary"))
    return "https://en.wiktionary.org/wiki/";
  if (!strcmp(id, "archwiki"))
    return "https://wiki.archlinux.org/title/";
  if (!strcmp(id, "debianwiki"))
    return "https://wiki.debian.org/";
  if (!strcmp(id, "gentoo"))
    return "https://wiki.gentoo.org/wiki/";
  if (!strcmp(id, "nixos"))
    return "https://wiki.nixos.org/wiki/";
  if (!strcmp(id, "mediawiki"))
    return "https://www.mediawiki.org/wiki/";
  if (!strcmp(id, "wikibooks"))
    return "https://en.wikibooks.org/wiki/";
  if (!strcmp(id, "wikiquote"))
    return "https://en.wikiquote.org/wiki/";
  return "https://en.wikipedia.org/wiki/";
}

static int collect(SearchResult **out, int max_results, SearchResult *tmp,
                   int n) {
  if (n <= 0) {
    *out = NULL;
    return 0;
  }
  SearchResult *arr = alloc_results_array(n, max_results);
  if (!arr) {
    for (int i = 0; i < n; i++) {
      free(tmp[i].url);
      free(tmp[i].title);
      free(tmp[i].snippet);
    }
    return 0;
  }
  int cap = n < max_results ? n : max_results;
  for (int i = 0; i < cap; i++) {
    snippet_strip_feed_meta(tmp[i].snippet);
    arr[i] = tmp[i];
  }
  for (int i = cap; i < n; i++) {
    free(tmp[i].url);
    free(tmp[i].title);
    free(tmp[i].snippet);
  }
  *out = arr;
  return cap;
}

static int parse_mediawiki(const char *engine_id, const char *json,
                           SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *search = strstr(json, "\"search\"");
  if (!search)
    return 0;
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = strchr(search, '[');
  if (!p)
    return 0;
  p++;
  while (p < lim && n < 32 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *snip_raw = dup_key_in(obj, end, "snippet");
    char *snip = strip_tags(snip_raw ? snip_raw : "");
    free(snip_raw);
    if (title && title[0]) {
      tmp[n].title = title;
      tmp[n].snippet = snip ? snip : strdup("");
      tmp[n].url = wiki_article_url(wiki_prefix_for(engine_id), title);
      n++;
    } else {
      free(title);
      free(snip);
    }
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static char *mwmbl_join_values(const char *arr, const char *end) {
  size_t cap = 256;
  char *out = malloc(cap);
  if (!out)
    return NULL;
  size_t w = 0;
  out[0] = '\0';
  const char *p = arr;
  while (p < end) {
    const char *v = strstr(p, "\"value\"");
    if (!v || v >= end)
      break;
    const char *colon = strchr(v, ':');
    if (!colon || colon >= end)
      break;
    char *piece = dup_json_string_at(colon + 1, end);
    if (piece) {
      size_t pl = strlen(piece);
      if (w + pl + 1 >= cap) {
        cap = (w + pl + 64) * 2;
        char *grown = realloc(out, cap);
        if (!grown) {
          free(piece);
          free(out);
          return NULL;
        }
        out = grown;
      }
      memcpy(out + w, piece, pl);
      w += pl;
      out[w] = '\0';
      free(piece);
    }
    p = colon + 1;
  }
  return out;
}

static int parse_mwmbl(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  SearchResult tmp[40] = {0};
  int n = 0;
  const char *p = json;
  while (p < lim && n < 40 && n < max_results) {
    const char *obj = strstr(p, "{\"url\"");
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *url = dup_key_in(obj, end, "url");
    const char *title_arr = strstr(obj, "\"title\"");
    char *title = NULL;
    if (title_arr && title_arr < end) {
      const char *br = strchr(title_arr, '[');
      if (br && br < end)
        title = mwmbl_join_values(br, end);
    }
    const char *ex = strstr(obj, "\"extract\"");
    char *snip = NULL;
    if (ex && ex < end) {
      const char *br = strchr(ex, '[');
      if (br && br < end)
        snip = mwmbl_join_values(br, end);
    }
    if (url && url[0] && title && title[0]) {
      tmp[n].url = url;
      tmp[n].title = title;
      tmp[n].snippet = snip ? snip : strdup("");
      n++;
    } else {
      free(url);
      free(title);
      free(snip);
    }
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_hn(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *hits = strstr(json, "\"hits\"");
  if (!hits)
    return 0;
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = strchr(hits, '[');
  if (!p)
    return 0;
  p++;
  while (p < lim && n < 32 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *url = dup_key_in(obj, end, "url");
    char *oid = dup_key_in(obj, end, "objectID");
    char *snip = dup_key_in(obj, end, "story_text");
    if (!snip)
      snip = dup_key_in(obj, end, "comment_text");
    if (title && title[0]) {
      if (!url || !url[0]) {
        free(url);
        char buf[256];
        snprintf(buf, sizeof(buf), "https://news.ycombinator.com/item?id=%s",
                 oid ? oid : "0");
        url = strdup(buf);
      }
      tmp[n].title = title;
      tmp[n].url = url;
      tmp[n].snippet = snip ? snip : strdup("");
      n++;
    } else {
      free(title);
      free(url);
      free(snip);
    }
    free(oid);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_reddit(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = json;
  while (p < lim && n < 32 && n < max_results) {
    const char *obj = strstr(p, "\"kind\":\"t3\"");
    if (!obj)
      break;
    const char *data = strstr(obj, "\"data\"");
    if (!data)
      break;
    const char *brace = strchr(data, '{');
    if (!brace)
      break;
    const char *end = object_end(brace, lim);
    char *title = dup_key_in(brace, end, "title");
    char *url = dup_key_in(brace, end, "url");
    char *permalink = dup_key_in(brace, end, "permalink");
    char *selftext = dup_key_in(brace, end, "selftext");
    if (title && title[0]) {
      if ((!url || !url[0] || strncmp(url, "/", 1) == 0) && permalink) {
        free(url);
        char buf[1024];
        snprintf(buf, sizeof(buf), "https://old.reddit.com%s", permalink);
        url = strdup(buf);
      }
      tmp[n].title = title;
      tmp[n].url = url ? url : strdup("https://old.reddit.com/");
      tmp[n].snippet = selftext ? selftext : strdup("");
      n++;
    } else {
      free(title);
      free(url);
      free(selftext);
    }
    free(permalink);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_lobsters_json(const char *json, SearchResult **out,
                               int max_results) {
  const char *lim = json + strlen(json);
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = json;
  while (p < lim && n < 32 && n < max_results) {
    const char *obj = strstr(p, "\"short_id\"");
    if (!obj)
      break;
    while (obj > json && *obj != '{')
      obj--;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *url = dup_key_in(obj, end, "url");
    char *sid = dup_key_in(obj, end, "short_id");
    char *desc = dup_key_in(obj, end, "description");
    if (title && title[0]) {
      if (!url || !url[0]) {
        free(url);
        char buf[256];
        snprintf(buf, sizeof(buf), "https://lobste.rs/s/%s", sid ? sid : "");
        url = strdup(buf);
      }
      tmp[n].title = title;
      tmp[n].url = url;
      tmp[n].snippet = desc ? desc : strdup("");
      n++;
    } else {
      free(title);
      free(url);
      free(desc);
    }
    free(sid);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_qwant(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *items = strstr(json, "\"items\"");
  if (!items)
    return 0;
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = strchr(items, '[');
  if (!p)
    return 0;
  p++;
  while (p < lim && n < 32 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *url = dup_key_in(obj, end, "url");
    char *desc = dup_key_in(obj, end, "desc");
    if (!desc)
      desc = dup_key_in(obj, end, "description");
    if (title && url && title[0] && url[0]) {
      tmp[n].title = title;
      tmp[n].url = url;
      tmp[n].snippet = desc ? desc : strdup("");
      n++;
    } else {
      free(title);
      free(url);
      free(desc);
    }
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_lemmy(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *posts = strstr(json, "\"posts\"");
  if (!posts)
    return 0;
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = posts;
  while (p < lim && n < 32 && n < max_results) {
    const char *postk = strstr(p, "\"post\"");
    if (!postk)
      break;
    const char *brace = strchr(postk, '{');
    if (!brace)
      break;
    const char *end = object_end(brace, lim);
    char *title = dup_key_in(brace, end, "name");
    char *url = dup_key_in(brace, end, "url");
    char *ap = dup_key_in(brace, end, "ap_id");
    char *body = dup_key_in(brace, end, "body");
    if (title && title[0]) {
      if (!url || !url[0]) {
        free(url);
        url = ap;
        ap = NULL;
      }
      tmp[n].title = title;
      tmp[n].url = url ? url : strdup("https://lemmy.world/");
      tmp[n].snippet = body ? body : strdup("");
      n++;
      free(ap);
    } else {
      free(title);
      free(url);
      free(body);
      free(ap);
    }
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

/* stackexchange items carry owner.link before the question link; scan every
 * link key and take the first that points at a question */
static char *dup_so_question_link(const char *obj, const char *end) {
  const char *p = obj;
  while (p < end) {
    const char *k = find_key(p, end, "link");
    if (!k)
      return NULL;
    char *v = dup_json_string_at(k, end);
    if (v && strstr(v, "stackoverflow.com/questions/"))
      return v;
    free(v);
    p = k + 1;
  }
  return NULL;
}

static int parse_stackoverflow(const char *json, SearchResult **out,
                               int max_results) {
  const char *lim = json + strlen(json);
  const char *items = strstr(json, "\"items\"");
  if (!items)
    return 0;
  SearchResult tmp[32] = {0};
  int n = 0;
  const char *p = strchr(items, '[');
  if (!p)
    return 0;
  p++;
  while (p < lim && n < 32 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *url = dup_so_question_link(obj, end);
    char *body = dup_key_in(obj, end, "body");
    if (!body)
      body = dup_key_in(obj, end, "excerpt");
    if (title && url && title[0] && url[0]) {
      tmp[n].title = title;
      tmp[n].url = url;
      tmp[n].snippet = body ? strip_tags(body) : strdup("");
      free(body);
      n++;
    } else {
      free(title);
      free(url);
      free(body);
    }
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

/* numeric json value (unquoted) */
static long long num_in(const char *obj, const char *end, const char *key,
                        long long fallback) {
  const char *p = find_key(obj, end, key);
  if (!p)
    return fallback;
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    p++;
  if (p >= end || *p < '0' || *p > '9')
    return fallback;
  char *ep = NULL;
  long long v = strtoll(p, &ep, 10);
  return ep == p ? fallback : v;
}

/* fold whitespace runs into single spaces, in place */
static void collapse_ws(char *s) {
  if (!s)
    return;
  char *r = s, *w = s;
  int pending = 0;
  while (*r) {
    if (*r == ' ' || *r == '\t' || *r == '\n' || *r == '\r') {
      pending = w > s;
      r++;
      continue;
    }
    if (pending) {
      *w++ = ' ';
      pending = 0;
    }
    *w++ = *r++;
  }
  *w = '\0';
}

/* "Venue, Year - cited N - abstract" style snippet for paper results */
static char *paper_snippet(const char *who, const char *venue,
                           long long year, long long cites,
                           const char *abstract) {
  char meta[384];
  meta[0] = '\0';
  size_t off = 0;
  if (who && *who)
    off += snprintf(meta + off, sizeof(meta) - off, "%s", who);
  if (off >= sizeof(meta))
    off = sizeof(meta) - 1;
  if (venue && *venue)
    off += snprintf(meta + off, sizeof(meta) - off, "%s%s", off ? ". " : "",
                    venue);
  if (off >= sizeof(meta))
    off = sizeof(meta) - 1;
  if (year > 0)
    off += snprintf(meta + off, sizeof(meta) - off, "%s%lld", off ? ", " : "",
                    year);
  if (off >= sizeof(meta))
    off = sizeof(meta) - 1;
  if (cites > 0)
    snprintf(meta + off, sizeof(meta) - off, " (%lld citations)", cites);
  char *abs_txt = strip_tags(abstract);
  collapse_ws(abs_txt);
  if (abs_txt && strlen(abs_txt) > 420) {
    abs_txt[420] = '\0';
    /* clip at a word boundary */
    char *sp = strrchr(abs_txt, ' ');
    if (sp)
      *sp = '\0';
    strcat(abs_txt, "...");
  }
  size_t need = strlen(meta) + strlen(abs_txt ? abs_txt : "") + 4;
  char *out = malloc(need);
  if (out)
    snprintf(out, need, "%s%s%s", meta, meta[0] && abs_txt && abs_txt[0] ? " - " : "",
             abs_txt ? abs_txt : "");
  free(abs_txt);
  return out;
}

/* join a flat json string array like ["a","b"] -> "a, b" */
static char *join_str_array(const char *arr, const char *end) {
  if (!arr || *arr != '[')
    return NULL;
  size_t cap = 256, w = 0;
  char *out = malloc(cap);
  if (!out)
    return NULL;
  out[0] = '\0';
  const char *p = arr + 1;
  int first = 1;
  while (p < end && *p && *p != ']') {
    while (p < end && *p != '"' && *p != ']')
      p++;
    if (p >= end || *p == ']')
      break;
    const char *s = ++p;
    while (p < end && *p) {
      if (*p == '\\' && p + 1 < end) {
        p += 2;
        continue;
      }
      if (*p == '"')
        break;
      p++;
    }
    char *piece = json_unescape_range(s, (size_t)(p - s));
    p++;
    if (!piece || !*piece) {
      free(piece);
      continue;
    }
    size_t pl = strlen(piece);
    size_t sep = first ? 0 : 2;
    if (w + pl + sep + 1 >= cap) {
      cap = (w + pl + 64) * 2;
      char *nb = realloc(out, cap);
      if (!nb) {
        free(piece);
        break;
      }
      out = nb;
    }
    if (!first) {
      out[w++] = ',';
      out[w++] = ' ';
    }
    memcpy(out + w, piece, pl);
    w += pl;
    out[w] = '\0';
    first = 0;
    free(piece);
  }
  return out;
}

/* array value for a key -> raw '[' text position */
static const char *arr_key_in(const char *obj, const char *end,
                              const char *key) {
  const char *p = find_key(obj, end, key);
  if (!p)
    return NULL;
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
    p++;
  return (p < end && *p == '[') ? p : NULL;
}

static int parse_openlibrary(const char *json, SearchResult **out,
                             int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"docs\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *key = dup_key_in(obj, end, "key");
    const char *arr = arr_key_in(obj, end, "author_name");
    char *authors = arr ? join_str_array(arr, end) : NULL;
    long long year = num_in(obj, end, "first_publish_year", 0);
    char *ia = dup_key_in(obj, end, "ia");

    if (title && title[0] && key && *key) {
      char url[512];
      snprintf(url, sizeof(url), "https://openlibrary.org%s", key);
      tmp[n].url = strdup(url);
      tmp[n].title = title;
      tmp[n].snippet =
          paper_snippet(authors, NULL, year, 0, ia ? "Borrowable on archive.org" : NULL);
      if (year > 0) {
        char y[24];
        snprintf(y, sizeof(y), "%lld", year);
        tmp[n].date = strdup(y);
      }
      n++;
    } else {
      free(title);
    }
    free(key);
    free(authors);
    free(ia);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_gbooks(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"items\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    const char *vi = strstr(obj, "\"volumeInfo\"");
    const char *vend = vi ? strstr(vi, "\"industryIdentifiers\"") : end;
    if (!vend || vend > end)
      vend = end;
    if (!vi || vi > end) {
      p = end;
      continue;
    }
    char *title = dup_key_in(vi, end, "title");
    char *desc = dup_key_in(vi, end, "description");
    char *link = dup_key_in(vi, end, "infoLink");
    if (!link)
      link = dup_key_in(vi, end, "canonicalVolumeLink");
    const char *arr = arr_key_in(vi, end, "authors");
    char *authors = arr ? join_str_array(arr, end) : NULL;
    char *pubdate = dup_key_in(vi, end, "publishedDate");
    long long year = pubdate ? strtoll(pubdate, NULL, 10) : 0;

    if (title && title[0] && link && link[0]) {
      tmp[n].url = link;
      tmp[n].title = title;
      tmp[n].snippet = paper_snippet(authors, NULL, year, 0, desc);
      if (pubdate && *pubdate)
        tmp[n].date = strdup(pubdate);
      n++;
    } else {
      free(title);
      free(link);
    }
    free(desc);
    free(authors);
    free(pubdate);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_itunes(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"results\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *name = dup_key_in(obj, end, "collectionName");
    if (!name)
      name = dup_key_in(obj, end, "trackName");
    char *artist = dup_key_in(obj, end, "artistName");
    char *url = dup_key_in(obj, end, "collectionViewUrl");
    if (!url)
      url = dup_key_in(obj, end, "trackViewUrl");
    char *genre = dup_key_in(obj, end, "primaryGenreName");
    char *feed = dup_key_in(obj, end, "feedUrl");

    if (name && name[0] && url && url[0]) {
      tmp[n].url = url;
      tmp[n].title = name;
      char *snip = paper_snippet(artist, genre, 0, 0, NULL);
      if (feed && *feed && snip) {
        size_t need = strlen(snip) + strlen(feed) + 12;
        char *nb = malloc(need);
        if (nb) {
          snprintf(nb, need, "%s - %s", snip, feed);
          free(snip);
          snip = nb;
        }
      }
      tmp[n].snippet = snip;
      n++;
    } else {
      free(name);
      free(url);
    }
    free(artist);
    free(genre);
    free(feed);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_librivox(const char *json, SearchResult **out,
                          int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"books\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *url = dup_key_in(obj, end, "url_librivox");
    if (!url)
      url = dup_key_in(obj, end, "url_text_source");
    char *desc = dup_key_in(obj, end, "description");
    /* first author name pair inside the authors array */
    char *first = NULL, *last = NULL;
    const char *au = arr_key_in(obj, end, "authors");
    if (au) {
      const char *ao = strchr(au, '{');
      const char *aoend = ao ? object_end(ao, end) : NULL;
      if (ao && aoend) {
        first = dup_key_in(ao, aoend, "first_name");
        last = dup_key_in(ao, aoend, "last_name");
      }
    }
    char author[192];
    author[0] = '\0';
    if (first || last)
      snprintf(author, sizeof(author), "%s %s", first ? first : "",
               last ? last : "");

    if (title && title[0] && url && url[0]) {
      tmp[n].url = url;
      tmp[n].title = title;
      tmp[n].snippet =
          paper_snippet(*author ? author : NULL, "LibriVox", 0, 0, desc);
      n++;
    } else {
      free(title);
      free(url);
    }
    free(desc);
    free(first);
    free(last);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_themealdb(const char *json, SearchResult **out,
                           int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"meals\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *name = dup_key_in(obj, end, "strMeal");
    char *idm = dup_key_in(obj, end, "idMeal");
    char *instr = dup_key_in(obj, end, "strInstructions");
    char *cat = dup_key_in(obj, end, "strCategory");
    char *area = dup_key_in(obj, end, "strArea");
    char *src = dup_key_in(obj, end, "strSource");

    char url[768];
    if (src && *src)
      snprintf(url, sizeof(url), "%s", src);
    else if (idm && *idm)
      snprintf(url, sizeof(url), "https://www.themealdb.com/meal.php?c=%s",
               idm);
    else
      url[0] = '\0';

    if (name && name[0] && url[0]) {
      char tags[128];
      snprintf(tags, sizeof(tags), "%s%s%s", area ? area : "",
               area && cat ? " - " : "", cat ? cat : "");
      tmp[n].url = strdup(url);
      tmp[n].title = name;
      tmp[n].snippet = paper_snippet(NULL, *tags ? tags : NULL, 0, 0, instr);
      n++;
    } else {
      free(name);
    }
    free(idm);
    free(instr);
    free(cat);
    free(area);
    free(src);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_europepmc(const char *json, SearchResult **out,
                           int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"resultList\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *abs = dup_key_in(obj, end, "abstractText");
    char *authors = dup_key_in(obj, end, "authorString");
    char *journal = dup_key_in(obj, end, "journalTitle");
    char *year_s = dup_key_in(obj, end, "pubYear");
    long long year = year_s ? strtoll(year_s, NULL, 10) : 0;
    long long cites = num_in(obj, end, "citedByCount", 0);
    char *doi = dup_key_in(obj, end, "doi");
    char *pmcid = dup_key_in(obj, end, "pmcid");
    char *pmid = dup_key_in(obj, end, "pmid");

    char urlbuf[768];
    if (doi && *doi)
      snprintf(urlbuf, sizeof(urlbuf), "https://doi.org/%s", doi);
    else if (pmcid && *pmcid)
      snprintf(urlbuf, sizeof(urlbuf), "https://europepmc.org/articles/%s",
               pmcid);
    else if (pmid && *pmid)
      snprintf(urlbuf, sizeof(urlbuf), "https://europepmc.org/article/MED/%s",
               pmid);
    else
      urlbuf[0] = '\0';

    if (title && title[0] && urlbuf[0]) {
      tmp[n].url = strdup(urlbuf);
      tmp[n].title = title;
      tmp[n].snippet = paper_snippet(authors, journal, year, cites, abs);
      if (year_s)
        tmp[n].date = strdup(year_s);
      n++;
    } else {
      free(title);
    }
    free(abs);
    free(authors);
    free(journal);
    free(year_s);
    free(doi);
    free(pmcid);
    free(pmid);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_s2(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"data\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "title");
    char *abs = dup_key_in(obj, end, "abstract");
    char *url = dup_key_in(obj, end, "url");
    char *venue = dup_key_in(obj, end, "venue");
    char *doi = NULL;
    /* externalIds is a nested object - pull DOI out of it when present */
    const char *ext = strstr(obj, "\"externalIds\"");
    if (ext && ext < end)
      doi = dup_key_in(ext, end, "DOI");
    long long year = num_in(obj, end, "year", 0);
    long long cites = num_in(obj, end, "citationCount", 0);

    /* first author name when present */
    const char *au = strstr(obj, "\"authors\"");
    char *first_author = NULL;
    if (au && au < end) {
      const char *nb = strstr(au, "\"name\"");
      if (nb && nb < end) {
        const char *nbend = strchr(nb, '}');
        if (!nbend || nbend > end)
          nbend = end;
        first_author = dup_json_string_at(find_key(nb, nbend, "name"), nbend);
      }
    }

    char *final_url = NULL;
    if (doi && *doi) {
      size_t need = strlen(doi) + 17;
      final_url = malloc(need);
      if (final_url)
        snprintf(final_url, need, "https://doi.org/%s", doi);
    }
    if (!final_url)
      final_url = url;

    if (title && title[0] && final_url && final_url[0]) {
      char buf[32];
      buf[0] = '\0';
      if (year > 0)
        snprintf(buf, sizeof(buf), "%lld", year);
      tmp[n].url = final_url;
      tmp[n].title = title;
      tmp[n].snippet = paper_snippet(first_author, venue, year, cites, abs);
      if (buf[0])
        tmp[n].date = strdup(buf);
      n++;
      if (final_url != url)
        free(url);
    } else {
      free(title);
      if (final_url != url)
        free(final_url);
      free(url);
    }
    free(abs);
    free(venue);
    free(doi);
    free(first_author);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

/* openalex abstract_inverted_index: {"word":[pos,...], ...} -> sentence */
static char *openalex_abstract(const char *obj, const char *end) {
  const char *ai = strstr(obj, "\"abstract_inverted_index\"");
  if (!ai || ai > end)
    return NULL;
  const char *ob = strchr(ai, '{');
  if (!ob || ob > end)
    return NULL;
  const char *oe = object_end(ob, end);
  if (oe <= ob)
    return NULL;

  /* two passes: size, then place words at positions */
  int maxpos = -1;
  const char *q = ob;
  while (q < oe) {
    const char *br = strchr(q, '[');
    if (!br || br > oe)
      break;
    while (br < oe && *br != ']') {
      if (*br >= '0' && *br <= '9') {
        int v = (int)strtol(br, NULL, 10);
        if (v > maxpos)
          maxpos = v;
      }
      br++;
    }
    q = br < oe ? br + 1 : oe;
  }
  if (maxpos < 0 || maxpos > 4000)
    return NULL;

  char **slots = calloc((size_t)maxpos + 1, sizeof(char *));
  if (!slots)
    return NULL;

  /* words can occupy many slots - keep unique pointers for cleanup */
  char **words = calloc((size_t)maxpos + 1, sizeof(char *));
  if (!words) {
    free(slots);
    return NULL;
  }
  int nwords = 0;

  q = ob + 1;
  while (q < oe && nwords <= maxpos) {
    /* word is a json string key followed by a positions array */
    while (q < oe && *q != '"')
      q++;
    const char *ws = ++q;
    while (q < oe && *q != '"')
      q++;
    if (q >= oe)
      break;
    char *word = malloc((size_t)(q - ws) + 1);
    if (!word)
      break;
    memcpy(word, ws, (size_t)(q - ws));
    word[q - ws] = '\0';
    words[nwords++] = word;
    q++;
    const char *br = strchr(q, '[');
    if (!br || br > oe)
      break;
    while (br < oe && *br != ']') {
      if (*br >= '0' && *br <= '9') {
        char *nend = NULL;
        int v = (int)strtol(br, &nend, 10);
        if (v >= 0 && v <= maxpos)
          slots[v] = word;
        if (nend)
          br = nend;
        else
          br++;
        continue;
      }
      br++;
    }
    q = br < oe ? br + 1 : oe;
  }

  /* join into a string, cap at ~700 chars */
  size_t cap = 1024;
  char *out = malloc(cap);
  if (!out) {
    free(slots);
    return NULL;
  }
  size_t len = 0;
  for (int i = 0; i <= maxpos && len < 700; i++) {
    if (!slots[i])
      continue;
    size_t wl = strlen(slots[i]);
    if (len + wl + 2 > cap) {
      cap = (len + wl + 2) * 2;
      char *nb = realloc(out, cap);
      if (!nb)
        break;
      out = nb;
    }
    memcpy(out + len, slots[i], wl);
    len += wl;
    out[len++] = ' ';
  }
  if (len > 0)
    out[len - 1] = '\0';
  else
    out[0] = '\0';
  for (int i = 0; i < nwords; i++)
    free(words[i]);
  free(words);
  free(slots);
  return out;
}

static int parse_openalex(const char *json, SearchResult **out,
                          int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"results\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *title = dup_key_in(obj, end, "display_name");
    char *doi = dup_key_in(obj, end, "doi");
    long long year = num_in(obj, end, "publication_year", 0);
    long long cites = num_in(obj, end, "cited_by_count", 0);
    /* venue name lives in primary_location.source.display_name - the
       last display_name in the object before the doi list is usually it;
       grab display_name under source explicitly */
    char *venue = NULL;
    const char *src = strstr(obj, "\"source\"");
    if (src && src < end)
      venue = dup_key_in(src, end, "display_name");

    char *abs = openalex_abstract(obj, end);
    if (title && title[0] && doi && doi[0]) {
      tmp[n].url = strdup(doi);
      tmp[n].title = title;
      tmp[n].snippet = paper_snippet(NULL, venue, year, cites, abs);
      char buf[32];
      if (year > 0) {
        snprintf(buf, sizeof(buf), "%lld", year);
        tmp[n].date = strdup(buf);
      }
      n++;
    } else {
      free(title);
    }
    free(doi);
    free(venue);
    free(abs);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

static int parse_osm(const char *json, SearchResult **out, int max_results);
static int parse_peertube(const char *json, SearchResult **out, int max_results);

int parse_json_engine(const char *engine_id, const char *json,
                      SearchResult **out_results, int max_results) {
  *out_results = NULL;
  if (!json || !engine_id)
    return 0;
  if (!strcmp(engine_id, "wikipedia") || !strcmp(engine_id, "wiktionary") ||
      !strcmp(engine_id, "archwiki") || !strcmp(engine_id, "debianwiki") ||
      !strcmp(engine_id, "gentoo") || !strcmp(engine_id, "nixos") ||
      !strcmp(engine_id, "mediawiki") || !strcmp(engine_id, "wikibooks") ||
      !strcmp(engine_id, "wikiquote"))
    return parse_mediawiki(engine_id, json, out_results, max_results);
  if (!strcmp(engine_id, "mwmbl"))
    return parse_mwmbl(json, out_results, max_results);
  if (!strcmp(engine_id, "hn"))
    return parse_hn(json, out_results, max_results);
  if (!strcmp(engine_id, "reddit"))
    return parse_reddit(json, out_results, max_results);
  if (!strcmp(engine_id, "lobsters"))
    return parse_lobsters_json(json, out_results, max_results);
  if (!strcmp(engine_id, "qwant"))
    return parse_qwant(json, out_results, max_results);
  if (!strcmp(engine_id, "lemmy"))
    return parse_lemmy(json, out_results, max_results);
  if (!strcmp(engine_id, "stackoverflow"))
    return parse_stackoverflow(json, out_results, max_results);
  if (!strcmp(engine_id, "europepmc"))
    return parse_europepmc(json, out_results, max_results);
  if (!strcmp(engine_id, "s2") || !strcmp(engine_id, "semanticscholar"))
    return parse_s2(json, out_results, max_results);
  if (!strcmp(engine_id, "openalex"))
    return parse_openalex(json, out_results, max_results);
  if (!strcmp(engine_id, "openlibrary"))
    return parse_openlibrary(json, out_results, max_results);
  if (!strcmp(engine_id, "gbooks"))
    return parse_gbooks(json, out_results, max_results);
  if (!strcmp(engine_id, "itunes"))
    return parse_itunes(json, out_results, max_results);
  if (!strcmp(engine_id, "librivox"))
    return parse_librivox(json, out_results, max_results);
  if (!strcmp(engine_id, "themealdb"))
    return parse_themealdb(json, out_results, max_results);
  if (!strcmp(engine_id, "osm") || !strcmp(engine_id, "maps"))
    return parse_osm(json, out_results, max_results);
  if (!strcmp(engine_id, "peertube") || !strcmp(engine_id, "videos"))
    return parse_peertube(json, out_results, max_results);
  return 0;
}

/* openstreetmap nominatim - top-level array of place objects */
static int parse_osm(const char *json, SearchResult **out, int max_results) {
  const char *lim = json + strlen(json);
  const char *p = strchr(json, '[');
  if (!p)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *name = dup_key_in(obj, end, "display_name");
    char *lat = dup_key_in(obj, end, "lat");
    char *lon = dup_key_in(obj, end, "lon");
    char *type = dup_key_in(obj, end, "type");
    char *osmtype = dup_key_in(obj, end, "osm_type");
    long long osmid = num_in(obj, end, "osm_id", 0);
    double imp = 0.0;
    {
      const char *iv = find_key(obj, end, "importance");
      if (iv)
        imp = strtod(iv, NULL);
    }

    /* shorten the comma-heavy display name - first 2 parts carry the name
       and locality, the rest is administrative cruft */
    char *short_name = NULL;
    if (name) {
      short_name = strdup(name);
      int commas = 0;
      for (char *c = short_name; *c; c++) {
        if (*c == ',')
          commas++;
        if (commas >= 3) {
          *c = '\0';
          break;
        }
      }
    }

    if (short_name && *short_name && osmtype && *osmtype && osmid > 0) {
      char url[512];
      snprintf(url, sizeof(url), "https://www.openstreetmap.org/%s/%lld",
               osmtype, osmid);
      tmp[n].url = strdup(url);
      tmp[n].title = short_name;
      short_name = NULL;
      char meta[160];
      snprintf(meta, sizeof(meta), "%s%s%s %s,%s",
               type ? type : "", imp > 0 ? " - importance " : "",
               "", lat ? lat : "", lon ? lon : "");
      /* build "type - lat, lon" cleanly */
      char snip[256];
      snprintf(snip, sizeof(snip), "%s%s%s, %s",
               type && *type ? type : "place",
               lat ? " - " : "", lat ? lat : "", lon ? lon : "");
      tmp[n].snippet = strdup(snip);
      n++;
    }
    free(short_name);
    free(name);
    free(lat);
    free(lon);
    free(type);
    free(osmtype);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}

/* peertube search api - {total, data:[{name,uuid,channel{...},publishedAt,
   duration,nsfw}]} */
static int parse_peertube(const char *json, SearchResult **out,
                          int max_results) {
  const char *lim = json + strlen(json);
  const char *list = strstr(json, "\"data\"");
  if (!list)
    return 0;
  SearchResult tmp[24] = {0};
  int n = 0;
  const char *p = strchr(list, '[');
  if (!p)
    return 0;
  while (p < lim && n < 24 && n < max_results) {
    const char *obj = strchr(p, '{');
    if (!obj)
      break;
    const char *end = object_end(obj, lim);
    char *name = dup_key_in(obj, end, "name");
    char *uuid = dup_key_in(obj, end, "uuid");
    char *host = dup_key_in(obj, end, "host");
    long long nsfw = num_in(obj, end, "nsfw", 0);
    long long dur = num_in(obj, end, "duration", 0);
    char *pub = dup_key_in(obj, end, "publishedAt");

    /* channel object: {"channel":{"displayName":"x","host":"y"}} - the first
       "host" key at top level is the video channel host */
    char *chan = NULL;
    const char *ck = find_key(obj, end, "channel");
    if (ck) {
      const char *co = strchr(ck, '{');
      if (co && co < end) {
        const char *ce = object_end(co, end);
        if (ce)
          chan = dup_key_in(co, ce, "displayName");
      }
    }

    if (name && *name && uuid && *uuid && host && *host && !nsfw) {
      char url[512];
      snprintf(url, sizeof(url), "https://%s/w/%s", host, uuid);
      tmp[n].url = strdup(url);
      tmp[n].title = name;
      char meta[192];
      if (dur > 0)
        snprintf(meta, sizeof(meta), "%s%s%s - %lld:%02lld",
                 chan ? chan : "", dur ? " - " : "", "", dur / 60, dur % 60);
      else
        snprintf(meta, sizeof(meta), "%s", chan ? chan : "");
      tmp[n].snippet = strdup(meta);
      if (pub) {
        /* 2024-01-15T... -> keep the date part */
        char d[16];
        strncpy(d, pub, 10);
        d[10] = '\0';
        tmp[n].date = strdup(d);
      }
      n++;
    } else {
      free(name);
    }
    free(uuid);
    free(host);
    free(pub);
    free(chan);
    p = end;
  }
  return collect(out, max_results, tmp, n);
}
