#ifndef PIECE_MANAGER_H
#define PIECE_MANAGER_H

#include <stdint.h>
#include "torrent.h"

/* Same value as peer.h's BLOCK_SIZE. Kept as an independent, identical
   macro so this header has no hard dependency on peer.h (piece_manager
   is used from downloader.c, which pulls both in). */
#ifndef BLOCK_SIZE
#define BLOCK_SIZE (16 * 1024)
#endif

/* --- Cross-platform mutex shim (Windows is the target; POSIX kept so
   this module can be built/tested on other platforms too). --- */
#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION pm_lock_t;
#define PM_LOCK_INIT(l)    InitializeCriticalSection(l)
#define PM_LOCK_DESTROY(l) DeleteCriticalSection(l)
#define PM_LOCK(l)         EnterCriticalSection(l)
#define PM_UNLOCK(l)       LeaveCriticalSection(l)
#else
#include <pthread.h>
typedef pthread_mutex_t pm_lock_t;
#define PM_LOCK_INIT(l)    pthread_mutex_init(l, NULL)
#define PM_LOCK_DESTROY(l) pthread_mutex_destroy(l)
#define PM_LOCK(l)         pthread_mutex_lock(l)
#define PM_UNLOCK(l)       pthread_mutex_unlock(l)
#endif

typedef enum {
    PIECE_MISSING = 0,     /* nobody is working on it */
    PIECE_IN_PROGRESS,     /* exactly one peer thread owns it right now */
    PIECE_COMPLETE          /* downloaded, hash-verified, in file_buffer */
} PieceState;

typedef struct {
    PieceState state;
    uint32_t   length;         /* this piece's real length (last piece may be short) */
    uint32_t   blocks_total;
    uint32_t   blocks_have;
    uint8_t*   block_have;     /* 1 byte per block, 0/1 */
} PieceInfo;

typedef struct {
    Torrent*  torrent;

    PieceInfo* pieces;
    uint32_t   num_pieces;
    uint32_t   pieces_complete;

    uint32_t*  rarity;   /* count of connected peers known to have each piece */

    uint8_t*   file_buffer;   /* single contiguous buffer for the whole torrent */
    uint64_t   total_length;

    pm_lock_t lock; /* guards: piece state transitions, rarity[], pieces_complete */
} PieceManager;

/**
 * @brief Allocates the piece table and a single in-memory buffer big enough
 *        to hold the entire torrent (torrent->length bytes). Each piece
 *        owns a fixed, non-overlapping byte range within that buffer, so
 *        once a piece is assigned to exactly one peer thread, that thread
 *        can write into its slice of the buffer without any locking.
 */
PieceManager* piece_manager_create(Torrent* torrent);
void piece_manager_destroy(PieceManager* pm);

/* Bump/undo rarity counts from a peer's bitfield (call on BITFIELD receipt
   and again on disconnect, so rarest-first selection stays accurate). */
void piece_manager_add_peer_bitfield(PieceManager* pm, const uint8_t* bitfield, uint32_t bitfield_len);
void piece_manager_remove_peer_bitfield(PieceManager* pm, const uint8_t* bitfield, uint32_t bitfield_len);

/**
 * @brief Picks the rarest PIECE_MISSING piece that the given peer's
 *        bitfield claims to have, and atomically marks it PIECE_IN_PROGRESS
 *        so no other thread will pick it up. Returns -1 if this peer has
 *        nothing left that we still need.
 */
int piece_manager_select_piece(PieceManager* pm, const uint8_t* peer_bitfield, uint32_t bitfield_len);

/**
 * @brief Returns the next not-yet-received block (offset,length) within a
 *        piece, or 0 if every block has already been stored. Only ever
 *        call this for a piece your thread currently owns (PIECE_IN_PROGRESS
 *        under your ownership) — there's no locking here because a piece
 *        is only ever touched by the single thread that owns it.
 *        No request pipelining in this basic model: request one block,
 *        wait for it to arrive (store_block), then ask for the next.
 */
int piece_manager_next_block(PieceManager* pm, uint32_t piece_index, uint32_t* begin_out, uint32_t* length_out);

/**
 * @brief Copies a received block into file_buffer at its absolute offset.
 *        When the last block of a piece arrives, the whole piece is
 *        SHA1-verified against torrent->pieces[index].hash.
 * @return  1  block stored, piece still incomplete
 *          2  block stored, piece now complete AND verified
 *         -1  piece completed but failed hash verification (reset to
 *             PIECE_MISSING for re-download by someone else)
 *          0  bad piece_index/begin/length (should not happen with a
 *             well-behaved peer)
 */
int piece_manager_store_block(PieceManager* pm, uint32_t piece_index, uint32_t begin,
    const uint8_t* data, uint32_t length);

int piece_manager_is_piece_complete(PieceManager* pm, uint32_t piece_index);
int piece_manager_all_complete(PieceManager* pm);
uint32_t piece_manager_pieces_remaining(PieceManager* pm);

/* Call when a peer thread dies/disconnects mid-piece, so the piece goes
   back to PIECE_MISSING and someone else can pick it up. Safe to call
   even if the piece already completed (no-op in that case). */
void piece_manager_release_piece(PieceManager* pm, uint32_t piece_index);

#endif
