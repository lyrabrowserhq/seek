#include "beaker_globals.h"
#include "security_headers.h"
#include "test_harness.h"
#include <stdio.h>
#include <string.h>

static void reset_state(void) {
  beaker_set_csp_policy(NULL);
  beaker_set_permissions_policy(NULL);
  beaker_set_hsts_max_age_sec(-1);
  beaker_set_hsts_include_subdomains(0);
  memset(current_request_buffer, 0, sizeof(current_request_buffer));
}

static void test_default_contains_core_headers(void) {
  reset_state();
  char buf[8192];
  int n = beaker_format_security_headers(buf, sizeof(buf));
  TEST_ASSERT(n > 0);
  TEST_ASSERT(strstr(buf, "Content-Security-Policy:") != NULL);
  TEST_ASSERT(strstr(buf, "X-Frame-Options: DENY") != NULL);
  TEST_ASSERT(strstr(buf, "Strict-Transport-Security:") == NULL);
}

static void test_fixed_hsts(void) {
  reset_state();
  beaker_set_hsts_max_age_sec(120);
  beaker_set_hsts_include_subdomains(1);
  char buf[8192];
  int n = beaker_format_security_headers(buf, sizeof(buf));
  TEST_ASSERT(n > 0);
  TEST_ASSERT(strstr(buf, "Strict-Transport-Security:") != NULL);
  TEST_ASSERT(strstr(buf, "max-age=120") != NULL);
  TEST_ASSERT(strstr(buf, "includeSubDomains") != NULL);
}

static void test_auto_hsts_forwarded_https(void) {
  reset_state();
  snprintf(current_request_buffer, sizeof(current_request_buffer),
           "GET / HTTP/1.1\r\n"
           "Host: example.test\r\n"
           "X-Forwarded-Proto: https\r\n"
           "\r\n");
  beaker_set_hsts_max_age_sec(-1);
  char buf[8192];
  int n = beaker_format_security_headers(buf, sizeof(buf));
  TEST_ASSERT(n > 0);
  TEST_ASSERT(strstr(buf, "Strict-Transport-Security:") != NULL);
  TEST_ASSERT(strstr(buf, "max-age=31536000") != NULL);
}

static void test_buffer_too_small(void) {
  reset_state();
  char tiny[8];
  int n = beaker_format_security_headers(tiny, sizeof(tiny));
  TEST_ASSERT(n < 0);
}

static void test_snprint_empty_ok(void) {
  reset_state();
  char out[16384];
  int n = beaker_snprint_http_empty_response(out, sizeof(out), "HTTP/1.1 404 Not Found");
  TEST_ASSERT(n > 0 && n < (int)sizeof(out));
  TEST_ASSERT(strncmp(out, "HTTP/1.1 404", 12) == 0);
}

static void test_snprint_html_ok(void) {
  reset_state();
  char out[16384];
  const char *body = "<!doctype html><title>t</title>";
  int n = beaker_snprint_http_html_response(out, sizeof(out), "HTTP/1.1 200 OK", body);
  TEST_ASSERT(n > 0 && n < (int)sizeof(out));
  TEST_ASSERT(strstr(out, "Content-Type: text/html") != NULL);
  TEST_ASSERT(strstr(out, body) != NULL);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_default_contains_core_headers();
  test_fixed_hsts();
  test_auto_hsts_forwarded_https();
  test_buffer_too_small();
  test_snprint_empty_ok();
  test_snprint_html_ok();
  TEST_MAIN_RETURN();
}
