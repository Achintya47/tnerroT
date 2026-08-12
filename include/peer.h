#ifndef PEER_H
#define PEER_H

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
typedef SOCKET socket_t;
#else
/* Not the target platform (dev environment is Windows), but kept so the
   rest of the codebase can still be syntax/logic-checked on other OSes. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#define closesocket(s) close(s)
#endif

/* Peer Wire Protocol message IDs (BEP 3) */
typedef enum {
    MSG_CHOKE          = 0,
    MSG_UNCHOKE        = 1,
    MSG_INTERESTED     = 2,
    MSG_NOT_INTERESTED = 3,
    MSG_HAVE           = 4,
    MSG_BITFIELD       = 5,
    MSG_REQUEST        = 6,
    MSG_PIECE          = 7,
    MSG_CANCEL         = 8,
    MSG_KEEPALIVE      = -1  /* internal marker: zero-length message */
} MessageID;

/* Standard block size used for piece requests (BEP 3 convention). */
#define BLOCK_SIZE (16 * 1024)

#define HANDSHAKE_LEN 68
#define PEER_ID_LEN   20
#define INFO_HASH_LEN 20

typedef struct {
    int      id;       /* MessageID value, or MSG_KEEPALIVE */
    uint8_t* payload;  /* heap-allocated, NULL for zero-length messages */
    uint32_t length;   /* payload length in bytes */
} PeerMessage;

typedef struct {
    socket_t sock;

    unsigned char info_hash[INFO_HASH_LEN];
    unsigned char peer_id[PEER_ID_LEN];         /* ours, sent in the handshake */
    unsigned char remote_peer_id[PEER_ID_LEN];  /* theirs, from the handshake */

    uint8_t* bitfield;      /* remote peer's have-map, 1 bit per piece, MSB-first */
    uint32_t bitfield_len;  /* bytes */

    int am_choking;
    int am_interested;
    int peer_choking;
    int peer_interested;

    uint32_t ip;    /* network byte order, as received from the tracker */
    uint16_t port;  /* host byte order */
} PeerConnection;

/* --- Networking bring-up (Windows: wraps WSAStartup/WSACleanup) --- */
int  peer_network_init(void);
void peer_network_cleanup(void);

/* --- Low-level TCP helpers: loop until 'len' bytes are moved or an error occurs --- */
int send_all(socket_t sock, const void* buf, size_t len);
int recv_all(socket_t sock, void* buf, size_t len);

/* --- Connection lifecycle --- */
int  peer_connect(PeerConnection* pc, uint32_t ip, uint16_t port);
void peer_close(PeerConnection* pc);

/* --- Handshake: <pstrlen=19><"BitTorrent protocol"><reserved[8]><info_hash><peer_id> --- */
int peer_send_handshake(PeerConnection* pc, const unsigned char info_hash[INFO_HASH_LEN],
    const unsigned char peer_id[PEER_ID_LEN]);
int peer_recv_handshake(PeerConnection* pc, const unsigned char expected_info_hash[INFO_HASH_LEN]);

/* --- Message framing: <length prefix (4 bytes)><id (1 byte)><payload> --- */
int  peer_send_message(PeerConnection* pc, int id, const uint8_t* payload, uint32_t length);
int  peer_recv_message(PeerConnection* pc, PeerMessage* out); /* blocking; allocates out->payload */
void peer_free_message(PeerMessage* msg);

/* --- Convenience senders for the fixed-shape messages --- */
int peer_send_keepalive(PeerConnection* pc);
int peer_send_choke(PeerConnection* pc);
int peer_send_unchoke(PeerConnection* pc);
int peer_send_interested(PeerConnection* pc);
int peer_send_not_interested(PeerConnection* pc);
int peer_send_have(PeerConnection* pc, uint32_t index);
int peer_send_request(PeerConnection* pc, uint32_t index, uint32_t begin, uint32_t length);
int peer_send_cancel(PeerConnection* pc, uint32_t index, uint32_t begin, uint32_t length);

/* --- Bitfield helpers (tracking which pieces the *remote* peer has) --- */
int  peer_ensure_bitfield(PeerConnection* pc, uint32_t num_pieces);
void peer_set_bitfield(PeerConnection* pc, const uint8_t* data, uint32_t len);
void peer_mark_have(PeerConnection* pc, uint32_t index);
int  peer_has_piece(const PeerConnection* pc, uint32_t index);

#endif
