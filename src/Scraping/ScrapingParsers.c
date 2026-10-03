#include "../Utility/Unescape.h"
#include "../Utility/XmlHelper.h"
#include "Config.h"
#include "JsonEngines.h"
#include "Scraping.h"
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

xmlXPathContextPtr create_xpath_context(xmlDocPtr doc) {
  return xmlXPathNewContext(doc);
}

void free_xpath_objects(xmlXPathContextPtr ctx, xmlXPathObjectPtr obj) {
  if (obj)
    xmlXPathFreeObject(obj);
  if (ctx)
    xmlXPathFreeContext(ctx);
}

SearchResult *alloc_results_array(int capacity, int max_results) {
  int count = capacity < max_results ? capacity : max_results;
  return xml_result_alloc(capacity, count);
}

void assign_result(SearchResult *result, char *url, char *title, char *snippet,
                   int unescape) {
  result->url = unescape ? unescape_search_url(url) : strdup(url ? url : "");
  result->title = strdup(title ? title : "No Title");
  result->snippet = strdup(snippet ? snippet : "");
}

void free_xml_node_list(char *title, char *url, char *snippet) {
  if (title)
    xmlFree(title);
  if (url)
    xmlFree(url);
  if (snippet)
    xmlFree(snippet);
}

static int parse_ddg_lite(const char *engine_name, xmlDocPtr doc,
                          SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj =
      xml_xpath_eval(ctx, "//tr[not(contains(@class, "
                          "'result-sponsored'))]//a[@class='result-link']");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_links = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_links, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_links && found_count < max_results; i++) {
    xmlNodePtr link_node = obj->nodesetval->nodeTab[i];
    char *title = xml_node_content(link_node);
    char *url = (char *)xmlGetProp(link_node, (xmlChar *)"href");
    char *snippet_text = NULL;

    xmlNodePtr current = link_node->parent;
    while (current && xmlStrcasecmp(current->name, (const xmlChar *)"tr") != 0)
      current = current->parent;

    if (current && current->next) {
      xmlNodePtr snippet_row = current->next;
      while (snippet_row &&
             xmlStrcasecmp(snippet_row->name, (const xmlChar *)"tr") != 0)
        snippet_row = snippet_row->next;
      if (snippet_row) {
        ctx->node = snippet_row;
        xmlXPathObjectPtr s_obj =
            xml_xpath_eval(ctx, ".//td[@class='result-snippet']");
        if (s_obj && s_obj->nodesetval && s_obj->nodesetval->nodeNr > 0)
          snippet_text = xml_node_content(s_obj->nodesetval->nodeTab[0]);
        if (s_obj)
          xmlXPathFreeObject(s_obj);
        ctx->node = NULL;
      }
    }

    assign_result(&(*out_results)[found_count], url, title, snippet_text, 1);
    free_xml_node_list(title, url, snippet_text);
    found_count++;
  }

  free_xpath_objects(ctx, obj);
  return found_count;
}

static int parse_startpage(const char *engine_name, xmlDocPtr doc,
                           SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj =
      xml_xpath_eval(ctx, "//div[contains(@class, 'result')]");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_results = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_results, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_results && found_count < max_results; i++) {
    xmlNodePtr result_node = obj->nodesetval->nodeTab[i];
    ctx->node = result_node;

    xmlXPathObjectPtr link_obj =
        xml_xpath_eval(ctx, ".//a[contains(@class, 'result-link')]");
    char *url =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? (char *)xmlGetProp(link_obj->nodesetval->nodeTab[0],
                                 (xmlChar *)"href")
            : NULL;

    xmlXPathObjectPtr title_obj =
        xml_xpath_eval(ctx, ".//h2[contains(@class, 'wgl-title')]");
    char *title = (title_obj && title_obj->nodesetval &&
                   title_obj->nodesetval->nodeNr > 0)
                      ? xml_node_content(title_obj->nodesetval->nodeTab[0])
                      : NULL;

    xmlXPathObjectPtr snippet_obj =
        xml_xpath_eval(ctx, ".//p[contains(@class, 'description')]");
    char *snippet_text =
        (snippet_obj && snippet_obj->nodesetval &&
         snippet_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(snippet_obj->nodesetval->nodeTab[0])
            : NULL;

    if (url && title) {
      assign_result(&(*out_results)[found_count], url, title, snippet_text, 0);
      found_count++;
    }

    free_xml_node_list(title, url, snippet_text);
    if (link_obj)
      xmlXPathFreeObject(link_obj);
    if (title_obj)
      xmlXPathFreeObject(title_obj);
    if (snippet_obj)
      xmlXPathFreeObject(snippet_obj);
  }

  ctx->node = NULL;
  free_xpath_objects(ctx, obj);
  return found_count;
}

static int parse_brave(const char *engine_name, xmlDocPtr doc,
                       SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj =
      xml_xpath_eval(ctx, "//div[contains(@class, 'result-wrapper')]");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_results = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_results, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_results && found_count < max_results; i++) {
    xmlNodePtr result_node = obj->nodesetval->nodeTab[i];
    ctx->node = result_node;

    xmlXPathObjectPtr link_obj =
        xml_xpath_eval(ctx, ".//a[contains(@class, 'l1')]");
    char *url =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? (char *)xmlGetProp(link_obj->nodesetval->nodeTab[0],
                                 (xmlChar *)"href")
            : NULL;

    xmlXPathObjectPtr title_obj =
        xml_xpath_eval(ctx, ".//div[contains(@class, 'search-snippet-title')]");
    char *title =
        (title_obj && title_obj->nodesetval && title_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(title_obj->nodesetval->nodeTab[0])
            : NULL;

    xmlXPathObjectPtr snippet_obj = xml_xpath_eval(
        ctx,
        ".//div[contains(@class, 'generic-snippet')]//div[contains(@class, 'content')]");
    char *snippet_text =
        (snippet_obj && snippet_obj->nodesetval &&
         snippet_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(snippet_obj->nodesetval->nodeTab[0])
            : NULL;

    if (url && title) {
      assign_result(&(*out_results)[found_count], url, title, snippet_text, 0);
      found_count++;
    }

    free_xml_node_list(title, url, snippet_text);
    if (link_obj)
      xmlXPathFreeObject(link_obj);
    if (title_obj)
      xmlXPathFreeObject(title_obj);
    if (snippet_obj)
      xmlXPathFreeObject(snippet_obj);
  }

  ctx->node = NULL;
  free_xpath_objects(ctx, obj);
  return found_count;
}

static int parse_yahoo(const char *engine_name, xmlDocPtr doc,
                       SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj =
      xml_xpath_eval(ctx, "//div[contains(@class, 'algo-sr')]");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_results = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_results, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_results && found_count < max_results; i++) {
    xmlNodePtr result_node = obj->nodesetval->nodeTab[i];
    ctx->node = result_node;

    xmlXPathObjectPtr link_obj = xml_xpath_eval(
        ctx, ".//div[contains(@class, 'compTitle')]//a[@target='_blank']");
    char *url =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? (char *)xmlGetProp(link_obj->nodesetval->nodeTab[0],
                                 (xmlChar *)"href")
            : NULL;

    xmlXPathObjectPtr title_obj =
        xml_xpath_eval(ctx, ".//h3[contains(@class, 'title')]");
    char *title = (title_obj && title_obj->nodesetval &&
                   title_obj->nodesetval->nodeNr > 0)
                      ? xml_node_content(title_obj->nodesetval->nodeTab[0])
                      : NULL;

    xmlXPathObjectPtr snippet_obj =
        xml_xpath_eval(ctx, ".//div[contains(@class, 'compText')]//p");
    char *snippet_text =
        (snippet_obj && snippet_obj->nodesetval &&
         snippet_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(snippet_obj->nodesetval->nodeTab[0])
            : NULL;

    if (url && title) {
      assign_result(&(*out_results)[found_count], url, title, snippet_text, 1);
      found_count++;
    }

    free_xml_node_list(title, url, snippet_text);
    if (link_obj)
      xmlXPathFreeObject(link_obj);
    if (title_obj)
      xmlXPathFreeObject(title_obj);
    if (snippet_obj)
      xmlXPathFreeObject(snippet_obj);
  }

  ctx->node = NULL;
  free_xpath_objects(ctx, obj);
  return found_count;
}

static int parse_mojeek(const char *engine_name, xmlDocPtr doc,
                        SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj =
      xml_xpath_eval(ctx, "//ul[@class='results-standard']/li[starts-with(@class, 'r')]");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_results = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_results, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_results && found_count < max_results; i++) {
    xmlNodePtr result_node = obj->nodesetval->nodeTab[i];
    ctx->node = result_node;

    xmlXPathObjectPtr link_obj =
        xml_xpath_eval(ctx, ".//a[@class='title']");
    char *url =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? (char *)xmlGetProp(link_obj->nodesetval->nodeTab[0],
                                 (xmlChar *)"href")
            : NULL;

    char *title = (link_obj && link_obj->nodesetval &&
                   link_obj->nodesetval->nodeNr > 0)
                      ? xml_node_content(link_obj->nodesetval->nodeTab[0])
                      : NULL;

    xmlXPathObjectPtr snippet_obj = xml_xpath_eval(ctx, ".//p[@class='s']");
    char *snippet_text =
        (snippet_obj && snippet_obj->nodesetval &&
         snippet_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(snippet_obj->nodesetval->nodeTab[0])
            : NULL;

    if (url && title) {
      assign_result(&(*out_results)[found_count], url, title, snippet_text, 0);
      found_count++;
    }

    free_xml_node_list(title, url, snippet_text);
    if (link_obj)
      xmlXPathFreeObject(link_obj);
    if (snippet_obj)
      xmlXPathFreeObject(snippet_obj);
  }

  ctx->node = NULL;
  free_xpath_objects(ctx, obj);
  return found_count;
}

static int parse_wiby(const char *engine_name, xmlDocPtr doc,
                      SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj =
      xml_xpath_eval(ctx, "//blockquote[.//a[@class='tlink']]");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_results = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_results, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_results && found_count < max_results; i++) {
    xmlNodePtr result_node = obj->nodesetval->nodeTab[i];
    ctx->node = result_node;

    xmlXPathObjectPtr link_obj =
        xml_xpath_eval(ctx, ".//a[@class='tlink']");
    char *url =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? (char *)xmlGetProp(link_obj->nodesetval->nodeTab[0],
                                 (xmlChar *)"href")
            : NULL;
    char *title =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(link_obj->nodesetval->nodeTab[0])
            : NULL;

    xmlXPathObjectPtr snippet_obj =
        xml_xpath_eval(ctx, ".//p[not(@class='url')]");
    char *snippet_text =
        (snippet_obj && snippet_obj->nodesetval &&
         snippet_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(snippet_obj->nodesetval->nodeTab[0])
            : NULL;

    if (url && title) {
      assign_result(&(*out_results)[found_count], url, title, snippet_text, 0);
      found_count++;
    }

    free_xml_node_list(title, url, snippet_text);
    if (link_obj)
      xmlXPathFreeObject(link_obj);
    if (snippet_obj)
      xmlXPathFreeObject(snippet_obj);
  }

  ctx->node = NULL;
  free_xpath_objects(ctx, obj);
  return found_count;
}

static int parse_yacy(const char *engine_name, xmlDocPtr doc,
                      SearchResult **out_results, int max_results) {
  (void)engine_name;
  int found_count = 0;

  xmlXPathContextPtr ctx = create_xpath_context(doc);
  if (!ctx)
    return 0;

  xmlXPathObjectPtr obj = xml_xpath_eval(ctx, "//item");

  if (!obj || !obj->nodesetval || obj->nodesetval->nodeNr == 0) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  int num_items = obj->nodesetval->nodeNr;
  *out_results = alloc_results_array(num_items, max_results);
  if (!*out_results) {
    free_xpath_objects(ctx, obj);
    return 0;
  }

  for (int i = 0; i < num_items && found_count < max_results; i++) {
    xmlNodePtr item_node = obj->nodesetval->nodeTab[i];
    ctx->node = item_node;

    xmlXPathObjectPtr title_obj = xml_xpath_eval(ctx, "./title");
    char *title = (title_obj && title_obj->nodesetval &&
                   title_obj->nodesetval->nodeNr > 0)
                      ? xml_node_content(title_obj->nodesetval->nodeTab[0])
                      : NULL;

    xmlXPathObjectPtr link_obj = xml_xpath_eval(ctx, "./link");
    char *url =
        (link_obj && link_obj->nodesetval && link_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(link_obj->nodesetval->nodeTab[0])
            : NULL;

    xmlXPathObjectPtr desc_obj = xml_xpath_eval(ctx, "./description");
    char *snippet_text =
        (desc_obj && desc_obj->nodesetval && desc_obj->nodesetval->nodeNr > 0)
            ? xml_node_content(desc_obj->nodesetval->nodeTab[0])
            : NULL;

    if (url && title) {
      assign_result(&(*out_results)[found_count], url, title, snippet_text, 0);
      found_count++;
    }

    free_xml_node_list(title, url, snippet_text);
    if (title_obj)
      xmlXPathFreeObject(title_obj);
    if (link_obj)
      xmlXPathFreeObject(link_obj);
    if (desc_obj)
      xmlXPathFreeObject(desc_obj);
  }

  ctx->node = NULL;
  free_xpath_objects(ctx, obj);
  return found_count;
}

static char yacy_base_url[BUFFER_SIZE_LARGE];

void configure_lyra_engine(const char *instance) {
  if (!instance || instance[0] == '\0')
    instance = DEFAULT_INDEX_URL;

  char cleaned[BUFFER_SIZE_LARGE];
  snprintf(cleaned, sizeof(cleaned), "%s", instance);
  size_t n = strlen(cleaned);
  while (n > 0 && cleaned[n - 1] == '/') {
    cleaned[n - 1] = '\0';
    n--;
  }

  const char *host = cleaned;
  const char *scheme = strstr(cleaned, "://");
  if (scheme)
    host = scheme + 3;
  char hostbuf[ENGINE_HOST_MAX];
  snprintf(hostbuf, sizeof(hostbuf), "%s", host);
  char *slash = strchr(hostbuf, '/');
  if (slash)
    *slash = '\0';

  for (int i = 0; i < ENGINE_COUNT; i++) {
    if (strcmp(ENGINE_REGISTRY[i].id, "lyra") != 0)
      continue;
    snprintf(ENGINE_REGISTRY[i].base_url, sizeof(ENGINE_REGISTRY[i].base_url),
             "%s/v1/search.rss?limit=%d&q=", cleaned, MAX_RESULTS_PER_ENGINE);
    snprintf(ENGINE_REGISTRY[i].host_header,
             sizeof(ENGINE_REGISTRY[i].host_header), "%s", hostbuf);
    snprintf(ENGINE_REGISTRY[i].referer, sizeof(ENGINE_REGISTRY[i].referer),
             "%s/", cleaned);
    break;
  }
}

void configure_yacy_engine(const char *instance) {
  if (!instance || instance[0] == '\0')
    instance = DEFAULT_YACY_INSTANCE;

  snprintf(yacy_base_url, sizeof(yacy_base_url),
           "%s/"
           "yacysearch.rss?resource=global&urlmaskfilter=.*&prefermaskfilter=&"
           "nav=all&maximumRecords=%d&query=",
           instance, MAX_RESULTS_PER_ENGINE);

  for (int i = 0; i < ENGINE_COUNT; i++) {
    if (strcmp(ENGINE_REGISTRY[i].id, "yacy") == 0) {
      snprintf(ENGINE_REGISTRY[i].base_url,
               sizeof(ENGINE_REGISTRY[i].base_url), "%.*s",
               (int)sizeof(ENGINE_REGISTRY[i].base_url) - 1, yacy_base_url);
      break;
    }
  }
}

SearchEngine ENGINE_REGISTRY[] = {
    {.id = "lyra",
     .name = "Lyra Index",
     .base_url = "https://index.lyrabrowser.com/v1/search.rss?limit=20&q=",
     .host_header = "index.lyrabrowser.com",
     .referer = "https://index.lyrabrowser.com/",
     .page_param = "page",
     .page_multiplier = 1,
     .page_base = 1,
     .parser = parse_yacy,
     .enabled = 1,
     .is_xml = 1},
    {.id = "ddg",
     .name = "DuckDuckGo Lite",
     .base_url = "https://lite.duckduckgo.com/lite/?q=",
     .host_header = "lite.duckduckgo.com",
     .referer = "https://lite.duckduckgo.com/",
     .page_param = "s",
     .page_multiplier = 30,
     .page_base = 0,
     .parser = parse_ddg_lite,
     .enabled = 1},
    {.id = "brave",
     .name = "Brave",
     .base_url = "https://search.brave.com/search?q=",
     .host_header = "search.brave.com",
     .referer = "https://search.brave.com/",
     .page_param = "offset",
     .page_multiplier = 10,
     .page_base = 0,
     .parser = parse_brave,
     .enabled = 1},
    {.id = "yahoo",
     .name = "Yahoo",
     .base_url = "https://search.yahoo.com/search?p=",
     .host_header = "search.yahoo.com",
     .referer = "https://search.yahoo.com/",
     .page_param = "b",
     .page_multiplier = 10,
     .page_base = 1,
     .parser = parse_yahoo,
     .enabled = 0},
    {.id = "startpage",
     .name = "Startpage",
     .base_url = "https://www.startpage.com/sp/search?query=",
     .host_header = "www.startpage.com",
     .referer = "https://www.startpage.com/",
     .page_param = "page",
     .page_multiplier = 1,
     .page_base = 1,
     .parser = parse_startpage,
     .enabled = 0},
    {.id = "wikipedia",
     .name = "Wikipedia",
     .base_url = "https://en.wikipedia.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=10&srsearch=",
     .host_header = "en.wikipedia.org",
     .referer = "https://en.wikipedia.org/",
     .page_param = "sroffset",
     .page_multiplier = 10,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "wiktionary",
     .name = "Wiktionary",
     .base_url = "https://en.wiktionary.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "en.wiktionary.org",
     .referer = "https://en.wiktionary.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "archwiki",
     .name = "Arch Wiki",
     .base_url = "https://wiki.archlinux.org/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "wiki.archlinux.org",
     .referer = "https://wiki.archlinux.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "debianwiki",
     .name = "Debian Wiki",
     .base_url = "https://wiki.debian.org/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "wiki.debian.org",
     .referer = "https://wiki.debian.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "nixos",
     .name = "NixOS Wiki",
     .base_url = "https://wiki.nixos.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "wiki.nixos.org",
     .referer = "https://wiki.nixos.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "gentoo",
     .name = "Gentoo Wiki",
     .base_url = "https://wiki.gentoo.org/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "wiki.gentoo.org",
     .referer = "https://wiki.gentoo.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "mediawiki",
     .name = "MediaWiki",
     .base_url = "https://www.mediawiki.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "www.mediawiki.org",
     .referer = "https://www.mediawiki.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "mwmbl",
     .name = "Mwmbl",
     .base_url = "https://api.mwmbl.org/search/?s=",
     .host_header = "api.mwmbl.org",
     .referer = "https://mwmbl.org/",
     .page_param = "",
     .page_multiplier = 0,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "hn",
     .name = "Hacker News",
     .base_url = "https://hn.algolia.com/api/v1/search?hitsPerPage=12&query=",
     .host_header = "hn.algolia.com",
     .referer = "https://news.ycombinator.com/",
     .page_param = "page",
     .page_multiplier = 1,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 1,
     .is_json = 1},
    {.id = "reddit",
     .name = "Reddit",
     .base_url = "https://old.reddit.com/search.json?q=",
     .host_header = "old.reddit.com",
     .referer = "https://old.reddit.com/",
     .page_param = "",
     .page_multiplier = 0,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "lobsters",
     .name = "Lobsters",
     .base_url = "https://lobste.rs/search.json?q=",
     .host_header = "lobste.rs",
     .referer = "https://lobste.rs/",
     .page_param = "",
     .page_multiplier = 0,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "qwant",
     .name = "Qwant",
     .base_url = "https://api.qwant.com/v3/search/web?locale=en_US&count=10&q=",
     .host_header = "api.qwant.com",
     .referer = "https://www.qwant.com/",
     .page_param = "offset",
     .page_multiplier = 10,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "mojeek",
     .name = "Mojeek",
     .base_url = "https://www.mojeek.com/search?q=",
     .host_header = "www.mojeek.com",
     .referer = "https://www.mojeek.com/",
     .page_param = "s",
     .page_multiplier = 10,
     .page_base = 1,
     .parser = parse_mojeek,
     .enabled = 1},
    {.id = "wiby",
     .name = "Wiby",
     .base_url = "https://wiby.me/?q=",
     .host_header = "wiby.me",
     .referer = "https://wiby.me/",
     .page_param = "p",
     .page_multiplier = 1,
     .page_base = 1,
     .parser = parse_wiby,
     .enabled = 1},
    {.id = "yacy",
     .name = "YaCy",
     .base_url = "",
     .host_header = "",
     .referer = "",
     .page_param = "startRecord",
     .page_multiplier = 10,
     .page_base = 0,
     .parser = parse_yacy,
     .enabled = 0,
     .is_xml = 1},
    {.id = "wikibooks",
     .name = "Wikibooks",
     .base_url = "https://en.wikibooks.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "en.wikibooks.org",
     .referer = "https://en.wikibooks.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "wikiquote",
     .name = "Wikiquote",
     .base_url = "https://en.wikiquote.org/w/api.php?action=query&list=search&format=json&utf8=1&srlimit=8&srsearch=",
     .host_header = "en.wikiquote.org",
     .referer = "https://en.wikiquote.org/",
     .page_param = "sroffset",
     .page_multiplier = 8,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "lemmy",
     .name = "Lemmy",
     .base_url = "https://lemmy.world/api/v3/search?type_=Posts&sort=TopAll&limit=10&q=",
     .host_header = "lemmy.world",
     .referer = "https://lemmy.world/",
     .page_param = "",
     .page_multiplier = 0,
     .page_base = 0,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1},
    {.id = "stackoverflow",
     .name = "Stack Overflow",
     .base_url = "https://api.stackexchange.com/2.3/search/advanced?pagesize=10&order=desc&sort=relevance&site=stackoverflow&q=",
     .host_header = "api.stackexchange.com",
     .referer = "https://stackoverflow.com/",
     .page_param = "page",
     .page_multiplier = 1,
     .page_base = 1,
     .json_parser = parse_json_engine,
     .enabled = 0,
     .is_json = 1}};

const int ENGINE_COUNT = sizeof(ENGINE_REGISTRY) / sizeof(SearchEngine);

SearchEngine EXTRA_ENGINES[MAX_EXTRA_ENGINES];
int extra_engine_count = 0;

int engines_total(void) { return ENGINE_COUNT + extra_engine_count; }

const SearchEngine *engine_at(int idx) {
  if (idx < 0)
    return NULL;
  if (idx < ENGINE_COUNT)
    return &ENGINE_REGISTRY[idx];
  if (idx - ENGINE_COUNT < extra_engine_count)
    return &EXTRA_ENGINES[idx - ENGINE_COUNT];
  return NULL;
}

SearchEngine *engine_mutable_at(int idx) {
  if (idx < 0)
    return NULL;
  if (idx < ENGINE_COUNT)
    return &ENGINE_REGISTRY[idx];
  if (idx - ENGINE_COUNT < extra_engine_count)
    return &EXTRA_ENGINES[idx - ENGINE_COUNT];
  return NULL;
}

static ParserFunc parser_by_name(const char *name) {
  if (!name || !name[0])
    return NULL;
  if (!strcasecmp(name, "ddg"))
    return parse_ddg_lite;
  if (!strcasecmp(name, "startpage"))
    return parse_startpage;
  if (!strcasecmp(name, "brave"))
    return parse_brave;
  if (!strcasecmp(name, "yahoo"))
    return parse_yahoo;
  if (!strcasecmp(name, "mojeek"))
    return parse_mojeek;
  if (!strcasecmp(name, "wiby"))
    return parse_wiby;
  if (!strcasecmp(name, "yacy"))
    return parse_yacy;
  if (!strcasecmp(name, "lyra"))
    return parse_yacy;
  return NULL;
}

static JsonParserFunc json_parser_by_name(const char *name) {
  if (!name || !name[0])
    return NULL;
  if (!strcasecmp(name, "wikipedia") || !strcasecmp(name, "wiktionary") ||
      !strcasecmp(name, "archwiki") || !strcasecmp(name, "debianwiki") ||
      !strcasecmp(name, "gentoo") || !strcasecmp(name, "nixos") ||
      !strcasecmp(name, "mediawiki") || !strcasecmp(name, "mwmbl") ||
      !strcasecmp(name, "hn") || !strcasecmp(name, "reddit") ||
      !strcasecmp(name, "lobsters") || !strcasecmp(name, "qwant") ||
      !strcasecmp(name, "wikibooks") || !strcasecmp(name, "wikiquote") ||
      !strcasecmp(name, "lemmy") || !strcasecmp(name, "stackoverflow"))
    return parse_json_engine;
  return NULL;
}

static void trim_inplace(char *s) {
  if (!s)
    return;
  char *start = s;
  while (*start == ' ' || *start == '\t')
    start++;
  if (start > s)
    memmove(s, start, strlen(start) + 1);
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
    s[n - 1] = '\0';
    n--;
  }
}

static int flush_engine_block(char id[ENGINE_ID_MAX], char name[ENGINE_NAME_MAX],
                              char base_url[ENGINE_URL_MAX],
                              char host_header[ENGINE_HOST_MAX],
                              char referer[ENGINE_REFERER_MAX],
                              char page_param[ENGINE_PAGE_PARAM_MAX],
                              int *page_multiplier, int *page_base,
                              char parser_name[32]) {
  if (!id[0] || !base_url[0] || !parser_name[0])
    return 0;

  ParserFunc p = parser_by_name(parser_name);
  JsonParserFunc jp = json_parser_by_name(parser_name);
  if (!p && !jp) {
    fprintf(stderr, "[WARN] Unknown engine parser: %s (engine %s)\n",
            parser_name, id);
    return -1;
  }

  if (extra_engine_count >= MAX_EXTRA_ENGINES) {
    fprintf(stderr, "[WARN] Max extra engines (%d) reached; skipping %s\n",
            MAX_EXTRA_ENGINES, id);
    return -1;
  }

  SearchEngine *e = &EXTRA_ENGINES[extra_engine_count];
  memset(e, 0, sizeof(*e));
  strncpy(e->id, id, sizeof(e->id) - 1);
  strncpy(e->name, name[0] ? name : id, sizeof(e->name) - 1);
  strncpy(e->base_url, base_url, sizeof(e->base_url) - 1);
  strncpy(e->host_header, host_header, sizeof(e->host_header) - 1);
  strncpy(e->referer, referer, sizeof(e->referer) - 1);
  strncpy(e->page_param, page_param, sizeof(e->page_param) - 1);
  e->page_multiplier = *page_multiplier;
  e->page_base = *page_base;
  e->parser = p;
  e->json_parser = jp;
  e->is_json = jp != NULL;
  e->enabled = 1;
  extra_engine_count++;
  fprintf(stderr, "[INFO] Loaded extra search engine: %s\n", e->id);
  return 0;
}

int scraping_load_engines_file(const char *path) {
  if (!path || !path[0])
    return 0;

  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "[WARN] Could not open engines_file: %s\n", path);
    return -1;
  }

  char id[ENGINE_ID_MAX] = {0};
  char name[ENGINE_NAME_MAX] = {0};
  char base_url[ENGINE_URL_MAX] = {0};
  char host_header[ENGINE_HOST_MAX] = {0};
  char referer[ENGINE_REFERER_MAX] = {0};
  char page_param[ENGINE_PAGE_PARAM_MAX] = {0};
  int page_multiplier = 1;
  int page_base = 1;
  char parser_name[32] = {0};
  int in_engine = 0;
  int have_any = 0;

  char line[768];
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\r\n")] = 0;
    trim_inplace(line);
    if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[') {
      if (in_engine && have_any) {
        flush_engine_block(id, name, base_url, host_header, referer, page_param,
                           &page_multiplier, &page_base, parser_name);
      }
      in_engine = 0;
      have_any = 0;
      memset(id, 0, sizeof(id));
      memset(name, 0, sizeof(name));
      memset(base_url, 0, sizeof(base_url));
      memset(host_header, 0, sizeof(host_header));
      memset(referer, 0, sizeof(referer));
      memset(page_param, 0, sizeof(page_param));
      page_multiplier = 1;
      page_base = 1;
      memset(parser_name, 0, sizeof(parser_name));

      char *end = strchr(line, ']');
      if (!end)
        continue;
      *end = '\0';
      if (strcasecmp(line + 1, "engine") != 0)
        continue;
      in_engine = 1;
      continue;
    }

    if (!in_engine)
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;
    trim_inplace(key);
    trim_inplace(val);
    have_any = 1;

    if (!strcasecmp(key, "id"))
      strncpy(id, val, sizeof(id) - 1);
    else if (!strcasecmp(key, "name"))
      strncpy(name, val, sizeof(name) - 1);
    else if (!strcasecmp(key, "base_url"))
      strncpy(base_url, val, sizeof(base_url) - 1);
    else if (!strcasecmp(key, "host_header"))
      strncpy(host_header, val, sizeof(host_header) - 1);
    else if (!strcasecmp(key, "referer"))
      strncpy(referer, val, sizeof(referer) - 1);
    else if (!strcasecmp(key, "page_param"))
      strncpy(page_param, val, sizeof(page_param) - 1);
    else if (!strcasecmp(key, "page_multiplier"))
      page_multiplier = atoi(val);
    else if (!strcasecmp(key, "page_base"))
      page_base = atoi(val);
    else if (!strcasecmp(key, "parser"))
      strncpy(parser_name, val, sizeof(parser_name) - 1);
  }

  if (in_engine && have_any)
    flush_engine_block(id, name, base_url, host_header, referer, page_param,
                       &page_multiplier, &page_base, parser_name);

  fclose(fp);
  return 0;
}

static int engine_id_compare(const char *engine_id, const char *config_id) {
  if (!engine_id || !config_id)
    return 0;
  while (*engine_id && *config_id) {
    char e = *engine_id;
    char c = *config_id;
    if (e >= 'A' && e <= 'Z')
      e = e - 'A' + 'a';
    if (c >= 'A' && c <= 'Z')
      c = c - 'A' + 'a';
    if (e != c)
      return 0;
    engine_id++;
    config_id++;
  }
  return *engine_id == *config_id;
}

void apply_engines_config(const char *engines_str) {
  int total = engines_total();

  if (!engines_str || engines_str[0] == '\0')
    return;

  for (int i = 0; i < total; i++) {
    SearchEngine *e = engine_mutable_at(i);
    if (e)
      e->enabled = 0;
  }

  char *copy = strdup(engines_str);
  if (!copy)
    return;

  char *saveptr;
  char *token = strtok_r(copy, ",", &saveptr);

  while (token) {
    while (*token == ' ' || *token == '\t')
      token++;

    if (strcmp(token, "*") == 0) {
      for (int i = 0; i < total; i++) {
        SearchEngine *e = engine_mutable_at(i);
        if (e)
          e->enabled = 1;
      }
    } else if (token[0] == '-' && token[1] != '\0') {
      char *engine_id = token + 1;
      int found = 0;
      for (int i = 0; i < total; i++) {
        SearchEngine *e = engine_mutable_at(i);
        if (e && engine_id_compare(e->id, engine_id)) {
          e->enabled = 0;
          found = 1;
          break;
        }
      }
      if (!found) {
        fprintf(stderr, "[WARN] Unknown engine: %s\n", engine_id);
      }
    } else {
      int found = 0;
      for (int i = 0; i < total; i++) {
        SearchEngine *e = engine_mutable_at(i);
        if (e && engine_id_compare(e->id, token)) {
          e->enabled = 1;
          found = 1;
          break;
        }
      }
      if (!found) {
        fprintf(stderr, "[WARN] Unknown engine: %s\n", token);
      }
    }

    token = strtok_r(NULL, ",", &saveptr);
  }

  free(copy);
}
