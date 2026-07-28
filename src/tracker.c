/**
 * @brief   : HTTP tracker client. Builds a GET /announce request from a
 *           parsed Torrent, sends it over a TCP socket (WinSock), and
 *           decodes the bencoded tracker response into a
 *           TrackerHTTPGetResponse.
 *              http:// -> HTTP GET + bencoded response (BEP 3)
 *              udp:// -> binary connect/announce datagram (BEP 15)
 */

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tracker.h"
#include "torrent.h"
#include "btypes.h"
#include "bencoder.h"
#include "RFC1738_url_parser.h"

// #pragma comment(lib, "Ws2_32.lib")

/* Hardcoded client Listening port (BitTorrent convention : 6881 - 6889) */
#define CLIENT_PORT 6881

/* Hardcoded 20-Byte client-id/peer-id, Array size is exactly 20, the null
    terminator is not stored */
static const unsigned char PEER_ID[20] = {'-', 'C', 'B', '0', '0', '0', '1', '-', 
                                          '0','0','0','0','0','0','0','0','0','0','0','1'};


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

TrackerHTTPGetResponse* connect_tracker_http(Torrent* torrent) {
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


/* ---------------------------------------------------------------------
 * UDP tracker (BEP 15)
 *
 * Wire format is fixed-size and big-endian, so instead of trusting struct
 * layout/padding we serialise by hand into plain byte buffers using the
 * put_u.. / get_u.. helpers below - that keeps it correct regardless of
 * struct alignment or host endianness, and is easy to check line-by-line
 * against the BEP 15 offset tables.
 * --------------------------------------------------------------------- */
 
#define UDP_PROTOCOL_ID      0x41727101980ULL
#define UDP_ACTION_CONNECT   0
#define UDP_ACTION_ANNOUNCE  1
#define UDP_ACTION_ERROR     3
#define UDP_TIMEOUT_BASE_SEC 15
#define UDP_MAX_RETRIES      4

static void put_u16(unsigned char* p, uint16_t v) {
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v);
}
 
static void put_u32(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)(v);
}
 
static void put_u64(unsigned char* p, uint64_t v) {
    put_u32(p,     (uint32_t)(v >> 32));
    put_u32(p + 4, (uint32_t)(v & 0xFFFFFFFFu));
}
 
static uint32_t get_u32(const unsigned char* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
 
static uint64_t get_u64(const unsigned char* p) {
    return ((uint64_t)get_u32(p) << 32) | (uint64_t)get_u32(p + 4);
}


static uint32_t random_transaction_id(void) {
    static int seeded = 0;
 
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }
 
    /* rand() alone is often only 15 bits wide (RAND_MAX == 0x7FFF on
       MSVC), so combine two calls to fill 32 bits */
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}
 
static void set_recv_timeout(SOCKET sock, int seconds) {
    DWORD timeout_ms = (DWORD)seconds * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
}
 
/**
 * @brief Opens a UDP socket "connected" to host:port.
 * @note  connect() on a SOCK_DGRAM socket doesn't perform a handshake —
 *        it just fixes the peer address so we can use send()/recv()
 *        instead of sendto()/recvfrom(), and so stray packets from other
 *        hosts are filtered out by the OS.
 */
static SOCKET udp_connect_socket(const char* host, unsigned short port) {
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    char port_str[6];
 
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
 
    snprintf(port_str, sizeof(port_str), "%u", port);
 
    if (getaddrinfo(host, port_str, &hints, &result) != 0)
        return INVALID_SOCKET;
 
    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
 
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }
 
    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        closesocket(sock);
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }
 
    freeaddrinfo(result);
    return sock;
}
 
/**
 * @brief Connect handshake (BEP 15 "Connect"): obtains a connection_id,
 *        retrying with the spec's backoff schedule on timeout.
 * @return 1 on success (*connection_id_out set), 0 on timeout/error/
 *         tracker-reported error (which is printed to stderr).
 */
static int udp_connect(SOCKET sock, uint64_t* connection_id_out) {
    unsigned char req[16];
    unsigned char resp[512];
 
    put_u64(req,     UDP_PROTOCOL_ID);
    put_u32(req + 8, UDP_ACTION_CONNECT);
 
    uint32_t txn = random_transaction_id();
    put_u32(req + 12, txn);
 
    for (int n = 0; n <= UDP_MAX_RETRIES; n++) {
        if (send(sock, (const char*)req, sizeof(req), 0) == SOCKET_ERROR)
            return 0;
 
        set_recv_timeout(sock, UDP_TIMEOUT_BASE_SEC * (1 << n));
 
        int got = recv(sock, (char*)resp, sizeof(resp), 0);
 
        if (got < 8)
            continue; /* timed out, or too short to even read action+transaction_id */
 
        if (get_u32(resp + 4) != txn)
            continue; /* stray/mismatched packet; just retry */
 
        uint32_t action = get_u32(resp);
 
        if (action == UDP_ACTION_CONNECT && got >= 16) {
            *connection_id_out = get_u64(resp + 8);
            return 1;
        }
 
        if (action == UDP_ACTION_ERROR) {
            int msg_len = got - 8;
            fprintf(stderr, "connect_tracker: udp tracker error: %.*s\n",
                msg_len > 0 ? msg_len : 0, (const char*)resp + 8);
            return 0;
        }
    }
 
    return 0;
}
 
/**
 * @brief Announce (BEP 15 "Announce"): sends our stats, gets back
 *        interval/seeder/leecher counts plus a compact peer list.
 * @return 1 on success (*resp_len_out set, resp_buf holds the raw reply),
 *         0 on timeout/error.
 */
static int udp_announce(SOCKET sock, uint64_t connection_id, Torrent* torrent,
    unsigned char* resp_buf, size_t resp_buf_size, int* resp_len_out) {
 
    unsigned char req[98];
 
    put_u64(req,      connection_id);
    put_u32(req + 8,  UDP_ACTION_ANNOUNCE);
 
    uint32_t txn = random_transaction_id();
    put_u32(req + 12, txn);
 
    memcpy(req + 16, torrent->info_hash, 20);
    memcpy(req + 36, PEER_ID, 20);
 
    put_u64(req + 56, 0);                  /* downloaded */
    put_u64(req + 64, torrent->length);    /* left */
    put_u64(req + 72, 0);                  /* uploaded */
    put_u32(req + 80, 0);                  /* event: 0 = none */
    put_u32(req + 84, 0);                  /* IP address: 0 = let tracker decide */
    put_u32(req + 88, 0);                  /* key: unused for now */
    put_u32(req + 92, (uint32_t)-1);       /* num_want: -1 = default */
    put_u16(req + 96, CLIENT_PORT);
 
    for (int n = 0; n <= UDP_MAX_RETRIES; n++) {
        if (send(sock, (const char*)req, sizeof(req), 0) == SOCKET_ERROR)
            return 0;
 
        set_recv_timeout(sock, UDP_TIMEOUT_BASE_SEC * (1 << n));
 
        int got = recv(sock, (char*)resp_buf, (int)resp_buf_size, 0);
 
        if (got < 8)
            continue;
 
        if (get_u32(resp_buf + 4) != txn)
            continue;
 
        uint32_t action = get_u32(resp_buf);
 
        if (action == UDP_ACTION_ANNOUNCE && got >= 20) {
            *resp_len_out = got;
            return 1;
        }
 
        if (action == UDP_ACTION_ERROR) {
            *resp_len_out = got;
            return 1; /* caller checks the action byte to surface the message */
        }
    }
 
    return 0;
}
 
static TrackerHTTPGetResponse* connect_tracker_udp(Torrent* torrent) {
    char* host = NULL;
    unsigned short port = 0;
 
    if (!parse_udp_announce_url(torrent->announce, &host, &port)) {
        fprintf(stderr, "connect_tracker: malformed udp announce URL: %s\n",
            torrent->announce);
        return NULL;
    }
 
    TrackerHTTPGetResponse* response = NULL;
    SOCKET sock = INVALID_SOCKET;
    int wsa_ready = 0;
 
    if (!winsock_startup()) {
        fprintf(stderr, "connect_tracker: WSAStartup failed\n");
        goto cleanup;
    }
    wsa_ready = 1;
 
    sock = udp_connect_socket(host, port);
 
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "connect_tracker: failed to reach udp tracker %s:%u\n", host, port);
        goto cleanup;
    }
 
    uint64_t connection_id = 0;
 
    if (!udp_connect(sock, &connection_id)) {
        fprintf(stderr, "connect_tracker: udp connect handshake failed/timed out\n");
        goto cleanup;
    }
 
    unsigned char resp_buf[2048]; /* 20-byte header + room for lots of 6-byte peers */
    int resp_len = 0;
 
    if (!udp_announce(sock, connection_id, torrent, resp_buf, sizeof(resp_buf), &resp_len)) {
        fprintf(stderr, "connect_tracker: udp announce failed/timed out\n");
        goto cleanup;
    }
 
    response = calloc(1, sizeof(TrackerHTTPGetResponse));
 
    if (!response)
        goto cleanup;
 
    if (get_u32(resp_buf) == UDP_ACTION_ERROR) {
        int msg_len = resp_len - 8;
 
        if (msg_len > 0) {
            response->failure_reason = malloc((size_t)msg_len + 1);
 
            if (response->failure_reason) {
                memcpy(response->failure_reason, resp_buf + 8, (size_t)msg_len);
                response->failure_reason[msg_len] = '\0';
            }
        }
 
        goto cleanup;
    }
 
    response->interval   = get_u32(resp_buf + 8);
    response->incomplete  = get_u32(resp_buf + 12); /* leechers */
    response->complete    = get_u32(resp_buf + 16); /* seeders */
 
    int peer_bytes = resp_len - 20;
    size_t count = (peer_bytes > 0) ? (size_t)peer_bytes / 6 : 0;
 
    if (count > 0) {
        response->peers = malloc(sizeof(Peer) * count);
 
        if (response->peers) {
            for (size_t i = 0; i < count; i++) {
                const unsigned char* p = resp_buf + 20 + i * 6;
 
                /* Keep IP/port in network byte order, same convention as
                   the HTTP compact-peers path — callers use ntohl/ntohs. */
                memcpy(&response->peers[i].ip,   p,     4);
                memcpy(&response->peers[i].port, p + 4, 2);
            }
 
            response->num_peers = count;
        }
    }
 
cleanup:
    if (sock != INVALID_SOCKET)
        closesocket(sock);
 
    if (wsa_ready)
        WSACleanup();
 
    free(host);
 
    return response;
}
 

TrackerHTTPGetResponse* connect_tracker(Torrent* torrent) {
    if (!torrent || !torrent->announce)
        return NULL;
 
    if (strncmp(torrent->announce, "udp://", 6) == 0)
        return connect_tracker_udp(torrent);
 
    if (strncmp(torrent->announce, "http://", 7) == 0)
        return connect_tracker_http(torrent);
 
    fprintf(stderr, "connect_tracker: unsupported announce scheme: %s\n",
        torrent->announce);
    return NULL;
}