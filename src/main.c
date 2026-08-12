#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btypes.h"
#include "torrent.h"
#include "bencoder.h"
#include "tracker.h"
#include "peer.h"
#include "piece_manager.h"
#include "file_writer.h"
#include "downloader.h"
#include "log.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#endif

/* Must match the PEER_ID tracker.c announces to the tracker with (it's
   file-local there, so we can't #include it — kept identical by hand).
   If tracker.c's PEER_ID ever changes, update this too. */
static const unsigned char MY_PEER_ID[20] = {'-', 'C', 'B', '0', '0', '0', '1', '-',
                                              '0','0','0','0','0','0','0','0','0','0','0','1'};

char* read_file(const char* path, size_t* size_out) {
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

int main(int argc, char* argv[]) {

    /* Force unbuffered stdout. Without this, printf output can sit in a
       buffer and never appear if the program later hangs (e.g. in a
       socket call) instead of exiting normally — making a hang look
       like it happened instantly with zero output, when it actually
       happened much later. Cheap to leave in permanently. */
    setvbuf(stdout, NULL, _IONBF, 0);

    log_init();

    /* BT_LOG_LEVEL=debug for full per-message tracing (bitfields, haves,
       every block request/response); default is the quieter INFO level
       (connections, handshakes, unchokes, piece completions, summaries). */
    const char* level_env = getenv("BT_LOG_LEVEL");

    if (level_env && (strcmp(level_env, "debug") == 0 || strcmp(level_env, "DEBUG") == 0))
        log_set_level(LOG_DEBUG);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <torrent-file>\n", argv[0]); 
        return EXIT_FAILURE;
    }
    size_t file_size;

    char* buffer =
        read_file(argv[1], &file_size);

    if (!buffer)
        return EXIT_FAILURE;

    Parser parser = {
        .data = buffer,
        .size = file_size,
        .pos = 0
    };

    BValue* root =
        decode_value(&parser);

    if (!root) {
        destroy_value(root);
        fprintf(stderr, "Failed to decode torrent file\n");
        return EXIT_FAILURE;
    }

    Torrent* torrent = torrent_parse(root);

    if (!torrent) {
        fprintf(stderr, "Failed to parse torrent metadata\n");
        destroy_value(root);
        free(buffer);
        return EXIT_FAILURE;
    }

    


    torrent_print(torrent);

    fprintf(stderr, "[checkpoint] parsing done, about to announce to: %s\n", torrent->announce);

    TrackerHTTPGetResponse* response = connect_tracker(torrent);

    if (!response) {
        fprintf(stderr, "Tracker announce failed\n");
        destroy_value(root);
        torrent_destroy(torrent);
        free(buffer);
        return EXIT_FAILURE;
    }

    if (response->failure_reason) {
        fprintf(stderr, "Tracker returned failure: %s\n", response->failure_reason);
        tracker_response_destroy(response);
        destroy_value(root);
        torrent_destroy(torrent);
        free(buffer);
        return EXIT_FAILURE;
    }

    printf("Tracker OK: interval=%llu, seeders=%llu, leechers=%llu, peers=%llu\n",
        (unsigned long long)response->interval,
        (unsigned long long)response->complete,
        (unsigned long long)response->incomplete,
        (unsigned long long)response->num_peers);

    for (uint64_t i = 0; i < response->num_peers; i++) {
        struct in_addr addr;
        addr.s_addr = response->peers[i].ip; /* already network order */

        printf("  peer[%llu] %s:%u\n",
            (unsigned long long)i,
            inet_ntoa(addr),
            ntohs(response->peers[i].port));
    }

    if (response->num_peers == 0) {
        fprintf(stderr, "Tracker returned no peers\n");
        tracker_response_destroy(response);
        destroy_value(root);
        torrent_destroy(torrent);
        free(buffer);
        return EXIT_FAILURE;
    }

    /* --- Peer Wire Protocol: fetch every piece into a shared in-memory
       buffer, thread-per-peer, then flush that buffer to disk. --- */

    PieceManager* pm = piece_manager_create(torrent);

    if (!pm) {
        fprintf(stderr, "Failed to allocate piece manager (torrent too large for memory?)\n");
        tracker_response_destroy(response);
        destroy_value(root);
        torrent_destroy(torrent);
        free(buffer);
        return EXIT_FAILURE;
    }

    if (!peer_network_init()) {
        fprintf(stderr, "Failed to initialise networking\n");
        piece_manager_destroy(pm);
        tracker_response_destroy(response);
        destroy_value(root);
        torrent_destroy(torrent);
        free(buffer);
        return EXIT_FAILURE;
    }

    int complete = start_download(torrent, pm, response->peers, response->num_peers, MY_PEER_ID);

    if (!complete) {
        fprintf(stderr, "Download incomplete: %u/%u pieces missing\n",
            piece_manager_pieces_remaining(pm), pm->num_pieces);
    }
    else if (!file_writer_save(torrent, pm->file_buffer, pm->total_length, "downloads")) {
        fprintf(stderr, "Failed to write completed download to disk\n");
        complete = 0;
    }
    else {
        printf("Saved to downloads/%s\n", torrent->name);
    }

    peer_network_cleanup();
    piece_manager_destroy(pm);

    tracker_response_destroy(response);

    destroy_value(root);

    torrent_destroy(torrent);

    free(buffer);

    return complete ? EXIT_SUCCESS : EXIT_FAILURE;
}