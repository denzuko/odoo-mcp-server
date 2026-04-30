/*
 * test.h — minimal inline xUnit test harness
 *
 * No external framework. Pattern: suite → test case → assertion.
 * xUnit-compliant output: passes/failures/total on exit.
 *
 * Usage:
 *   #include "test.h"
 *   TEST(my_thing) { ASSERT(1 == 1); }
 *   int main(void) { RUN(my_thing); return xunit_summary(); }
 *
 * Da Planet Security / denzuko <denzuko@dapla.net>
 * BSD 2-Clause License
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int _tests_run    = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

/* Declare a test function */
#define TEST(name) static void test_##name(void)

/* Run a test, print result */
#define RUN(name)                                                    \
    do {                                                             \
        _tests_run++;                                                \
        printf("  %-48s ", #name);                                   \
        fflush(stdout);                                              \
        _xunit_current_failed = 0;                                   \
        test_##name();                                               \
        if (0 == _xunit_current_failed) {                           \
            _tests_passed++;                                         \
            printf("PASS\n");                                        \
        }                                                            \
    } while (0)

static int _xunit_current_failed = 0;

/* Base assertion */
#define ASSERT(cond)                                                 \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL\n    assertion failed: %s\n"               \
                   "    at %s:%d\n", #cond, __FILE__, __LINE__);    \
            _tests_failed++;                                         \
            _xunit_current_failed = 1;                              \
            return;                                                  \
        }                                                            \
    } while (0)

#define ASSERT_NULL(p)      ASSERT((p) == NULL)
#define ASSERT_NOTNULL(p)   ASSERT((p) != NULL)
#define ASSERT_INT(a, b)    ASSERT((a) == (b))
#define ASSERT_STR(a, b)    ASSERT(0 == strcmp((a), (b)))
#define ASSERT_CONTAINS(haystack, needle) \
    ASSERT(NULL != strstr((haystack), (needle)))

/* Print summary and return exit code */
static inline int xunit_summary(void)
{
    printf("\n%d/%d passed", _tests_passed, _tests_run);
    if (_tests_failed) printf("  (%d FAILED)", _tests_failed);
    printf("\n");
    return (_tests_failed > 0) ? 1 : 0;
}

#endif /* TEST_H */
