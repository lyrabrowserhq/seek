/* engine html parsers on lexbor (Hdoc) instead of libxml2 xpath.
   each returns the number of results written into *out_results */
#include "Hdoc.h"
#include "Scraping.h"
#include "../Utility/Unescape.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static SearchResult *alloc_out(int wanted, int max) {
  int n = wanted < max ? wanted : max;
  if (n <= 0)
    n = max > 0 ? max : 16;
  return calloc((size_t)n, sizeof(SearchResult));
}

static void put_result(SearchResult *r, const char *url, const char *title,
                       const char *snippet, int unescape_url) {
  r->url = unescape_url ? unescape_search_url(url ? url : "")
                        : strdup(url ? url : "");
  r->title = strdup(title && *title ? title : "No Title");
  r->snippet = strdup(snippet ? snippet : "");
  r->date = NULL;
}

/* all strings from hnode_* are malloc'd - single free helper */
static void free3(char *a, char *b, char *c) {
  free(a);
  free(b);
  free(c);
}

static char *find_text(Hdoc *d, Hnode *ctx, const char *css) {
  Hnode *n = hdoc_find1(d, ctx, css);
  return n ? hnode_text(n) : NULL;
}

static char *find_href(Hdoc *d, Hnode *ctx, const char *css) {
  Hnode *n = hdoc_find1(d, ctx, css);
  return n ? hnode_attr(n, "href") : NULL;
}

int parse_ddg_lite_h(const char *engine_name, Hdoc *doc,
                     SearchResult **out_results, int max_results) {
  (void)engine_name;
  HnodeList links = hdoc_select(
      doc, NULL, "tr:not([class*='result-sponsored']) a.result-link");
  if (links.count == 0) {
    hlist_free(&links);
    return 0;
  }
  *out_results = alloc_out(links.count, max_results);
  if (!*out_results) {
    hlist_free(&links);
    return 0;
  }

  int found = 0;
  for (int i = 0; i < links.count && found < max_results; i++) {
    Hnode *a = links.items[i];
    char *title = hnode_text(a);
    char *url = hnode_attr(a, "href");
    char *snippet = NULL;

    /* the link lives in a tr; its sibling tr holds the snippet cell */
    Hnode *tr = hnode_closest(doc, a, "tr");
    Hnode *next = tr ? hnode_next_sibling(tr) : NULL;
    while (next && strcasecmp(hnode_tag(next), "tr") != 0)
      next = hnode_next_sibling(next);
    if (next)
      snippet = find_text(doc, next, "td.result-snippet");

    if (url && title && *url && *title) {
      put_result(&(*out_results)[found], url, title, snippet, 1);
      found++;
    }
    free3(title, url, snippet);
  }
  hlist_free(&links);
  return found;
}

int parse_startpage_h(const char *engine_name, Hdoc *doc,
                      SearchResult **out_results, int max_results) {
  (void)engine_name;
  HnodeList boxes = hdoc_select(doc, NULL, "div[class*='result']");
  if (boxes.count == 0) {
    hlist_free(&boxes);
    return 0;
  }
  *out_results = alloc_out(boxes.count, max_results);
  if (!*out_results) {
    hlist_free(&boxes);
    return 0;
  }

  int found = 0;
  for (int i = 0; i < boxes.count && found < max_results; i++) {
    Hnode *box = boxes.items[i];
    char *url = find_href(doc, box, "a[class*='result-link']");
    char *title = find_text(doc, box, "h2[class*='wgl-title']");
    char *snippet = find_text(doc, box, "p[class*='description']");
    if (url && title) {
      put_result(&(*out_results)[found], url, title, snippet, 0);
      found++;
    }
    free3(url, title, snippet);
  }
  hlist_free(&boxes);
  return found;
}

int parse_brave_h(const char *engine_name, Hdoc *doc,
                  SearchResult **out_results, int max_results) {
  (void)engine_name;
  HnodeList boxes = hdoc_select(doc, NULL, "div[class*='result-wrapper']");
  if (boxes.count == 0) {
    hlist_free(&boxes);
    return 0;
  }
  *out_results = alloc_out(boxes.count, max_results);
  if (!*out_results) {
    hlist_free(&boxes);
    return 0;
  }

  int found = 0;
  for (int i = 0; i < boxes.count && found < max_results; i++) {
    Hnode *box = boxes.items[i];
    char *url = find_href(doc, box, "a[class*='l1']");
    char *title = find_text(doc, box, "div[class*='search-snippet-title']");
    char *snippet =
        find_text(doc, box, "div[class*='generic-snippet'] div[class*='content']");
    if (url && title) {
      put_result(&(*out_results)[found], url, title, snippet, 0);
      found++;
    }
    free3(url, title, snippet);
  }
  hlist_free(&boxes);
  return found;
}

int parse_yahoo_h(const char *engine_name, Hdoc *doc,
                  SearchResult **out_results, int max_results) {
  (void)engine_name;
  HnodeList boxes = hdoc_select(doc, NULL, "div[class*='algo-sr']");
  if (boxes.count == 0) {
    hlist_free(&boxes);
    return 0;
  }
  *out_results = alloc_out(boxes.count, max_results);
  if (!*out_results) {
    hlist_free(&boxes);
    return 0;
  }

  int found = 0;
  for (int i = 0; i < boxes.count && found < max_results; i++) {
    Hnode *box = boxes.items[i];
    char *url = find_href(doc, box, "div[class*='compTitle'] a[target='_blank']");
    char *title = find_text(doc, box, "h3[class*='title']");
    char *snippet = find_text(doc, box, "div[class*='compText'] p");
    if (url && title) {
      put_result(&(*out_results)[found], url, title, snippet, 1);
      found++;
    }
    free3(url, title, snippet);
  }
  hlist_free(&boxes);
  return found;
}

int parse_mojeek_h(const char *engine_name, Hdoc *doc,
                   SearchResult **out_results, int max_results) {
  (void)engine_name;
  HnodeList boxes =
      hdoc_select(doc, NULL, "ul.results-standard > li[class^='r']");
  if (boxes.count == 0) {
    hlist_free(&boxes);
    return 0;
  }
  *out_results = alloc_out(boxes.count, max_results);
  if (!*out_results) {
    hlist_free(&boxes);
    return 0;
  }

  int found = 0;
  for (int i = 0; i < boxes.count && found < max_results; i++) {
    Hnode *box = boxes.items[i];
    Hnode *a = hdoc_find1(doc, box, "a.title");
    char *url = a ? hnode_attr(a, "href") : NULL;
    char *title = a ? hnode_text(a) : NULL;
    char *snippet = find_text(doc, box, "p.s");
    if (url && title) {
      put_result(&(*out_results)[found], url, title, snippet, 0);
      found++;
    }
    free3(url, title, snippet);
  }
  hlist_free(&boxes);
  return found;
}

int parse_wiby_h(const char *engine_name, Hdoc *doc,
                 SearchResult **out_results, int max_results) {
  (void)engine_name;
  /* xpath //blockquote[.//a[@class='tlink']] -> css :has() */
  HnodeList boxes = hdoc_select(doc, NULL, "blockquote:has(a.tlink)");
  if (boxes.count == 0) {
    hlist_free(&boxes);
    return 0;
  }
  *out_results = alloc_out(boxes.count, max_results);
  if (!*out_results) {
    hlist_free(&boxes);
    return 0;
  }

  int found = 0;
  for (int i = 0; i < boxes.count && found < max_results; i++) {
    Hnode *box = boxes.items[i];
    Hnode *a = hdoc_find1(doc, box, "a.tlink");
    char *url = a ? hnode_attr(a, "href") : NULL;
    char *title = a ? hnode_text(a) : NULL;
    char *snippet = find_text(doc, box, "p:not(.url)");
    if (url && title) {
      put_result(&(*out_results)[found], url, title, snippet, 0);
      found++;
    }
    free3(url, title, snippet);
  }
  hlist_free(&boxes);
  return found;
}
