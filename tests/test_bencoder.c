/**
 * @file    test_bencoder.c
 * @brief   Tests for the bencode encoder / decoder (bencoder.c + btypes.c).
 *
 * Strategy: decoding now reads directly from an in-memory buffer via a
 * Parser struct, so we build a Parser over a literal bencoded string and run
 * decode_value() on it. Encoding still writes to FILE*, so for encoding we
 * build a BValue tree, encode it to a tmpfile, then read the raw bytes back
 * and compare them to the expected string.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include "btypes.h"
#include "bencoder.h"
#include "test_utils.h"
 

/* Build a Parser over a literal bencoded string. The Parser only stores a
   pointer into s, so s must stay alive for as long as the Parser is used
   (string literals and the slurp()'d buffer below are both fine). */
static Parser make_input(const char* s) {
    Parser p;
    p.data = s;
    p.size = strlen(s);
    p.pos = 0;
    return p;
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
 
    Parser p;
    BValue* v;
 
    p = make_input("i42e");
    v = decode_value(&p);
    CHECK("i42e -> 42",        v && v->type == BINT && v->value.integer.value == 42);
    destroy_value(v);
 
    p = make_input("i0e");
    v = decode_value(&p);
    CHECK("i0e -> 0",          v && v->type == BINT && v->value.integer.value == 0);
    destroy_value(v);
 
    p = make_input("i-7e");
    v = decode_value(&p);
    CHECK("i-7e -> -7",        v && v->type == BINT && v->value.integer.value == -7);
    destroy_value(v);
 
    p = make_input("i1000000e");
    v = decode_value(&p);
    CHECK("large positive",    v && v->type == BINT && v->value.integer.value == 1000000);
    destroy_value(v);
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
 
    Parser p;
    BValue* v;
 
    p = make_input("4:spam");
    v = decode_value(&p);
    CHECK("4:spam length",     v && v->type == BSTRING && v->value.string.length == 4);
    CHECK("4:spam data",       v && memcmp(v->value.string.data, "spam", 4) == 0);
    destroy_value(v);
 
    /* Empty string */
    p = make_input("0:");
    v = decode_value(&p);
    CHECK("0: length == 0",    v && v->type == BSTRING && v->value.string.length == 0);
    destroy_value(v);
 
    /* String with embedded spaces */
    p = make_input("5:hello");
    v = decode_value(&p);
    CHECK("5:hello length",    v && v->type == BSTRING && v->value.string.length == 5);
    CHECK("5:hello data",      v && memcmp(v->value.string.data, "hello", 5) == 0);
    destroy_value(v);
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
 
    Parser p;
    BValue* v;
 
    /* l4:spam4:eggse */
    p = make_input("l4:spam4:eggse");
    v = decode_value(&p);
    CHECK("list type",       v && v->type == BLIST);
    CHECK("list count == 2", v && v->value.list.count == 2);
    BValue* item0 = list_get(v, 0);
    BValue* item1 = list_get(v, 1);
    CHECK("list[0] = spam",  item0 && memcmp(item0->value.string.data, "spam", 4) == 0);
    CHECK("list[1] = eggs",  item1 && memcmp(item1->value.string.data, "eggs", 4) == 0);
    destroy_value(v);
 
    /* Empty list */
    p = make_input("le");
    v = decode_value(&p);
    CHECK("empty list count == 0", v && v->type == BLIST && v->value.list.count == 0);
    destroy_value(v);
 
    /* Nested list: lli1eei2ee -> [[1], 2] */
    p = make_input("lli1eei2ee");
    v = decode_value(&p);
    CHECK("nested list type",        v && v->type == BLIST);
    CHECK("nested outer count == 2", v && v->value.list.count == 2);
    BValue* inner = list_get(v, 0);
    CHECK("inner is list",           inner && inner->type == BLIST);
    CHECK("inner[0] == 1",           list_get(inner, 0) && list_get(inner, 0)->value.integer.value == 1);
    CHECK("outer[1] == 2",           list_get(v, 1)     && list_get(v, 1)->value.integer.value == 2);
    destroy_value(v);
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
 
    Parser p;
    BValue* v;
 
    /* d3:cow3:moo4:spam4:eggse */
    p = make_input("d3:cow3:moo4:spam4:eggse");
    v = decode_value(&p);
    CHECK("dict type",       v && v->type == BDICT);
    CHECK("dict count == 2", v && v->value.dict.count == 2);
 
    BValue* cow  = dict_get(v, "cow",  3);
    BValue* spam = dict_get(v, "spam", 4);
    CHECK("cow  -> moo",  cow  && memcmp(cow->value.string.data,  "moo",  3) == 0);
    CHECK("spam -> eggs", spam && memcmp(spam->value.string.data, "eggs", 4) == 0);
    CHECK("missing key returns NULL", dict_get(v, "nope", 4) == NULL);
    destroy_value(v);
 
    /* Dict with integer value */
    p = make_input("d6:lengthi1234ee");
    v = decode_value(&p);
    BValue* length = dict_get(v, "length", 6);
    CHECK("dict int value", length && length->value.integer.value == 1234);
    destroy_value(v);
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
 
    int len;
    char* buf = slurp(f, &len);
    fclose(f);
 
    /* decode_value() now reads from an in-memory buffer rather than a
       FILE*, so build a Parser directly over the encoded bytes. We size
       it from len (not strlen) since bencoded data may legitimately
       contain embedded NUL bytes. */
    Parser p;
    p.data = buf;
    p.size = (size_t)len;
    p.pos  = 0;
 
    BValue* decoded = decode_value(&p);
    free(buf);
 
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