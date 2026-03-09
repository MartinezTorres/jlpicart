// test_helpers.h — shared CHECK macro and reporting for host tests.
#pragma once
#include <cstdio>
#include <cstdlib>

static int g_tests_run    = 0;
static int g_tests_failed = 0;

#define CHECK(expr) do { \
    g_tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
        g_tests_failed++; \
    } \
} while (0)

static inline int test_summary() {
    if (g_tests_failed == 0) {
        printf("OK: %d/%d tests passed\n", g_tests_run, g_tests_run);
        return EXIT_SUCCESS;
    }
    printf("FAILED: %d/%d tests failed\n", g_tests_failed, g_tests_run);
    return EXIT_FAILURE;
}
