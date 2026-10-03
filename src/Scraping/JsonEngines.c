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
  SearchResult tmp[32];
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
  SearchResult tmp[40];
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
  SearchResult tmp[32];
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
  SearchResult tmp[32];
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
  SearchResult tmp[32];
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
  SearchResult tmp[32];
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
  SearchResult tmp[32];
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

static int parse_stackoverflow(const char *json, SearchResult **out,
                               int max_results) {
  const char *lim = json + strlen(json);
  const char *items = strstr(json, "\"items\"");
  if (!items)
    return 0;
  SearchResult tmp[32];
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
    char *url = dup_key_in(obj, end, "link");
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
  return 0;
}
