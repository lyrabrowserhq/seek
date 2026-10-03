#include "../src/Proxy/Proxy.h"
#include "test_harness.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_load_and_select(void) {
  char tmpl[] = "/tmp/seek_proxy_XXXXXX";
  int fd = mkstemp(tmpl);
  TEST_ASSERT(fd >= 0);
  const char *lines = "# comment\n"
                     "socks5://127.0.0.1:9999\n";
  ssize_t w = write(fd, lines, strlen(lines));
  close(fd);
  TEST_ASSERT_EQ(w, (ssize_t)strlen(lines));

  free_proxy_list();
  int n = load_proxy_list(tmpl);
  TEST_ASSERT(n >= 1);

  Proxy *p = get_random_proxy();
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT(strcmp(p->host, "127.0.0.1") == 0);
  TEST_ASSERT_EQ(p->port, 9999);

  free_proxy_list();
  unlink(tmpl);
}

static void test_load_missing(void) {
  free_proxy_list();
  TEST_ASSERT_EQ(load_proxy_list("/nonexistent/proxy_list_xyz.txt"), -1);
}

static void test_set_proxy_config(void) {
  set_proxy_config("http://127.0.0.1:1", 0, 0);
  TEST_ASSERT(strncmp(proxy_url, "http://", 7) == 0);
}

static void test_socks5h_line(void) {
  char tmpl[] = "/tmp/seek_proxyh_XXXXXX";
  int fd = mkstemp(tmpl);
  TEST_ASSERT(fd >= 0);
  const char *lines = "socks5h://10.64.0.1:1080\n";
  ssize_t w = write(fd, lines, strlen(lines));
  close(fd);
  TEST_ASSERT_EQ(w, (ssize_t)strlen(lines));
  free_proxy_list();
  int n = load_proxy_list(tmpl);
  TEST_ASSERT(n >= 1);
  Proxy *p = get_random_proxy();
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT(strcmp(p->host, "10.64.0.1") == 0);
  TEST_ASSERT_EQ(p->port, 1080);
  free_proxy_list();
  unlink(tmpl);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_load_and_select();
  test_load_missing();
  test_set_proxy_config();
  test_socks5h_line();
  TEST_MAIN_RETURN();
}
