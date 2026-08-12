/**
 * @authors : @Achintya47, @loki533
 * @brief   : Peer-Wire-Protocol downloading. Two layers:
 *
 *   1. peer_download_session() — everything needed for ONE peer:
 *      connect -> handshake -> read bitfield/have until unchoked ->
 *      loop { pick rarest piece this peer has -> request its blocks
 *      one at a time -> hand each to piece_manager }. This is the
 *      "single-peer, single-piece PoC" logic, and it's exactly what
 *      each thread below runs — nothing peer-specific lives outside it.
 *
 *   2. start_download() — spins up one Windows thread per peer (capped
 *      at MAX_CONCURRENT_PEERS), all hammering the same PieceManager.
 *      Pieces are the unit of ownership (see piece_manager.h), so no
 *      two threads ever write overlapping bytes of file_buffer and no
 *      locking is needed on the hot path.
 *
 * No pipelining and no WSAPoll yet on purpose — one block requested at a
 * time, one blocking recv() per thread. Good enough for correctness first;
 * event-driven I/O and endgame mode are the natural next step.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "peer.h"
#include "downloader.h"

#include "log.h"

#ifdef _WIN32
#include <windows.h>
#endif

static uint32_t count_set_bits(const uint8_t* data, uint32_t len) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < len; i++) {
        unsigned char byte = data[i];
        while (byte) {
            count += byte & 1;
            byte >>= 1;
        }
    }

    return count;
}

/**
 * @brief Drains messages until either we're unchoked (ready to download)
 *        or the connection dies. Along the way it registers the peer's
 *        bitfield/have announcements with the piece manager, and answers
 *        BITFIELD/HAVE by (re)declaring interest whenever we're not
 *        already interested.
 * @return 1 if unchoked and ready, 0 on any failure/EOF.
 */
static int wait_until_unchoked(PeerConnection* pc, PieceManager* pm, int* bitfield_registered) {
    while (1) {
        PeerMessage msg;

        if (!peer_recv_message(pc, &msg)) {
            log_msg(LOG_WARN, pc->ip, pc->port, "connection lost while waiting to be unchoked");
            return 0;
        }

        switch (msg.id) {
            case MSG_KEEPALIVE:
                log_msg(LOG_DEBUG, pc->ip, pc->port, "keep-alive");
                break;

            case MSG_BITFIELD:
                peer_set_bitfield(pc, msg.payload, msg.length);
                piece_manager_add_peer_bitfield(pm, pc->bitfield, pc->bitfield_len);
                *bitfield_registered = 1;
                log_msg(LOG_INFO, pc->ip, pc->port, "bitfield received: has %u/%u pieces",
                    count_set_bits(pc->bitfield, pc->bitfield_len), pm->num_pieces);
                break;

            case MSG_HAVE:
                if (msg.length >= 4) {
                    uint32_t idx = ((uint32_t)msg.payload[0] << 24) | ((uint32_t)msg.payload[1] << 16) |
                                   ((uint32_t)msg.payload[2] << 8)  |  (uint32_t)msg.payload[3];
                    peer_mark_have(pc, idx);
                    log_msg(LOG_DEBUG, pc->ip, pc->port, "have piece %u", idx);
                }
                break;

            case MSG_CHOKE:
                pc->peer_choking = 1;
                log_msg(LOG_DEBUG, pc->ip, pc->port, "choked");
                break;

            case MSG_UNCHOKE:
                pc->peer_choking = 0;
                log_msg(LOG_INFO, pc->ip, pc->port, "unchoked — ready to download");
                break;

            default:
                /* interested/not-interested/request/piece/cancel from a
                   peer we haven't uploaded to yet — nothing to do here */
                break;
        }

        peer_free_message(&msg);

        if (!pc->am_interested) {
            peer_send_interested(pc);
            log_msg(LOG_DEBUG, pc->ip, pc->port, "sent interested");
        }

        if (!pc->peer_choking)
            return 1;
    }
}

/**
 * @brief Requests every remaining block of 'piece_index' one at a time,
 *        storing each as it arrives. Bails out (returning 0) on choke or
 *        any I/O failure, leaving the piece manager to reset the piece.
 */
static int download_piece(PeerConnection* pc, PieceManager* pm, uint32_t piece_index) {
    uint32_t begin, length;

    while (piece_manager_next_block(pm, piece_index, &begin, &length)) {

        log_msg(LOG_DEBUG, pc->ip, pc->port, "requesting piece %u block @%u (%u bytes)",
            piece_index, begin, length);

        if (!peer_send_request(pc, piece_index, begin, length)) {
            log_msg(LOG_WARN, pc->ip, pc->port, "failed to send request for piece %u @%u — connection lost",
                piece_index, begin);
            return 0;
        }

        int got_this_block = 0;

        while (!got_this_block) {
            PeerMessage msg;

            if (!peer_recv_message(pc, &msg)) {
                log_msg(LOG_WARN, pc->ip, pc->port,
                    "connection lost waiting for piece %u block @%u", piece_index, begin);
                return 0;
            }

            if (msg.id == MSG_PIECE && msg.length >= 8) {
                uint32_t r_index = ((uint32_t)msg.payload[0] << 24) | ((uint32_t)msg.payload[1] << 16) |
                                    ((uint32_t)msg.payload[2] << 8)  |  (uint32_t)msg.payload[3];
                uint32_t r_begin = ((uint32_t)msg.payload[4] << 24) | ((uint32_t)msg.payload[5] << 16) |
                                    ((uint32_t)msg.payload[6] << 8)  |  (uint32_t)msg.payload[7];

                if (r_index == piece_index && r_begin == begin) {
                    int rc = piece_manager_store_block(pm, piece_index, r_begin,
                        msg.payload + 8, msg.length - 8);

                    peer_free_message(&msg);

                    if (rc == -1) {
                        log_msg(LOG_WARN, pc->ip, pc->port,
                            "piece %u FAILED SHA1 verification — discarding, will retry from another peer",
                            piece_index);
                        return 0;
                    }

                    if (rc == 0) {
                        log_msg(LOG_ERROR, pc->ip, pc->port,
                            "piece %u block @%u rejected by piece manager (bad begin/length)",
                            piece_index, begin);
                        return 0;
                    }

                    if (rc == 2)
                        log_msg(LOG_INFO, pc->ip, pc->port, "piece %u COMPLETE and verified (SHA1 OK)", piece_index);
                    else
                        log_msg(LOG_DEBUG, pc->ip, pc->port, "received piece %u block @%u", piece_index, begin);

                    got_this_block = 1;
                    continue;
                }

                log_msg(LOG_DEBUG, pc->ip, pc->port,
                    "ignoring stale/mismatched block (got piece %u @%u, expected piece %u @%u)",
                    r_index, r_begin, piece_index, begin);
            }
            else if (msg.id == MSG_CHOKE) {
                pc->peer_choking = 1;
                log_msg(LOG_WARN, pc->ip, pc->port, "choked mid-piece %u — aborting this piece", piece_index);
                peer_free_message(&msg);
                return 0;
            }
            else if (msg.id == MSG_HAVE && msg.length >= 4) {
                uint32_t idx = ((uint32_t)msg.payload[0] << 24) | ((uint32_t)msg.payload[1] << 16) |
                               ((uint32_t)msg.payload[2] << 8)  |  (uint32_t)msg.payload[3];
                peer_mark_have(pc, idx);
            }
            /* anything else (keep-alive, requests from them, etc.) — ignore */

            peer_free_message(&msg);
        }
    }

    return 1;
}

void peer_download_session(Torrent* torrent, PieceManager* pm,
    uint32_t peer_ip, uint16_t peer_port, const unsigned char my_peer_id[20]) {

    PeerConnection pc;
    uint32_t pieces_downloaded = 0;

    log_msg(LOG_INFO, peer_ip, peer_port, "session starting");

    if (!peer_connect(&pc, peer_ip, peer_port)) {
        log_msg(LOG_WARN, peer_ip, peer_port, "session ended: could not connect");
        return;
    }

    if (!peer_send_handshake(&pc, torrent->info_hash, my_peer_id) ||
        !peer_recv_handshake(&pc, torrent->info_hash)) {
        log_msg(LOG_WARN, peer_ip, peer_port, "session ended: handshake failed");
        peer_close(&pc);
        return;
    }

    if (!peer_ensure_bitfield(&pc, pm->num_pieces)) {
        log_msg(LOG_ERROR, peer_ip, peer_port, "session ended: out of memory allocating bitfield");
        peer_close(&pc);
        return;
    }

    int bitfield_registered = 0;

    if (!wait_until_unchoked(&pc, pm, &bitfield_registered))
        goto cleanup;

    while (1) {
        int piece_index = piece_manager_select_piece(pm, pc.bitfield, pc.bitfield_len);

        if (piece_index < 0) {
            log_msg(LOG_INFO, pc.ip, pc.port, "no more needed pieces from this peer — ending session");
            break; /* this peer has nothing left that we still need */
        }

        log_msg(LOG_DEBUG, pc.ip, pc.port, "selected piece %d (rarest-first)", piece_index);

        if (!download_piece(&pc, pm, (uint32_t)piece_index)) {
            piece_manager_release_piece(pm, (uint32_t)piece_index);
            break; /* connection is bad (or we got choked) — stop this peer */
        }

        /* download_piece() only returns success once piece_manager has
           already verified the piece's SHA1 and marked it COMPLETE. */
        pieces_downloaded++;
    }

cleanup:
    if (bitfield_registered)
        piece_manager_remove_peer_bitfield(pm, pc.bitfield, pc.bitfield_len);

    log_msg(LOG_INFO, peer_ip, peer_port, "session ended: %u piece(s) downloaded, %u/%u total complete",
        pieces_downloaded, pm->num_pieces - piece_manager_pieces_remaining(pm), pm->num_pieces);

    peer_close(&pc);
}

#ifdef _WIN32

typedef struct {
    Torrent* torrent;
    PieceManager* pm;
    uint32_t peer_ip;
    uint16_t peer_port;
    unsigned char my_peer_id[20];
} PeerThreadArgs;

static DWORD WINAPI peer_thread_entry(LPVOID arg) {
    PeerThreadArgs* args = (PeerThreadArgs*)arg;

    peer_download_session(args->torrent, args->pm, args->peer_ip, args->peer_port, args->my_peer_id);

    free(args);
    return 0;
}

int start_download(Torrent* torrent, PieceManager* pm, const Peer* peers,
    uint64_t num_peers, const unsigned char my_peer_id[20]) {

    if (!torrent || !pm || !peers || num_peers == 0)
        return piece_manager_all_complete(pm);

    uint64_t count = num_peers;
    if (count > MAX_CONCURRENT_PEERS)
        count = MAX_CONCURRENT_PEERS;

    log_msg(LOG_INFO, 0, 0, "starting download: %u pieces total, spawning up to %llu peer thread(s) (of %llu peers from tracker)",
        pm->num_pieces, (unsigned long long)count, (unsigned long long)num_peers);

    HANDLE* threads = calloc((size_t)count, sizeof(HANDLE));

    if (!threads)
        return 0;

    uint64_t spawned = 0;

    for (uint64_t i = 0; i < count; i++) {
        PeerThreadArgs* args = malloc(sizeof(PeerThreadArgs));

        if (!args)
            continue;

        args->torrent = torrent;
        args->pm = pm;
        args->peer_ip = peers[i].ip;
        args->peer_port = ntohs(peers[i].port); /* compact format is network-order */
        memcpy(args->my_peer_id, my_peer_id, 20);

        HANDLE h = CreateThread(NULL, 0, peer_thread_entry, args, 0, NULL);

        if (h == NULL) {
            log_msg(LOG_ERROR, args->peer_ip, args->peer_port, "CreateThread failed, skipping this peer");
            free(args);
            continue;
        }

        threads[spawned++] = h;
    }

    log_msg(LOG_INFO, 0, 0, "%llu peer thread(s) spawned, waiting for completion...", (unsigned long long)spawned);

    if (spawned > 0) {
        WaitForMultipleObjects((DWORD)spawned, threads, TRUE, INFINITE);

        for (uint64_t i = 0; i < spawned; i++)
            CloseHandle(threads[i]);
    }

    free(threads);

    log_msg(LOG_INFO, 0, 0, "download finished: %u/%u pieces complete",
        pm->num_pieces - piece_manager_pieces_remaining(pm), pm->num_pieces);

    return piece_manager_all_complete(pm);
}

#else /* non-Windows fallback, single-threaded, for portability/testing only */

int start_download(Torrent* torrent, PieceManager* pm, const Peer* peers,
    uint64_t num_peers, const unsigned char my_peer_id[20]) {

    if (!torrent || !pm || !peers || num_peers == 0)
        return piece_manager_all_complete(pm);

    uint64_t count = num_peers;
    if (count > MAX_CONCURRENT_PEERS)
        count = MAX_CONCURRENT_PEERS;

    for (uint64_t i = 0; i < count; i++) {
        peer_download_session(torrent, pm, peers[i].ip, ntohs(peers[i].port), my_peer_id);

        if (piece_manager_all_complete(pm))
            break;
    }

    return piece_manager_all_complete(pm);
}

#endif