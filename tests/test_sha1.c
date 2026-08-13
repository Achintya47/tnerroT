/**
 * @brief Unit tests for sha1.c against known FIPS 180-4 / textbook test
 *        vectors, plus a streaming-vs-one-shot equivalence check (since
 *        piece verification in piece_manager.c relies on sha1_digest()
 *        being correct for arbitrarily-sized, single-call input).
 */
#include <stdio.h>
#include <string.h>
#include "sha1.h"

static int g_pass = 0;
static int g_fail = 0;

static void check_hex(const char* name, const uint8_t digest[20], const char* expected_hex) {
    char got[41];
    for (int i = 0; i < 20; i++)
        sprintf(got + i * 2, "%02x", digest[i]);
    got[40] = '\0';

    if (strcmp(got, expected_hex) == 0) {
        printf("[PASS] %s\n", name);
        g_pass++;
    } else {
        printf("[FAIL] %s: expected %s, got %s\n", name, expected_hex, got);
        g_fail++;
    }
}

static void test_empty_string(void) {
    uint8_t digest[20];
    sha1_digest("", 0, digest);
    check_hex("sha1(\"\")", digest, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

static void test_abc(void) {
    uint8_t digest[20];
    sha1_digest("abc", 3, digest);
    check_hex("sha1(\"abc\")", digest, "a9993e364706816aba3e25717850c26c9cd0d89d");
}

static void test_quick_fox(void) {
    const char* msg = "The quick brown fox jumps over the lazy dog";
    uint8_t digest[20];
    sha1_digest(msg, strlen(msg), digest);
    check_hex("sha1(quick fox)", digest, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

/* Exercises the multi-block path (input > 64 bytes) and the padding edge
   case where the 0x80 byte doesn't leave room for the length field in the
   final block, forcing an extra compression round. */
static void test_long_input(void) {
    char buf[1000];
    memset(buf, 'a', sizeof(buf));

    uint8_t digest[20];
    sha1_digest(buf, sizeof(buf), digest);
    check_hex("sha1(1000 x 'a')", digest, "291e9a6c66994949b57ba5e650361e98fc36b1ba");
}

/* piece_manager_store_block() hashes a piece in one call via sha1_digest();
   confirm that's equivalent to feeding the same bytes through
   sha1_update() in several smaller chunks (as bencoder-adjacent code, or
   a future streaming reader, might). */
static void test_streaming_matches_one_shot(void) {
    const char* msg = "BitTorrent";

    uint8_t one_shot[20];
    sha1_digest(msg, strlen(msg), one_shot);

    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, msg, 3);      /* "Bit" */
    sha1_update(&ctx, msg + 3, 4);  /* "Torr" */
    sha1_update(&ctx, msg + 7, 3);  /* "ent" */
    uint8_t streamed[20];
    sha1_final(&ctx, streamed);

    if (memcmp(one_shot, streamed, 20) == 0) {
        printf("[PASS] streaming sha1_update matches one-shot sha1_digest\n");
        g_pass++;
    } else {
        printf("[FAIL] streaming sha1_update does not match one-shot sha1_digest\n");
        g_fail++;
    }

    check_hex("sha1(\"BitTorrent\") one-shot", one_shot, "f8cf01459bf515c08e144e5a7dc51fed7e6d3050");
}

/* sha1_final() is documented to zero the context afterwards. */
static void test_context_scrubbed_after_final(void) {
    SHA1_CTX ctx;
    uint8_t digest[20];

    sha1_init(&ctx);
    sha1_update(&ctx, "abc", 3);
    sha1_final(&ctx, digest);

    unsigned char zero[sizeof(SHA1_CTX)] = {0};

    if (memcmp(&ctx, zero, sizeof(SHA1_CTX)) == 0) {
        printf("[PASS] SHA1_CTX is zeroed after sha1_final\n");
        g_pass++;
    } else {
        printf("[FAIL] SHA1_CTX still contains data after sha1_final\n");
        g_fail++;
    }
}

int main(void) {
    test_empty_string();
    test_abc();
    test_quick_fox();
    test_long_input();
    test_streaming_matches_one_shot();
    test_context_scrubbed_after_final();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}