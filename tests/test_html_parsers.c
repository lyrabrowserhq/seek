#include "../src/Scraping/Hdoc.h"
#include "../src/Scraping/Scraping.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

int parse_ddg_lite_h(const char *, Hdoc *, SearchResult **, int);
int parse_startpage_h(const char *, Hdoc *, SearchResult **, int);
int parse_brave_h(const char *, Hdoc *, SearchResult **, int);
int parse_yahoo_h(const char *, Hdoc *, SearchResult **, int);
int parse_mojeek_h(const char *, Hdoc *, SearchResult **, int);
int parse_wiby_h(const char *, Hdoc *, SearchResult **, int);

typedef int (*Hfn)(const char *, Hdoc *, SearchResult **, int);

static int run(const char *html, Hfn fn, SearchResult **out) {
  Hdoc *d = hdoc_parse(html, strlen(html));
  if (!d)
    return -1;
  int n = fn("test", d, out, 20);
  hdoc_free(d);
  return n;
}

static void free_results(SearchResult *r, int n) {
  if (!r)
    return;
  for (int i = 0; i < n; i++) {
    free(r[i].url);
    free(r[i].title);
    free(r[i].snippet);
    free(r[i].date);
  }
  free(r);
}

/* ddg lite: table rows, result link in a.result-link, snippet in the
   following tr > td.result-snippet */
static const char *DDG_HTML =
    "<html><body><table>"
    "<tr class='result-sponsored'><td><a class='result-link' href='/l/?kh=-1&uddg=https%3A%2F%2Fad.example'>Ad</a></td></tr>"
    "<tr><td><a class='result-link' href='//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.org%2Fa'>First &amp; Result</a></td></tr>"
    "<tr><td class='result-snippet'>Snippet one text</td></tr>"
    "<tr><td><a class='result-link' href='https://example.org/b'>Second</a></td></tr>"
    "<tr><td class='result-snippet'>Snippet two</td></tr>"
    "</table></body></html>";

static void test_ddg_lite(void) {
  SearchResult *r = NULL;
  int n = run(DDG_HTML, parse_ddg_lite_h, &r);
  TEST_ASSERT(n >= 2);
  TEST_ASSERT(r[0].url && strstr(r[0].url, "example.org"));
  TEST_ASSERT((r[0].title) && strstr((r[0].title), "First"));
  TEST_ASSERT((r[0].snippet) && strstr((r[0].snippet), "Snippet one"));
  free_results(r, n);
}

static const char *STARTPAGE_HTML =
    "<html><body><div class='w-gl__result result'>"
    "<a class='result-link' href='https://a.example'>Link</a>"
    "<h2 class='wgl-title'>Title One</h2>"
    "<p class='description'>Desc one</p></div>"
    "<div class='result result--ad'><a href='/'>ad</a></div>"
    "<div class='result'>"
    "<a class='result-link' href='https://b.example'>Link</a>"
    "<h2 class='wgl-title'>Title Two</h2>"
    "<p class='description'>Desc two</p></div></body></html>";

static void test_startpage(void) {
  SearchResult *r = NULL;
  int n = run(STARTPAGE_HTML, parse_startpage_h, &r);
  TEST_ASSERT(n >= 2);
  TEST_ASSERT((r[0].title) && strstr((r[0].title), "Title One"));
  TEST_ASSERT((r[0].snippet) && strstr((r[0].snippet), "Desc one"));
  free_results(r, n);
}

static const char *BRAVE_HTML =
    "<html><body><div class='snippet result-wrapper'>"
    "<a class='result-header l1' href='https://b.example'>x</a>"
    "<div class='search-snippet-title'>Brave One</div>"
    "<div class='generic-snippet'><div class='content'>Brave snippet</div></div></div>"
    "<div class='snippet result-wrapper'>"
    "<a class='l1' href='https://c.example'>x</a>"
    "<div class='search-snippet-title'>Brave Two</div></div>"
    "</body></html>";

static void test_brave(void) {
  SearchResult *r = NULL;
  int n = run(BRAVE_HTML, parse_brave_h, &r);
  TEST_ASSERT(n >= 2);
  TEST_ASSERT((r[0].title) && strstr((r[0].title), "Brave One"));
  TEST_ASSERT((r[0].snippet) && strstr((r[0].snippet), "Brave snippet"));
  free_results(r, n);
}

static const char *YAHOO_HTML =
    "<html><body><div class='dd algo-sr'>"
    "<div class='compTitle'><a target='_blank' href='https://r.search.yahoo.com/_ylt=x/RU=https%3A%2F%2Fsite.example'>x</a></div>"
    "<h3 class='title'>Yahoo One</h3>"
    "<div class='compText'><p>Yahoo snippet</p></div></div></body></html>";

static void test_yahoo(void) {
  SearchResult *r = NULL;
  int n = run(YAHOO_HTML, parse_yahoo_h, &r);
  TEST_ASSERT(n >= 1);
  TEST_ASSERT((r[0].title) && strstr((r[0].title), "Yahoo One"));
  free_results(r, n);
}

static const char *MOJEEK_HTML =
    "<html><body><ul class='results-standard'>"
    "<li class='r1'><a class='title' href='https://m1.example'>Mojo One</a><p class='s'>snip1</p></li>"
    "<li class='r2'><a class='title' href='https://m2.example'>Mojo Two</a><p class='s'>snip2</p></li>"
    "</ul></body></html>";

static void test_mojeek(void) {
  SearchResult *r = NULL;
  int n = run(MOJEEK_HTML, parse_mojeek_h, &r);
  TEST_ASSERT(n == 2);
  TEST_ASSERT((r[1].title) && strstr((r[1].title), "Mojo Two"));
  free_results(r, n);
}

static const char *WIBY_HTML =
    "<html><body><blockquote><a class='tlink' href='https://w.example'>Wiby Title</a>"
    "<p class='url'>w.example</p><p>Wiby body text here</p></blockquote>"
    "<blockquote><p>no tlink - skipped</p></blockquote></body></html>";

static void test_wiby(void) {
  SearchResult *r = NULL;
  int n = run(WIBY_HTML, parse_wiby_h, &r);
  TEST_ASSERT(n == 1);
  TEST_ASSERT((r[0].title) && strstr((r[0].title), "Wiby Title"));
  TEST_ASSERT((r[0].snippet) && strstr((r[0].snippet), "body text"));
  free_results(r, n);
}

int main(void) {
  test_ddg_lite();
  test_startpage();
  test_brave();
  test_yahoo();
  test_mojeek();
  test_wiby();
  TEST_MAIN_RETURN();
}
