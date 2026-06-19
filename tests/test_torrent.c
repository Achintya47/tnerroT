/**
 * @file    test_torrent.c
 * @brief   Tests for torrent_parse() (torrent.c).
 *
 * We build BValue trees that look like real .torrent dicts and feed them
 * straight to torrent_parse(), so we don't need to touch the filesystem for
 * most tests. The one exception is the "real file" test at the end, which
 * reads big-buck-bunny.torrent into memory, decodes it via the Parser-based
 * decode_value(), and checks a few known-good values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btypes.h"
#include "bencoder.h"
#include "torrent.h"
#include "test_utils.h"


/* Build 'num_pieces' fake SHA-1 blocks (20 bytes each), all filled with
   the byte 'fill'. Returns a heap-allocated buffer; caller must free(). */
static char* make_pieces(int num_pieces, unsigned char fill) {
    int len = num_pieces * 20;
    char* buf = malloc(len);
    memset(buf, fill, len);
    return buf;
}

/* Construct a minimal valid info-dict BValue. */
static BValue* make_info(const char* name,
                          long long   file_length,
                          long long   piece_length,
                          int         num_pieces) {
    char* pieces_data = make_pieces(num_pieces, 0xAB);

    BValue* info = create_dict();
    dict_insert(info, "name",         4, create_string(name, (int)strlen(name)));
    dict_insert(info, "length",       6, create_int(file_length));
    dict_insert(info, "piece length", 12, create_int(piece_length));
    dict_insert(info, "pieces",       6, create_string(pieces_data, num_pieces * 20));

    free(pieces_data);
    return info;
}

/* Construct a full .torrent root dict. */
static BValue* make_root(const char* announce,
                          const char* name,
                          long long   file_length,
                          long long   piece_length,
                          int         num_pieces) {
    BValue* root = create_dict();
    dict_insert(root, "announce", 8, create_string(announce, (int)strlen(announce)));
    dict_insert(root, "info",     4, make_info(name, file_length, piece_length, num_pieces));
    return root;
}

/* Read a whole file into a heap buffer. Caller must free(). Returns NULL
   on any failure (mirrors the read_file() used by the real torrent CLI). */
static char* read_file(const char* path, size_t* size_out) {
    FILE* fp = fopen(path, "rb");

    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char* buffer = malloc(size);

    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (fread(buffer, 1, size, fp) != (size_t)size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    *size_out = (size_t)size;

    return buffer;
}



static void test_basic_parse(void) {
    SECTION("basic parse");

    BValue* root = make_root(
        "udp://tracker.example.com:6969",
        "my-file.iso",
        1024 * 1024,   /* 1 MB */
        512 * 1024,    /* 512 KB pieces */
        2              /* 2 pieces */
    );

    Torrent* t = torrent_parse(root);
    CHECK("returns non-NULL",           t != NULL);
    CHECK("announce URL",               t && strcmp(t->announce, "udp://tracker.example.com:6969") == 0);
    CHECK("name",                       t && strcmp(t->name, "my-file.iso") == 0);
    CHECK("length",                     t && t->length == 1024 * 1024);
    CHECK("piece_length",               t && t->piece_length == 512 * 1024);
    CHECK("num_pieces",                 t && t->num_pieces == 2);
    CHECK("pieces non-NULL",            t && t->pieces != NULL);

    torrent_destroy(t);
    destroy_value(root);
}

static void test_piece_hashes(void) {
    SECTION("piece hash bytes");

    /* Fill every hash byte with 0xCD so we can verify they were copied. */
    int num_pieces = 3;
    char* pieces_data = make_pieces(num_pieces, 0xCD);

    BValue* info = create_dict();
    dict_insert(info, "name",         4, create_string("test", 4));
    dict_insert(info, "length",       6, create_int(1000));
    dict_insert(info, "piece length", 12, create_int(512));
    dict_insert(info, "pieces",       6, create_string(pieces_data, num_pieces * 20));
    free(pieces_data);

    BValue* root = create_dict();
    dict_insert(root, "announce", 8, create_string("http://t.example.com", 20));
    dict_insert(root, "info",     4, info);

    Torrent* t = torrent_parse(root);
    CHECK("num_pieces == 3", t && t->num_pieces == 3);

    /* Every byte of every hash should be 0xCD */
    int all_ok = 1;
    if (t) {
        for (int i = 0; i < 3 && all_ok; i++)
            for (int j = 0; j < 20 && all_ok; j++)
                if (t->pieces[i].hash[j] != 0xCD)
                    all_ok = 0;
    }
    CHECK("hash bytes correctly copied", all_ok);

    torrent_destroy(t);
    destroy_value(root);
}

/* Multi File Test */

static void test_multifile_length(void) {
    SECTION("multi-file torrent (files list)");

    /* Build an info dict with a 'files' list instead of 'length' */
    BValue* file1 = create_dict();
    dict_insert(file1, "length", 6, create_int(100));
    dict_insert(file1, "path",   4, create_string("a.txt", 5));

    BValue* file2 = create_dict();
    dict_insert(file2, "length", 6, create_int(200));
    dict_insert(file2, "path",   4, create_string("b.txt", 5));

    BValue* files = create_list();
    list_append(files, file1);
    list_append(files, file2);

    char* pieces_data = make_pieces(1, 0x00);
    BValue* info = create_dict();
    dict_insert(info, "name",         4,  create_string("multi", 5));
    dict_insert(info, "files",        5,  files);
    dict_insert(info, "piece length", 12, create_int(512));
    dict_insert(info, "pieces",       6,  create_string(pieces_data, 20));
    free(pieces_data);

    BValue* root = create_dict();
    dict_insert(root, "announce", 8, create_string("http://t.example.com", 20));
    dict_insert(root, "info",     4, info);

    Torrent* t = torrent_parse(root);
    CHECK("multi-file parse succeeds",    t != NULL);
    CHECK("multi-file total length",      t && t->length == 300);

    torrent_destroy(t);
    destroy_value(root);
}

/* Testing against Bad Input */

static void test_bad_input(void) {
    SECTION("bad input returns NULL");

    /* NULL */
    CHECK("NULL root",   torrent_parse(NULL) == NULL);

    /* Wrong type (a list instead of a dict) */
    BValue* lst = create_list();
    CHECK("list root",   torrent_parse(lst) == NULL);
    destroy_value(lst);

    /* Missing 'announce' */
    BValue* no_announce = create_dict();
    dict_insert(no_announce, "info", 4, create_dict());
    Torrent* t = torrent_parse(no_announce);
    CHECK("missing announce returns NULL", t == NULL);
    torrent_destroy(t);
    destroy_value(no_announce);

    /* pieces length not a multiple of 20 */
    char* bad_pieces = malloc(21);
    memset(bad_pieces, 0, 21);
    BValue* info = create_dict();
    dict_insert(info, "name",         4,  create_string("x", 1));
    dict_insert(info, "length",       6,  create_int(1000));
    dict_insert(info, "piece length", 12, create_int(512));
    dict_insert(info, "pieces",       6,  create_string(bad_pieces, 21)); /* 21 is bad */
    free(bad_pieces);

    BValue* root = create_dict();
    dict_insert(root, "announce", 8, create_string("http://t.example.com", 20));
    dict_insert(root, "info",     4, info);

    t = torrent_parse(root);
    CHECK("bad pieces length returns NULL", t == NULL);
    torrent_destroy(t);
    destroy_value(root);
}

/* Big Buck Bunny */

static void test_real_torrent(const char* path) {
    SECTION("real file: big-buck-bunny.torrent");

    size_t file_size;
    char* buffer = read_file(path, &file_size);

    if (!buffer) {
        printf("  [SKIP] could not open %s\n", path);
        return;
    }

    Parser parser = {
        .data = buffer,
        .size = file_size,
        .pos  = 0
    };

    BValue* root = decode_value(&parser);

    CHECK("file decodes to dict", root && root->type == BDICT);

    Torrent* t = torrent_parse(root);
    CHECK("torrent_parse succeeds",     t != NULL);
    CHECK("name = Big Buck Bunny",      t && strcmp(t->name, "Big Buck Bunny") == 0);
    CHECK("length = 276445467 bytes",   t && t->length == 276445467ULL);
    CHECK("piece_length = 262144",      t && t->piece_length == 262144ULL);
    CHECK("num_pieces = 1055",          t && t->num_pieces == 1055);
    CHECK("pieces non-NULL",            t && t->pieces != NULL);

    /* Spot-check the first hash (known from running the binary earlier):
       2020a7789d6f8b18623b2dca1de527df3f27230c */
    if (t && t->pieces) {
        unsigned char expected[20] = {
            0x20,0x20,0xa7,0x78,0x9d,0x6f,0x8b,0x18,
            0x62,0x3b,0x2d,0xca,0x1d,0xe5,0x27,0xdf,
            0x3f,0x27,0x23,0x0c
        };
        CHECK("first piece hash correct",
              memcmp(t->pieces[0].hash, expected, 20) == 0);
    }

    torrent_destroy(t);
    destroy_value(root);
    free(buffer);
}

/* Main */

int main(int argc, char* argv[]) {
    printf("=== Torrent Parser Tests ===\n");

    test_basic_parse();
    test_piece_hashes();
    test_multifile_length();
    test_bad_input();

    /* Accept torrent path as optional arg, or try the default location */
    const char* torrent_path = (argc > 1) ? argv[1] : "big-buck-bunny.torrent";
    test_real_torrent(torrent_path);

    SUMMARY();
    return EXIT_STATUS();
}