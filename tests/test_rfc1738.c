/**
 * @brief Unit tests for RFC1738_url_parser.c: info-hash percent-encoding,
 *        and splitting http:// / udp:// announce URLs into host/port/path.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "RFC1738_url_parser.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); g_pass++; } \
    else      { printf("[FAIL] %s\n", name); g_fail++; } \
} while (0)

static void test_encode_info_hash_all_zero(void) {
    unsigned char hash[20] = {0};
    char out[61];

    encode_info_hash(hash, out);

    CHECK(strlen(out) == 60, "encode_info_hash: output length is 60");
    CHECK(strncmp(out, "%00%00%00", 9) == 0, "encode_info_hash: all-zero hash starts with %00%00%00");
    CHECK(strcmp(out + 57, "%00") == 0, "encode_info_hash: all-zero hash ends with %00");
}

static void test_encode_info_hash_sequential(void) {
    unsigned char hash[20];
    for (int i = 0; i < 20; i++)
        hash[i] = (unsigned char)i;

    char out[61];
    encode_info_hash(hash, out);

    /* byte 0x00 -> %00, byte 0x01 -> %01, ..., byte 0x13 (19) -> %13 */
    CHECK(strncmp(out, "%00%01%02%03", 12) == 0, "encode_info_hash: sequential bytes encode in order");
    CHECK(strcmp(out + 57, "%13") == 0, "encode_info_hash: last byte (19 = 0x13) encodes correctly");
    CHECK(out[60] == '\0', "encode_info_hash: output is null-terminated");
}

static void test_parse_announce_url_basic(void) {
    char* host = NULL;
    char* path = NULL;
    unsigned short port = 0;

    int ok = parse_announce_url("http://tracker.example.com/announce", &host, &port, &path);

    CHECK(ok == 1, "parse_announce_url: basic URL returns success");
    CHECK(host && strcmp(host, "tracker.example.com") == 0, "parse_announce_url: host parsed correctly");
    CHECK(port == 80, "parse_announce_url: defaults to port 80 when none given");
    CHECK(path && strcmp(path, "/announce") == 0, "parse_announce_url: path parsed correctly");

    free(host);
    free(path);
}

static void test_parse_announce_url_with_port(void) {
    char* host = NULL;
    char* path = NULL;
    unsigned short port = 0;

    int ok = parse_announce_url("http://tracker.example.com:6969/announce", &host, &port, &path);

    CHECK(ok == 1, "parse_announce_url: URL with explicit port returns success");
    CHECK(port == 6969, "parse_announce_url: explicit port parsed correctly");
    CHECK(host && strcmp(host, "tracker.example.com") == 0, "parse_announce_url: host parsed correctly (with port)");
    CHECK(path && strcmp(path, "/announce") == 0, "parse_announce_url: path parsed correctly (with port)");

    free(host);
    free(path);
}

static void test_parse_announce_url_no_path_defaults_to_slash(void) {
    char* host = NULL;
    char* path = NULL;
    unsigned short port = 0;

    int ok = parse_announce_url("http://tracker.example.com", &host, &port, &path);

    CHECK(ok == 1, "parse_announce_url: URL without path returns success");
    CHECK(path && strcmp(path, "/") == 0, "parse_announce_url: missing path defaults to \"/\"");

    free(host);
    free(path);
}

static void test_parse_announce_url_rejects_non_http(void) {
    char* host = NULL;
    char* path = NULL;
    unsigned short port = 0;

    int ok_udp = parse_announce_url("udp://tracker.example.com:80", &host, &port, &path);
    CHECK(ok_udp == 0, "parse_announce_url: rejects udp:// scheme");

    int ok_ftp = parse_announce_url("ftp://tracker.example.com/announce", &host, &port, &path);
    CHECK(ok_ftp == 0, "parse_announce_url: rejects unrelated ftp:// scheme");
}

static void test_parse_udp_announce_url_basic(void) {
    char* host = NULL;
    unsigned short port = 0;

    int ok = parse_udp_announce_url("udp://tracker.example.com:6969/announce", &host, &port);

    CHECK(ok == 1, "parse_udp_announce_url: basic URL returns success");
    CHECK(host && strcmp(host, "tracker.example.com") == 0, "parse_udp_announce_url: host parsed correctly");
    CHECK(port == 6969, "parse_udp_announce_url: port parsed correctly");

    free(host);
}

static void test_parse_udp_announce_url_default_port(void) {
    char* host = NULL;
    unsigned short port = 0;

    int ok = parse_udp_announce_url("udp://tracker.example.com", &host, &port);

    CHECK(ok == 1, "parse_udp_announce_url: URL without port returns success");
    CHECK(port == 80, "parse_udp_announce_url: defaults to port 80 when none given");

    free(host);
}

static void test_parse_udp_announce_url_rejects_http(void) {
    char* host = NULL;
    unsigned short port = 0;

    int ok = parse_udp_announce_url("http://tracker.example.com:80", &host, &port);
    CHECK(ok == 0, "parse_udp_announce_url: rejects http:// scheme");
}

static void test_parse_announce_url_rejects_empty_host(void) {
    char* host = NULL;
    char* path = NULL;
    unsigned short port = 0;

    /* "http://:80/announce" — host component is empty */
    int ok = parse_announce_url("http://:80/announce", &host, &port, &path);
    CHECK(ok == 0, "parse_announce_url: rejects a URL with an empty host");
}

int main(void) {
    test_encode_info_hash_all_zero();
    test_encode_info_hash_sequential();
    test_parse_announce_url_basic();
    test_parse_announce_url_with_port();
    test_parse_announce_url_no_path_defaults_to_slash();
    test_parse_announce_url_rejects_non_http();
    test_parse_udp_announce_url_basic();
    test_parse_udp_announce_url_default_port();
    test_parse_udp_announce_url_rejects_http();
    test_parse_announce_url_rejects_empty_host();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}