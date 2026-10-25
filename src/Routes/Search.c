#include "Search.h"
#include "../Infobox/Calculator.h"
#include "../Infobox/ColorWidget.h"
#include "../Infobox/CurrencyConversion.h"
#include "../Infobox/DateTimeWidget.h"
#include "../Infobox/DevTools.h"
#include "../Infobox/Dictionary.h"
#include "../Infobox/MeetingPlanner.h"
#include "../Infobox/UnitConversion.h"
#include "../Infobox/Weather.h"
#include "../Infobox/Wikipedia.h"
#include "../Infobox/Today.h"
#include "../Bangs.h"
#include "../Cache/Cache.h"
#include "../Limiter/RateLimit.h"
#include "../Scraping/Scraping.h"
#include "../Routes/Favicon.h"
#include "../Routes/ImageProxy.h"
#include "../Utility/Display.h"
#include "../Utility/ErrorPage.h"
#include "../Utility/HtmlEscape.h"
#include "../Utility/Utility.h"
#include "../Utility/HttpClient.h"
#include "../Utility/DomainPrefs.h"
#include "../Utility/JsonHelper.h"
#include "../Utility/Rank.h"
#include "../Utility/Slop.h"
#include "../Utility/Unescape.h"
#include "../Utility/XmlHelper.h"
#include "Config.h"
#include <beaker.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>

extern Config global_config;

char *get_base_url(const char *input);

typedef struct {
  int handler_idx;
  int success;
  const char *query;
  InfoBox (*fetch_fn)(char *);
  int (*check_fn)(const char *);
  InfoBox result;
} InfoBoxThreadData;

static char *build_search_request_cache_key(const char *query,
                                            const char *engine_id, int page,
                                            const char *client_key) {
  char scope_key[BUFFER_SIZE_MEDIUM];
  snprintf(scope_key, sizeof(scope_key), "search_request:%s:%s",
           engine_id ? engine_id : "all", client_key ? client_key : "unknown");
  return cache_compute_key(query, page, scope_key);
}

#define MERGE_RANK_K 10.0

typedef struct {
  char *key;
  char *url;
  char *title;
  char *snippet;
  char *date;
  char *host;
  char engine_ids[256];
  double score;
  int first_seen;
} MergedResult;

static int hex_val(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/* decode %XX only for chars that never change a url's meaning so encoded
 * and unencoded spellings of the same page dedup together */
static int safe_decode_char(int c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9'))
    return 1;
  switch (c) {
  case '-':
  case '_':
  case '.':
  case '~':
  case '(':
  case ')':
    return 1;
  }
  return 0;
}

static void decode_pct_inplace(char *s) {
  char *d = s;
  for (char *p = s; *p; p++) {
    if (*p == '%' && p[1] && p[2]) {
      int h = hex_val(p[1]), l = hex_val(p[2]);
      if (h >= 0 && l >= 0 && safe_decode_char(h * 16 + l)) {
        *d++ = (char)(h * 16 + l);
        p += 2;
        continue;
      }
    }
    *d++ = *p;
  }
  *d = '\0';
}

static char *canonicalize_url(const char *url) {
  if (!url || !url[0])
    return NULL;
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s", url);
  decode_pct_inplace(buf);
  char *host = strstr(buf, "://");
  char *end;
  if (host) {
    for (char *p = buf; p < host; p++)
      if (*p >= 'A' && *p <= 'Z')
        *p += 32;
    host += 3;
    end = host;
    while (*end && *end != '/' && *end != '?' && *end != '#')
      end++;
  } else {
    host = buf;
    end = host + strlen(host);
  }
  for (char *p = host; p < end; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p += 32;
  if (strncmp(host, "www.", 4) == 0 && host + 4 < end) {
    size_t tail = strlen(host + 4);
    memmove(host, host + 4, tail + 1);
    end -= 4;
  }
  char *frag = strchr(host ? host : buf, '#');
  if (frag)
    *frag = '\0';
  /* strip tracking params so utm copies of a page dedup with the bare one */
  char *q = strchr(buf, '?');
  if (q) {
    char *dst = q + 1;
    char *src = dst;
    int first = 1;
    static const char *trackers[] = {
        "fbclid", "gclid", "dclid", "msclkid", "mc_cid", "mc_eid",
        "igshid", "_ga",    "_gl",   "ref_src", "ved",
    };
    while (*src) {
      char *amp = strchr(src, '&');
      size_t plen = amp ? (size_t)(amp - src) : strlen(src);
      char *eq = memchr(src, '=', plen);
      size_t nlen = eq ? (size_t)(eq - src) : plen;
      int drop = nlen > 4 && strncmp(src, "utm_", 4) == 0;
      for (size_t t = 0; !drop && t < sizeof(trackers) / sizeof(trackers[0]);
           t++) {
        size_t tl = strlen(trackers[t]);
        if (nlen == tl && strncmp(src, trackers[t], tl) == 0)
          drop = 1;
      }
      if (!drop) {
        if (!first)
          *dst++ = '&';
        memmove(dst, src, plen);
        dst += plen;
        first = 0;
      }
      src += plen + (amp ? 1 : 0);
    }
    *dst = '\0';
    if (dst == q + 1)
      *q = '\0';
  }
  size_t len = strlen(buf);
  if (len > 1 && buf[len - 1] == '/')
    buf[len - 1] = '\0';
  return strdup(buf);
}

static int cookie_allows_forums(void) {
  char *c = get_cookie("forums");
  int ok = 1;
  if (c && c[0] == '0')
    ok = 0;
  free(c);
  return ok;
}

static int allow_forums_for_query(const char *q, const char *lens) {
  return rank_query_allows_forums(q) || cookie_allows_forums() ||
         lens_forces_forums(lens);
}

static int merged_result_cmp(const void *a, const void *b) {
  const MergedResult *ma = a;
  const MergedResult *mb = b;
  if (ma->score != mb->score)
    return ma->score < mb->score ? 1 : -1;
  return ma->first_seen - mb->first_seen;
}

static void merged_result_append_engine(MergedResult *m, const char *id) {
  if (!id || !id[0])
    return;
  char existing[256];
  snprintf(existing, sizeof(existing), "%s", m->engine_ids);
  for (char *tok = strtok(existing, ","); tok; tok = strtok(NULL, ",")) {
    if (strcmp(tok, id) == 0)
      return;
  }
  size_t cur = strlen(m->engine_ids);
  size_t add = strlen(id) + (cur > 0 ? 1 : 0);
  if (cur + add < sizeof(m->engine_ids)) {
    if (cur > 0)
      strcat(m->engine_ids, ",");
    strcat(m->engine_ids, id);
  }
}

#define MAX_SITE_OPS 8

typedef struct {
  char includes[MAX_SITE_OPS][256];
  int n_includes;
  char excludes[MAX_SITE_OPS][256];
  int n_excludes;
} SiteFilter;

static int host_suffix_match(const char *host, const char *domain) {
  size_t hl = host ? strlen(host) : 0;
  size_t dl = domain ? strlen(domain) : 0;
  if (!hl || !dl || hl < dl)
    return 0;
  if (hl == dl)
    return strcasecmp(host, domain) == 0;
  return host[hl - dl - 1] == '.' && strcasecmp(host + hl - dl, domain) == 0;
}

/* pull site:x and -site:x tokens out of the query so the merge can enforce
 * them even when an upstream engine ignores the operator */
static void parse_site_ops(const char *q, SiteFilter *f) {
  memset(f, 0, sizeof(*f));
  if (!q)
    return;
  const char *p = q;
  while (*p) {
    if (isspace((unsigned char)*p) || *p == '"') {
      p++;
      continue;
    }
    char tok[300];
    size_t tn = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '"' &&
           tn < sizeof(tok) - 1)
      tok[tn++] = *p++;
    while (*p && !isspace((unsigned char)*p) && *p != '"')
      p++;
    tok[tn] = '\0';
    int neg = 0;
    const char *t = tok;
    if (*t == '-') {
      neg = 1;
      t++;
    }
    if (strncasecmp(t, "site:", 5) != 0 || !t[5])
      continue;
    if (neg && f->n_excludes < MAX_SITE_OPS)
      snprintf(f->excludes[f->n_excludes++], 256, "%s", t + 5);
    else if (!neg && f->n_includes < MAX_SITE_OPS)
      snprintf(f->includes[f->n_includes++], 256, "%s", t + 5);
  }
}

/* results that are themselves a search form or engine results page are
 * junk hits - drop engine ui urls from the merge */
static int result_url_is_search_ui(const char *url) {
  static const char *engine_hosts[] = {
      "google.com",    "bing.com",      "duckduckgo.com", "brave.com",
      "ecosia.org",    "startpage.com", "qwant.com",      "mojeek.com",
      "yandex.com",    "baidu.com",     "mwmbl.org",      "wiby.me",
      "marginalia.nu", "stract.com",    "naver.com",      "kagi.com",
      "yep.com",       "swisscows.com", "metager.org",    "gibiru.com",
      "lycos.com",     "entireweb.com", "ask.com",        "aol.com",
      "github.com",    "gitlab.com",    "sourceforge.net"};
  if (!url)
    return 0;
  char *host = get_base_url(url);
  if (!host)
    return 0;
  int is_engine = 0;
  for (size_t i = 0; i < sizeof(engine_hosts) / sizeof(engine_hosts[0]); i++) {
    if (host_suffix_match(host, engine_hosts[i])) {
      is_engine = 1;
      break;
    }
  }
  if (!is_engine) {
    free(host);
    return 0;
  }
  const char *p = strstr(url, "://");
  p = p ? strchr(p + 3, '/') : NULL;
  const char *path = p ? p : "/";
  int ui = strncmp(path, "/search", 7) == 0 || strncmp(path, "/lite", 5) == 0 ||
           strncmp(path, "/html", 5) == 0 || strncmp(path, "/s?", 3) == 0 ||
           strncmp(path, "/res/", 5) == 0 || strncmp(path, "/find", 5) == 0 ||
           strcmp(path, "/") == 0;
  free(host);
  return ui;
}

static int site_filter_pass(const SiteFilter *f, const char *host) {
  if (f->n_includes) {
    int ok = 0;
    for (int i = 0; i < f->n_includes; i++)
      if (host_suffix_match(host, f->includes[i]))
        ok = 1;
    if (!ok)
      return 0;
  }
  for (int i = 0; i < f->n_excludes; i++)
    if (host_suffix_match(host, f->excludes[i]))
      return 0;
  return 1;
}

static int merge_rank_results(ScrapeJob *jobs, SearchResult **all_results,
                              int engine_idx, const char *query,
                              const char *lens, MergedResult **out) {
  int total = 0;
  for (int i = 0; i < engine_idx; i++)
    total += jobs[i].results_count;
  *out = NULL;
  if (total <= 0)
    return 0;

  MergedResult *merged = calloc((size_t)total, sizeof(MergedResult));
  if (!merged)
    return -1;

  int allow_forums = allow_forums_for_query(query, lens);
  SiteFilter site_ops;
  parse_site_ops(query, &site_ops);
  int filter_active = site_ops.n_includes || site_ops.n_excludes;
  int n = 0;
  int seq = 0;
  for (int i = 0; i < engine_idx; i++) {
    const char *eid = jobs[i].engine ? jobs[i].engine->id : "";
    for (int j = 0; j < jobs[i].results_count; j++) {
      SearchResult *r = &all_results[i][j];
      char *rhost = get_base_url(r->url ? r->url : "");
      double weight =
          rank_host_weight(r->url, allow_forums) * dprefs_url_weight(r->url) *
          lens_host_weight(r->url, lens);
      if (lens_is_valid(lens)) {
        int ln = 0;
        const char *const *le = lens_engines(lens, &ln);
        for (int li = 0; li < ln; li++)
          if (strcmp(eid, le[li]) == 0 && weight < 3.0) {
            weight = rank_host_weight(r->url, allow_forums) *
                     dprefs_url_weight(r->url) * 3.5;
            break;
          }
      }
      int drop = (filter_active && !site_filter_pass(&site_ops, rhost)) ||
                 weight <= 0.0 || result_url_is_search_ui(r->url) ||
                 !url_is_web(r->url);
      if (drop) {
        free(rhost);
        free(r->url);
        free(r->title);
        free(r->snippet);
        free(r->date);
        r->url = NULL;
        r->title = NULL;
        r->snippet = NULL;
        r->date = NULL;
        continue;
      }
      char *key = r->url ? canonicalize_url(r->url) : NULL;
      double rs = (1.0 / (MERGE_RANK_K + j)) * weight;

      if (key) {
        int k;
        for (k = 0; k < n; k++) {
          if (merged[k].key && strcmp(merged[k].key, key) == 0) {
            merged[k].score += rs;
            merged_result_append_engine(&merged[k], eid);
            free(key);
            break;
          }
        }
        if (k < n) {
          free(rhost);
          continue;
        }
      }

      merged[n].key = key;
      merged[n].url = r->url;
      merged[n].title = r->title;
      merged[n].snippet = r->snippet;
      merged[n].date = r->date;
      merged[n].host = rhost;
      snippet_strip_feed_meta(merged[n].snippet);
      merged[n].score = rs;
      merged[n].first_seen = seq++;
      merged[n].engine_ids[0] = '\0';
      merged_result_append_engine(&merged[n], eid);
      r->url = NULL;
      r->title = NULL;
      r->snippet = NULL;
      r->date = NULL;
      n++;
    }
  }

  /* apply lens and domain prefs again on the merged score: per-engine
   * weights alone get diluted when a result is confirmed by many engines */
  if (lens_is_valid(lens)) {
    int ln = 0;
    const char *const *le = lens_engines(lens, &ln);
    for (int i = 0; i < n; i++) {
      int from_lens_engine = 0;
      char ids[256];
      snprintf(ids, sizeof(ids), "%s", merged[i].engine_ids);
      for (char *tok = strtok(ids, ","); tok; tok = strtok(NULL, ","))
        for (int li = 0; li < ln; li++)
          if (strcmp(tok, le[li]) == 0)
            from_lens_engine = 1;
      if (from_lens_engine)
        merged[i].score *= 3.5;
      else
        merged[i].score *= lens_host_weight(merged[i].url, lens);
    }
  }
  {
    char *pc = get_cookie("dprefs");
    if (pc && pc[0]) {
      for (int i = 0; i < n; i++)
        merged[i].score *= dprefs_url_weight(merged[i].url);
    }
    free(pc);
  }
  qsort(merged, (size_t)n, sizeof(MergedResult), merged_result_cmp);
  *out = merged;
  return n;
}

static int apply_slop_to_merged(MergedResult *merged, int n, int mode,
                                double **scores_out) {
  *scores_out = NULL;
  if (!merged || n <= 0 || mode == SLOP_OFF)
    return n;
  double *scores = calloc((size_t)n, sizeof(double));
  if (!scores)
    return n;
  const char **titles = malloc(sizeof(char *) * (size_t)n);
  const char **snips = malloc(sizeof(char *) * (size_t)n);
  if (!titles || !snips) {
    free(titles);
    free(snips);
    free(scores);
    return n;
  }
  for (int i = 0; i < n; i++) {
    titles[i] = merged[i].title;
    snips[i] = merged[i].snippet;
  }
  if (slop_score_pages(titles, snips, n, scores) < 0) {
    free(titles);
    free(snips);
    free(scores);
    return n;
  }
  free(titles);
  free(snips);
  if (mode == SLOP_HIDE) {
    int w = 0;
    for (int i = 0; i < n; i++) {
      if (scores[i] >= 0.55) {
        free(merged[i].key);
        free(merged[i].url);
        free(merged[i].title);
        free(merged[i].snippet);
        free(merged[i].date);
        free(merged[i].host);
        continue;
      }
      if (w != i)
        merged[w] = merged[i];
      scores[w] = scores[i];
      w++;
    }
    n = w;
  } else if (mode == SLOP_DERANK) {
    for (int i = 0; i < n; i++)
      merged[i].score *= (1.0 - 0.85 * scores[i]);
    qsort(merged, (size_t)n, sizeof(MergedResult), merged_result_cmp);
  }
  *scores_out = scores;
  return n;
}

static void free_merged_results(MergedResult *merged, int count) {
  if (!merged)
    return;
  for (int i = 0; i < count; i++) {
    free(merged[i].key);
    free(merged[i].url);
    free(merged[i].title);
    free(merged[i].snippet);
    free(merged[i].date);
    free(merged[i].host);
  }
  free(merged);
}

static void free_search_jobs(ScrapeJob *jobs, SearchResult **all_results,
                             int engine_idx);
static char *fetch_did_you_mean(const char *query);

typedef struct {
  int (*check_fn)(const char *query);
  InfoBox (*fetch_fn)(char *query);
  char *(*url_construct_fn)(const char *query);
} InfoBoxHandler;

enum {
  RESULT_FIELD_COUNT = 12,
  PAGER_WINDOW_SIZE = 5,
  MAX_RESULTS_PER_HOST = 2,
};

static InfoBox fetch_wiki_wrapper(char *query) {
  char *url = construct_wiki_url(query);
  if (!url)
    return (InfoBox){0};
  InfoBox result = fetch_wiki_data(url);
  free(url);
  return result;
}

static int always_true(const char *query) {
  (void)query;
  return 1;
}

static InfoBox fetch_dict_wrapper(char *query) {
  return fetch_dictionary_data(query);
}
static InfoBox fetch_calc_wrapper(char *query) {
  return fetch_calc_data(query);
}
static InfoBox fetch_unit_wrapper(char *query) {
  return fetch_unit_conv_data(query);
}
static InfoBox fetch_currency_wrapper(char *query) {
  return fetch_currency_data(query);
}
static InfoBox fetch_datetime_wrapper(char *query) {
  return fetch_datetime_widget_data(query);
}
static InfoBox fetch_weather_wrapper(char *query) {
  return fetch_weather_data(query);
}
static InfoBox fetch_devtools_wrapper(char *query) {
  return fetch_devtools_data(query);
}
static InfoBox fetch_color_wrapper(char *query) {
  return fetch_color_widget_data(query);
}
static InfoBox fetch_meeting_wrapper(char *query) {
  return fetch_meeting_planner_data(query);
}
char *get_base_url(const char *input) {
    if (!input) return NULL;

    const char *start = input;

    const char *protocol_pos = strstr(input, "://");
    if (protocol_pos) {
        start = protocol_pos + 3;
    }

    const char *end = start;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }

    size_t len = end - start;

    char *domain = (char *)malloc(len + 1);
    if (!domain) return NULL;

    strncpy(domain, start, len);
    domain[len] = '\0';

    return domain;
}
static InfoBoxHandler handlers[] = {
    {is_dictionary_query, fetch_dict_wrapper, NULL},
    {calc_query_matches, fetch_calc_wrapper, NULL},
    {is_unit_conv_query, fetch_unit_wrapper, NULL},
    {is_currency_query, fetch_currency_wrapper, NULL},
    {is_devtools_query, fetch_devtools_wrapper, NULL},
    {is_color_widget_query, fetch_color_wrapper, NULL},
    {is_meeting_planner_query, fetch_meeting_wrapper, NULL},
    {is_datetime_widget_query, fetch_datetime_wrapper, NULL},
    {is_weather_query, fetch_weather_wrapper, NULL},
    {always_true, fetch_wiki_wrapper, construct_wiki_url},
};
enum { HANDLER_COUNT = sizeof(handlers) / sizeof(handlers[0]) };

static void *infobox_thread_func(void *arg) {
  InfoBoxThreadData *data = (InfoBoxThreadData *)arg;
  if (!data->fetch_fn)
    return NULL;
  if (data->check_fn && !data->check_fn(data->query)) {
    data->success = 0;
    return NULL;
  }

  data->result = data->fetch_fn((char *)data->query);
  data->success = (data->result.title != NULL && data->result.extract != NULL &&
                   strlen(data->result.extract) > 10);
  if (data->success && data->handler_idx == HANDLER_COUNT - 1 &&
      data->result.extract && strstr(data->result.extract, "widget-dictionary"))
    data->success = 0;
  return NULL;
}

static void format_elapsed_time(double secs, char *buf, size_t bufsize) {
  if (secs < 1.0) {
    snprintf(buf, bufsize, "%.2f seconds", secs);
  } else if (secs < 60.0) {
    snprintf(buf, bufsize, "%.2f seconds", secs);
  } else if (secs < 3600.0) {
    int m = (int)(secs / 60);
    int s = (int)(secs - m * 60);
    if (s > 0)
      snprintf(buf, bufsize, "%d minute%s %d second%s", m, m == 1 ? "" : "s",
               s, s == 1 ? "" : "s");
    else
      snprintf(buf, bufsize, "%d minute%s", m, m == 1 ? "" : "s");
  } else {
    int h = (int)(secs / 3600);
    int m = (int)((secs - h * 3600) / 60);
    if (m > 0)
      snprintf(buf, bufsize, "%d hour%s %d minute%s", h, h == 1 ? "" : "s", m,
               m == 1 ? "" : "s");
    else
      snprintf(buf, bufsize, "%d hour%s", h, h == 1 ? "" : "s");
  }
}

static const char *infobox_modifier_class(int handler_idx) {
  switch (handler_idx) {
  case 0:
    return " infobox--widget infobox--dictionary";
  case 1:
    return " infobox--widget infobox--calculator";
  case 2:
    return " infobox--widget infobox--conversion";
  case 3:
    return " infobox--widget infobox--currency";
  case 4:
    return " infobox--widget infobox--devtools";
  case 5:
    return " infobox--widget infobox--color";
  case 6:
    return " infobox--widget infobox--meeting";
  case 7:
    return " infobox--widget infobox--datetime";
  case 8:
    return " infobox--widget infobox--weather";
  case 9:
    return " infobox--wikipedia";
  default:
    return "";
  }
}

static void add_infobox_to_collection(InfoBox *infobox,
                                      StringMatrix *collection, int handler_idx,
                                      const char *read_more_label) {
  if (!infobox || !infobox->title || !infobox->title[0] || !infobox->extract ||
      !infobox->extract[0])
    return;
  char *proxied = NULL;
  const char *thumb = infobox->thumbnail_url;
  if (thumb && thumb[0] &&
      (strncmp(thumb, "http://", 7) == 0 || strncmp(thumb, "https://", 8) == 0))
    proxied = proxy_wrap_image_url(thumb);
  const char *info_url =
      (infobox->url && (url_is_web(infobox->url) ||
                        strcmp(infobox->url, "#") == 0))
          ? infobox->url
          : "#";
  const char *values[INFOBOX_FIELD_COUNT] = {
      infobox->title,        proxied ? proxied : "",
      infobox->extract,      info_url,
      read_more_label,       infobox_modifier_class(handler_idx),
  };
  string_matrix_append(collection, values, INFOBOX_FIELD_COUNT);
  free(proxied);
}

static void add_link_to_collection(const char *href, const char *label,
                                   const char *class_name,
                                   StringMatrix *collection) {
  const char *values[LINK_FIELD_COUNT] = {href, label, class_name};
  string_matrix_append(collection, values, LINK_FIELD_COUNT);
}

static void add_engine_filter(const char *href, const char *label,
                              const char *class_name, const char *icon_src,
                              StringMatrix *collection) {
  const char *values[4] = {href, label, class_name,
                           icon_src ? icon_src : ""};
  string_matrix_append(collection, values, 4);
}

static const char *engine_name_for_id(const char *id) {
  int i;
  if (!id || !id[0])
    return "";
  for (i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && strcmp(eng->id, id) == 0)
      return eng->name;
  }
  return id;
}

static char *engine_stack_html(const char *ids) {
  char copy[256];
  char names[512];
  char names_esc[768];
  char *save = NULL;
  char *token;
  FILE *fp;
  char *buf = NULL;
  size_t n = 0;
  int first = 1;

  if (!ids || !ids[0])
    return strdup("");

  snprintf(copy, sizeof(copy), "%s", ids);
  names[0] = '\0';
  for (token = strtok_r(copy, ",", &save); token;
       token = strtok_r(NULL, ",", &save)) {
    const char *label = engine_name_for_id(token);
    if (!first && strlen(names) + 2 < sizeof(names))
      strcat(names, ", ");
    strncat(names, label, sizeof(names) - strlen(names) - 1);
    first = 0;
  }
  html_escape_attr(names, names_esc, sizeof(names_esc));

  snprintf(copy, sizeof(copy), "%s", ids);
  save = NULL;
  fp = open_memstream(&buf, &n);
  if (!fp)
    return strdup("");
  fprintf(fp, "<span class=\"result-engines\" title=\"%s\">", names_esc);
  for (token = strtok_r(copy, ",", &save); token;
       token = strtok_r(NULL, ",", &save)) {
    char alt[ENGINE_NAME_MAX * 2];
    char *src;
    html_escape_attr(engine_name_for_id(token), alt, sizeof(alt));
    src = favicon_url_for_engine_id(token);
    if (src && strstr(src, "icon-placeholder")) {
      int ei;
      for (ei = 0; ei < engines_total(); ei++) {
        const SearchEngine *eng = engine_at(ei);
        if (eng && strcmp(eng->id, token) == 0 && eng->referer[0]) {
          free(src);
          src = favicon_proxy_url_for_page(eng->referer);
          break;
        }
      }
    }
    fprintf(fp,
            "<img src=\"%s\" alt=\"%s\" title=\"%s\" width=\"18\" height=\"18\" "
            "loading=\"lazy\" decoding=\"async\" class=\"engine-stack-ico\" "
            "data-imgfb=\"1\">",
            src ? src : "/static/icon-placeholder.svg", alt, alt);
    free(src);
  }
  fputs("</span>", fp);
  fclose(fp);
  return buf ? buf : strdup("");
}

static void add_warning_to_collection(const char *engine_name,
                                      const char *warning_message,
                                      StringMatrix *collection) {
  const char *values[] = {engine_name, warning_message};
  string_matrix_append(collection, values, 2);
}

static const char *warning_message_for_job(const ScrapeJob *job,
                                           const char *fetch_error_msg,
                                           const char *parse_mismatch_msg,
                                           const char *blocked_msg) {
  switch (job->status) {
  case SCRAPE_STATUS_FETCH_ERROR:
    return fetch_error_msg;
  case SCRAPE_STATUS_PARSE_MISMATCH:
    return parse_mismatch_msg;
  case SCRAPE_STATUS_BLOCKED:
    return blocked_msg;
  default:
    return NULL;
  }
}

static int engine_id_matches(const char *left, const char *right) {
  if (!left || !right)
    return 0;

  while (*left && *right) {
    char l = *left;
    char r = *right;

    if (l >= 'A' && l <= 'Z')
      l = l - 'A' + 'a';
    if (r >= 'A' && r <= 'Z')
      r = r - 'A' + 'a';

    if (l != r)
      return 0;

    left++;
    right++;
  }

  return *left == *right;
}

static const SearchEngine *find_enabled_engine(const char *engine_id) {
  if (!engine_id || engine_id[0] == '\0' || engine_id_matches(engine_id, "all"))
    return NULL;

  for (int i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && eng->enabled && engine_id_matches(eng->id, engine_id)) {
      return eng;
    }
  }

  return NULL;
}

static char *build_search_href_tv(const char *query, const char *engine_id,
                                  const char *lens, int page,
                                  const char *time_filter, int verbatim);

static char *build_search_href(const char *query, const char *engine_id,
                               const char *lens, int page) {
  return build_search_href_tv(query, engine_id, lens, page, "", 0);
}

static char *build_search_href_tv(const char *query, const char *engine_id,
                                  const char *lens, int page,
                                  const char *time_filter, int verbatim) {
  char *enc_query = curl_easy_escape(NULL, query ? query : "", 0);
  if (!enc_query)
    return NULL;
  const char *safe_query = enc_query;
  int use_engine = engine_id && engine_id[0] != '\0' &&
                   !engine_id_matches(engine_id, "all");
  size_t needed = strlen("/search?q=") + strlen(safe_query) + 1;

  if (use_engine)
    needed += strlen("&engine=") + strlen(engine_id);
  if (lens_is_valid(lens))
    needed += strlen("&lens=") + strlen(lens);
  if (page > 1)
    needed += strlen("&p=") + 16;
  if (time_filter && time_filter[0])
    needed += strlen("&t=") + strlen(time_filter);
  if (verbatim)
    needed += strlen("&v=1");

  char *href = (char *)malloc(needed);
  if (!href) {
    curl_free(enc_query);
    return NULL;
  }

  snprintf(href, needed, "/search?q=%s", safe_query);

  if (use_engine) {
    strcat(href, "&engine=");
    strcat(href, engine_id);
  }
  if (lens_is_valid(lens)) {
    strcat(href, "&lens=");
    strcat(href, lens);
  }
  if (time_filter && time_filter[0]) {
    strcat(href, "&t=");
    strcat(href, time_filter);
  }
  if (verbatim)
    strcat(href, "&v=1");

  if (page > 1) {
    char page_buf[16];
    snprintf(page_buf, sizeof(page_buf), "%d", page);
    strcat(href, "&p=");
    strcat(href, page_buf);
  }

  curl_free(enc_query);
  return href;
}

/* escape the snippet then wrap escaped query terms in <b> - both sides are
   escaped first so injected markup never survives, and the <b> itself is
   added by us after escaping */
static char *highlight_terms(const char *snippet, const char *query) {
  if (!snippet || !snippet[0])
    return strdup("");
  size_t cap = strlen(snippet) * 8 + 256;
  char *esc = malloc(cap);
  if (!esc)
    return strdup("");
  html_escape_text(snippet, esc, cap);

  if (!query || !query[0])
    return esc;

  /* collect unique alnum terms len>=2 */
  char qcopy[512];
  snprintf(qcopy, sizeof(qcopy), "%s", query);
  char *terms[32];
  int nt = 0;
  for (char *tok = strtok(qcopy, " \t"); tok && nt < 32;
       tok = strtok(NULL, " \t")) {
    /* strip operator prefixes like site: -site: " phrase quotes */
    while (*tok == '-' || *tok == '"' || *tok == '+')
      tok++;
    if (strchr(tok, ':'))
      continue;
    if (strlen(tok) < 2)
      continue;
    int dup = 0;
    for (int i = 0; i < nt; i++)
      if (!strcasecmp(terms[i], tok))
        dup = 1;
    if (!dup)
      terms[nt++] = tok;
  }

  /* longest-first so "attention" wins over "a" inside "attention" */
  for (int i = 0; i < nt; i++)
    for (int j = i + 1; j < nt; j++)
      if (strlen(terms[j]) > strlen(terms[i])) {
        char *t = terms[i];
        terms[i] = terms[j];
        terms[j] = t;
      }

  size_t ocap = strlen(esc) + nt * 8 + 256;
  char *out = malloc(ocap);
  if (!out)
    return esc;
  size_t o = 0;
  const char *p = esc;
  while (*p && o + 16 < ocap) {
    int matched = 0;
    for (int i = 0; i < nt; i++) {
      size_t tl = strlen(terms[i]);
      if (strncasecmp(p, terms[i], tl) == 0 &&
          (p == esc || !isalnum((unsigned char)p[-1])) &&
          !isalnum((unsigned char)p[tl]) &&
          !(p != esc && p[-1] == '&' && p[tl] == ';')) {
        if (o + tl + 8 < ocap) {
          memcpy(out + o, "<b>", 3);
          o += 3;
          memcpy(out + o, p, tl);
          o += tl;
          memcpy(out + o, "</b>", 4);
          o += 4;
          p += tl;
          matched = 1;
        }
        break;
      }
    }
    if (!matched)
      out[o++] = *p++;
  }
  out[o] = '\0';
  free(esc);
  return out;
}

/* "Tue, 06 Oct 2026 10:00:00 +0000" -> "6 Oct 2026" */
static char *format_result_date(const char *raw) {
  if (!raw || !raw[0])
    return NULL;
  const char *start = strstr(raw, ", ");
  start = start ? start + 2 : raw;
  while (*start == ' ')
    start++;
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  char *end = strptime(start, "%d %b %Y", &tm);
  if (!end)
    end = strptime(start, "%Y-%m-%d", &tm);
  if (!end || end == start)
    return NULL;
  char buf[32];
  if (strftime(buf, sizeof(buf), "%e %b %Y", &tm) == 0)
    return NULL;
  char *s = buf;
  while (*s == ' ')
    s++;
  return strdup(s);
}

typedef struct {
  char host[256];
  int total;
  int shown;
  int last;
} HostStat;

static int host_stat_index(HostStat *stats, int n, const char *host) {
  for (int i = 0; i < n; i++) {
    if (strcmp(stats[i].host, host) == 0)
      return i;
  }
  return -1;
}

/* keep at most MAX_RESULTS_PER_HOST results per host. hidden results count
 * against the last shown one, which gets a "more from host" link */
static void cluster_merged_results(MergedResult *merged, int n, int *keep,
                                   int *extra) {
  HostStat *stats = calloc((size_t)n, sizeof(HostStat));
  if (!stats) {
    for (int i = 0; i < n; i++)
      keep[i] = 1;
    return;
  }
  int nstats = 0;
  for (int i = 0; i < n; i++) {
    const char *host = merged[i].host ? merged[i].host : "";
    if (!host[0])
      continue;
    int s = host_stat_index(stats, nstats, host);
    if (s < 0 && nstats < n) {
      s = nstats++;
      snprintf(stats[s].host, sizeof(stats[s].host), "%s", host);
    }
    if (s >= 0)
      stats[s].total++;
  }
  for (int i = 0; i < n; i++) {
    const char *host = merged[i].host ? merged[i].host : "";
    if (!host[0]) {
      keep[i] = 1;
      continue;
    }
    int s = host_stat_index(stats, nstats, host);
    if (s < 0 || stats[s].shown < MAX_RESULTS_PER_HOST) {
      keep[i] = 1;
      if (s >= 0) {
        stats[s].shown++;
        stats[s].last = i;
      }
    }
  }
  for (int s = 0; s < nstats; s++) {
    int hidden = stats[s].total - stats[s].shown;
    if (hidden > 0)
      extra[stats[s].last] = hidden;
  }
  free(stats);
}

/* /search?q=site:host+query keeps the original terms under the site scope */
static char *build_more_from_href(const char *host, const char *query,
                                  const char *engine_id, const char *lens) {
  if (!host || !host[0])
    return NULL;
  size_t qn = strlen(host) + strlen(query ? query : "") + 8;
  char *q = malloc(qn);
  if (!q)
    return NULL;
  snprintf(q, qn, "site:%s %s", host, query ? query : "");
  char *href = build_search_href(q, engine_id, lens, 1);
  free(q);
  return href;
}

int results_handler(UrlParams *params) {
  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "format") == 0 &&
          strcmp(params->params[i].value, "json") == 0) {
        return json_search_handler(params);
      }
    }
  }

  TemplateContext ctx = new_context();
  TodayItems today = {0};
  char *raw_query = "";
  const char *selected_engine_id = "all";
  const char *selected_lens = "";
  const char *time_filter = "";
  int verbatim = 0;
  int page = 1;
  int btnI = 0;

  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);
  const char *rate_limit_msg = beaker_get_locale_value(locale, "rate_limit");
  if (!rate_limit_msg)
    rate_limit_msg = "Slow down! Too many searches from you!";
  const char *no_results_msg = beaker_get_locale_value(locale, "no_results");
  if (!no_results_msg)
    no_results_msg = "No results found";
  const char *error_render_msg =
      beaker_get_locale_value(locale, "error_render");
  if (!error_render_msg)
    error_render_msg = "Error rendering results";
  const char *did_you_mean_msg =
      beaker_get_locale_value(locale, "did_you_mean");
  if (!did_you_mean_msg)
    did_you_mean_msg = "Did you mean";
  const char *read_more_msg = beaker_get_locale_value(locale, "read_more");
  if (!read_more_msg)
    read_more_msg = "Read More";
  const char *first_msg = beaker_get_locale_value(locale, "pagination_first");
  if (!first_msg)
    first_msg = "First";
  const char *prev_msg = beaker_get_locale_value(locale, "pagination_prev");
  if (!prev_msg)
    prev_msg = "Prev";
  const char *next_msg = beaker_get_locale_value(locale, "pagination_next");
  if (!next_msg)
    next_msg = "Next";
  const char *untitled_msg = beaker_get_locale_value(locale, "untitled");
  if (!untitled_msg)
    untitled_msg = "Untitled";
  const char *slop_badge_msg = beaker_get_locale_value(locale, "slop_badge");
  if (!slop_badge_msg)
    slop_badge_msg = "AI";
  const char *warn_fetch_msg =
      beaker_get_locale_value(locale, "warning_fetch_error");
  if (!warn_fetch_msg)
    warn_fetch_msg = "request failed before Seek could read search results.";
  const char *warn_parse_msg =
      beaker_get_locale_value(locale, "warning_parse_mismatch");
  if (!warn_parse_msg)
    warn_parse_msg =
        "returned search results in a format Seek could not parse.";
  const char *warn_blocked_msg =
      beaker_get_locale_value(locale, "warning_blocked");
  if (!warn_blocked_msg)
    warn_blocked_msg =
        "returned a captcha or another blocking page instead of search "
        "results.";
  free(locale);

  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "q") == 0) {
        raw_query = params->params[i].value;
      } else if (strcmp(params->params[i].key, "p") == 0) {
        int parsed = atoi(params->params[i].value);
        if (parsed > 1)
          page = parsed;
      } else if (strcmp(params->params[i].key, "engine") == 0) {
        selected_engine_id = params->params[i].value;
      } else if (strcmp(params->params[i].key, "lens") == 0) {
        if (lens_is_valid(params->params[i].value))
          selected_lens = params->params[i].value;
      } else if (strcmp(params->params[i].key, "t") == 0) {
        time_filter = params->params[i].value;
      } else if (strcmp(params->params[i].key, "v") == 0) {
        verbatim = atoi(params->params[i].value) != 0;
      } else if (strcmp(params->params[i].key, "btnI") == 0) {
        btnI = atoi(params->params[i].value);
      }
    }
  }

  context_set(&ctx, "query", raw_query);
  {
    char *q_enc = curl_easy_escape(NULL, raw_query, 0);
    context_set(&ctx, "query_enc", q_enc ? q_enc : "");
    curl_free(q_enc);
  }

  /* lyra-only query decorations - other engines see the raw text */
  char *lyra_query = NULL;
  {
    char dbuf[16];
    dbuf[0] = '\0';
    if (time_filter[0]) {
      long days = !strcmp(time_filter, "day") ? 1
                  : !strcmp(time_filter, "week") ? 7
                  : !strcmp(time_filter, "month") ? 30 : 365;
      time_t cut = time(NULL) - days * 86400;
      struct tm tmv;
      gmtime_r(&cut, &tmv);
      strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &tmv);
    }
    size_t n = strlen(raw_query) + 40;
    lyra_query = malloc(n);
    if (lyra_query) {
      if (verbatim && time_filter[0])
        snprintf(lyra_query, n, "\"%s\" after:%s", raw_query, dbuf);
      else if (verbatim)
        snprintf(lyra_query, n, "\"%s\"", raw_query);
      else if (time_filter[0])
        snprintf(lyra_query, n, "%s after:%s", raw_query, dbuf);
      else
        snprintf(lyra_query, n, "%s", raw_query);
    }
  }
  char page_str[16];
  snprintf(page_str, sizeof(page_str), "%d", page);
  context_set(&ctx, "page", page_str);

  if (query_wants_today(raw_query)) {
    today_load(&today);
    today_apply(&ctx, &today);
  }

  if (!raw_query || strlen(raw_query) == 0) {
    today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
    send_redirect("/");
    return 0;
  }

  char *bang_redirect = bang_resolve_redirect_url(raw_query);
  if (bang_redirect) {
    send_redirect(bang_redirect);
    free(bang_redirect);
    today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
    return 0;
  }

  const SearchEngine *selected_engine = find_enabled_engine(selected_engine_id);
  if (!selected_engine)
    selected_engine_id = "all";

  if (strcmp(time_filter, "day") && strcmp(time_filter, "week") &&
      strcmp(time_filter, "month") && strcmp(time_filter, "year"))
    time_filter = "";

  context_set(&ctx, "selected_engine", selected_engine_id);
  context_set(&ctx, "selected_lens", selected_lens);
  context_set(&ctx, "time_filter", time_filter);
  context_set(&ctx, "verbatim", verbatim ? "1" : "");
  char *search_href = build_search_href(raw_query, selected_engine_id,
                                        selected_lens, 1);
  context_set(&ctx, "search_href", search_href ? search_href : "/search");
  free(search_href);

  char **user_engines = NULL;
  int user_engine_count = 0;
  int has_user_pref =
      (get_user_engines(&user_engines, &user_engine_count) == 0);

  int enabled_engine_count = 0;
  for (int i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && eng->enabled &&
        (!has_user_pref ||
         user_engines_contains(eng->id, user_engines, user_engine_count)) &&
        (!selected_engine || eng == selected_engine) &&
        (page <= 1 || eng->page_param[0] != '\0')) {
      enabled_engine_count++;
    }
  }

  char client_key[BUFFER_SIZE_SMALL];
  rate_limit_get_client_key(client_key, sizeof(client_key));

  char *request_cache_key = build_search_request_cache_key(
      raw_query, selected_engine_id, page, client_key);
  int request_is_cached = 0;

  if (request_cache_key && get_cache_ttl_search() > 0) {
    char *cached_marker = NULL;
    size_t cached_marker_size = 0;

    if (cache_get(request_cache_key, (time_t)get_cache_ttl_search(),
                  &cached_marker, &cached_marker_size) == 0) {
      request_is_cached = 1;
    }

    free(cached_marker);
  }

  if (enabled_engine_count > 0 && !request_is_cached) {
    RateLimitConfig rate_limit_config = {
        .max_requests = global_config.rate_limit_search_requests,
        .interval_seconds = global_config.rate_limit_search_interval,
    };
    RateLimitResult rate_limit_result =
        rate_limit_check("search", &rate_limit_config);
    if (rate_limit_result.limited) {
      char retry_after_header[64];
      char *page_html = render_error_page(rate_limit_msg, "", "/");
      snprintf(retry_after_header, sizeof(retry_after_header),
               "Retry-After: %d", rate_limit_result.retry_after_seconds);
      if (page_html) {
        send_response_with_status("429 Too Many Requests", page_html,
                                  retry_after_header);
        free(page_html);
      } else {
        send_response_with_status("429 Too Many Requests",
                                  "<h1>429</h1>", retry_after_header);
      }
      free(request_cache_key);
      today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
      if (user_engines) {
        for (int i = 0; i < user_engine_count; i++)
          free(user_engines[i]);
        free(user_engines);
      }
      return -1;
    }

    if (request_cache_key && get_cache_ttl_search() > 0) {
      cache_set(request_cache_key, "1", 1);
    }
  }
  free(request_cache_key);

  pthread_t infobox_threads[HANDLER_COUNT];
  InfoBoxThreadData infobox_data[HANDLER_COUNT];
  int infobox_spawned[HANDLER_COUNT];

  for (int i = 0; i < HANDLER_COUNT; i++) {
    infobox_data[i].handler_idx = i;
    infobox_data[i].success = 0;
    infobox_data[i].query = raw_query;
    infobox_data[i].fetch_fn = handlers[i].fetch_fn;
    infobox_data[i].check_fn = handlers[i].check_fn;
    infobox_data[i].result = (InfoBox){0};
    infobox_spawned[i] = 0;
  }

  /* rendered-page cache: repeat queries skip scrape, merge, and render.
     the key covers everything that changes output - query, page, engine
     filter, lens, locale, engine prefs, domain prefs, slop mode. must run
     before infobox threads spawn so an early return cannot free the stack
     frame they write into */
  char *page_key = NULL;
  {
    char *lang = get_locale(NULL);
    char *eng_c = get_cookie("engines");
    char *dpref_c = get_cookie("dprefs");
    char *forums_c = get_cookie("forums");
    char *ulens_c = get_cookie("ulens");
    char *defeng_c = get_cookie("default_engine");
    char scope[4096];
    snprintf(scope, sizeof(scope), "pg|%s|%s|%s|%s|%s|%d|%s|%s|%s|%s|%d",
             selected_engine_id, selected_lens, lang ? lang : "",
             eng_c ? eng_c : "", dpref_c ? dpref_c : "", slop_cookie_mode(),
             forums_c ? forums_c : "", ulens_c ? ulens_c : "",
             defeng_c ? defeng_c : "", time_filter, verbatim);
    page_key = cache_compute_key(raw_query, page, scope);
    free(lang);
    free(eng_c);
    free(dpref_c);
    free(forums_c);
    free(ulens_c);
    free(defeng_c);
  }
  {
    char *cached_page = NULL;
    size_t cached_len = 0;
    if (page_key &&
        cache_get(page_key, get_cache_ttl_search(), &cached_page,
                  &cached_len) == 0 &&
        cached_page) {
      send_response(cached_page);
      free(cached_page);
      free(page_key);
      free(lyra_query);
      free_context(&ctx);
      today_items_free(&today);
      if (user_engines) {
        for (int i = 0; i < user_engine_count; i++)
          free(user_engines[i]);
        free(user_engines);
      }
      return 0;
    }
  }

  if (page == 1) {
    for (int i = 0; i < HANDLER_COUNT; i++) {
      if (handlers[i].check_fn && !handlers[i].check_fn(raw_query))
        continue;
      infobox_spawned[i] = 1;
      pthread_create(&infobox_threads[i], NULL, infobox_thread_func,
                     &infobox_data[i]);
    }
  }

  ScrapeJob jobs[MAX_ENGINE_JOBS];
  SearchResult *all_results[MAX_ENGINE_JOBS];

  int engine_idx = 0;
  for (int i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && eng->enabled &&
        (!has_user_pref ||
         user_engines_contains(eng->id, user_engines, user_engine_count)) &&
        (!selected_engine || eng == selected_engine) &&
        (page <= 1 || eng->page_param[0] != '\0')) {
      all_results[engine_idx] = NULL;
      jobs[engine_idx].engine = eng;
      jobs[engine_idx].query = (!strcmp(eng->id, "lyra") && lyra_query) ? lyra_query : raw_query;
      jobs[engine_idx].out_results = &all_results[engine_idx];
      jobs[engine_idx].max_results = MAX_RESULTS_PER_ENGINE;
      jobs[engine_idx].results_count = 0;
      jobs[engine_idx].page = page;
      jobs[engine_idx].handle = NULL;
      jobs[engine_idx].response.memory = NULL;
      jobs[engine_idx].response.size = 0;
      jobs[engine_idx].response.capacity = 0;
      jobs[engine_idx].http_status = 0;
      jobs[engine_idx].status = SCRAPE_STATUS_PENDING;
      jobs[engine_idx].cache_key = NULL;
      jobs[engine_idx].cache_lock_held = 0;
      jobs[engine_idx].user_selected = selected_engine != NULL;
      jobs[engine_idx].did_you_mean = NULL;
      engine_idx++;
    }
  }

  /* a lens means the user wants that vertical - fire its engines even when
     the engine filter is something else, so lens=forums actually shows
     forum threads rather than demoted general hits */
  if (selected_lens[0] != '\0') {
    int lens_n = 0;
    const char *const *lens_engs = lens_engines(selected_lens, &lens_n);
    for (int li = 0; li < lens_n && engine_idx < MAX_ENGINE_JOBS; li++) {
      const SearchEngine *leng = NULL;
      for (int i = 0; i < engines_total(); i++) {
        const SearchEngine *e = engine_at(i);
        if (e && e->enabled && !strcmp(e->id, lens_engs[li])) {
          leng = e;
          break;
        }
      }
      if (!leng)
        continue;
      if (has_user_pref &&
          !user_engines_contains(leng->id, user_engines, user_engine_count))
        continue;
      if (page > 1 && leng->page_param[0] == '\0')
        continue;
      int already = 0;
      for (int j = 0; j < engine_idx; j++)
        if (jobs[j].engine == leng)
          already = 1;
      if (already)
        continue;
      all_results[engine_idx] = NULL;
      jobs[engine_idx].engine = leng;
      jobs[engine_idx].query = raw_query;
      jobs[engine_idx].out_results = &all_results[engine_idx];
      jobs[engine_idx].max_results = MAX_RESULTS_PER_ENGINE;
      jobs[engine_idx].results_count = 0;
      jobs[engine_idx].page = page;
      jobs[engine_idx].handle = NULL;
      jobs[engine_idx].response.memory = NULL;
      jobs[engine_idx].response.size = 0;
      jobs[engine_idx].response.capacity = 0;
      jobs[engine_idx].http_status = 0;
      jobs[engine_idx].status = SCRAPE_STATUS_PENDING;
      jobs[engine_idx].cache_key = NULL;
      jobs[engine_idx].cache_lock_held = 0;
      jobs[engine_idx].user_selected = selected_engine != NULL;
      jobs[engine_idx].did_you_mean = NULL;
      engine_idx++;
    }
  }

  /* category-grouped display order so the filter row reads sensibly */
  static const char *engine_display_order[] = {
      "lyra",
      "brave",     "ddg",        "yahoo",     "startpage", "qwant",
      "mojeek",    "mwmbl",      "wiby",      "yacy",
      "wikipedia", "wiktionary", "wikibooks", "wikiquote",
      "stackoverflow", "hn",     "reddit",    "lobsters",  "lemmy",
      "archwiki",  "debianwiki", "nixos",     "gentoo",    "mediawiki",
      "arxiv",     "s2",         "openalex",  "europepmc",
      "openlibrary", "gbooks",   "itunes",    "themealdb",
      "osm",       "peertube",
  };
  int filter_engine_count = 0;
  for (int i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && eng->enabled &&
        (!has_user_pref ||
         user_engines_contains(eng->id, user_engines, user_engine_count)))
      filter_engine_count++;
  }

  if (filter_engine_count > 1) {
    StringMatrix filter_matrix;
    string_matrix_init(&filter_matrix);
    char *all_href =
        build_search_href(raw_query, "all", selected_lens, 1);

    add_engine_filter(
        all_href, "All",
        selected_engine ? "engine-filter" : "engine-filter active", "",
        &filter_matrix);
    free(all_href);

    /* walk the display-order list first, then any unlisted engines */
    for (size_t oi = 0; oi < sizeof(engine_display_order) /
                                sizeof(engine_display_order[0]);
         oi++) {
      const SearchEngine *eng = NULL;
      for (int i = 0; i < engines_total(); i++) {
        const SearchEngine *e = engine_at(i);
        if (e && !strcmp(e->id, engine_display_order[oi])) {
          eng = e;
          break;
        }
      }
      if (!eng || !eng->enabled ||
          (has_user_pref && !user_engines_contains(
                                eng->id, user_engines, user_engine_count)))
        continue;
      char *filter_href =
          build_search_href(raw_query, eng->id, selected_lens, 1);
      const char *filter_class =
          (selected_engine && eng == selected_engine) ? "engine-filter active"
                                                       : "engine-filter";
      char *icon = favicon_url_for_engine_id(eng->id);
      add_engine_filter(filter_href, eng->name, filter_class,
                        icon ? icon : "", &filter_matrix);
      free(icon);
      free(filter_href);
    }
    for (int i = 0; i < engines_total(); i++) {
      const SearchEngine *eng = engine_at(i);
      if (!eng || !eng->enabled ||
          (has_user_pref && !user_engines_contains(
                                eng->id, user_engines, user_engine_count)))
        continue;
      int listed = 0;
      for (size_t oi = 0; oi < sizeof(engine_display_order) /
                                  sizeof(engine_display_order[0]);
           oi++)
        if (!strcmp(eng->id, engine_display_order[oi]))
          listed = 1;
      if (listed)
        continue;
      char *filter_href =
          build_search_href(raw_query, eng->id, selected_lens, 1);
      const char *filter_class =
          (selected_engine && eng == selected_engine) ? "engine-filter active"
                                                       : "engine-filter";
      char *icon = favicon_url_for_engine_id(eng->id);
      add_engine_filter(filter_href, eng->name, filter_class,
                        icon ? icon : "", &filter_matrix);
      free(icon);
      free(filter_href);
    }

    /* first N stay as chips, the rest go into the overflow dropdown */
    const int visible = 10;
    int vis_n = filter_matrix.count < visible ? filter_matrix.count : visible;
    if (vis_n > 0) {
      context_set_array_of_arrays(&ctx, "engine_filters", filter_matrix.rows,
                                  vis_n, filter_matrix.field_counts);
    }
    if (filter_matrix.count > vis_n) {
      context_set_array_of_arrays(&ctx, "engine_filters_more",
                                  filter_matrix.rows + vis_n,
                                  filter_matrix.count - vis_n,
                                  filter_matrix.field_counts);
    }
    string_matrix_free(&filter_matrix);
  }

  {
    static const char *lenses[] = {"",        "forums",  "docs",   "wiki",
                                   "papers",   "recipes", "books",  "podcasts",
                                   "shopping", "news",    "maps",   "videos",
                                   "custom"};
    int has_custom = 0;
    {
      char *u = get_cookie("ulens");
      has_custom = (u && u[0]);
      free(u);
    }
    StringMatrix lens_matrix;
    string_matrix_init(&lens_matrix);
    size_t lens_n = sizeof(lenses) / sizeof(lenses[0]);
    if (!has_custom)
      lens_n--; /* drop the custom chip when no domains are configured */
    for (size_t li = 0; li < lens_n; li++) {
      const char *id = lenses[li];
      char *lens_href =
          build_search_href_tv(raw_query, selected_engine_id, id, 1,
                               time_filter, verbatim);
      const char *label = "All";
      if (!strcmp(id, "forums"))
        label = "Forums";
      else if (!strcmp(id, "docs"))
        label = "Docs";
      else if (!strcmp(id, "wiki"))
        label = "Wikis";
      else if (!strcmp(id, "papers"))
        label = "Papers";
      else if (!strcmp(id, "recipes"))
        label = "Recipes";
      else if (!strcmp(id, "books"))
        label = "Books";
      else if (!strcmp(id, "podcasts"))
        label = "Podcasts";
      else if (!strcmp(id, "shopping"))
        label = "Shopping";
      else if (!strcmp(id, "news"))
        label = "News";
      else if (!strcmp(id, "maps"))
        label = "Maps";
      else if (!strcmp(id, "videos"))
        label = "Videos";
      else if (!strcmp(id, "custom"))
        label = "Custom";
      const char *icon = "";
      if (!strcmp(id, ""))
        icon = "ui-icon-layout-grid";
      else if (!strcmp(id, "forums"))
        icon = "ui-icon-message-square";
      else if (!strcmp(id, "docs"))
        icon = "ui-icon-file-text";
      else if (!strcmp(id, "wiki"))
        icon = "ui-icon-book-open";
      else if (!strcmp(id, "papers"))
        icon = "ui-icon-graduation-cap";
      else if (!strcmp(id, "recipes"))
        icon = "ui-icon-utensils";
      else if (!strcmp(id, "books"))
        icon = "ui-icon-library";
      else if (!strcmp(id, "podcasts"))
        icon = "ui-icon-audio-lines";
      else if (!strcmp(id, "shopping"))
        icon = "ui-icon-shopping-bag";
      else if (!strcmp(id, "news"))
        icon = "ui-icon-newspaper";
      else if (!strcmp(id, "maps"))
        icon = "ui-icon-map-pin";
      else if (!strcmp(id, "videos"))
        icon = "ui-icon-play";
      else if (!strcmp(id, "custom"))
        icon = "ui-icon-funnel";
      const char *cls = !strcmp(id, selected_lens) ? "engine-filter active"
                                                   : "engine-filter";
      add_engine_filter(lens_href, label, cls, icon, &lens_matrix);
      free(lens_href);
    }
    if (lens_matrix.count > 0) {
      context_set_array_of_arrays(&ctx, "lens_filters", lens_matrix.rows,
                                  lens_matrix.count,
                                  lens_matrix.field_counts);
    }
    string_matrix_free(&lens_matrix);
  }

  /* time filter chips - carried by every link via build_search_href_tv */
  {
    static const char *const time_vals[] = {"", "day", "week", "month", "year"};
    static const char *const time_labels[] = {"Any time", "Past 24h",
                                              "Past week", "Past month",
                                              "Past year"};
    StringMatrix time_matrix;
    string_matrix_init(&time_matrix);
    for (int ti = 0; ti < 5; ti++) {
      char *th = build_search_href_tv(raw_query, selected_engine_id,
                                      selected_lens, 1, time_vals[ti],
                                      verbatim);
      const char *cls = !strcmp(time_vals[ti], time_filter)
                            ? "engine-filter active"
                            : "engine-filter";
      add_engine_filter(th, time_labels[ti], cls, "", &time_matrix);
      free(th);
    }
    context_set_array_of_arrays(&ctx, "time_filters", time_matrix.rows,
                                time_matrix.count, time_matrix.field_counts);
    string_matrix_free(&time_matrix);
  }

  if (engine_idx > 0) {
    struct timeval tv_start, tv_end;
    gettimeofday(&tv_start, NULL);
    scrape_engines_parallel(jobs, engine_idx);
    gettimeofday(&tv_end, NULL);
    double elapsed =
        (tv_end.tv_sec - tv_start.tv_sec) +
        (tv_end.tv_usec - tv_start.tv_usec) / 1000000.0;

    char search_time_buf[64];
    format_elapsed_time(elapsed, search_time_buf, sizeof(search_time_buf));
    context_set(&ctx, "search_time", search_time_buf);
  }

  if (page == 1) {
    for (int i = 0; i < HANDLER_COUNT; i++) {
      if (infobox_spawned[i])
        pthread_join(infobox_threads[i], NULL);
    }
  }

  if (btnI) {
    for (int i = 0; i < engine_idx; i++) {
      if (jobs[i].results_count > 0 && all_results[i][0].url) {
        char *redirect_url = strdup(all_results[i][0].url);
        for (int j = 0; j < enabled_engine_count; j++)
          xml_result_free(all_results[j], jobs[j].results_count);
        if (page == 1) {
          for (int j = 0; j < HANDLER_COUNT; j++) {
            free_infobox(&infobox_data[j].result);
          }
        }
        today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
        if (redirect_url) {
          send_redirect(redirect_url);
          free(redirect_url);
        }
        return 0;
      }
    }
    for (int i = 0; i < enabled_engine_count; i++) {
      free(all_results[i]);
    }
    if (page == 1) {
      for (int i = 0; i < HANDLER_COUNT; i++) {
        free_infobox(&infobox_data[i].result);
      }
    }
    today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
    char *page_html =
        render_error_page(no_results_msg, "", search_href ? "/search" : "/");
    if (page_html) {
      send_response(page_html);
      free(page_html);
    } else {
      send_response("<h1>No results</h1>");
    }
    return 0;
  }

  StringMatrix infobox_matrix;
  string_matrix_init(&infobox_matrix);

  if (page == 1) {
    for (int i = 0; i < HANDLER_COUNT; i++) {
      if (!infobox_data[i].success)
        continue;
      int dup = 0;
      for (int j = 0; j < i; j++) {
        if (!infobox_data[j].success || !infobox_data[j].result.title ||
            !infobox_data[i].result.title)
          continue;
        if (strcmp(infobox_data[j].result.title,
                   infobox_data[i].result.title) == 0) {
          dup = 1;
          break;
        }
      }
      if (dup)
        continue;
      add_infobox_to_collection(&infobox_data[i].result, &infobox_matrix, i,
                                read_more_msg);
    }
  }

  if (infobox_matrix.count > 0) {
    context_set_array_of_arrays(&ctx, "infoboxes", infobox_matrix.rows,
                                infobox_matrix.count,
                                infobox_matrix.field_counts);
  }
  string_matrix_free(&infobox_matrix);

  int total_results = 0;
  int failed_engines = 0;
  for (int i = 0; i < engine_idx; i++) {
    total_results += jobs[i].results_count;
    if (jobs[i].results_count == 0)
      failed_engines++;
  }

  if (failed_engines > 0 && total_results > 0) {
    context_set(&ctx, "partial_results", "1");
  } else if (total_results == 0 && engine_idx > 0 &&
             failed_engines == engine_idx) {
    context_set(&ctx, "all_failed", "1");
  }

  if (total_results == 0) {
    int warning_count = 0;
    for (int i = 0; i < enabled_engine_count; i++) {
      if (warning_message_for_job(&jobs[i], warn_fetch_msg, warn_parse_msg,
                                  warn_blocked_msg))
        warning_count++;
    }

    if (warning_count > 0) {
      StringMatrix warning_matrix;
      string_matrix_init(&warning_matrix);

      for (int i = 0; i < enabled_engine_count; i++) {
        const char *warning_message = warning_message_for_job(
            &jobs[i], warn_fetch_msg, warn_parse_msg, warn_blocked_msg);
        if (!warning_message)
          continue;

        add_warning_to_collection(jobs[i].engine->name, warning_message,
                                  &warning_matrix);
      }

      if (warning_matrix.count > 0) {
        context_set_array_of_arrays(&ctx, "engine_warnings", warning_matrix.rows,
                                    warning_matrix.count,
                                    warning_matrix.field_counts);
      }
      string_matrix_free(&warning_matrix);
    }
  }

  if (total_results > 0) {
    MergedResult *merged = NULL;
    int unique_count =
        merge_rank_results(jobs, all_results, engine_idx, raw_query,
                           selected_lens, &merged);
    double *slop_scores = NULL;
    int slop_mode = slop_cookie_mode();
    if (unique_count > 0)
      unique_count =
          apply_slop_to_merged(merged, unique_count, slop_mode, &slop_scores);
    int *keep = NULL;
    int *extra = NULL;
    int rendered_count = unique_count;
    {
      char rc[16];
      snprintf(rc, sizeof(rc), "%d", rendered_count);
      context_set(&ctx, "result_count", rc);
    }
    if (unique_count > 0) {
      keep = calloc((size_t)unique_count, sizeof(int));
      extra = calloc((size_t)unique_count, sizeof(int));
      if (keep && extra) {
        if (strcasestr(raw_query, "site:")) {
          for (int i = 0; i < unique_count; i++)
            keep[i] = 1;
        } else {
          cluster_merged_results(merged, unique_count, keep, extra);
        }
        rendered_count = 0;
        for (int i = 0; i < unique_count; i++)
          rendered_count += keep[i];
      } else {
        free(keep);
        free(extra);
        keep = NULL;
        extra = NULL;
      }
    }
    char ***results_matrix = NULL;
    int *results_inner_counts = NULL;
    if (rendered_count > 0) {
      results_matrix = (char ***)malloc(sizeof(char **) * rendered_count);
      results_inner_counts = (int *)malloc(sizeof(int) * rendered_count);
    }
    if (unique_count < 0 ||
        (rendered_count > 0 &&
         (!results_matrix || !results_inner_counts))) {
      free(keep);
      free(extra);
      free_merged_results(merged, unique_count < 0 ? 0 : unique_count);
      free(slop_scores);
      if (results_matrix)
        free(results_matrix);
      if (results_inner_counts)
        free(results_inner_counts);
      char *html = render_template("results.html", &ctx);
      if (html) {
        if (page_key)
          cache_set(page_key, html, strlen(html));
        send_response(html);
        free(html);
      }
      free_search_jobs(jobs, all_results, engine_idx);
      if (page == 1) {
        for (int i = 0; i < HANDLER_COUNT; i++) {
          free_infobox(&infobox_data[i].result);
        }
      }
      today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
      if (user_engines) {
        for (int i = 0; i < user_engine_count; i++)
          free(user_engines[i]);
        free(user_engines);
      }
      return 0;
    }

    int w = 0;
    for (int i = 0; i < unique_count; i++) {
      if (keep && !keep[i])
        continue;
      char *display_url = merged[i].url;

      results_matrix[w] =
          (char **)malloc(sizeof(char *) * RESULT_FIELD_COUNT);
      if (!results_matrix[w]) {
        free(keep);
        free(extra);
        free_merged_results(merged, unique_count);
        free(slop_scores);
        for (int k = 0; k < w; k++) {
          for (int f = 0; f < RESULT_FIELD_COUNT; f++)
            free(results_matrix[k][f]);
          free(results_matrix[k]);
        }
        free(results_matrix);
        free(results_inner_counts);
        free_search_jobs(jobs, all_results, engine_idx);
        if (page == 1) {
          for (int t = 0; t < HANDLER_COUNT; t++) {
            free_infobox(&infobox_data[t].result);
          }
        }
        char *html = render_template("results.html", &ctx);
        if (html) {
          send_response(html);
          free(html);
        }
        today_items_free(&today);
    free(lyra_query);
    free_context(&ctx);
        if (user_engines) {
          for (int t = 0; t < user_engine_count; t++)
            free(user_engines[t]);
          free(user_engines);
        }
        return 0;
      }
      char *pretty_url = pretty_display_url(display_url);

      results_matrix[w][0] = strdup(display_url ? display_url : "");
      results_matrix[w][1] = strdup(pretty_url ? pretty_url : "");
      results_matrix[w][2] =
          merged[i].title ? strdup(merged[i].title) : strdup(untitled_msg);
      results_matrix[w][3] =
          merged[i].snippet ? strdup(merged[i].snippet) : strdup("");
      results_matrix[w][11] =
          highlight_terms(merged[i].snippet, raw_query);
      results_matrix[w][4] = favicon_proxy_url_for_page(display_url);
      if (!results_matrix[w][4])
        results_matrix[w][4] = strdup("");
      results_matrix[w][5] = engine_stack_html(merged[i].engine_ids);
      if (slop_mode == SLOP_SCORE && slop_scores && slop_scores[i] >= 0.35) {
        char badge[64];
        snprintf(badge, sizeof(badge), "%s %d%%", slop_badge_msg,
                 (int)(slop_scores[i] * 100.0 + 0.5));
        results_matrix[w][6] = strdup(badge);
      } else {
        results_matrix[w][6] = strdup("");
      }
      results_matrix[w][7] =
          merged[i].host ? strdup(merged[i].host) : strdup("");
      char *date_str = format_result_date(merged[i].date);
      results_matrix[w][8] = date_str ? date_str : strdup("");
      if (extra && extra[i] > 0) {
        char *more_href =
            build_more_from_href(merged[i].host, raw_query,
                                 selected_engine_id, selected_lens);
        results_matrix[w][9] = more_href ? more_href : strdup("");
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", extra[i]);
        results_matrix[w][10] = strdup(cnt);
      } else {
        results_matrix[w][9] = strdup("");
        results_matrix[w][10] = strdup("");
      }

      results_inner_counts[w] = RESULT_FIELD_COUNT;
      w++;

      free(pretty_url);
    }

    free(keep);
    free(extra);
    free_merged_results(merged, unique_count);
    free(slop_scores);
    free_search_jobs(jobs, all_results, engine_idx);

    if (rendered_count > 0) {
      context_set_array_of_arrays(&ctx, "results", results_matrix,
                                  rendered_count, results_inner_counts);
      /* thin result set - surface the index spell suggestion above the
         results rather than only on a fully empty page */
      if (rendered_count < 3) {
        context_set(&ctx, "did_you_mean_msg", did_you_mean_msg);
        for (int i = 0; i < engine_idx; i++) {
          if (jobs[i].did_you_mean && *jobs[i].did_you_mean) {
            context_set(&ctx, "thin_suggestion", jobs[i].did_you_mean);
            char *sug_enc = curl_easy_escape(NULL, jobs[i].did_you_mean, 0);
            char sug_href[512];
            snprintf(sug_href, sizeof(sug_href), "/search?q=%s",
                     sug_enc ? sug_enc : "");
            curl_free(sug_enc);
            context_set(&ctx, "thin_suggestion_href", sug_href);
            break;
          }
        }
      }
    } else {
      context_set(&ctx, "no_results", "1");
      context_set(&ctx, "no_results_msg", no_results_msg);
    }

    if (page > 1 || rendered_count >= 10) {
    StringMatrix pager_matrix;
    string_matrix_init(&pager_matrix);
    int pager_start = page <= 3 ? 1 : page - 2;
    int pager_end = pager_start + PAGER_WINDOW_SIZE - 1;

    if (page > 3) {
      char *first_href = build_search_href_tv(raw_query, selected_engine_id,
                                     selected_lens, 1, time_filter, verbatim);
      add_link_to_collection(first_href, first_msg, "pagination-btn",
                             &pager_matrix);
      free(first_href);
    }

    if (page > 1) {
      char *prev_href =
          build_search_href_tv(raw_query, selected_engine_id,
                                     selected_lens, page - 1, time_filter, verbatim);
      add_link_to_collection(prev_href, prev_msg, "pagination-btn prev",
                             &pager_matrix);
      free(prev_href);
    }

    for (int i = pager_start; i <= pager_end; i++) {
      char label[16];
      snprintf(label, sizeof(label), "%d", i);
      char *page_href = build_search_href_tv(raw_query, selected_engine_id,
                                   selected_lens, i, time_filter, verbatim);
      add_link_to_collection(
          page_href, label,
          i == page ? "pagination-btn pagination-current" : "pagination-btn",
          &pager_matrix);
      free(page_href);
    }

    char *next_href = build_search_href(raw_query, selected_engine_id,
                                     selected_lens, page + 1);
    add_link_to_collection(next_href, next_msg, "pagination-btn next",
                             &pager_matrix);
    free(next_href);

    if (pager_matrix.count > 0) {
      context_set_array_of_arrays(&ctx, "pagination_links", pager_matrix.rows,
                                  pager_matrix.count,
                                  pager_matrix.field_counts);
    }
    string_matrix_free(&pager_matrix);
    }

    char *html = render_template("results.html", &ctx);
    if (html) {
      if (page_key)
        cache_set(page_key, html, strlen(html));
      send_response(html);
      free(html);
    } else {
      char *page_html = render_error_page(error_render_msg, "", "/");
      if (page_html) {
        send_response_with_status("500 Internal Server Error", page_html,
                                  NULL);
        free(page_html);
      } else {
        send_status("500 Internal Server Error");
      }
    }

    free_string_matrix(results_matrix, results_inner_counts,
                               rendered_count);
  } else {
    context_set(&ctx, "no_results", "1");
    context_set(&ctx, "no_results_msg", no_results_msg);
    context_set(&ctx, "did_you_mean_msg", did_you_mean_msg);

    /* prefer the index's own correction - it knows which terms actually
       have documents; fall back to the upstream suggestion */
    char *suggestion = NULL;
    for (int i = 0; i < engine_idx; i++) {
      if (jobs[i].did_you_mean && *jobs[i].did_you_mean) {
        suggestion = strdup(jobs[i].did_you_mean);
        break;
      }
    }
    if (!suggestion)
      suggestion = fetch_did_you_mean(raw_query);
    if (suggestion) {
      context_set(&ctx, "suggestion", suggestion);
      char *sug_enc = curl_easy_escape(NULL, suggestion, 0);
      char sug_href[512];
      snprintf(sug_href, sizeof(sug_href), "/search?q=%s",
               sug_enc ? sug_enc : "");
      curl_free(sug_enc);
      context_set(&ctx, "suggestion_href", sug_href);
      free(suggestion);
    }

    char *html = render_template("results.html", &ctx);
    if (html) {
      if (page_key)
        cache_set(page_key, html, strlen(html));
      send_response(html);
      free(html);
    } else {
      char *page_html = render_error_page(error_render_msg, "", "/");
      if (page_html) {
        send_response_with_status("500 Internal Server Error", page_html,
                                  NULL);
        free(page_html);
      } else {
        send_status("500 Internal Server Error");
      }
    }

    free_search_jobs(jobs, all_results, engine_idx);
  }

  if (page == 1) {
    for (int i = 0; i < HANDLER_COUNT; i++) {
      free_infobox(&infobox_data[i].result);
    }
  }
  today_items_free(&today);
  free(lyra_query);
  free_context(&ctx);
  if (user_engines) {
    for (int i = 0; i < user_engine_count; i++)
      free(user_engines[i]);
    free(user_engines);
  }

  return 0;
}

static char *xml_escape_text(const char *s) {
  size_t n = 0;
  const char *p = s ? s : "";
  for (; *p; p++) {
    if (*p == '&')
      n += 5;
    else if (*p == '<' || *p == '>')
      n += 4;
    else if (*p == '"' || *p == '\'')
      n += 6;
    else
      n++;
  }
  char *out = malloc(n + 1);
  if (!out)
    return NULL;
  char *w = out;
  for (p = s ? s : ""; *p; p++) {
    if (*p == '&') {
      memcpy(w, "&amp;", 5);
      w += 5;
    } else if (*p == '<') {
      memcpy(w, "&lt;", 4);
      w += 4;
    } else if (*p == '>') {
      memcpy(w, "&gt;", 4);
      w += 4;
    } else if (*p == '"') {
      memcpy(w, "&quot;", 6);
      w += 6;
    } else if (*p == '\'') {
      memcpy(w, "&apos;", 6);
      w += 6;
    } else
      *w++ = *p;
  }
  *w = '\0';
  return out;
}

/* Fetch the DDG autocomplete endpoint and return the first suggestion that
   differs from the query, i.e. a likely spelling correction. */
static char *fetch_did_you_mean(const char *query) {
  if (!query || !query[0])
    return NULL;

  CURL *tmp = curl_easy_init();
  if (!tmp)
    return NULL;
  char *enc = curl_easy_escape(tmp, query, 0);
  curl_easy_cleanup(tmp);
  if (!enc)
    return NULL;

  char url[1024];
  snprintf(url, sizeof(url),
           "https://duckduckgo.com/ac/?q=%s&type=list", enc);
  free(enc);

  HttpResponse resp = http_get(url, NULL);
  if (!resp.memory)
    return NULL;

  /* shape: ["query",["s1","s2",...]] pull first element of the array */
  const char *p = strstr(resp.memory, "[[");
  char *best = NULL;
  if (p) {
    const char *v = strchr(p + 2, '"');
    if (v) {
      const char *e = strchr(v + 1, '"');
      if (e && e > v + 1) {
        size_t n = (size_t)(e - (v + 1));
        char *cand = malloc(n + 1);
        if (cand) {
          memcpy(cand, v + 1, n);
          cand[n] = '\0';
          if (strcasecmp(cand, query) != 0)
            best = cand;
          else
            free(cand);
        }
      }
    }
  }

  http_response_free(&resp);
  return best;
}

static void free_search_jobs(ScrapeJob *jobs, SearchResult **all_results,
                             int engine_idx) {
  for (int i = 0; i < engine_idx; i++) {
    if (!all_results[i])
      continue;
    for (int j = 0; j < jobs[i].results_count; j++) {
      free(all_results[i][j].url);
      free(all_results[i][j].title);
      free(all_results[i][j].snippet);
      free(all_results[i][j].date);
    }
    free(all_results[i]);
  }
}

static int setup_search_jobs(const char *raw_query, const char *selected_engine_id,
                             int page, ScrapeJob *jobs,
                             SearchResult **all_results,
                             const char *lyra_query) {
  const SearchEngine *selected_engine = find_enabled_engine(selected_engine_id);
  int engine_idx = 0;

  char **user_engines = NULL;
  int user_engine_count = 0;
  int has_user_pref =
      (get_user_engines(&user_engines, &user_engine_count) == 0);

  for (int i = 0; i < engines_total(); i++) {
    const SearchEngine *eng = engine_at(i);
    if (eng && eng->enabled &&
        (!has_user_pref ||
         user_engines_contains(eng->id, user_engines, user_engine_count)) &&
        (!selected_engine || eng == selected_engine) &&
        (page <= 1 || eng->page_param[0] != '\0')) {
      all_results[engine_idx] = NULL;
      jobs[engine_idx].engine = eng;
      jobs[engine_idx].query = (!strcmp(eng->id, "lyra") && lyra_query) ? lyra_query : (char *)raw_query;
      jobs[engine_idx].out_results = &all_results[engine_idx];
      jobs[engine_idx].max_results = MAX_RESULTS_PER_ENGINE;
      jobs[engine_idx].results_count = 0;
      jobs[engine_idx].page = page;
      jobs[engine_idx].handle = NULL;
      jobs[engine_idx].response.memory = NULL;
      jobs[engine_idx].response.size = 0;
      jobs[engine_idx].response.capacity = 0;
      jobs[engine_idx].http_status = 0;
      jobs[engine_idx].status = SCRAPE_STATUS_PENDING;
      jobs[engine_idx].cache_key = NULL;
      jobs[engine_idx].cache_lock_held = 0;
      jobs[engine_idx].user_selected = selected_engine != NULL;
      jobs[engine_idx].did_you_mean = NULL;
      engine_idx++;
    }
  }

  if (engine_idx > 0)
    scrape_engines_parallel(jobs, engine_idx);

  if (user_engines) {
    for (int i = 0; i < user_engine_count; i++)
      free(user_engines[i]);
    free(user_engines);
  }

  return engine_idx;
}

int json_search_handler(UrlParams *params) {
  char *raw_query = "";
  const char *selected_engine_id = "all";
  int page = 1;

  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "q") == 0) {
        raw_query = params->params[i].value;
      } else if (strcmp(params->params[i].key, "p") == 0) {
        int parsed = atoi(params->params[i].value);
        if (parsed > 1)
          page = parsed;
      } else if (strcmp(params->params[i].key, "engine") == 0) {
        selected_engine_id = params->params[i].value;
      }
    }
  }

  if (!raw_query || strlen(raw_query) == 0) {
    serve_data("{\"redirect\":\"/\"}", 18, "application/json; charset=UTF-8");
    return 0;
  }

  char *bang_redirect = bang_resolve_redirect_url(raw_query);
  if (bang_redirect) {
    char *buf = NULL;
    size_t blen = 0;
    FILE *fp = open_memstream(&buf, &blen);
    if (fp) {
      char *loc = json_string_value(bang_redirect);
      fprintf(fp, "{\"redirect\":%s}", loc ? loc : "\"\"");
      fclose(fp);
      free(loc);
      if (buf && blen > 0)
        serve_data(buf, blen, "application/json; charset=UTF-8");
      free(buf);
    }
    free(bang_redirect);
    return 0;
  }

  if (!find_enabled_engine(selected_engine_id))
    selected_engine_id = "all";

  ScrapeJob jobs[MAX_ENGINE_JOBS];
  SearchResult *all_results[MAX_ENGINE_JOBS];
  int engine_idx =
      setup_search_jobs(raw_query, selected_engine_id, page, jobs, all_results, NULL);

  char *buf = NULL;
  size_t blen = 0;
  FILE *fp = open_memstream(&buf, &blen);
  if (!fp) {
    free_search_jobs(jobs, all_results, engine_idx);
    return -1;
  }

  char *q_esc = json_string_value(raw_query);
  char *eng_esc = json_string_value(selected_engine_id);
  fprintf(fp,
          "{\"query\":%s,\"page\":%d,\"engine\":%s,\"results\":[",
          q_esc ? q_esc : "\"\"", page, eng_esc ? eng_esc : "\"all\"");

  MergedResult *merged = NULL;
  int merged_count =
      merge_rank_results(jobs, all_results, engine_idx, raw_query, NULL,
                         &merged);

  int first = 1;
  enum { JSON_MAX_ITEMS = 50 };
  for (int i = 0; i < merged_count && i < JSON_MAX_ITEMS; i++) {
    MergedResult *r = &merged[i];
    char *t_esc = json_string_value(r->title ? r->title : "");
    char *s_esc = json_string_value(r->snippet ? r->snippet : "");
    char *u_esc = json_string_value(r->url ? r->url : "");
    char *e_esc = json_string_value(r->engine_ids);
    fprintf(fp, "%s{\"url\":%s,\"title\":%s,\"snippet\":%s,\"engines\":%s}",
            first ? "" : ",", u_esc ? u_esc : "\"\"",
            t_esc ? t_esc : "\"\"", s_esc ? s_esc : "\"\"",
            e_esc ? e_esc : "\"\"");
    first = 0;
    free(t_esc);
    free(s_esc);
    free(u_esc);
    free(e_esc);
  }

  fprintf(fp, "]}");
  fclose(fp);
  free(q_esc);
  free(eng_esc);

  if (buf && blen > 0)
    serve_data(buf, blen, "application/json; charset=UTF-8");
  free(buf);

  free_merged_results(merged, merged_count);
  free_search_jobs(jobs, all_results, engine_idx);
  return 0;
}

int rss_handler(UrlParams *params) {
  char *raw_query = "";
  const char *selected_engine_id = "all";
  int page = 1;

  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "q") == 0) {
        raw_query = params->params[i].value;
      } else if (strcmp(params->params[i].key, "p") == 0) {
        int parsed = atoi(params->params[i].value);
        if (parsed > 1)
          page = parsed;
      } else if (strcmp(params->params[i].key, "engine") == 0) {
        selected_engine_id = params->params[i].value;
      }
    }
  }

  if (!raw_query || strlen(raw_query) == 0) {
    send_response("<?xml version=\"1.0\" encoding=\"UTF-8\"?><error>No query</error>");
    return -1;
  }

  char *bang_redirect = bang_resolve_redirect_url(raw_query);
  if (bang_redirect) {
    send_redirect(bang_redirect);
    free(bang_redirect);
    return 0;
  }

  if (!find_enabled_engine(selected_engine_id))
    selected_engine_id = "all";

  ScrapeJob jobs[MAX_ENGINE_JOBS];
  SearchResult *all_results[MAX_ENGINE_JOBS];
  int engine_idx =
      setup_search_jobs(raw_query, selected_engine_id, page, jobs, all_results, NULL);

  char *buf = NULL;
  size_t blen = 0;
  FILE *fp = open_memstream(&buf, &blen);
  if (!fp)
    return -1;

  char *q_esc = xml_escape_text(raw_query);
  char *search_href =
      build_search_href(raw_query, selected_engine_id, NULL, page);
  char channel_link[BUFFER_SIZE_LARGE];
  if (global_config.domain[0] != '\0' && search_href) {
    snprintf(channel_link, sizeof(channel_link), "%s%s", global_config.domain,
             search_href);
  } else if (search_href) {
    snprintf(channel_link, sizeof(channel_link), "%s", search_href);
  } else {
    channel_link[0] = '/';
    channel_link[1] = '\0';
  }
  char *clink_esc = xml_escape_text(channel_link);

  fprintf(fp,
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<rss version=\"2.0\">\n<channel>\n"
          "<title>Seek: %s</title>\n"
          "<link>%s</link>\n"
          "<description>Metasearch snapshot for this query</description>\n",
          q_esc ? q_esc : "", clink_esc ? clink_esc : "");

  MergedResult *merged = NULL;
  int merged_count =
      merge_rank_results(jobs, all_results, engine_idx, raw_query, NULL,
                         &merged);

  enum { RSS_MAX_ITEMS = 20 };
  for (int i = 0; i < merged_count && i < RSS_MAX_ITEMS; i++) {
    MergedResult *r = &merged[i];
    char *t_esc = xml_escape_text(r->title ? r->title : "");
    char *s_esc = xml_escape_text(r->snippet ? r->snippet : "");
    char *u_esc = xml_escape_text(r->url ? r->url : "");
    fprintf(fp, "<item>\n<title>%s</title>\n<link>%s</link>\n",
            t_esc ? t_esc : "", u_esc ? u_esc : "");
    if (s_esc && s_esc[0])
      fprintf(fp, "<description>%s</description>\n", s_esc);
    fprintf(fp, "</item>\n");
    free(t_esc);
    free(s_esc);
    free(u_esc);
  }

  fprintf(fp, "</channel>\n</rss>\n");
  fclose(fp);
  free(q_esc);
  free(clink_esc);
  free(search_href);

  if (buf && blen > 0)
    serve_data(buf, blen, "application/rss+xml");

  free(buf);

  free_merged_results(merged, merged_count);
  free_search_jobs(jobs, all_results, engine_idx);
  return 0;
}
