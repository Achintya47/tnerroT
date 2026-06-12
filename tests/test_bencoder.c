// test_bencoder.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

char* encode_int(const int num);
char* encode_string(char* str);

static int tests_run = 0;
static int tests_failed = 0;

#define EXPECT_STRING_EQ(actual, expected)                            \
do {                                                                  \
    tests_run++;                                                      \
    if (strcmp((actual), (expected)) != 0) {                          \
        tests_failed++;                                               \
        fprintf(stderr,                                               \
                "[FAIL] %s:%d\n"                                      \
                "  Expected: \"%s\"\n"                                \
                "  Got:      \"%s\"\n",                               \
                __FILE__,                                             \
                __LINE__,                                             \
                (expected),                                           \
                (actual));                                            \
    }                                                                 \
} while (0)

typedef struct {
    int input;
    const char* expected;
} IntTestCase;

typedef struct {
    char* input;
    const char* expected;
} StringTestCase;

void test_encode_int(void) {

    IntTestCase tests[] = {
        {42, "i42e"},
        {0, "i0e"},
        {-42, "i-42e"},
        {1, "i1e"},
        {-1, "i-1e"},
        {INT_MAX, "i2147483647e"},
        {INT_MIN, "i-2147483648e"}
    };

    size_t count = sizeof(tests) / sizeof(tests[0]);

    for (size_t i = 0; i < count; i++) {

        char* result = encode_int(tests[i].input);

        EXPECT_STRING_EQ(result, tests[i].expected);

        free(result);
    }
}

void test_encode_string(void) {

    StringTestCase tests[] = {
        {"", "0:"},
        {"a", "1:a"},
        {"spam", "4:spam"},
        {"12345", "5:12345"},
        {"hello world", "11:hello world"},
        {"!@#$%^", "6:!@#$%^"},
        {" ", "1: "},
        {"\n", "1:\n"},
        {"\t", "1:\t"},
        {":", "1::"},
        {"abcdefghijklmnopqrstuvwxyz",
         "26:abcdefghijklmnopqrstuvwxyz"}
    };

    size_t count = sizeof(tests) / sizeof(tests[0]);

    for (size_t i = 0; i < count; i++) {

        char* result = encode_string(tests[i].input);

        EXPECT_STRING_EQ(result, tests[i].expected);

        free(result);
    }
}

int main(void) {

    printf("Running bencoder tests...\n");

    test_encode_int();
    test_encode_string();

    printf("\n");
    printf("Tests Run    : %d\n", tests_run);
    printf("Tests Failed : %d\n", tests_failed);
    printf("Tests Passed : %d\n", tests_run - tests_failed);

    if (tests_failed == 0) {
        printf("\nAll tests passed.\n");
        return EXIT_SUCCESS;
    }

    printf("\nSome tests failed.\n");
    return EXIT_FAILURE;
}