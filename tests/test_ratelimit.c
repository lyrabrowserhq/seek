#include "../src/Limiter/RateLimit.h"
#include "test_harness.h"
#include <string.h>

static void test_disabled(void) {
  RateLimitConfig cfg = {.max_requests = 0, .interval_seconds = 0};
  for (int i = 0; i < 5; i++) {
    RateLimitResult r = rate_limit_check("test_disabled", &cfg);
    TEST_ASSERT_EQ(r.limited, 0);
  }
}

static void test_basic_limit(void) {
  RateLimitConfig cfg = {.max_requests = 2, .interval_seconds = 9999};
  RateLimitResult r;

  r = rate_limit_check("test_basic", &cfg);
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check("test_basic", &cfg);
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check("test_basic", &cfg);
  TEST_ASSERT_EQ(r.limited, 1);
  TEST_ASSERT(r.retry_after_seconds >= 1);
}

static void test_scopes_independent(void) {
  RateLimitConfig cfg = {.max_requests = 1, .interval_seconds = 9999};
  RateLimitResult r;

  r = rate_limit_check("test_scope_a", &cfg);
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check("test_scope_a", &cfg);
  TEST_ASSERT_EQ(r.limited, 1);
  r = rate_limit_check("test_scope_b", &cfg);
  TEST_ASSERT_EQ(r.limited, 0);
}

static void test_zero_interval(void) {
  RateLimitConfig cfg = {.max_requests = 1, .interval_seconds = 0};
  RateLimitResult r = rate_limit_check("test_zero_int", &cfg);
  TEST_ASSERT_EQ(r.limited, 0);
}

static void test_local_not_limited(void) {
  RateLimitConfig cfg = {.max_requests = 1, .interval_seconds = 9999};
  RateLimitResult r;
  r = rate_limit_check_ip("test_local", &cfg, "127.0.0.1");
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check_ip("test_local", &cfg, "10.0.0.4");
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check_ip("test_local", &cfg, "192.168.1.9");
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check_ip("test_local", &cfg, "172.17.0.2");
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check_ip("test_local", &cfg, "::1");
  TEST_ASSERT_EQ(r.limited, 0);
}

static void test_public_hashed_limit(void) {
  RateLimitConfig cfg = {.max_requests = 2, .interval_seconds = 9999};
  RateLimitResult r;
  r = rate_limit_check_ip("test_pub", &cfg, "8.8.8.8");
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check_ip("test_pub", &cfg, "8.8.8.8");
  TEST_ASSERT_EQ(r.limited, 0);
  r = rate_limit_check_ip("test_pub", &cfg, "8.8.8.8");
  TEST_ASSERT_EQ(r.limited, 1);
  TEST_ASSERT(r.retry_after_seconds >= 1);
}

static void test_ip_helpers(void) {
  TEST_ASSERT_EQ(rate_limit_ip_is_local("127.0.0.1"), 1);
  TEST_ASSERT_EQ(rate_limit_ip_is_local("10.1.2.3"), 1);
  TEST_ASSERT_EQ(rate_limit_ip_is_local("172.16.0.1"), 1);
  TEST_ASSERT_EQ(rate_limit_ip_is_local("192.168.0.1"), 1);
  TEST_ASSERT_EQ(rate_limit_ip_is_local("8.8.8.8"), 0);
  TEST_ASSERT_EQ(rate_limit_ip_is_local("1.1.1.1"), 0);
}

int main(void) {
  test_failures = 0;
  test_runs = 0;
  test_disabled();
  test_basic_limit();
  test_scopes_independent();
  test_zero_interval();
  test_local_not_limited();
  test_public_hashed_limit();
  test_ip_helpers();
  TEST_MAIN_RETURN();
}
