#include "../src/Utility/NetGuard.h"
#include "test_harness.h"
#include <arpa/inet.h>
#include <string.h>

static int v4(const char *ip) {
  struct sockaddr_in in;
  memset(&in, 0, sizeof(in));
  in.sin_family = AF_INET;
  if (inet_pton(AF_INET, ip, &in.sin_addr) != 1)
    return -1;
  return netguard_addr_blocked((struct sockaddr *)&in);
}

static int v6(const char *ip) {
  struct sockaddr_in6 in;
  memset(&in, 0, sizeof(in));
  in.sin6_family = AF_INET6;
  if (inet_pton(AF_INET6, ip, &in.sin6_addr) != 1)
    return -1;
  return netguard_addr_blocked((struct sockaddr *)&in);
}

static void test_v4_blocked_ranges(void) {
  TEST_ASSERT_EQ(v4("0.0.0.0"), 1);
  TEST_ASSERT_EQ(v4("0.1.2.3"), 1);
  TEST_ASSERT_EQ(v4("10.0.0.1"), 1);
  TEST_ASSERT_EQ(v4("10.255.255.255"), 1);
  TEST_ASSERT_EQ(v4("100.64.0.1"), 1);
  TEST_ASSERT_EQ(v4("100.127.255.255"), 1);
  TEST_ASSERT_EQ(v4("127.0.0.1"), 1);
  TEST_ASSERT_EQ(v4("127.53.0.1"), 1);
  TEST_ASSERT_EQ(v4("169.254.169.254"), 1);
  TEST_ASSERT_EQ(v4("172.16.0.1"), 1);
  TEST_ASSERT_EQ(v4("172.31.255.255"), 1);
  TEST_ASSERT_EQ(v4("192.0.0.1"), 1);
  TEST_ASSERT_EQ(v4("192.0.2.1"), 1);
  TEST_ASSERT_EQ(v4("192.168.0.1"), 1);
  TEST_ASSERT_EQ(v4("198.18.0.1"), 1);
  TEST_ASSERT_EQ(v4("198.19.255.1"), 1);
  TEST_ASSERT_EQ(v4("198.51.100.1"), 1);
  TEST_ASSERT_EQ(v4("203.0.113.1"), 1);
  TEST_ASSERT_EQ(v4("224.0.0.1"), 1);
  TEST_ASSERT_EQ(v4("255.255.255.255"), 1);
}

static void test_v4_allowed(void) {
  TEST_ASSERT_EQ(v4("8.8.8.8"), 0);
  TEST_ASSERT_EQ(v4("100.63.255.255"), 0);
  TEST_ASSERT_EQ(v4("172.15.0.1"), 0);
  TEST_ASSERT_EQ(v4("172.32.0.1"), 0);
  TEST_ASSERT_EQ(v4("11.0.0.1"), 0);
  TEST_ASSERT_EQ(v4("203.0.114.1"), 0);
}

static void test_v6_blocked_ranges(void) {
  TEST_ASSERT_EQ(v6("::"), 1);
  TEST_ASSERT_EQ(v6("::1"), 1);
  TEST_ASSERT_EQ(v6("fe80::1"), 1);
  TEST_ASSERT_EQ(v6("febf::1"), 1);
  TEST_ASSERT_EQ(v6("fec0::1"), 1);
  TEST_ASSERT_EQ(v6("fc00::1"), 1);
  TEST_ASSERT_EQ(v6("fd00::1"), 1);
  TEST_ASSERT_EQ(v6("ff02::1"), 1);
  TEST_ASSERT_EQ(v6("2001:db8::1"), 1);
  TEST_ASSERT_EQ(v6("2002::1"), 1);
  TEST_ASSERT_EQ(v6("2001:0::1"), 1);
  TEST_ASSERT_EQ(v6("64:ff9b::808:808"), 1);
  TEST_ASSERT_EQ(v6("::ffff:127.0.0.1"), 1);
  TEST_ASSERT_EQ(v6("::ffff:7f00:1"), 1);
  TEST_ASSERT_EQ(v6("::ffff:8.8.8.8"), 0);
  TEST_ASSERT_EQ(v6("::7f00:1"), 1);
}

static void test_v6_allowed(void) {
  TEST_ASSERT_EQ(v6("2001:4860:4860::8888"), 0);
  TEST_ASSERT_EQ(v6("2606:4700:4700::1111"), 0);
  TEST_ASSERT_EQ(v6("2001:db9::1"), 0);
  TEST_ASSERT_EQ(v6("::ffff:93.184.216.34"), 0);
}

static void test_host_check(void) {
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("127.0.0.1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("2130706433"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("0x7f.1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("0177.0.0.1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("[::1]"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("::1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("::ffff:127.0.0.1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("::ffff:7f00:1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("169.254.169.254"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("10.0.0.1"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("localhost"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("LOCALHOST"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("foo.localhost"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("nas.local"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("db.internal"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("intranet"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name(""), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("127.0.0.1:8087"), 0);
  TEST_ASSERT_EQ(
      netguard_host_is_public_literal_or_name("127.0.0.1:8087/health?"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("a b.com"), 0);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("a@b.com"), 0);

  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("8.8.8.8"), 1);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("example.com"), 1);
  TEST_ASSERT_EQ(netguard_host_is_public_literal_or_name("2001:4860:4860::8888"),
                 1);
}

static void test_url_host(void) {
  char host[256];
  TEST_ASSERT_EQ(
      netguard_url_host("http://127.0.0.1:8087#@example.com/health", host,
                        sizeof(host)),
      0);
  TEST_ASSERT(strcmp(host, "127.0.0.1") == 0);
  TEST_ASSERT_EQ(netguard_url_host("http://user:pw@example.com/x", host,
                                   sizeof(host)),
                 0);
  TEST_ASSERT(strcmp(host, "example.com") == 0);
  TEST_ASSERT_EQ(netguard_url_host("not a url", host, sizeof(host)), -1);
}

int main(void) {
  test_v4_blocked_ranges();
  test_v4_allowed();
  test_v6_blocked_ranges();
  test_v6_allowed();
  test_host_check();
  test_url_host();
  TEST_MAIN_RETURN();
}
