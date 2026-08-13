/**
 * @brief Unit tests for the pure, socket-free parts of peer.c: bitfield
 *        allocation, replacement, single-bit set/query. Deliberately does
 *        NOT touch peer_connect/peer_send_handshake/etc — those need a
 *        live TCP peer and aren't unit-testable without a network double,
 *        which is out of scope here (see downloader.c's design notes on
 *        why the peer-wire logic itself is otherwise hard to unit test).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "peer.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); g_pass++; } \
    else      { printf("[FAIL] %s\n", name); g_fail++; } \
} while (0)

static PeerConnection make_blank_connection(void) {
    PeerConnection pc;
    memset(&pc, 0, sizeof(pc));
    pc.sock = INVALID_SOCKET;
    return pc;
}

static void test_ensure_bitfield_allocates_correct_size(void) {
    PeerConnection pc = make_blank_connection();

    int ok = peer_ensure_bitfield(&pc, 10); /* 10 pieces -> ceil(10/8) = 2 bytes */

    CHECK(ok == 1, "peer_ensure_bitfield: succeeds");
    CHECK(pc.bitfield != NULL, "peer_ensure_bitfield: allocates a buffer");
    CHECK(pc.bitfield_len == 2, "peer_ensure_bitfield: rounds up to whole bytes (10 pieces -> 2 bytes)");
    CHECK(pc.bitfield[0] == 0 && pc.bitfield[1] == 0, "peer_ensure_bitfield: starts zeroed (no pieces known yet)");

    free(pc.bitfield);
}

static void test_ensure_bitfield_is_idempotent(void) {
    PeerConnection pc = make_blank_connection();

    peer_ensure_bitfield(&pc, 10);
    uint8_t* first_ptr = pc.bitfield;

    peer_ensure_bitfield(&pc, 10); /* second call should be a no-op */

    CHECK(pc.bitfield == first_ptr, "peer_ensure_bitfield: second call does not reallocate an existing bitfield");

    free(pc.bitfield);
}

static void test_mark_have_and_has_piece(void) {
    PeerConnection pc = make_blank_connection();
    peer_ensure_bitfield(&pc, 10);

    CHECK(peer_has_piece(&pc, 3) == 0, "peer_has_piece: false before any HAVE received");

    peer_mark_have(&pc, 3);
    CHECK(peer_has_piece(&pc, 3) == 1, "peer_mark_have: sets exactly the requested bit");
    CHECK(peer_has_piece(&pc, 2) == 0, "peer_has_piece: neighboring bit (2) unaffected");
    CHECK(peer_has_piece(&pc, 4) == 0, "peer_has_piece: neighboring bit (4) unaffected");

    peer_mark_have(&pc, 0);
    peer_mark_have(&pc, 9);
    CHECK(peer_has_piece(&pc, 0) == 1, "peer_mark_have: first piece index (0) settable");
    CHECK(peer_has_piece(&pc, 9) == 1, "peer_mark_have: last piece index (9) settable");

    free(pc.bitfield);
}

/* peer_mark_have()/peer_has_piece() must bounds-check against bitfield_len
   rather than trusting an out-of-range index from a malicious/buggy peer's
   HAVE message. */
static void test_out_of_range_index_is_ignored_safely(void) {
    PeerConnection pc = make_blank_connection();
    peer_ensure_bitfield(&pc, 10); /* covers indices 0..15 (2 bytes) */

    peer_mark_have(&pc, 1000); /* far past bitfield_len * 8 */
    CHECK(peer_has_piece(&pc, 1000) == 0, "peer_has_piece: out-of-range index returns 0 rather than reading OOB");

    /* Sanity: nothing else got corrupted by the out-of-range mark_have call */
    CHECK(pc.bitfield[0] == 0 && pc.bitfield[1] == 0, "peer_mark_have: out-of-range index does not corrupt the real buffer");

    free(pc.bitfield);
}

static void test_has_piece_with_no_bitfield(void) {
    PeerConnection pc = make_blank_connection();
    /* peer_ensure_bitfield() never called — bitfield is NULL */

    CHECK(peer_has_piece(&pc, 0) == 0, "peer_has_piece: returns 0 (not a crash) when no bitfield is set yet");
}

/* peer_set_bitfield() is used when an actual BITFIELD message arrives,
   replacing whatever (if anything) peer_ensure_bitfield() pre-allocated. */
static void test_set_bitfield_replaces_existing(void) {
    PeerConnection pc = make_blank_connection();
    peer_ensure_bitfield(&pc, 10);
    peer_mark_have(&pc, 0); /* pre-existing state that set_bitfield should replace */

    uint8_t incoming[2] = { 0xFF, 0x00 }; /* pieces 0-7 present, 8-9 absent */
    peer_set_bitfield(&pc, incoming, 2);

    CHECK(pc.bitfield_len == 2, "peer_set_bitfield: bitfield_len updated to the new length");
    CHECK(peer_has_piece(&pc, 0) == 1 && peer_has_piece(&pc, 7) == 1,
          "peer_set_bitfield: bits from the incoming BITFIELD are honored (0-7 set)");
    CHECK(peer_has_piece(&pc, 8) == 0, "peer_set_bitfield: bits not set in the incoming BITFIELD read as absent (piece 8)");

    free(pc.bitfield);
}

int main(void) {
    test_ensure_bitfield_allocates_correct_size();
    test_ensure_bitfield_is_idempotent();
    test_mark_have_and_has_piece();
    test_out_of_range_index_is_ignored_safely();
    test_has_piece_with_no_bitfield();
    test_set_bitfield_replaces_existing();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}