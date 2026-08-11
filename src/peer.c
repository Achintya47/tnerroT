/**
 * @brief : Peer Wire Protocol (BEP 3) — handshake, length-prefixed message
 *          framing, and the small set of send helpers a downloading client
 *          needs. This file is deliberately "dumb": it knows how to move
 *          bytes on the wire and how to shape the fixed messages, but it
 *          has no opinion on *when* to request what — that's piece_manager
 *          and downloader's job.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "peer.h"

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

int peer_network_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return 1;
#endif
}

void peer_network_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

int send_all(socket_t sock, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t sent = 0;

    while (sent < len) {
        int n = send(sock, p + sent, (int)(len - sent), 0);

        if (n == SOCKET_ERROR || n <= 0)
            return 0;

        sent += (size_t)n;
    }

    return 1;
}

int recv_all(socket_t sock, void* buf, size_t len) {
    char* p = (char*)buf;
    size_t got = 0;

    while (got < len) {
        int n = recv(sock, p + got, (int)(len - got), 0);

        /* n == 0 means the peer closed the connection cleanly */
        if (n == SOCKET_ERROR || n == 0)
            return 0;

        got += (size_t)n;
    }

    return 1;
}

/**
 * @brief Connects with a bounded timeout instead of the OS default (which
 *        on Windows can be ~20s and would stall a whole worker thread on a
 *        dead peer). Falls back to a straightforward blocking connect on
 *        non-Windows builds.
 */
int peer_connect(PeerConnection* pc, uint32_t ip, uint16_t port) {
    memset(pc, 0, sizeof(*pc));
    pc->sock = INVALID_SOCKET;

    pc->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (pc->sock == INVALID_SOCKET)
        return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = ip; /* already network byte order, from tracker */

#ifdef _WIN32

    // Make the socket non-blocking
    unsigned long mode = 1;
    ioctlsocket(pc->sock, FIONBIO, &mode); /* non-blocking, for the connect timeout */

    int rc = connect(pc->sock, (struct sockaddr*)&addr, sizeof(addr));

    if (rc == SOCKET_ERROR) {
        // Connection has failed, WSAEWOULDBLOCK refers to unfinished connection
        // Thus anything apart from that is a socket failure, thus close socket and move on
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(pc->sock);
            pc->sock = INVALID_SOCKET;
            return 0;
        }

        // Checking whether the socket is writable
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(pc->sock, &writefds);

        struct timeval tv;
        tv.tv_sec  = 5;
        tv.tv_usec = 0;

        // Wait until something happens at the socket for maximum 5 seconds
        rc = select(0, NULL, &writefds, NULL, &tv);

        // Nothing happened
        if (rc <= 0) {
            closesocket(pc->sock);
            pc->sock = INVALID_SOCKET;
            return 0; /* timed out or select error */
        }

        int err = 0;
        int errlen = sizeof(err);
        // SO_ERROR -> 0 -> SUCCESS
        //             non-zero -> CONNECTION FAILED

        getsockopt(pc->sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errlen);

        if (err != 0) {
            closesocket(pc->sock);
            pc->sock = INVALID_SOCKET;
            return 0;
        }
    }

    /* Back to blocking mode: the rest of this module uses simple
       blocking send/recv, which keeps the single-peer logic easy to
       follow (one thread == one peer == one blocking loop). */
    mode = 0;
    // Change the mode
    ioctlsocket(pc->sock, FIONBIO, &mode);
#else
    if (connect(pc->sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(pc->sock);
        pc->sock = INVALID_SOCKET;
        return 0;
    }
#endif

    pc->ip   = ip;
    pc->port = port;

    /* Peer Wire Protocol starts with both sides choked/not-interested. */
    pc->am_choking     = 1;
    pc->peer_choking   = 1;
    pc->am_interested  = 0;
    pc->peer_interested = 0;

    return 1;
}

void peer_close(PeerConnection* pc) {
    if (!pc)
        return;

    if (pc->sock != INVALID_SOCKET) {
        closesocket(pc->sock);
        pc->sock = INVALID_SOCKET;
    }

    free(pc->bitfield);
    pc->bitfield = NULL;
    pc->bitfield_len = 0;
}

int peer_send_handshake(PeerConnection* pc, const unsigned char info_hash[INFO_HASH_LEN],
    const unsigned char peer_id[PEER_ID_LEN]) {

    unsigned char buf[HANDSHAKE_LEN];

    buf[0] = 19;
    memcpy(buf + 1, "BitTorrent protocol", 19);
    memset(buf + 20, 0, 8); /* reserved bytes: no extensions negotiated */
    memcpy(buf + 28, info_hash, INFO_HASH_LEN);
    memcpy(buf + 48, peer_id, PEER_ID_LEN);

    memcpy(pc->info_hash, info_hash, INFO_HASH_LEN);
    memcpy(pc->peer_id, peer_id, PEER_ID_LEN);

    return send_all(pc->sock, buf, HANDSHAKE_LEN);
}

int peer_recv_handshake(PeerConnection* pc, const unsigned char expected_info_hash[INFO_HASH_LEN]) {
    unsigned char buf[HANDSHAKE_LEN];

    if (!recv_all(pc->sock, buf, HANDSHAKE_LEN))
        return 0;

    if (buf[0] != 19 || memcmp(buf + 1, "BitTorrent protocol", 19) != 0)
        return 0;

    if (memcmp(buf + 28, expected_info_hash, INFO_HASH_LEN) != 0)
        return 0; /* wrong torrent — talking to the wrong swarm */

    memcpy(pc->remote_peer_id, buf + 48, PEER_ID_LEN);

    return 1;
}

int peer_send_message(PeerConnection* pc, int id, const uint8_t* payload, uint32_t length) {
    if (id < 0) {
        /* keep-alive: bare 4-byte zero length, no id byte, no payload */
        unsigned char zero[4] = { 0, 0, 0, 0 };
        return send_all(pc->sock, zero, 4);
    }

    uint32_t msg_len = 1 + length; /* id byte + payload */
    unsigned char header[5];

    header[0] = (unsigned char)(msg_len >> 24);
    header[1] = (unsigned char)(msg_len >> 16);
    header[2] = (unsigned char)(msg_len >> 8);
    header[3] = (unsigned char)(msg_len);
    header[4] = (unsigned char)id;

    if (!send_all(pc->sock, header, 5))
        return 0;

    if (length > 0 && payload)
        return send_all(pc->sock, payload, length);

    return 1;
}

int peer_recv_message(PeerConnection* pc, PeerMessage* out) {
    unsigned char len_buf[4];

    if (!recv_all(pc->sock, len_buf, 4))
        return 0;

    uint32_t msg_len = ((uint32_t)len_buf[0] << 24) | ((uint32_t)len_buf[1] << 16) |
                        ((uint32_t)len_buf[2] << 8)  |  (uint32_t)len_buf[3];

    if (msg_len == 0) {
        out->id = MSG_KEEPALIVE;
        out->length = 0;
        out->payload = NULL;
        return 1;
    }

    /* Sanity cap: a full 16KB block message is ~16KB + 13 bytes overhead.
       Anything wildly larger than that means a misbehaving peer or a
       desynced stream — bail rather than trying a huge malloc(). */
    if (msg_len > (1u << 20))
        return 0;

    unsigned char id_byte;

    if (!recv_all(pc->sock, &id_byte, 1))
        return 0;

    uint32_t payload_len = msg_len - 1;
    uint8_t* payload = NULL;

    if (payload_len > 0) {
        payload = malloc(payload_len);

        if (!payload)
            return 0;

        if (!recv_all(pc->sock, payload, payload_len)) {
            free(payload);
            return 0;
        }
    }

    out->id = id_byte;
    out->length = payload_len;
    out->payload = payload;

    return 1;
}

void peer_free_message(PeerMessage* msg) {
    if (!msg)
        return;

    free(msg->payload);
    msg->payload = NULL;
    msg->length = 0;
}

int peer_send_keepalive(PeerConnection* pc) {
    return peer_send_message(pc, MSG_KEEPALIVE, NULL, 0);
}

int peer_send_choke(PeerConnection* pc) {
    if (!peer_send_message(pc, MSG_CHOKE, NULL, 0))
        return 0;
    pc->am_choking = 1;
    return 1;
}

int peer_send_unchoke(PeerConnection* pc) {
    if (!peer_send_message(pc, MSG_UNCHOKE, NULL, 0))
        return 0;
    pc->am_choking = 0;
    return 1;
}

int peer_send_interested(PeerConnection* pc) {
    if (!peer_send_message(pc, MSG_INTERESTED, NULL, 0))
        return 0;
    pc->am_interested = 1;
    return 1;
}

int peer_send_not_interested(PeerConnection* pc) {
    if (!peer_send_message(pc, MSG_NOT_INTERESTED, NULL, 0))
        return 0;
    pc->am_interested = 0;
    return 1;
}

int peer_send_have(PeerConnection* pc, uint32_t index) {
    unsigned char payload[4];

    payload[0] = (unsigned char)(index >> 24);
    payload[1] = (unsigned char)(index >> 16);
    payload[2] = (unsigned char)(index >> 8);
    payload[3] = (unsigned char)(index);

    return peer_send_message(pc, MSG_HAVE, payload, 4);
}

int peer_send_request(PeerConnection* pc, uint32_t index, uint32_t begin, uint32_t length) {
    unsigned char payload[12];

    payload[0]  = (unsigned char)(index >> 24);
    payload[1]  = (unsigned char)(index >> 16);
    payload[2]  = (unsigned char)(index >> 8);
    payload[3]  = (unsigned char)(index);

    payload[4]  = (unsigned char)(begin >> 24);
    payload[5]  = (unsigned char)(begin >> 16);
    payload[6]  = (unsigned char)(begin >> 8);
    payload[7]  = (unsigned char)(begin);

    payload[8]  = (unsigned char)(length >> 24);
    payload[9]  = (unsigned char)(length >> 16);
    payload[10] = (unsigned char)(length >> 8);
    payload[11] = (unsigned char)(length);

    return peer_send_message(pc, MSG_REQUEST, payload, 12);
}

int peer_send_cancel(PeerConnection* pc, uint32_t index, uint32_t begin, uint32_t length) {
    unsigned char payload[12];

    payload[0]  = (unsigned char)(index >> 24);
    payload[1]  = (unsigned char)(index >> 16);
    payload[2]  = (unsigned char)(index >> 8);
    payload[3]  = (unsigned char)(index);

    payload[4]  = (unsigned char)(begin >> 24);
    payload[5]  = (unsigned char)(begin >> 16);
    payload[6]  = (unsigned char)(begin >> 8);
    payload[7]  = (unsigned char)(begin);

    payload[8]  = (unsigned char)(length >> 24);
    payload[9]  = (unsigned char)(length >> 16);
    payload[10] = (unsigned char)(length >> 8);
    payload[11] = (unsigned char)(length);

    return peer_send_message(pc, MSG_CANCEL, payload, 12);
}

int peer_ensure_bitfield(PeerConnection* pc, uint32_t num_pieces) {
    if (pc->bitfield)
        return 1;

    uint32_t needed = (num_pieces + 7) / 8;

    pc->bitfield = calloc(needed, 1);

    if (!pc->bitfield)
        return 0;

    pc->bitfield_len = needed;
    return 1;
}

void peer_set_bitfield(PeerConnection* pc, const uint8_t* data, uint32_t len) {
    free(pc->bitfield);

    pc->bitfield = malloc(len);

    if (pc->bitfield)
        memcpy(pc->bitfield, data, len);

    pc->bitfield_len = pc->bitfield ? len : 0;
}

void peer_mark_have(PeerConnection* pc, uint32_t index) {
    uint32_t byte_i = index / 8;

    if (!pc->bitfield || byte_i >= pc->bitfield_len)
        return;

    pc->bitfield[byte_i] |= (unsigned char)(1u << (7 - (index % 8)));
}

int peer_has_piece(const PeerConnection* pc, uint32_t index) {
    uint32_t byte_i = index / 8;

    if (!pc->bitfield || byte_i >= pc->bitfield_len)
        return 0;

    return (pc->bitfield[byte_i] >> (7 - (index % 8))) & 1;
}
