/**
 * @file    test_bencoder.c
 * @brief   Tests for the bencode encoder / decoder (bencoder.c + btypes.c).
 *
 * Strategy: for decoding we write a bencoded string into a tmpfile, run
 * decode_value() on it, and inspect the result. For encoding we go the other
 * way: build a BValue tree, encode it to a tmpfile, then read the raw bytes
 * back and compare them to the expected string.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include "btypes.h"
#include "bencoder.h"
#include "test_utils.h"
 

/* Write a literal string into a fresh tmpfile and rewind it. */
static FILE* make_input(const char* s) {
    FILE* f = tmpfile();
    fputs(s, f);
    rewind(f);
    return f;
}
 
/* Read everything from f (from current position) into a heap buffer.
   Caller must free(). Returns the number of bytes read via *out_len. */
static char* slurp(FILE* f, int* out_len) {
    rewind(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char* buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    *out_len = (int)len;
    return buf;
}
 
/* Integer Testing */
 
static void test_decode_int(void) {
    SECTION("decode integer");
 
    FILE* f;
    BValue* v;
 
    f = make_input("i42e");
    v = decode_value(f);
    CHECK("i42e -> 42",        v && v->type == BINT && v->value.integer.value == 42);
    destroy_value(v); fclose(f);
 
    f = make_input("i0e");
    v = decode_value(f);
    CHECK("i0e -> 0",          v && v->type == BINT && v->value.integer.value == 0);
    destroy_value(v); fclose(f);
 
    f = make_input("i-7e");
    v = decode_value(f);
    CHECK("i-7e -> -7",        v && v->type == BINT && v->value.integer.value == -7);
    destroy_value(v); fclose(f);
 
    f = make_input("i1000000e");
    v = decode_value(f);
    CHECK("large positive",    v && v->type == BINT && v->value.integer.value == 1000000);
    destroy_value(v); fclose(f);
}
 
static void test_encode_int(void) {
    SECTION("encode integer");
 
    FILE* f; int len; char* buf;
 
    BValue* v = create_int(42);
    f = tmpfile();
    encode_value(f, v);
    buf = slurp(f, &len);
    CHECK("42  -> i42e",  strcmp(buf, "i42e") == 0);
    destroy_value(v); fclose(f); free(buf);
 
    v = create_int(-3);
    f = tmpfile();
    encode_value(f, v);
    buf = slurp(f, &len);
    CHECK("-3  -> i-3e",  strcmp(buf, "i-3e") == 0);
    destroy_value(v); fclose(f); free(buf);
 
    v = create_int(0);
    f = tmpfile();
    encode_value(f, v);
    buf = slurp(f, &len);
    CHECK("0   -> i0e",   strcmp(buf, "i0e")  == 0);
    destroy_value(v); fclose(f); free(buf);
}
 
/* String Testing */
 
static void test_decode_string(void) {
    SECTION("decode string");
 
    FILE* f;
    BValue* v;
 
    f = make_input("4:spam");
    v = decode_value(f);
    CHECK("4:spam length",     v && v->type == BSTRING && v->value.string.length == 4);
    CHECK("4:spam data",       v && memcmp(v->value.string.data, "spam", 4) == 0);
    destroy_value(v); fclose(f);
 
    /* Empty string */
    f = make_input("0:");
    v = decode_value(f);
    CHECK("0: length == 0",    v && v->type == BSTRING && v->value.string.length == 0);
    destroy_value(v); fclose(f);
 
    /* String with embedded spaces */
    f = make_input("5:hello");
    v = decode_value(f);
    CHECK("5:hello length",    v && v->type == BSTRING && v->value.string.length == 5);
    CHECK("5:hello data",      v && memcmp(v->value.string.data, "hello", 5) == 0);
    destroy_value(v); fclose(f);
}
 
static void test_encode_string(void) {
    SECTION("encode string");
 
    FILE* f; int len; char* buf;
 
    BValue* v = create_string("spam", 4);
    f = tmpfile();
    encode_value(f, v);
    buf = slurp(f, &len);
    CHECK("spam -> 4:spam",  strcmp(buf, "4:spam") == 0);
    destroy_value(v); fclose(f); free(buf);
 
    v = create_string("", 0);
    f = tmpfile();
    encode_value(f, v);
    buf = slurp(f, &len);
    CHECK("empty -> 0:",     strcmp(buf, "0:") == 0);
    destroy_value(v); fclose(f); free(buf);
}
 
/* List Testing */
 
static void test_decode_list(void) {
    SECTION("decode list");
 
    FILE* f;
    BValue* v;
 
    /* l4:spam4:eggse */
    f = make_input("l4:spam4:eggse");
    v = decode_value(f);
    CHECK("list type",       v && v->type == BLIST);
    CHECK("list count == 2", v && v->value.list.count == 2);
    BValue* item0 = list_get(v, 0);
    BValue* item1 = list_get(v, 1);
    CHECK("list[0] = spam",  item0 && memcmp(item0->value.string.data, "spam", 4) == 0);
    CHECK("list[1] = eggs",  item1 && memcmp(item1->value.string.data, "eggs", 4) == 0);
    destroy_value(v); fclose(f);
 
    /* Empty list */
    f = make_input("le");
    v = decode_value(f);
    CHECK("empty list count == 0", v && v->type == BLIST && v->value.list.count == 0);
    destroy_value(v); fclose(f);
 
    /* Nested list: lli1eei2ee -> [[1], 2] */
    f = make_input("lli1eei2ee");
    v = decode_value(f);
    CHECK("nested list type",        v && v->type == BLIST);
    CHECK("nested outer count == 2", v && v->value.list.count == 2);
    BValue* inner = list_get(v, 0);
    CHECK("inner is list",           inner && inner->type == BLIST);
    CHECK("inner[0] == 1",           list_get(inner, 0) && list_get(inner, 0)->value.integer.value == 1);
    CHECK("outer[1] == 2",           list_get(v, 1)     && list_get(v, 1)->value.integer.value == 2);
    destroy_value(v); fclose(f);
}
 
static void test_encode_list(void) {
    SECTION("encode list");
 
    FILE* f; int len; char* buf;
 
    BValue* list = create_list();
    list_append(list, create_string("spam", 4));
    list_append(list, create_string("eggs", 4));
    f = tmpfile();
    encode_value(f, list);
    buf = slurp(f, &len);
    CHECK("list encode", strcmp(buf, "l4:spam4:eggse") == 0);
    destroy_value(list); fclose(f); free(buf);
}
 
/* Dictionary Testing */
 
static void test_decode_dict(void) {
    SECTION("decode dict");
 
    FILE* f;
    BValue* v;
 
    /* d3:cow3:moo4:spam4:eggse */
    f = make_input("d3:cow3:moo4:spam4:eggse");
    v = decode_value(f);
    CHECK("dict type",       v && v->type == BDICT);
    CHECK("dict count == 2", v && v->value.dict.count == 2);
 
    BValue* cow  = dict_get(v, "cow",  3);
    BValue* spam = dict_get(v, "spam", 4);
    CHECK("cow  -> moo",  cow  && memcmp(cow->value.string.data,  "moo",  3) == 0);
    CHECK("spam -> eggs", spam && memcmp(spam->value.string.data, "eggs", 4) == 0);
    CHECK("missing key returns NULL", dict_get(v, "nope", 4) == NULL);
    destroy_value(v); fclose(f);
 
    /* Dict with integer value */
    f = make_input("d6:lengthi1234ee");
    v = decode_value(f);
    BValue* length = dict_get(v, "length", 6);
    CHECK("dict int value", length && length->value.integer.value == 1234);
    destroy_value(v); fclose(f);
}
 
static void test_encode_dict(void) {
    SECTION("encode dict");
 
    FILE* f; int len; char* buf;
 
    BValue* dict = create_dict();
    dict_insert(dict, "key", 3, create_string("val", 3));
    f = tmpfile();
    encode_value(f, dict);
    buf = slurp(f, &len);
    CHECK("dict encode", strcmp(buf, "d3:key3:vale") == 0);
    destroy_value(dict); fclose(f); free(buf);
}
 
/* Decode + Encode */
 
static void test_roundtrip(void) {
    SECTION("encode -> decode round-trip");
 
    /* Build a small tree, encode it, decode it back, verify. */
    BValue* original = create_dict();
    dict_insert(original, "name",   4, create_string("torrent", 7));
    dict_insert(original, "pieces", 6, create_int(1055));
 
    BValue* tiers = create_list();
    list_append(tiers, create_string("http://tracker.example.com", 26));
    dict_insert(original, "trackers", 8, tiers);
 
    FILE* f = tmpfile();
    encode_value(f, original);
    rewind(f);
    BValue* decoded = decode_value(f);
    fclose(f);
 
    CHECK("round-trip type is dict",    decoded && decoded->type == BDICT);
 
    BValue* name = dict_get(decoded, "name", 4);
    CHECK("round-trip name value",
          name && memcmp(name->value.string.data, "torrent", 7) == 0);
 
    BValue* pieces = dict_get(decoded, "pieces", 6);
    CHECK("round-trip pieces value",    pieces && pieces->value.integer.value == 1055);
 
    BValue* trackers = dict_get(decoded, "trackers", 8);
    CHECK("round-trip trackers is list",  trackers && trackers->type == BLIST);
    CHECK("round-trip trackers count",    trackers && trackers->value.list.count == 1);
 
    destroy_value(original);
    destroy_value(decoded);
}
 
/* Main Function */
 
int main(void) {
    printf("=== Bencoder Tests ===\n");
 
    test_decode_int();
    test_encode_int();
    test_decode_string();
    test_encode_string();
    test_decode_list();
    test_encode_list();
    test_decode_dict();
    test_encode_dict();
    test_roundtrip();
 
    SUMMARY();
    return EXIT_STATUS();
}