#include "../src/Config.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_load_missing(void) {
  Config c;
  memset(&c, 0, sizeof(c));
  TEST_ASSERT_EQ(load_config("/nonexistent/seeker_config_xyz.ini", &c), -1);
}

static void test_load_minimal(void) {
  char tmpl[] = "/tmp/seek_cfg_XXXXXX";
  int fd = mkstemp(tmpl);
  TEST_ASSERT(fd >= 0);
  const char *cfg = "[server]\n"
                    "host = 127.0.0.1\n"
                    "port = 8080\n"
                    "domain = example.test\n"
                    "[proxy]\n"
                    "proxy = http://127.0.0.1:1\n"
                    "max_retries = 5\n"
                    "randomize_username = 0\n"
                    "randomize_password = 0\n"
                    "[cache]\n"
                    "dir = /tmp/cache_dir\n"
                    "ttl_search = 100\n"
                    "ttl_infobox = 200\n"
                    "[engines]\n"
                    "engines = ddg,brave\n"
                    "default_engine = ddg\n"
                    "yacy_instance = http://127.0.0.1:8090\n"
                    "index_url = http://127.0.0.1:8091\n"
                    "[rate_limit]\n"
                    "search_requests = 10\n"
                    "search_interval = 60\n"
                    "images_requests = 20\n"
                    "images_interval = 30\n"
                    "news_requests = 5\n"
                    "news_interval = 15\n";
  ssize_t w = write(fd, cfg, strlen(cfg));
  close(fd);
  TEST_ASSERT_EQ(w, (ssize_t)strlen(cfg));

  Config c;
  memset(&c, 0, sizeof(c));
  TEST_ASSERT_EQ(load_config(tmpl, &c), 0);
  TEST_ASSERT(strcmp(c.host, "127.0.0.1") == 0);
  TEST_ASSERT_EQ(c.port, 8080);
  TEST_ASSERT(strcmp(c.domain, "example.test") == 0);
  TEST_ASSERT_EQ(c.max_proxy_retries, 5);
  TEST_ASSERT_EQ(c.cache_ttl_search, 100);
  TEST_ASSERT_EQ(c.cache_ttl_infobox, 200);
  TEST_ASSERT(strstr(c.engines, "ddg") != NULL);
  TEST_ASSERT(strcmp(c.default_engine, "ddg") == 0);
  TEST_ASSERT(strcmp(c.yacy_instance, "http://127.0.0.1:8090") == 0);
  TEST_ASSERT(strcmp(c.index_url, "http://127.0.0.1:8091") == 0);
  TEST_ASSERT_EQ(c.rate_limit_search_requests, 10);
  TEST_ASSERT_EQ(c.rate_limit_search_interval, 60);
  TEST_ASSERT_EQ(c.rate_limit_images_requests, 20);
  TEST_ASSERT_EQ(c.rate_limit_images_interval, 30);
  TEST_ASSERT_EQ(c.rate_limit_news_requests, 5);
  TEST_ASSERT_EQ(c.rate_limit_news_interval, 15);
  unlink(tmpl);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_load_missing();
  test_load_minimal();
  TEST_MAIN_RETURN();
}
