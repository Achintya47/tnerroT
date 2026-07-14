/**
 * @brief   : HTTP tracker client. Builds a GET /announce request from a
 *           parsed Torrent, sends it over a TCP socket (WinSock), and
 *           decodes the bencoded tracker response into a
 *           TrackerHTTPGetResponse.
 * @note    : UDP trackers (udp://) are not handled here yet.
 */

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tracker.h"
#include "torrent.h"
#include "btypes.h"
#include "bencoder.h"
#include "RFC1738_url_parser.h"

#pragma comment(lib, "Ws2_32.lib")

/* Hardcoded client Listening port (BitTorrent convention : 6881 - 6889) */
#define CLIENT_PORT 6881

/* Hardcoded 20-Byte client-id/peer-id, Array size is exactly 20, the null
    terminator is not stored */
static const unsigned char PEER_ID[20] = "-CB0001-000000000001";

static int winsock_startup(void) {
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

static SOCKET tracker_connect(const char* host, unsigned short port){ 
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    char port_str[6];

    memset(&hints, 0, sizeof(hints));
    memset(&hints, 0, sizeof(hints));

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    snprintf(port_str, sizeof(port_str), "%u", port);

    if (getaddrinfo(host, port_str, &hints, &result) != 0)
        return INVALID_SOCKET;
    
    SOCKET sock = socket(result -> ai_family, result -> ai_socktype, result -> ai_protocol);

    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    if (connect(sock, result -> ai_addr, (int)result -> ai_addrlen) == SOCKET_ERROR) {
        closesocket(sock);
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    freeaddrinfo(result);
    return sock;

}

/**
 * @brief : Sends 'request' and reads until the tracker closes the connection.
 *          We always announce with "Connection : close", so a clean EOF from recv() marks
 *          the end of the response - no Content-Lenght bookkeeping needed.
 * @return : 1 on Success , 0 on failure
 */
static int tracker_roundtrip(SOCKET sock, const char* request, 
    char** out, size_t* out_len) {

        if (send(sock, request, (int)strlen(request), 0) == SOCKET_ERROR)
            return 0;
        
        size_t capacity = 8192;
        size_t length = 0;
        char* buffer = malloc(capacity);

        if (!buffer)
            return 0;
        
        int bytes;
        char chunk[4096];

        while((bytes = recv(sock, chunk, sizeof(chunk), 0)) > 0) {
            if (length + (size_t)bytes > capacity) {
                capacity = (length + (size_t)bytes) * 2;
                char* grown = realloc(buffer, capacity);

                if (!grown) {
                    free(buffer);
                    return 0;
                }

                buffer = grown;
            }

            memcpy(buffer + length, chunk, (size_t)bytes);
            length += (size_t)bytes;
        }

        *out = buffer;
        *out_len = length;
        return 1;
}

static char* build_http_get_request(const TrackerHTTPGetRequest* req) {
    char enc_hash[61];
    char enc_peer[61];

    /* encode_info_hash simple hex encodes the 20 raw bytes, so it works
        basically for any encoding task */
    encode_info_hash(req -> info_hash, enc_hash);
    encode_info_hash(req -> peer_id, enc_peer);

    size_t needed = strlen(req -> path) + strlen(req -> host) + 512;
    char* buffer = malloc(needed);

    if (!buffer)
        return NULL;
    
    snprintf(buffer, needed,
        "GET %s?info_hash=%s&peer_id=%s&port=%u&uploaded=%llu"
        "&downloaded=%llu&left=%llu&compact=%d HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        req->path, enc_hash, enc_peer, req->port,
        (unsigned long long)req->uploaded,
        (unsigned long long)req->downloaded,
        (unsigned long long)req->left,
        req->compact,
        req->host);
 
    return buffer;
}

/**
 * @brief : Splits a raw HTTP Response into headers / body on the blank line
 * @note : Assumes unchunked body ending at connection close, for which we send 
 *          "Connection: close", a tracker that ignores that and
 *          replies chunked will not parse correctly, not handled yet.
 */
static const char* http_body(const char* raw, size_t raw_len, size_t* body_len) {
    for (size_t i = 0; i + 3 < raw_len; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' &&
            raw[i + 2] == '\r' && raw[i + 3] == '\n') {
                *body_len = raw_len - (i + 4);
                return raw + i + 4;
            }
    }

    *body_len = 0;
    return NULL;
}

static char* dup_bstring(BValue* v) {
    if (!v || v->type != BSTRING)
        return NULL;
    
    char* s = malloc((size_t)v -> value.string.length + 1);

    if (!s) return NULL;

    memcpy(s, v -> value.string.data, (size_t)v -> value.string.length);
    s[v -> value.string.length] = '\0';

    return s;
}

/**
 * @brief : Fills a TrackerHTTPGetResponse from the decoded bencoded dict
 * @note : Non-Compact (list of dicts) peers format is not handled.
 */
static int parse_tracker_dict(BValue* dict, TrackerHTTPGetResponse* out) {
    if (!dict || dict->type != BDICT)
        return 0;
 
    BValue* failure = dict_get(dict, "failure reason", 14);
 
    if (failure) {
        out->failure_reason = dup_bstring(failure);
        return 1;
    }
 
    BValue* warning = dict_get(dict, "warning message", 15);
    if (warning)
        out->warning_message = dup_bstring(warning);
 
    BValue* interval = dict_get(dict, "interval", 8);
    if (interval && interval->type == BINT)
        out->interval = (uint64_t)interval->value.integer.value;
 
    BValue* min_interval = dict_get(dict, "min interval", 12);
    if (min_interval && min_interval->type == BINT)
        out->min_interval = (uint64_t)min_interval->value.integer.value;
 
    BValue* tracker_id = dict_get(dict, "tracker id", 10);
    if (tracker_id)
        out->tracker_id = dup_bstring(tracker_id);
 
    BValue* complete = dict_get(dict, "complete", 8);
    if (complete && complete->type == BINT)
        out->complete = (uint64_t)complete->value.integer.value;
 
    BValue* incomplete = dict_get(dict, "incomplete", 10);
    if (incomplete && incomplete->type == BINT)
        out->incomplete = (uint64_t)incomplete->value.integer.value;
 
    BValue* peers = dict_get(dict, "peers", 5);
 
    if (peers && peers->type == BSTRING) {
        size_t count = (size_t)peers->value.string.length / sizeof(Peer);
 
        out->peers = malloc(sizeof(Peer) * count);
 
        if (out->peers) {
            memcpy(out->peers, peers->value.string.data, count * sizeof(Peer));
            out->num_peers = count;
        }
    }
 
    return 1;
}

TrackerHTTPGetResponse* connect_tracker(Torrent* torrent) {
    if (!torrent || !torrent->announce)
        return NULL;
 
    char* host = NULL;
    char* path = NULL;
    char* request = NULL;
    char* raw = NULL;
    BValue* decoded = NULL;
    unsigned short port = 0;
    SOCKET sock = INVALID_SOCKET;
    int wsa_ready = 0;
    TrackerHTTPGetResponse* response = NULL;
 
    if (!parse_announce_url(torrent->announce, &host, &port, &path)) {
        fprintf(stderr, "connect_tracker: unsupported or malformed announce URL: %s\n",
            torrent->announce);
        goto cleanup;
    }
 
    TrackerHTTPGetRequest req = {0};
 
    req.host = host;
    req.path = path;
    req.port = CLIENT_PORT;
    req.left = torrent->length;
    req.compact = 1;
 
    memcpy(req.info_hash, torrent->info_hash, 20);
    memcpy(req.peer_id, PEER_ID, 20);
 
    request = build_http_get_request(&req);
 
    if (!request)
        goto cleanup;
 
    if (!winsock_startup()) {
        fprintf(stderr, "connect_tracker: WSAStartup failed\n");
        goto cleanup;
    }
    wsa_ready = 1;
 
    sock = tracker_connect(host, port);
 
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "connect_tracker: failed to connect to %s:%u\n", host, port);
        goto cleanup;
    }
 
    size_t raw_len = 0;
 
    if (!tracker_roundtrip(sock, request, &raw, &raw_len)) {
        fprintf(stderr, "connect_tracker: failed to read tracker response\n");
        goto cleanup;
    }
 
    size_t body_len = 0;
    const char* body = http_body(raw, raw_len, &body_len);
 
    if (!body) {
        fprintf(stderr, "connect_tracker: malformed HTTP response (no header/body split)\n");
        goto cleanup;
    }
 
    Parser parser = { .data = body, .size = body_len, .pos = 0 };
 
    decoded = decode_value(&parser);
 
    if (!decoded) {
        fprintf(stderr, "connect_tracker: failed to decode tracker response\n");
        goto cleanup;
    }
 
    response = calloc(1, sizeof(TrackerHTTPGetResponse));
 
    if (response)
        parse_tracker_dict(decoded, response);
 
cleanup:
    if (decoded)
        destroy_value(decoded);
 
    if (sock != INVALID_SOCKET)
        closesocket(sock);
 
    if (wsa_ready)
        WSACleanup();
 
    free(raw);
    free(request);
    free(host);
    free(path);
 
    return response;
}
 
void tracker_response_destroy(TrackerHTTPGetResponse* resp) {
    if (!resp)
        return;
 
    free(resp->failure_reason);
    free(resp->warning_message);
    free(resp->tracker_id);
    free(resp->peers);
    free(resp);
}
