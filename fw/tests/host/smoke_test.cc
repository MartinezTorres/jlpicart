// Smoke test — validates that the host test harness itself works.
// No firmware logic is tested here; this is just a build/run sanity check.

#include <cstdio>
#include <cstdlib>

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(expr) do { \
    tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
        tests_failed++; \
    } \
} while (0)

int main() {
    // Trivial checks to confirm the harness compiles and runs.
    CHECK(1 + 1 == 2);
    CHECK(sizeof(int) >= 4);
    CHECK(sizeof(void*) >= 4);

    if (tests_failed == 0) {
        printf("OK: %d/%d tests passed\n", tests_run, tests_run);
        return EXIT_SUCCESS;
    } else {
        printf("FAILED: %d/%d tests failed\n", tests_failed, tests_run);
        return EXIT_FAILURE;
    }
}
