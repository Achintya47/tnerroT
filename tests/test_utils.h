#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>

/* Simple test tracking */
static int _tests_run    = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define CHECK(desc, cond)                                               \
    do {                                                                \
        _tests_run++;                                                   \
        if (cond) {                                                     \
            _tests_passed++;                                            \
            printf("  [PASS] %s\n", desc);                             \
        } else {                                                        \
            _tests_failed++;                                            \
            printf("  [FAIL] %s  (line %d)\n", desc, __LINE__);        \
        }                                                               \
    } while (0)

#define SECTION(name) printf("\n-- %s --\n", name)

#define SUMMARY()                                                           \
    do {                                                                    \
        printf("\n%d/%d tests passed", _tests_passed, _tests_run);         \
        if (_tests_failed)                                                  \
            printf("  (%d failed)\n", _tests_failed);                      \
        else                                                                \
            printf(" -- all good!\n");                                      \
    } while (0)

/* Returns 0 on success, 1 if any test failed (usable as main() return) */
#define EXIT_STATUS() (_tests_failed ? 1 : 0)

#endif /* TEST_UTILS_H */