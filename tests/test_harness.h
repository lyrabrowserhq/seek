#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <stdlib.h>

static int test_failures;
static int test_runs;

#define TEST_ASSERT(cond)                                                      \
  do {                                                                         \
    test_runs++;                                                               \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      test_failures++;                                                         \
    }                                                                          \
  } while (0)

#define TEST_ASSERT_EQ(a, b) TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NULL(p) TEST_ASSERT((p) == NULL)
#define TEST_ASSERT_NOT_NULL(p) TEST_ASSERT((p) != NULL)

#define TEST_MAIN_RETURN()                                                     \
  do {                                                                         \
    if (test_failures > 0) {                                                   \
      fprintf(stderr, "%d failures (%d checks)\n", test_failures, test_runs);    \
      return 1;                                                                \
    }                                                                          \
    fprintf(stderr, "ok (%d checks)\n", test_runs);                            \
    return 0;                                                                  \
  } while (0)

#endif
