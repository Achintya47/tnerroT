/**
 * @brief Unit tests for piece_manager.c: creation/teardown, rarest-first
 *        piece selection, block-by-block storage, SHA1 verification (both
 *        the success and the corrupt-piece/re-download paths), and the
 *        completion counters.
 *
 * Compiled with -DBLOCK_SIZE=4 (see the Makefile/CI step for this test)
 * so a tiny, hand-computed torrent can still exercise the multi-block
 * path without needing megabytes of test data.
 *
 * Builds a Torrent by hand rather than going through torrent_parse() —
 * piece_manager.c only depends on a handful of Torrent fields (piece
 * length, total length, per-piece SHA1 hashes), so this keeps the test
 * self-contained and independent of the bencoder.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "piece_manager.h"
#include "torrent.h"
#include "sha1.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("[PASS] %s\n", name); g_pass++; } \
    else      { printf("[FAIL] %s\n", name); g_fail++; } \
} while (0)

/* Two pieces: piece 0 is 8 bytes (2 blocks of BLOCK_SIZE=4), piece 1 is
   4 bytes (1 block). Total length 12. */
#define PIECE0_DATA "AAAABBBB"
#define PIECE1_DATA "CCCC"

static Torrent* make_test_torrent(void) {
    Torrent* t = calloc(1, sizeof(Torrent));

    t->piece_length = 8;
    t->length = 12;
    t->num_pieces = 2;
    t->pieces = malloc(sizeof(PieceHash) * 2);

    sha1_digest(PIECE0_DATA, 8, t->pieces[0].hash);
    sha1_digest(PIECE1_DATA, 4, t->pieces[1].hash);

    return t;
}

static void free_test_torrent(Torrent* t) {
    free(t->pieces);
    free(t);
}

static void test_create_and_destroy(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    CHECK(pm != NULL, "piece_manager_create: succeeds for a valid torrent");
    CHECK(pm->num_pieces == 2, "piece_manager_create: num_pieces matches torrent");
    CHECK(pm->total_length == 12, "piece_manager_create: total_length matches torrent->length");
    CHECK(pm->pieces[0].blocks_total == 2, "piece_manager_create: piece 0 split into 2 blocks (8 / BLOCK_SIZE=4)");
    CHECK(pm->pieces[1].blocks_total == 1, "piece_manager_create: piece 1 (4 bytes) is a single block");
    CHECK(pm->pieces[0].state == PIECE_MISSING, "piece_manager_create: pieces start MISSING");
    CHECK(pm->pieces_complete == 0, "piece_manager_create: nothing complete yet");

    piece_manager_destroy(pm);
    free_test_torrent(t);

    /* destroying NULL must be safe */
    piece_manager_destroy(NULL);
    CHECK(1, "piece_manager_destroy: NULL is a safe no-op");
}

static void test_create_rejects_bad_input(void) {
    CHECK(piece_manager_create(NULL) == NULL, "piece_manager_create: NULL torrent returns NULL");

    Torrent t = {0};
    t.num_pieces = 0;
    CHECK(piece_manager_create(&t) == NULL, "piece_manager_create: zero-piece torrent returns NULL");
}

static void test_block_storage_multi_block_piece(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    uint32_t begin, length;

    /* First block of piece 0 not yet stored */
    int has_block = piece_manager_next_block(pm, 0, &begin, &length);
    CHECK(has_block == 1, "piece_manager_next_block: reports a block available before anything is stored");
    CHECK(begin == 0 && length == 4, "piece_manager_next_block: first block is [0, 4)");

    int rc = piece_manager_store_block(pm, 0, 0, (const uint8_t*)"AAAA", 4);
    CHECK(rc == 1, "piece_manager_store_block: first of two blocks stores as 'still incomplete'");
    CHECK(piece_manager_is_piece_complete(pm, 0) == 0, "piece_manager_is_piece_complete: false after 1/2 blocks");

    piece_manager_next_block(pm, 0, &begin, &length);
    CHECK(begin == 4 && length == 4, "piece_manager_next_block: second block is [4, 4)");

    rc = piece_manager_store_block(pm, 0, 4, (const uint8_t*)"BBBB", 4);
    CHECK(rc == 2, "piece_manager_store_block: final block completes AND verifies the piece");
    CHECK(piece_manager_is_piece_complete(pm, 0) == 1, "piece_manager_is_piece_complete: true after verification");
    CHECK(memcmp(pm->file_buffer, PIECE0_DATA, 8) == 0, "piece_manager_store_block: bytes land at the correct buffer offset");

    int no_more = piece_manager_next_block(pm, 0, &begin, &length);
    CHECK(no_more == 0, "piece_manager_next_block: no blocks left once piece is fully stored");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

static void test_corrupt_piece_is_rejected_and_resettable(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    /* Piece 1 is a single block; feed it the WRONG 4 bytes. */
    int rc = piece_manager_store_block(pm, 1, 0, (const uint8_t*)"ZZZZ", 4);
    CHECK(rc == -1, "piece_manager_store_block: corrupt final block fails SHA1 verification (-1)");
    CHECK(piece_manager_is_piece_complete(pm, 1) == 0, "piece_manager_is_piece_complete: false after failed verification");

    uint32_t begin, length;
    int has_block = piece_manager_next_block(pm, 1, &begin, &length);
    CHECK(has_block == 1 && begin == 0, "piece_manager_next_block: piece is re-requestable from scratch after a bad hash");

    /* Now feed the correct bytes — it should verify this time. */
    rc = piece_manager_store_block(pm, 1, 0, (const uint8_t*)PIECE1_DATA, 4);
    CHECK(rc == 2, "piece_manager_store_block: correct retry verifies successfully");
    CHECK(piece_manager_is_piece_complete(pm, 1) == 1, "piece_manager_is_piece_complete: true after successful retry");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

static void test_store_block_rejects_bad_arguments(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    int rc_bad_index = piece_manager_store_block(pm, 99, 0, (const uint8_t*)"AAAA", 4);
    CHECK(rc_bad_index == 0, "piece_manager_store_block: out-of-range piece_index rejected");

    int rc_bad_begin = piece_manager_store_block(pm, 0, 100, (const uint8_t*)"AAAA", 4);
    CHECK(rc_bad_begin == 0, "piece_manager_store_block: begin past piece length rejected");

    int rc_overrun = piece_manager_store_block(pm, 0, 4, (const uint8_t*)"AAAAAAAA", 8);
    CHECK(rc_overrun == 0, "piece_manager_store_block: begin+length past piece length rejected");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

static void test_all_complete_and_pieces_remaining(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    CHECK(piece_manager_all_complete(pm) == 0, "piece_manager_all_complete: false with nothing downloaded");
    CHECK(piece_manager_pieces_remaining(pm) == 2, "piece_manager_pieces_remaining: 2 remaining initially");

    piece_manager_store_block(pm, 0, 0, (const uint8_t*)"AAAA", 4);
    piece_manager_store_block(pm, 0, 4, (const uint8_t*)"BBBB", 4);
    CHECK(piece_manager_pieces_remaining(pm) == 1, "piece_manager_pieces_remaining: 1 remaining after piece 0");

    piece_manager_store_block(pm, 1, 0, (const uint8_t*)PIECE1_DATA, 4);
    CHECK(piece_manager_all_complete(pm) == 1, "piece_manager_all_complete: true once every piece verifies");
    CHECK(piece_manager_pieces_remaining(pm) == 0, "piece_manager_pieces_remaining: 0 once fully complete");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

/* bitfield_len is (num_pieces + 7) / 8 = 1 byte for a 2-piece torrent.
   Bit layout is MSB-first: bit 7 of byte 0 = piece 0, bit 6 = piece 1. */
static void test_rarest_first_selection(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    unsigned char peer_a_bitfield = 0x80; /* has piece 0 only */
    unsigned char peer_b_bitfield = 0xC0; /* has both pieces */

    piece_manager_add_peer_bitfield(pm, &peer_a_bitfield, 1);
    piece_manager_add_peer_bitfield(pm, &peer_b_bitfield, 1);
    /* rarity: piece 0 = 2 peers, piece 1 = 1 peer -> piece 1 is rarer */

    /* peer_b has both pieces; rarest-first must pick piece 1 (rarity 1) over
       piece 0 (rarity 2), even though piece 0 sorts first by index. */
    int chosen = piece_manager_select_piece(pm, &peer_b_bitfield, 1);
    CHECK(chosen == 1, "piece_manager_select_piece: picks the rarer piece (1), not just the first available");
    CHECK(pm->pieces[1].state == PIECE_IN_PROGRESS, "piece_manager_select_piece: marks the chosen piece IN_PROGRESS");

    /* peer_a only has piece 0; even though piece 1 is rarer, peer_a can't
       supply it, so piece 0 must be selected instead. */
    int chosen_a = piece_manager_select_piece(pm, &peer_a_bitfield, 1);
    CHECK(chosen_a == 0, "piece_manager_select_piece: only offers pieces the given peer actually has");

    /* Now nothing MISSING remains for peer_b (both pieces claimed). */
    int chosen_none = piece_manager_select_piece(pm, &peer_b_bitfield, 1);
    CHECK(chosen_none == -1, "piece_manager_select_piece: returns -1 once no MISSING piece is available for this peer");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

static void test_release_piece_resets_in_progress(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    unsigned char bitfield = 0xC0; /* peer has both pieces */

    int chosen = piece_manager_select_piece(pm, &bitfield, 1);
    CHECK(pm->pieces[chosen].state == PIECE_IN_PROGRESS, "setup: selected piece is IN_PROGRESS");

    piece_manager_release_piece(pm, (uint32_t)chosen);
    CHECK(pm->pieces[chosen].state == PIECE_MISSING, "piece_manager_release_piece: IN_PROGRESS piece resets to MISSING");

    /* Releasing an already-complete piece must be a safe no-op. */
    piece_manager_store_block(pm, 1, 0, (const uint8_t*)PIECE1_DATA, 4);
    piece_manager_release_piece(pm, 1);
    CHECK(pm->pieces[1].state == PIECE_COMPLETE, "piece_manager_release_piece: does not disturb an already-COMPLETE piece");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

static void test_remove_peer_bitfield_decrements_rarity(void) {
    Torrent* t = make_test_torrent();
    PieceManager* pm = piece_manager_create(t);

    unsigned char bitfield = 0x80; /* has piece 0 */

    piece_manager_add_peer_bitfield(pm, &bitfield, 1);
    CHECK(pm->rarity[0] == 1, "piece_manager_add_peer_bitfield: rarity incremented on announce");

    piece_manager_remove_peer_bitfield(pm, &bitfield, 1);
    CHECK(pm->rarity[0] == 0, "piece_manager_remove_peer_bitfield: rarity decremented on disconnect");

    /* Must never underflow below zero even if called more than announced. */
    piece_manager_remove_peer_bitfield(pm, &bitfield, 1);
    CHECK(pm->rarity[0] == 0, "piece_manager_remove_peer_bitfield: does not underflow past zero");

    piece_manager_destroy(pm);
    free_test_torrent(t);
}

int main(void) {
    test_create_and_destroy();
    test_create_rejects_bad_input();
    test_block_storage_multi_block_piece();
    test_corrupt_piece_is_rejected_and_resettable();
    test_store_block_rejects_bad_arguments();
    test_all_complete_and_pieces_remaining();
    test_rarest_first_selection();
    test_release_piece_resets_in_progress();
    test_remove_peer_bitfield_decrements_rarity();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}