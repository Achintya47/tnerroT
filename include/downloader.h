#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <stdint.h>
#include "torrent.h"
#include "tracker.h"       /* for the Peer{ip,port} compact-peer struct */
#include "piece_manager.h"

/* Upper bound on simultaneously-connected peers for this basic model. */
#define MAX_CONCURRENT_PEERS 61

/**
 * @brief Handshakes with one peer, drives its request/receive loop against
 *        the shared PieceManager until that peer has nothing left we need
 *        (or the connection dies), then returns. This is the whole
 *        "single peer" logic — start_download() below just runs many of
 *        these concurrently, one per OS thread.
 *
 * Safe (and useful) to call directly, synchronously, for the single-peer
 * proof-of-concept: connect to one peer, handshake, wait for unchoke,
 * download whichever pieces it has, and let piece_manager verify them —
 * before ever touching threads.
 */
void peer_download_session(Torrent* torrent, PieceManager* pm,
    uint32_t peer_ip, uint16_t peer_port, const unsigned char my_peer_id[20]);

/**
 * @brief Spawns up to MAX_CONCURRENT_PEERS worker threads (one per entry
 *        in 'peers'), each running peer_download_session() against the
 *        same PieceManager, and blocks until they've all finished (either
 *        the torrent completed, or every peer ran out of useful pieces /
 *        disconnected).
 *
 * @return 1 if the torrent is fully downloaded and verified afterwards,
 *         0 otherwise (caller can inspect piece_manager_pieces_remaining
 *         and retry with a fresh peer list / re-announce to the tracker).
 */
int start_download(Torrent* torrent, PieceManager* pm, const Peer* peers,
    uint64_t num_peers, const unsigned char my_peer_id[20]);

#endif
