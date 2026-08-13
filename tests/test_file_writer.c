/**
 * @brief Unit tests for file_writer.c: splitting one contiguous in-memory
 *        buffer back out to disk, for both single-file and multi-file
 *        torrent layouts (including nested subdirectories). Runs against
 *        a real (temporary) directory on disk — there's no in-memory
 *        filesystem to swap in here, so these are small integration
 *        tests rather than pure unit tests, but they're cheap and
 *        self-cleaning.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "file_writer.h"
#include "torrent.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); g_pass++; } \
    else      { printf("[FAIL] %s\n", name); g_fail++; } \
} while (0)

/* Reads a whole file back into a malloc'd buffer; caller frees.
   Returns NULL (and sets *len_out = 0) if the file can't be opened. */
static char* slurp(const char* path, long* len_out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) { *len_out = 0; return NULL; }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    char* buf = malloc((size_t)len + 1);
    size_t got = fread(buf, 1, (size_t)len, fp);
    fclose(fp);

    if (got != (size_t)len) { free(buf); *len_out = 0; return NULL; }

    buf[len] = '\0';
    *len_out = len;
    return buf;
}

static void test_single_file_torrent(void) {
    Torrent t = {0};
    t.name = "single.txt";
    t.num_files = 0;

    const char* data = "Hello, World!";  /* 13 bytes */
    const char* out_dir = "test_output_single";

    int ok = file_writer_save(&t, (const uint8_t*)data, strlen(data), out_dir);
    CHECK(ok == 1, "file_writer_save: single-file torrent reports success");

    long len = 0;
    char* content = slurp("test_output_single/single.txt", &len);

    CHECK(content != NULL, "file_writer_save: single-file output exists on disk");
    CHECK(content && len == (long)strlen(data), "file_writer_save: single-file output has correct length");
    CHECK(content && memcmp(content, data, strlen(data)) == 0, "file_writer_save: single-file output has correct bytes");

    free(content);
    remove("test_output_single/single.txt");
}

static void test_multi_file_torrent(void) {
    Torrent t = {0};
    t.name = "multiroot";
    t.num_files = 2;

    TorrentFile files[2] = {0};

    /* file 0: multiroot/a.txt, "AAAAA" (5 bytes) */
    files[0].length = 5;
    files[0].path_count = 1;
    files[0].path = malloc(sizeof(char*) * 1);
    files[0].path[0] = "a.txt";

    /* file 1: multiroot/sub/b.txt, "BBBBBBB" (7 bytes) — nested directory */
    files[1].length = 7;
    files[1].path_count = 2;
    files[1].path = malloc(sizeof(char*) * 2);
    files[1].path[0] = "sub";
    files[1].path[1] = "b.txt";

    t.files = files;

    const char* buffer = "AAAAABBBBBBB"; /* 5 + 7 = 12 bytes, back-to-back */
    const char* out_dir = "test_output_multi";

    int ok = file_writer_save(&t, (const uint8_t*)buffer, strlen(buffer), out_dir);
    CHECK(ok == 1, "file_writer_save: multi-file torrent reports success");

    long len_a = 0, len_b = 0;
    char* content_a = slurp("test_output_multi/multiroot/a.txt", &len_a);
    char* content_b = slurp("test_output_multi/multiroot/sub/b.txt", &len_b);

    CHECK(content_a != NULL, "file_writer_save: multi-file output a.txt exists");
    CHECK(content_a && len_a == 5 && memcmp(content_a, "AAAAA", 5) == 0,
          "file_writer_save: a.txt has correct bytes at offset 0");

    CHECK(content_b != NULL, "file_writer_save: nested multi-file output sub/b.txt exists");
    CHECK(content_b && len_b == 7 && memcmp(content_b, "BBBBBBB", 7) == 0,
          "file_writer_save: sub/b.txt has correct bytes at offset 5 (nested dir created)");

    free(content_a);
    free(content_b);
    free(files[0].path);
    free(files[1].path);

    remove("test_output_multi/multiroot/a.txt");
    remove("test_output_multi/multiroot/sub/b.txt");
}

static void test_rejects_null_arguments(void) {
    Torrent t = {0};
    uint8_t buf[4] = {0};

    CHECK(file_writer_save(NULL, buf, 4, "out") == 0, "file_writer_save: NULL torrent rejected");
    CHECK(file_writer_save(&t, NULL, 4, "out") == 0, "file_writer_save: NULL buffer rejected");
    CHECK(file_writer_save(&t, buf, 4, NULL) == 0, "file_writer_save: NULL output_dir rejected");
}

int main(void) {
    test_single_file_torrent();
    test_multi_file_torrent();
    test_rejects_null_arguments();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}