/**
 * @brief Unit tests for btypes.c: the BValue constructors, the growable
 *        list/dict containers (including the capacity-growth path), the
 *        accessor bounds checks, and recursive destroy_value().
 */
#include <stdio.h>
#include <string.h>
#include "btypes.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); g_pass++; } \
    else      { printf("[FAIL] %s\n", name); g_fail++; } \
} while (0)

static void test_create_int(void) {
    BValue* v = create_int(42);
    CHECK(v != NULL, "create_int: allocates");
    CHECK(v->type == BINT, "create_int: type is BINT");
    CHECK(v->value.integer.value == 42, "create_int: stores value");
    CHECK(v->encoded_begin == NULL && v->encoded_end == NULL,
          "create_int: encoded_begin/end are zero-initialized (not garbage)");
    destroy_value(v);
}

static void test_create_negative_int(void) {
    BValue* v = create_int(-1);
    CHECK(v != NULL && v->value.integer.value == -1, "create_int: handles negative values");
    destroy_value(v);
}

static void test_create_string(void) {
    BValue* v = create_string("spam", 4);
    CHECK(v != NULL, "create_string: allocates");
    CHECK(v->type == BSTRING, "create_string: type is BSTRING");
    CHECK(v->value.string.length == 4, "create_string: stores length");
    CHECK(memcmp(v->value.string.data, "spam", 4) == 0, "create_string: copies data");
    destroy_value(v);
}

static void test_create_string_is_a_copy(void) {
    char buf[] = "spam";
    BValue* v = create_string(buf, 4);

    buf[0] = 'X';   /* mutate the original after construction */

    CHECK(v->value.string.data[0] == 's', "create_string: stores an independent copy, not the original pointer");
    destroy_value(v);
}

static void test_list_append_and_get(void) {
    BValue* list = create_list();
    CHECK(list != NULL, "create_list: allocates");
    CHECK(list->type == BLIST, "create_list: type is BLIST");
    CHECK(list->value.list.count == 0, "create_list: starts empty");

    list_append(list, create_int(1));
    list_append(list, create_int(2));
    list_append(list, create_int(3));

    CHECK(list->value.list.count == 3, "list_append: count tracks appended items");
    CHECK(list_get(list, 0)->value.integer.value == 1, "list_get: index 0 correct");
    CHECK(list_get(list, 2)->value.integer.value == 3, "list_get: last index correct");

    destroy_value(list);
}

/* create_list() starts at capacity 4 — appending a 5th item must trigger
   realloc() growth without corrupting the earlier entries. */
static void test_list_append_grows_capacity(void) {
    BValue* list = create_list();

    for (int i = 0; i < 10; i++)
        list_append(list, create_int(i));

    CHECK(list->value.list.count == 10, "list_append: grows past initial capacity (10 items)");

    int ok = 1;
    for (int i = 0; i < 10; i++) {
        BValue* item = list_get(list, i);
        if (!item || item->value.integer.value != i) { ok = 0; break; }
    }
    CHECK(ok, "list_append: all items intact and in order after growth");

    destroy_value(list);
}

/* Regression test: an off-by-one here previously let index == count pass
   the bounds check and return an out-of-range item. */
static void test_list_get_bounds(void) {
    BValue* list = create_list();
    list_append(list, create_int(1));
    list_append(list, create_int(2));

    CHECK(list_get(list, -1) == NULL, "list_get: negative index returns NULL");
    CHECK(list_get(list, 2) == NULL, "list_get: index == count returns NULL (not the next unwritten slot)");
    CHECK(list_get(list, 99) == NULL, "list_get: far out-of-range index returns NULL");
    CHECK(list_get(NULL, 0) == NULL, "list_get: NULL list returns NULL");

    destroy_value(list);
}

static void test_dict_insert_and_get(void) {
    BValue* dict = create_dict();
    CHECK(dict != NULL, "create_dict: allocates");
    CHECK(dict->type == BDICT, "create_dict: type is BDICT");

    dict_insert(dict, "cow", 3, create_string("moo", 3));
    dict_insert(dict, "spam", 4, create_string("eggs", 4));

    BValue* cow = dict_get(dict, "cow", 3);
    BValue* spam = dict_get(dict, "spam", 4);

    CHECK(cow && memcmp(cow->value.string.data, "moo", 3) == 0, "dict_get: retrieves value for \"cow\"");
    CHECK(spam && memcmp(spam->value.string.data, "eggs", 4) == 0, "dict_get: retrieves value for \"spam\"");
    CHECK(dict_get(dict, "missing", 7) == NULL, "dict_get: missing key returns NULL");

    destroy_value(dict);
}

/* A key that's a prefix of another key must not false-positive-match —
   dict_get() has to check length before/along with content. */
static void test_dict_get_distinguishes_prefix_keys(void) {
    BValue* dict = create_dict();

    dict_insert(dict, "cow", 3, create_int(1));
    dict_insert(dict, "cows", 4, create_int(2));

    BValue* cow = dict_get(dict, "cow", 3);
    BValue* cows = dict_get(dict, "cows", 4);

    CHECK(cow && cow->value.integer.value == 1, "dict_get: \"cow\" is not shadowed by \"cows\"");
    CHECK(cows && cows->value.integer.value == 2, "dict_get: \"cows\" is distinct from \"cow\"");

    destroy_value(dict);
}

/* create_dict() starts at capacity 4 — same growth path as lists. */
static void test_dict_insert_grows_capacity(void) {
    BValue* dict = create_dict();
    char key[8];

    for (int i = 0; i < 10; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        dict_insert(dict, key, (int)strlen(key), create_int(i));
    }

    int ok = 1;
    for (int i = 0; i < 10; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        BValue* v = dict_get(dict, key, (int)strlen(key));
        if (!v || v->value.integer.value != i) { ok = 0; break; }
    }
    CHECK(ok, "dict_insert: all 10 entries intact and correctly keyed after growth");

    destroy_value(dict);
}

/* destroy_value() must recurse through nested lists/dicts without
   crashing or leaking the nested BValue*s it doesn't own directly. */
static void test_destroy_value_nested_structure(void) {
    BValue* root = create_dict();
    BValue* list = create_list();

    list_append(list, create_string("eggs", 4));
    list_append(list, create_string("eggs", 4));
    dict_insert(root, "spam", 4, list);
    dict_insert(root, "count", 5, create_int(2));

    /* No crash == pass; run under a sanitizer/valgrind in CI for real
       leak/overflow detection on top of this. */
    destroy_value(root);
    CHECK(1, "destroy_value: recursively tears down a nested dict/list tree without crashing");
}

static void test_destroy_value_null_is_safe(void) {
    destroy_value(NULL);
    CHECK(1, "destroy_value: NULL is a safe no-op");
}

int main(void) {
    test_create_int();
    test_create_negative_int();
    test_create_string();
    test_create_string_is_a_copy();
    test_list_append_and_get();
    test_list_append_grows_capacity();
    test_list_get_bounds();
    test_dict_insert_and_get();
    test_dict_get_distinguishes_prefix_keys();
    test_dict_insert_grows_capacity();
    test_destroy_value_nested_structure();
    test_destroy_value_null_is_safe();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}