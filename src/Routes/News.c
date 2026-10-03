#include "News.h"
#include "../Cache/Cache.h"
#include "../Limiter/RateLimit.h"
#include "../Routes/Favicon.h"
#include "../Utility/HttpClient.h"
#include "../Utility/ErrorPage.h"
#include "../Utility/JsonHelper.h"
#include "../Utility/Utility.h"
#include "../Utility/Unescape.h"
#include "Config.h"
#include <ctype.h>
#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NEWS_ITEMS_PER_SOURCE 8
#define NEWS_MAX_ITEMS 96
#define NEWS_FIELD_COUNT 8
#define NEWS_DESC_MAX 320
#define NEWS_PILLS 8

typedef struct {
  const char *id;
  const char *name;
  const char *feed_url;
  const char *site_url;
} NewsSource;

typedef struct {
  char *title;
  char *link;
  char *description;
  char *date;
  char *source_id;
  char *source_name;
  char *display_date;
  char *iso_date;
} NewsItem;

static const NewsSource DEFAULT_SOURCES[] = {
    {"bbc", "BBC News", "https://feeds.bbci.co.uk/news/rss.xml",
     "https://www.bbc.com/"},
    {"npr", "NPR", "https://feeds.npr.org/1001/rss.xml", "https://www.npr.org/"},
    {"guardian", "The Guardian", "https://www.theguardian.com/world/rss",
     "https://www.theguardian.com/"},
    {"reuters", "Reuters", "https://feeds.reuters.com/reuters/topNews",
     "https://www.reuters.com/"},
    {"ap", "AP", "https://feeds.apnews.com/rss/apf-topnews",
     "https://apnews.com/"},
    {"cnn", "CNN", "http://rss.cnn.com/rss/cnn_topstories.rss",
     "https://www.cnn.com/"},
    {"nyt", "NY Times",
     "https://rss.nytimes.com/services/xml/rss/nyt/World.xml",
     "https://www.nytimes.com/"},
    {"aljazeera", "Al Jazeera", "https://www.aljazeera.com/xml/rss/all.xml",
     "https://www.aljazeera.com/"},
    {"dw", "DW", "https://rss.dw.com/rdf/rss-en-all", "https://www.dw.com/"},
    {"france24", "France 24", "https://www.france24.com/en/rss",
     "https://www.france24.com/"},
    {"cbc", "CBC", "https://www.cbc.ca/webfeed/rss/rss-topstories",
     "https://www.cbc.ca/"},
    {"ars", "Ars Technica", "https://feeds.arstechnica.com/arstechnica/index",
     "https://arstechnica.com/"},
    {"register", "The Register", "https://www.theregister.com/headlines.atom",
     "https://www.theregister.com/"},
    {"phoronix", "Phoronix", "https://www.phoronix.com/rss.php",
     "https://www.phoronix.com/"},
    {"lwn", "LWN", "https://lwn.net/headlines/rss", "https://lwn.net/"},
    {"slashdot", "Slashdot", "https://rss.slashdot.org/Slashdot/slashdotMain",
     "https://slashdot.org/"},
    {"bleep", "BleepingComputer", "https://www.bleepingcomputer.com/feed/",
     "https://www.bleepingcomputer.com/"},
    {"propublica", "ProPublica",
     "https://www.propublica.org/feeds/propublica/main",
     "https://www.propublica.org/"},
    {"intercept", "The Intercept", "https://theintercept.com/feed/?rss",
     "https://theintercept.com/"},
    {"verge", "The Verge", "https://www.theverge.com/rss/index.xml",
     "https://www.theverge.com/"},
    {"techcrunch", "TechCrunch", "https://techcrunch.com/feed/",
     "https://techcrunch.com/"},
    {"wired", "Wired", "https://www.wired.com/feed/rss",
     "https://www.wired.com/"},
    {"pbs", "PBS NewsHour",
     "https://www.pbs.org/newshour/feeds/rss/headlines",
     "https://www.pbs.org/newshour/"},
    {"politico", "Politico", "https://rss.politico.com/politics-news.xml",
     "https://www.politico.com/"},
    {"hn", "Hacker News", "https://hnrss.org/frontpage",
     "https://news.ycombinator.com/"},
    {"four04", "404 Media", "https://www.404media.co/rss/",
     "https://www.404media.co/"},
    {"conversation", "The Conversation",
     "https://theconversation.com/articles.atom",
     "https://theconversation.com/"},
    {"democracynow", "Democracy Now",
     "https://www.democracynow.org/democracynow.rss",
     "https://www.democracynow.org/"},
    {"abcau", "ABC Australia",
     "https://www.abc.net.au/news/feed/51120/rss.xml",
     "https://www.abc.net.au/"},
};

static const int DEFAULT_SOURCE_COUNT =
    (int)(sizeof(DEFAULT_SOURCES) / sizeof(DEFAULT_SOURCES[0]));

static char *xml_node_text(xmlNodePtr node) {
  if (!node)
    return strdup("");
  xmlChar *content = xmlNodeGetContent(node);
  if (!content)
    return strdup("");
  char *out = strdup((const char *)content);
  xmlFree(content);
  return out;
}

static char *strip_html_tags(const char *input) {
  if (!input)
    return strdup("");

  size_t cap = strlen(input) + 1;
  char *out = malloc(cap);
  if (!out)
    return NULL;

  size_t w = 0;
  int in_tag = 0;
  for (size_t i = 0; input[i]; i++) {
    if (input[i] == '<') {
      in_tag = 1;
      continue;
    }
    if (input[i] == '>') {
      in_tag = 0;
      continue;
    }
    if (!in_tag) {
      if (w + 2 >= cap) {
        cap *= 2;
        char *grown = realloc(out, cap);
        if (!grown) {
          free(out);
          return NULL;
        }
        out = grown;
      }
      out[w++] = input[i];
    }
  }
  out[w] = '\0';

  char *trimmed = malloc(w + 1);
  if (!trimmed) {
    free(out);
    return NULL;
  }
  size_t start = 0;
  while (out[start] && isspace((unsigned char)out[start]))
    start++;
  size_t end = w;
  while (end > start && isspace((unsigned char)out[end - 1]))
    end--;
  size_t len = end - start;
  memcpy(trimmed, out + start, len);
  trimmed[len] = '\0';
  free(out);
  return trimmed;
}

static char *truncate_description(const char *input) {
  if (!input)
    return strdup("");
  size_t len = strlen(input);
  if (len <= NEWS_DESC_MAX)
    return strdup(input);

  char *out = malloc(NEWS_DESC_MAX + 4);
  if (!out)
    return NULL;
  memcpy(out, input, NEWS_DESC_MAX);
  size_t end = NEWS_DESC_MAX;
  while (end > 0 && isspace((unsigned char)out[end - 1]))
    end--;
  while (end > 0 && !isspace((unsigned char)out[end - 1]))
    end--;
  if (end < NEWS_DESC_MAX / 2)
    end = NEWS_DESC_MAX;
  out[end] = '\0';
  strcat(out, "...");
  return out;
}

static time_t parse_rss_date(const char *raw) {
  if (!raw || !raw[0])
    return (time_t)-1;
  return curl_getdate(raw, NULL);
}

static char *format_iso8601(time_t t) {
  struct tm utc;
  if (!gmtime_r(&t, &utc))
    return strdup("");
  char buf[40];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
    return strdup("");
  return strdup(buf);
}

static char *format_human_time(time_t pub) {
  if (pub == (time_t)-1)
    return strdup("");

  time_t now = time(NULL);
  double diff = difftime(now, pub);
  if (diff < 0)
    diff = 0;

  char buf[64];

  if (diff < 45)
    return strdup("Just now");
  if (diff < 90)
    return strdup("1 min ago");
  if (diff < 3600) {
    int mins = (int)(diff / 60);
    snprintf(buf, sizeof(buf), "%d min ago", mins);
    return strdup(buf);
  }
  if (diff < 5400)
    return strdup("1 hour ago");
  if (diff < 86400) {
    int hours = (int)(diff / 3600);
    snprintf(buf, sizeof(buf), "%d hours ago", hours);
    return strdup(buf);
  }

  struct tm pub_tm;
  struct tm now_tm;
  localtime_r(&pub, &pub_tm);
  localtime_r(&now, &now_tm);

  int pub_day = pub_tm.tm_year * 400 + pub_tm.tm_yday;
  int now_day = now_tm.tm_year * 400 + now_tm.tm_yday;

  if (pub_day == now_day - 1) {
    if (strftime(buf, sizeof(buf), "Yesterday at %l:%M %p", &pub_tm) > 0)
      return strdup(buf);
  }

  if (diff < 604800) {
    if (strftime(buf, sizeof(buf), "%A at %l:%M %p", &pub_tm) > 0)
      return strdup(buf);
  }

  if (pub_tm.tm_year == now_tm.tm_year) {
    if (strftime(buf, sizeof(buf), "%b %d at %l:%M %p", &pub_tm) > 0)
      return strdup(buf);
  }

  if (strftime(buf, sizeof(buf), "%b %d, %Y", &pub_tm) > 0)
    return strdup(buf);
  return strdup("");
}

static char *prepare_news_dates(const char *raw) {
  time_t t = parse_rss_date(raw);
  if (t == (time_t)-1)
    return strdup(raw ? raw : "");
  return format_human_time(t);
}

static char *prepare_news_date_iso(const char *raw) {
  time_t t = parse_rss_date(raw);
  if (t == (time_t)-1)
    return strdup(raw ? raw : "");
  return format_iso8601(t);
}

static char *first_xpath_text(xmlXPathContextPtr ctx, const char *expr) {
  xmlXPathObjectPtr obj = xmlXPathEvalExpression((const xmlChar *)expr, ctx);
  if (!obj || obj->nodesetval == NULL || obj->nodesetval->nodeNr == 0) {
    if (obj)
      xmlXPathFreeObject(obj);
    return NULL;
  }
  char *text = xml_node_text(obj->nodesetval->nodeTab[0]);
  xmlXPathFreeObject(obj);
  return text;
}

static char *first_link_href(xmlXPathContextPtr ctx) {
  xmlXPathObjectPtr obj =
      xmlXPathEvalExpression((const xmlChar *)".//link", ctx);
  if (!obj || obj->nodesetval == NULL || obj->nodesetval->nodeNr == 0) {
    if (obj)
      xmlXPathFreeObject(obj);
    return NULL;
  }

  for (int i = 0; i < obj->nodesetval->nodeNr; i++) {
    xmlNodePtr node = obj->nodesetval->nodeTab[i];
    xmlChar *href = xmlGetProp(node, (const xmlChar *)"href");
    if (href && href[0]) {
      char *out = strdup((const char *)href);
      xmlFree(href);
      xmlXPathFreeObject(obj);
      return out;
    }
    xmlFree(href);
    char *text = xml_node_text(node);
    if (text && text[0]) {
      xmlXPathFreeObject(obj);
      return text;
    }
    free(text);
  }

  xmlXPathFreeObject(obj);
  return NULL;
}

static int parse_feed_items(const char *xml, size_t size, const NewsSource *src,
                            NewsItem *out, int max_out) {
  if (!xml || size == 0 || !src || !out || max_out <= 0)
    return 0;

  xmlDocPtr doc =
      xmlReadMemory(xml, (int)size, src->feed_url, NULL,
                    XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if (!doc)
    return 0;

  xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
  if (!ctx) {
    xmlFreeDoc(doc);
    return 0;
  }

  xmlXPathObjectPtr entries =
      xmlXPathEvalExpression((const xmlChar *)"//item|//entry", ctx);
  int count = 0;
  if (!entries || entries->nodesetval == NULL) {
    if (entries)
      xmlXPathFreeObject(entries);
    xmlXPathFreeContext(ctx);
    xmlFreeDoc(doc);
    return 0;
  }

  for (int i = 0; i < entries->nodesetval->nodeNr && count < max_out; i++) {
    xmlNodePtr entry = entries->nodesetval->nodeTab[i];
    xmlXPathContextPtr item_ctx = xmlXPathNewContext(doc);
    if (!item_ctx)
      continue;
    item_ctx->node = entry;

    char *title = first_xpath_text(item_ctx, ".//title");
    char *link = first_link_href(item_ctx);
    char *desc = first_xpath_text(item_ctx,
                                  ".//description|.//summary|.//content");
    char *date = first_xpath_text(item_ctx,
                                  ".//pubDate|.//published|.//updated");

    xmlXPathFreeContext(item_ctx);

    if (!title || !title[0] || !link || !link[0]) {
      free(title);
      free(link);
      free(desc);
      free(date);
      continue;
    }

    char *clean_desc = desc ? strip_html_tags(desc) : strdup("");
    free(desc);
    char *short_desc = truncate_description(clean_desc ? clean_desc : "");
    free(clean_desc);

    char *raw_date = date ? date : strdup("");
    char *display_date = prepare_news_dates(raw_date);
    char *iso_date = prepare_news_date_iso(raw_date);

    out[count].title = title;
    out[count].link = link;
    out[count].description = short_desc ? short_desc : strdup("");
    out[count].date = raw_date;
    out[count].source_id = strdup(src->id);
    out[count].source_name = strdup(src->name);
    out[count].display_date = display_date ? display_date : strdup("");
    out[count].iso_date = iso_date ? iso_date : strdup("");
    count++;
  }

  xmlXPathFreeObject(entries);
  xmlXPathFreeContext(ctx);
  xmlFreeDoc(doc);
  return count;
}

static void free_news_item(NewsItem *item) {
  free(item->title);
  free(item->link);
  free(item->description);
  free(item->date);
  free(item->source_id);
  free(item->source_name);
  free(item->display_date);
  free(item->iso_date);
  item->title = NULL;
  item->link = NULL;
  item->description = NULL;
  item->date = NULL;
  item->source_id = NULL;
  item->source_name = NULL;
  item->display_date = NULL;
  item->iso_date = NULL;
}

static int query_matches(const char *haystack, const char *needle) {
  if (!needle || !needle[0])
    return 1;
  if (!haystack)
    return 0;

  size_t nlen = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    size_t i = 0;
    while (i < nlen && p[i] &&
           tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen)
      return 1;
  }
  return 0;
}

static int item_matches_query(const NewsItem *item, const char *query) {
  if (!query || !query[0])
    return 1;
  return query_matches(item->title, query) ||
         query_matches(item->description, query);
}

static const NewsSource *find_source(const char *source_id) {
  if (!source_id || !source_id[0])
    return NULL;
  for (int i = 0; i < DEFAULT_SOURCE_COUNT; i++) {
    if (strcasecmp(DEFAULT_SOURCES[i].id, source_id) == 0)
      return &DEFAULT_SOURCES[i];
  }
  return NULL;
}

static char *news_source_icon(const char *source_id, const char *link) {
  const NewsSource *s = find_source(source_id);
  if (s)
    return favicon_proxy_url_for_page(s->site_url);
  return favicon_proxy_url_for_page(link);
}

static char *build_news_href(const char *query, const char *source_id) {
  char *href = malloc(BUFFER_SIZE_LARGE);
  if (!href)
    return NULL;
  href[0] = '\0';
  strcat(href, "/news");
  int has_param = 0;
  if (query && query[0]) {
    strcat(href, "?q=");
    strcat(href, query);
    has_param = 1;
  }
  if (source_id && source_id[0]) {
    strcat(href, has_param ? "&source=" : "?source=");
    strcat(href, source_id);
  }
  return href;
}

extern Config global_config;

static char *build_news_request_cache_key(const char *query,
                                          const char *source,
                                          const char *client_key) {
  char scope_key[BUFFER_SIZE_MEDIUM];
  snprintf(scope_key, sizeof(scope_key), "news_request:%s:%s",
           source && source[0] ? source : "all",
           client_key ? client_key : "unknown");
  return cache_compute_key(query, 1, scope_key);
}

int news_handler(UrlParams *params) {
  TemplateContext ctx = new_context();
  char *locale = get_locale(NULL);
  beaker_set_locale(&ctx, locale);
  const char *rate_limit_msg =
      beaker_get_locale_value(locale, "rate_limit_news");
  if (!rate_limit_msg)
    rate_limit_msg = "Slow down! Too many news requests from you!";
  const char *error_news_msg = beaker_get_locale_value(locale, "error_news");
  if (!error_news_msg)
    error_news_msg = "Error loading news";
  const char *error_render_msg =
      beaker_get_locale_value(locale, "error_render");
  if (!error_render_msg)
    error_render_msg = "Error rendering results";
  const char *all_sources_msg = beaker_get_locale_value(locale, "all_sources");
  if (!all_sources_msg)
    all_sources_msg = "All sources";
  free(locale);
  char *raw_query = "";
  const char *selected_source = "";

  int want_json = 0;
  if (params) {
    for (int i = 0; i < params->count; i++) {
      if (strcmp(params->params[i].key, "q") == 0) {
        raw_query = params->params[i].value;
      } else if (strcmp(params->params[i].key, "source") == 0) {
        selected_source = params->params[i].value;
      } else if (strcmp(params->params[i].key, "format") == 0 &&
                 strcmp(params->params[i].value, "json") == 0) {
        want_json = 1;
      }
    }
  }

  char *display_query = url_decode_query(raw_query);
  context_set(&ctx, "query", display_query ? display_query : "");

  const NewsSource *only_source = find_source(selected_source);
  context_set(&ctx, "selected_source", only_source ? only_source->id : "");

  char client_key[BUFFER_SIZE_SMALL];
  rate_limit_get_client_key(client_key, sizeof(client_key));

  char *request_cache_key =
      build_news_request_cache_key(raw_query, selected_source, client_key);
  int request_is_cached = 0;

  if (request_cache_key && get_cache_ttl_infobox() > 0) {
    char *cached_marker = NULL;
    size_t cached_marker_size = 0;

    if (cache_get(request_cache_key, (time_t)get_cache_ttl_infobox(),
                  &cached_marker, &cached_marker_size) == 0) {
      request_is_cached = 1;
    }

    free(cached_marker);
  }

  if (!request_is_cached) {
    RateLimitConfig rate_limit_config = {
        .max_requests = global_config.rate_limit_news_requests,
        .interval_seconds = global_config.rate_limit_news_interval,
    };
    RateLimitResult rate_limit_result =
        rate_limit_check("news", &rate_limit_config);
    if (rate_limit_result.limited) {
      char retry_after_header[64];
      char *page_html = render_error_page(rate_limit_msg, "", "/news");
      snprintf(retry_after_header, sizeof(retry_after_header),
               "Retry-After: %d", rate_limit_result.retry_after_seconds);
      if (page_html) {
        send_response_with_status("429 Too Many Requests", page_html,
                                  retry_after_header);
        free(page_html);
      } else {
        send_response_with_status("429 Too Many Requests", "<h1>429</h1>",
                                  retry_after_header);
      }
      free(request_cache_key);
      free(display_query);
      free_context(&ctx);
      return -1;
    }

    if (request_cache_key && get_cache_ttl_infobox() > 0) {
      cache_set(request_cache_key, "1", 1);
    }
  }
  free(request_cache_key);

  NewsItem items[NEWS_MAX_ITEMS];
  int total = 0;

  const NewsSource *fetch_list[DEFAULT_SOURCE_COUNT];
  int fetch_n = 0;
  for (int s = 0; s < DEFAULT_SOURCE_COUNT; s++) {
    const NewsSource *src = &DEFAULT_SOURCES[s];
    if (only_source && src != only_source)
      continue;
    fetch_list[fetch_n++] = src;
  }

  HttpGetJob net_jobs[DEFAULT_SOURCE_COUNT];
  int net_idx[DEFAULT_SOURCE_COUNT];
  int net_n = 0;
  time_t ttl = (time_t)get_cache_ttl_infobox();
  const char *news_ua =
      "Mozilla/5.0 (compatible; Seek/1.0; +https://seek.lyrabrowser.com)";

  for (int s = 0; s < fetch_n && total < NEWS_MAX_ITEMS; s++) {
    const NewsSource *src = fetch_list[s];
    NewsItem batch[NEWS_ITEMS_PER_SOURCE];
    int n = 0;
    if (ttl > 0) {
      char cache_key[128];
      snprintf(cache_key, sizeof(cache_key), "news:%s", src->id);
      char *cached = NULL;
      size_t cached_size = 0;
      if (cache_get(cache_key, ttl, &cached, &cached_size) == 0 && cached) {
        n = parse_feed_items(cached, cached_size, src, batch,
                             NEWS_ITEMS_PER_SOURCE);
        free(cached);
      }
    }
    if (n > 0) {
      for (int i = 0; i < n; i++) {
        if (total >= NEWS_MAX_ITEMS ||
            !item_matches_query(&batch[i], display_query)) {
          free_news_item(&batch[i]);
          continue;
        }
        items[total++] = batch[i];
      }
      continue;
    }
    net_jobs[net_n].url = src->feed_url;
    net_jobs[net_n].user_agent = news_ua;
    net_jobs[net_n].handle = NULL;
    net_jobs[net_n].resp = (HttpResponse){0};
    net_idx[net_n] = s;
    net_n++;
  }

  if (net_n > 0)
    http_get_many(net_jobs, net_n, 4);

  for (int j = 0; j < net_n && total < NEWS_MAX_ITEMS; j++) {
    const NewsSource *src = fetch_list[net_idx[j]];
    NewsItem batch[NEWS_ITEMS_PER_SOURCE];
    int n = 0;
    if (net_jobs[j].resp.memory && net_jobs[j].resp.size > 0) {
      n = parse_feed_items(net_jobs[j].resp.memory, net_jobs[j].resp.size, src,
                           batch, NEWS_ITEMS_PER_SOURCE);
      if (n > 0 && ttl > 0) {
        char cache_key[128];
        snprintf(cache_key, sizeof(cache_key), "news:%s", src->id);
        cache_set(cache_key, net_jobs[j].resp.memory, net_jobs[j].resp.size);
      }
    }
    http_response_free(&net_jobs[j].resp);
    for (int i = 0; i < n; i++) {
      if (total >= NEWS_MAX_ITEMS ||
          !item_matches_query(&batch[i], display_query)) {
        free_news_item(&batch[i]);
        continue;
      }
      items[total++] = batch[i];
    }
  }

  if (!want_json) {
    const char *sites[DEFAULT_SOURCE_COUNT];
    for (int i = 0; i < DEFAULT_SOURCE_COUNT; i++)
      sites[i] = DEFAULT_SOURCES[i].site_url;
    favicon_prime_pages(sites, DEFAULT_SOURCE_COUNT);
  }

  if (want_json) {
    char *buf = NULL;
    size_t blen = 0;
    FILE *fp = open_memstream(&buf, &blen);
    if (fp) {
      char *src_esc =
          json_string_value(only_source ? only_source->id : "all");
      fprintf(fp, "{\"source\":%s,\"items\":[", src_esc ? src_esc : "\"\"");
      free(src_esc);
      for (int i = 0; i < total; i++) {
        char *t = json_string_value(items[i].title);
        char *u = json_string_value(items[i].link);
        char *d = json_string_value(items[i].description);
        char *sn = json_string_value(items[i].source_name);
        char *sid = json_string_value(items[i].source_id);
        char *iso = json_string_value(items[i].iso_date);
        char *dd = json_string_value(items[i].display_date);
        char *ico_url = news_source_icon(items[i].source_id, items[i].link);
        char *ic = json_string_value(ico_url);
        fprintf(fp,
                "%s{\"title\":%s,\"url\":%s,\"snippet\":%s,"
                "\"source\":%s,\"source_id\":%s,\"date\":%s,"
                "\"display_date\":%s,\"icon\":%s}",
                i == 0 ? "" : ",", t ? t : "\"\"", u ? u : "\"\"",
                d ? d : "\"\"", sn ? sn : "\"\"",
                sid ? sid : "\"\"", iso ? iso : "\"\"",
                dd ? dd : "\"\"", ic ? ic : "\"\"");
        free(t);
        free(u);
        free(d);
        free(sn);
        free(sid);
        free(iso);
        free(dd);
        free(ico_url);
        free(ic);
      }
      fprintf(fp, "]}");
      fclose(fp);
      if (buf && blen > 0)
        serve_data(buf, blen, "application/json; charset=UTF-8");
      free(buf);
    }
    for (int i = 0; i < total; i++)
      free_news_item(&items[i]);
    free(display_query);
    free_context(&ctx);
    return 0;
  }

  char ***article_matrix = NULL;
  int *inner_counts = NULL;
  if (total > 0) {
    article_matrix = malloc(sizeof(char **) * total);
    inner_counts = malloc(sizeof(int) * total);
    if (!article_matrix || !inner_counts) {
      free(article_matrix);
      free(inner_counts);
      for (int i = 0; i < total; i++)
        free_news_item(&items[i]);
      free(display_query);
      free_context(&ctx);
      char *page_html = render_error_page(error_news_msg, "", "/news");
      if (page_html) {
        send_response_with_status("502 Bad Gateway", page_html, NULL);
        free(page_html);
      } else {
        send_status("502 Bad Gateway");
      }
      return -1;
    }

    for (int i = 0; i < total; i++) {
      article_matrix[i] = malloc(sizeof(char *) * NEWS_FIELD_COUNT);
      article_matrix[i][0] = items[i].title;
      article_matrix[i][1] = items[i].link;
      article_matrix[i][2] = items[i].description;
      article_matrix[i][3] = items[i].iso_date;
      article_matrix[i][4] = items[i].display_date;
      article_matrix[i][5] = items[i].source_name;
      article_matrix[i][6] = items[i].source_id;
      article_matrix[i][7] = news_source_icon(items[i].source_id, items[i].link);
      if (!article_matrix[i][7] || !article_matrix[i][7][0]) {
        free(article_matrix[i][7]);
        article_matrix[i][7] = strdup("/static/icon-placeholder.svg");
      }
      inner_counts[i] = NEWS_FIELD_COUNT;
      free(items[i].date);
      items[i].date = NULL;
    }
    context_set_array_of_arrays(&ctx, "articles", article_matrix, total,
                                inner_counts);
  }

  char ***source_matrix = malloc(sizeof(char **) * (DEFAULT_SOURCE_COUNT + 1));
  int *source_inner = malloc(sizeof(int) * (DEFAULT_SOURCE_COUNT + 1));
  if (source_matrix && source_inner) {
    int sc = 0;
    char *all_href = build_news_href(raw_query, "");
    source_matrix[sc] = malloc(sizeof(char *) * 4);
    source_matrix[sc][0] = all_href ? all_href : strdup("/news");
    source_matrix[sc][1] = strdup(all_sources_msg);
    source_matrix[sc][2] = strdup(only_source ? "engine-filter" : "engine-filter active");
    source_matrix[sc][3] = strdup("");
    source_inner[sc] = 4;
    sc++;

    int shown = 1;
    int overflow_start = 0;
    for (int i = 0; i < DEFAULT_SOURCE_COUNT; i++) {
      int active = only_source && only_source == &DEFAULT_SOURCES[i];
      int in_pills = shown < NEWS_PILLS || active;
      if (!in_pills) {
        if (overflow_start == 0)
          overflow_start = i;
        continue;
      }
      source_matrix[sc] = malloc(sizeof(char *) * 4);
      char *href = build_news_href(raw_query, DEFAULT_SOURCES[i].id);
      source_matrix[sc][0] = href ? href : strdup("/news");
      source_matrix[sc][1] = strdup(DEFAULT_SOURCES[i].name);
      source_matrix[sc][2] =
          strdup(active ? "engine-filter active" : "engine-filter");
      char *ico = favicon_proxy_url_for_page(DEFAULT_SOURCES[i].site_url);
      source_matrix[sc][3] = ico ? ico : strdup("/static/icon-placeholder.svg");
      source_inner[sc] = 4;
      sc++;
      shown++;
    }
    int overflow_n = 0;
    char ***overflow_matrix = NULL;
    int *overflow_inner = NULL;
    if (overflow_start > 0) {
      overflow_n = DEFAULT_SOURCE_COUNT - overflow_start;
      overflow_matrix = malloc(sizeof(char **) * (size_t)overflow_n);
      overflow_inner = malloc(sizeof(int) * (size_t)overflow_n);
      int oc = 0;
      for (int i = overflow_start; i < DEFAULT_SOURCE_COUNT; i++) {
        if (only_source && only_source == &DEFAULT_SOURCES[i])
          continue;
        overflow_matrix[oc] = malloc(sizeof(char *) * 4);
        char *href = build_news_href(raw_query, DEFAULT_SOURCES[i].id);
        overflow_matrix[oc][0] = href ? href : strdup("/news");
        overflow_matrix[oc][1] = strdup(DEFAULT_SOURCES[i].name);
        overflow_matrix[oc][2] = strdup("engine-filter");
        char *ico = favicon_proxy_url_for_page(DEFAULT_SOURCES[i].site_url);
        overflow_matrix[oc][3] =
            ico ? ico : strdup("/static/icon-placeholder.svg");
        overflow_inner[oc] = 4;
        oc++;
      }
      overflow_n = oc;
      if (overflow_n > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", overflow_n);
        context_set(&ctx, "source_overflow", buf);
        context_set_array_of_arrays(&ctx, "source_overflow_items",
                                    overflow_matrix, overflow_n,
                                    overflow_inner);
      }
    }
    context_set_array_of_arrays(&ctx, "source_filters", source_matrix, sc,
                                source_inner);
    for (int i = 0; i < sc; i++) {
      free(source_matrix[i][0]);
      free(source_matrix[i][1]);
      free(source_matrix[i][2]);
      free(source_matrix[i][3]);
      free(source_matrix[i]);
    }
    free(source_matrix);
    free(source_inner);
    if (overflow_matrix) {
      for (int i = 0; i < overflow_n; i++) {
        free(overflow_matrix[i][0]);
        free(overflow_matrix[i][1]);
        free(overflow_matrix[i][2]);
        free(overflow_matrix[i][3]);
        free(overflow_matrix[i]);
      }
      free(overflow_matrix);
      free(overflow_inner);
    }
  }

  context_set(&ctx, "search_qs", (display_query && display_query[0])
                                     ? display_query
                                     : "");

  char *rendered = render_template("news.html", &ctx);
  if (rendered) {
    send_response(rendered);
    free(rendered);
  } else {
    char *page_html = render_error_page(error_render_msg, "", "/news");
    if (page_html) {
      send_response_with_status("500 Internal Server Error", page_html, NULL);
      free(page_html);
    } else {
      send_status("500 Internal Server Error");
    }
  }

  if (article_matrix)
    free_string_matrix(article_matrix, inner_counts, total);

  free(display_query);
  free_context(&ctx);
  return 0;
}
