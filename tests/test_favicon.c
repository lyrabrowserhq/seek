#include "../src/Routes/Favicon.h"
#include "test_harness.h"
#include <stdlib.h>
#include <string.h>

static void test_hash_stable(void) {
  char a[65];
  char b[65];
  TEST_ASSERT_EQ(favicon_host_hash("Example.COM", a, sizeof(a)), 0);
  TEST_ASSERT_EQ(favicon_host_hash("example.com", b, sizeof(b)), 0);
  TEST_ASSERT(strcmp(a, b) == 0);
  TEST_ASSERT_EQ((int)strlen(a), 64);
}

static void test_proxy_url(void) {
  char *u = favicon_proxy_url_for_page("https://news.ycombinator.com/item?id=1");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "/favicon?host=") != NULL);
  TEST_ASSERT(strstr(u, "news.ycombinator.com") != NULL);
  free(u);
  u = favicon_proxy_url_for_page("not-a-url");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "icon-placeholder") != NULL);
  free(u);
}

static void test_html_icon_href(void) {
  char out[256];
  const char *html =
      "<html><head><link rel=\"shortcut icon\" href=\"/icons/app.ico\">"
      "</head></html>";
  TEST_ASSERT_EQ(favicon_href_from_html(html, "example.com", out, sizeof(out)),
                 0);
  TEST_ASSERT(strstr(out, "https://example.com/icons/app.ico") != NULL);

  const char *abs =
      "<link rel=\"icon\" href=\"https://cdn.example.com/f.png\">";
  TEST_ASSERT_EQ(favicon_href_from_html(abs, "example.com", out, sizeof(out)),
                 0);
  TEST_ASSERT(strstr(out, "cdn.example.com/f.png") != NULL);
}

static void test_parent_host(void) {
  char out[256];
  TEST_ASSERT_EQ(favicon_parent_host("rss.cnn.com", out, sizeof(out)), 0);
  TEST_ASSERT(strcmp(out, "cnn.com") == 0);
  TEST_ASSERT_EQ(favicon_parent_host("cnn.com", out, sizeof(out)), -1);
}

static void test_engine_icon_url(void) {
  char *u = favicon_url_for_engine_id("lyra");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "/static/engines/lyra.svg") != NULL);
  free(u);
  u = favicon_url_for_engine_id("ddg");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "/favicon?host=") != NULL);
  TEST_ASSERT(strstr(u, "duckduckgo.com") != NULL);
  free(u);
  u = favicon_url_for_engine_id("hn");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "news.ycombinator.com") != NULL);
  free(u);
  u = favicon_url_for_engine_id("");
  TEST_ASSERT_NOT_NULL(u);
  TEST_ASSERT(strstr(u, "icon-placeholder") != NULL);
  free(u);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_hash_stable();
  test_proxy_url();
  test_html_icon_href();
  test_parent_host();
  test_engine_icon_url();
  TEST_MAIN_RETURN();
}
