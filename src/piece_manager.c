/**
 * @brief : Shared download state, handed to every peer thread.
 *
 * Design: each piece is owned by at most one thread at a time
 * (PIECE_IN_PROGRESS). Because piece N's bytes always land in the fixed
 * range [N*piece_length, N*piece_length + piece_length) of file_buffer,
 * and only its owning thread ever writes there, block-level writes need
 * no locking at all — only the small bits of *bookkeeping* shared across
 * threads (piece state, rarity[], pieces_complete) go through pm->lock.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "piece_manager.h"
#include "sha1.h"

PieceManager* piece_manager_create(Torrent* torrent) {
    if (!torrent || torrent->num_pieces == 0)
        return NULL;

    PieceManager* pm = calloc(1, sizeof(PieceManager));

    if (!pm)
        return NULL;

    pm->torrent = torrent;
    pm->num_pieces = (uint32_t)torrent->num_pieces;
    pm->total_length = torrent->length;

    /* Initialise the lock first so destroy() can always safely tear it
       down, regardless of which allocation below might fail. */
    PM_LOCK_INIT(&pm->lock);

    pm->pieces = calloc(pm->num_pieces, sizeof(PieceInfo));
    pm->rarity = calloc(pm->num_pieces, sizeof(uint32_t));
    pm->file_buffer = malloc((size_t)pm->total_length);

    if (!pm->pieces || !pm->rarity || !pm->file_buffer) {
        piece_manager_destroy(pm);
        return NULL;
    }

    for (uint32_t i = 0; i < pm->num_pieces; i++) {
        uint64_t offset = (uint64_t)i * torrent->piece_length;
        uint64_t piece_len = torrent->piece_length;

        /* last piece is usually shorter than piece_length */
        if (offset + piece_len > torrent->length)
            piece_len = torrent->length - offset;

        pm->pieces[i].length = (uint32_t)piece_len;
        pm->pieces[i].blocks_total = (uint32_t)((piece_len + BLOCK_SIZE - 1) / BLOCK_SIZE);
        pm->pieces[i].block_have = calloc(pm->pieces[i].blocks_total, 1);
        pm->pieces[i].state = PIECE_MISSING;

        if (!pm->pieces[i].block_have) {
            piece_manager_destroy(pm);
            return NULL;
        }
    }

    return pm;
}

void piece_manager_destroy(PieceManager* pm) {
    if (!pm)
        return;

    if (pm->pieces) {
        for (uint32_t i = 0; i < pm->num_pieces; i++)
            free(pm->pieces[i].block_have);
        free(pm->pieces);
    }

    free(pm->rarity);
    free(pm->file_buffer);

    PM_LOCK_DESTROY(&pm->lock);

    free(pm);
}

void piece_manager_add_peer_bitfield(PieceManager* pm, const uint8_t* bitfield, uint32_t bitfield_len) {
    if (!pm || !bitfield)
        return;

    PM_LOCK(&pm->lock);

    for (uint32_t i = 0; i < pm->num_pieces; i++) {
        uint32_t byte_i = i / 8;

        if (byte_i >= bitfield_len)
            continue;

        if ((bitfield[byte_i] >> (7 - (i % 8))) & 1)
            pm->rarity[i]++;
    }

    PM_UNLOCK(&pm->lock);
}

void piece_manager_remove_peer_bitfield(PieceManager* pm, const uint8_t* bitfield, uint32_t bitfield_len) {
    if (!pm || !bitfield)
        return;

    PM_LOCK(&pm->lock);

    for (uint32_t i = 0; i < pm->num_pieces; i++) {
        uint32_t byte_i = i / 8;

        if (byte_i >= bitfield_len)
            continue;

        if (((bitfield[byte_i] >> (7 - (i % 8))) & 1) && pm->rarity[i] > 0)
            pm->rarity[i]--;
    }

    PM_UNLOCK(&pm->lock);
}

int piece_manager_select_piece(PieceManager* pm, const uint8_t* peer_bitfield, uint32_t bitfield_len) {
    if (!pm || !peer_bitfield)
        return -1;

    int best = -1;
    uint32_t best_rarity = 0xFFFFFFFFu;

    PM_LOCK(&pm->lock);

    for (uint32_t i = 0; i < pm->num_pieces; i++) {
        if (pm->pieces[i].state != PIECE_MISSING)
            continue;

        uint32_t byte_i = i / 8;

        if (byte_i >= bitfield_len)
            continue;

        int peer_has = (peer_bitfield[byte_i] >> (7 - (i % 8))) & 1;

        if (!peer_has)
            continue;

        if (pm->rarity[i] < best_rarity) {
            best_rarity = pm->rarity[i];
            best = (int)i;
        }
    }

    if (best >= 0)
        pm->pieces[best].state = PIECE_IN_PROGRESS;

    PM_UNLOCK(&pm->lock);

    return best;
}

int piece_manager_next_block(PieceManager* pm, uint32_t piece_index, uint32_t* begin_out, uint32_t* length_out) {
    if (!pm || piece_index >= pm->num_pieces)
        return 0;

    PieceInfo* pi = &pm->pieces[piece_index];

    for (uint32_t b = 0; b < pi->blocks_total; b++) {
        if (pi->block_have[b])
            continue;

        uint32_t begin = b * BLOCK_SIZE;
        uint32_t length = BLOCK_SIZE;

        if (begin + length > pi->length)
            length = pi->length - begin;

        *begin_out = begin;
        *length_out = length;
        return 1;
    }

    return 0;
}

int piece_manager_store_block(PieceManager* pm, uint32_t piece_index, uint32_t begin,
    const uint8_t* data, uint32_t length) {

    if (!pm || piece_index >= pm->num_pieces || !data)
        return 0;

    PieceInfo* pi = &pm->pieces[piece_index];

    if (begin >= pi->length || (uint64_t)begin + length > pi->length)
        return 0;

    uint32_t block_index = begin / BLOCK_SIZE;

    if (block_index >= pi->blocks_total)
        return 0;

    uint64_t piece_offset = (uint64_t)piece_index * pm->torrent->piece_length;
    uint64_t abs_offset = piece_offset + begin;

    if (abs_offset + length > pm->total_length)
        return 0;

    /* No lock needed: piece_index is owned by exactly one thread while
       IN_PROGRESS, and [abs_offset, abs_offset+length) is unique to this
       block, so no two threads ever touch overlapping bytes. */
    memcpy(pm->file_buffer + abs_offset, data, length);

    if (!pi->block_have[block_index]) {
        pi->block_have[block_index] = 1;
        pi->blocks_have++;
    }

    if (pi->blocks_have < pi->blocks_total)
        return 1; /* piece still incomplete */

    /* All blocks in — verify against the SHA1 recorded in the .torrent. */
    unsigned char digest[20];
    sha1_digest(pm->file_buffer + piece_offset, pi->length, digest);

    if (memcmp(digest, pm->torrent->pieces[piece_index].hash, 20) != 0) {
        /* corrupt piece — wipe progress and let someone else retry it */
        memset(pi->block_have, 0, pi->blocks_total);
        pi->blocks_have = 0;

        PM_LOCK(&pm->lock);
        pi->state = PIECE_MISSING;
        PM_UNLOCK(&pm->lock);

        return -1;
    }

    PM_LOCK(&pm->lock);
    pi->state = PIECE_COMPLETE;
    pm->pieces_complete++;
    PM_UNLOCK(&pm->lock);

    return 2;
}

int piece_manager_is_piece_complete(PieceManager* pm, uint32_t piece_index) {
    if (!pm || piece_index >= pm->num_pieces)
        return 0;

    int result;

    PM_LOCK(&pm->lock);
    result = (pm->pieces[piece_index].state == PIECE_COMPLETE);
    PM_UNLOCK(&pm->lock);

    return result;
}

int piece_manager_all_complete(PieceManager* pm) {
    if (!pm)
        return 0;

    int result;

    PM_LOCK(&pm->lock);
    result = (pm->pieces_complete == pm->num_pieces);
    PM_UNLOCK(&pm->lock);

    return result;
}

uint32_t piece_manager_pieces_remaining(PieceManager* pm) {
    if (!pm)
        return 0;

    uint32_t result;

    PM_LOCK(&pm->lock);
    result = pm->num_pieces - pm->pieces_complete;
    PM_UNLOCK(&pm->lock);

    return result;
}

void piece_manager_release_piece(PieceManager* pm, uint32_t piece_index) {
    if (!pm || piece_index >= pm->num_pieces)
        return;

    PieceInfo* pi = &pm->pieces[piece_index];

    PM_LOCK(&pm->lock);
    if (pi->state == PIECE_IN_PROGRESS)
        pi->state = PIECE_MISSING;
    PM_UNLOCK(&pm->lock);
}
