#include "ns_turn_ratelimit.h"
#include "ns_turn_ratelimit_allowlist.h"

#include <unity.h>

#include <stdio.h>

#define ALLOWLIST_FILE "test_ratelimit_allowlist.tmp"

static void write_allowlist(const char *contents) {
  FILE *f = fopen(ALLOWLIST_FILE, "w");
  TEST_ASSERT_NOT_NULL(f);
  fputs(contents, f);
  fclose(f);
}

static ioa_addr addr(const char *ip, uint16_t port) {
  ioa_addr a = {0};
  TEST_ASSERT_EQUAL_INT(0, make_ioa_addr((const uint8_t *)ip, port, &a));
  return a;
}

void setUp(void) {
  ratelimit_init();
  turn_ratelimit_allowlist_clear();
}

void tearDown(void) { remove(ALLOWLIST_FILE); }

static void test_empty_allowlist_contains_nothing(void) {
  ioa_addr a = addr("192.0.2.1", 3478);
  TEST_ASSERT_EQUAL_size_t(0, turn_ratelimit_allowlist_size());
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_contains(&a));
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_contains(NULL));
  TEST_ASSERT_EQUAL_UINT32(0u, turn_ratelimit_allowlist_get_bypasses());
}

static void test_load_skips_comments_blanks_and_malformed_lines(void) {
  write_allowlist("# comment\n"
                  "\n"
                  "   \t\n"
                  "  192.0.2.1  \r\n"
                  "2001:db8::1\n"
                  "not-an-ip\n"
                  "192.0.2.1:3478\n"
                  "192.0.2.1\n"
                  "198.51.100.7");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));
  TEST_ASSERT_EQUAL_size_t(3, turn_ratelimit_allowlist_size());

  ioa_addr v4 = addr("192.0.2.1", 1);
  ioa_addr v6 = addr("2001:db8::1", 2);
  ioa_addr last = addr("198.51.100.7", 3);
  ioa_addr other = addr("192.0.2.2", 1);
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&v4));
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&v6));
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&last));
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_contains(&other));
  TEST_ASSERT_EQUAL_UINT32(3u, turn_ratelimit_allowlist_get_bypasses());
}

static void test_port_is_ignored(void) {
  write_allowlist("192.0.2.1\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));
  ioa_addr a = addr("192.0.2.1", 0);
  ioa_addr b = addr("192.0.2.1", 65535);
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&a));
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&b));
}

static void test_ipv4_mapped_ipv6_matches_ipv4_entry(void) {
  write_allowlist("192.0.2.1\n::ffff:198.51.100.7\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));

  ioa_addr mapped = addr("::ffff:192.0.2.1", 3478);
  TEST_ASSERT_EQUAL_INT(AF_INET6, mapped.ss.sa_family);
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&mapped));

  ioa_addr plain = addr("198.51.100.7", 3478);
  TEST_ASSERT_EQUAL_INT(AF_INET, plain.ss.sa_family);
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&plain));
}

static void test_missing_file_keeps_current_set(void) {
  write_allowlist("192.0.2.1\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));
  remove(ALLOWLIST_FILE);

  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_load(NULL));
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_load(""));
  ioa_addr a = addr("192.0.2.1", 3478);
  TEST_ASSERT_EQUAL_size_t(1, turn_ratelimit_allowlist_size());
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&a));
}

static void test_reload_replaces_set(void) {
  write_allowlist("192.0.2.1\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));
  write_allowlist("192.0.2.2\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));

  ioa_addr old_entry = addr("192.0.2.1", 3478);
  ioa_addr new_entry = addr("192.0.2.2", 3478);
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_contains(&old_entry));
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_contains(&new_entry));

  write_allowlist("# nothing left\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));
  TEST_ASSERT_EQUAL_size_t(0, turn_ratelimit_allowlist_size());
  TEST_ASSERT_FALSE(turn_ratelimit_allowlist_contains(&new_entry));
}

static void test_allowlisted_source_bypasses_limiter_without_a_bucket(void) {
  write_allowlist("192.0.2.1\n");
  TEST_ASSERT_TRUE(turn_ratelimit_allowlist_load(ALLOWLIST_FILE));

  ioa_addr allowed = addr("192.0.2.1", 3478);
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_FALSE(ratelimit_consume_address(&allowed, 1u, NULL, NULL));
  }
  TEST_ASSERT_EQUAL_UINT32(0u, ratelimit_count_occupied());
  TEST_ASSERT_EQUAL_UINT32(100u, turn_ratelimit_allowlist_get_bypasses());

  ioa_addr limited = addr("192.0.2.2", 3478);
  bool first_drop = false;
  TEST_ASSERT_FALSE(ratelimit_consume_address(&limited, 1u, NULL, NULL));
  TEST_ASSERT_TRUE(ratelimit_consume_address(&limited, 1u, &first_drop, NULL));
  TEST_ASSERT_TRUE(first_drop);
  TEST_ASSERT_EQUAL_UINT32(100u, turn_ratelimit_allowlist_get_bypasses());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_allowlist_contains_nothing);
  RUN_TEST(test_load_skips_comments_blanks_and_malformed_lines);
  RUN_TEST(test_port_is_ignored);
  RUN_TEST(test_ipv4_mapped_ipv6_matches_ipv4_entry);
  RUN_TEST(test_missing_file_keeps_current_set);
  RUN_TEST(test_reload_replaces_set);
  RUN_TEST(test_allowlisted_source_bypasses_limiter_without_a_bucket);
  return UNITY_END();
}
