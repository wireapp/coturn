#include "zrest.h"

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static void test_parses_client_username(void) {
  turn_zrest_username u = {0};
  TEST_ASSERT_TRUE(turn_zrest_parse_username("d=1700000000.v=1.k=3.t=s.r=AbC-_09", &u));
  TEST_ASSERT_FALSE(u.sft);
  TEST_ASSERT_EQUAL_UINT32(1700000000u, u.deadline);
  TEST_ASSERT_EQUAL_UINT32(3u, u.keyindex);
}

static void test_parses_sft_username(void) {
  turn_zrest_username u = {0};
  TEST_ASSERT_TRUE(turn_zrest_parse_username("sft-d=12.v=1.k=0.t=s.r=x", &u));
  TEST_ASSERT_TRUE(u.sft);
  TEST_ASSERT_EQUAL_UINT32(12u, u.deadline);
  TEST_ASSERT_EQUAL_UINT32(0u, u.keyindex);
}

static void test_random_part_may_be_empty(void) {
  turn_zrest_username u = {0};
  TEST_ASSERT_TRUE(turn_zrest_parse_username("d=1.v=1.k=1.t=s.r=", &u));
}

static void test_accepts_uint32_max(void) {
  turn_zrest_username u = {0};
  TEST_ASSERT_TRUE(turn_zrest_parse_username("d=4294967295.v=1.k=4294967295.t=s.r=a", &u));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, u.deadline);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, u.keyindex);
}

static void test_rejects_malformed(void) {
  static const char *const bad[] = {
      "",
      "user",
      "sft-",
      "d=.v=1.k=1.t=s.r=a",
      "d=1.v=1.k=.t=s.r=a",
      "d=-1.v=1.k=1.t=s.r=a",
      "d=1.v=2.k=1.t=s.r=a",
      "d=1.v=1.k=1.t=x.r=a",
      "d=1.v=1.k=1.t=s.r=a.b",
      "d=1.v=1.k=1.t=s.r=a b",
      "d=1.v=1.k=1.t=s",
      "SFT-d=1.v=1.k=1.t=s.r=a",
      "xsft-d=1.v=1.k=1.t=s.r=a",
      "sft-sft-d=1.v=1.k=1.t=s.r=a",
      " d=1.v=1.k=1.t=s.r=a",
      "d=4294967296.v=1.k=1.t=s.r=a",
      "d=1.v=1.k=99999999999999999999999.t=s.r=a",
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    turn_zrest_username u = {0};
    TEST_ASSERT_FALSE_MESSAGE(turn_zrest_parse_username(bad[i], &u), bad[i]);
  }
}

static void test_rejects_null(void) {
  turn_zrest_username u = {0};
  TEST_ASSERT_FALSE(turn_zrest_parse_username(NULL, &u));
  TEST_ASSERT_FALSE(turn_zrest_parse_username("d=1.v=1.k=1.t=s.r=a", NULL));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_client_username);
  RUN_TEST(test_parses_sft_username);
  RUN_TEST(test_random_part_may_be_empty);
  RUN_TEST(test_accepts_uint32_max);
  RUN_TEST(test_rejects_malformed);
  RUN_TEST(test_rejects_null);
  return UNITY_END();
}
