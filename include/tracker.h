#include "torrent.h"

/* This file will be containing all the message structs,
so that it is easier to send requests */
typedef enum {
    TRACKER_HTTP,
    TRACKER_HTTPS,
    TRACKER_UDP
} TrackerType;

/* Compact peers only, common format for HTTP trackers and UDP Trackers */
typedef struct {
    uint32_t ip;
    uint16_t port;
} Peer;

typedef struct {

    char* path; /* After the host, the path will be provided, ususally /announce */

    unsigned char info_hash[20]; /* URL-Encode this (RFC1738) */

    unsigned char peer_id[20]; /* Randomly Generated, let's keep it hardcoded */

    uint16_t port; /* Our port, 6881 - 6889(for torrent) */

    uint64_t uploaded; /* 0 (initially) */

    uint64_t downloaded; /* 0 (initially) */

    uint64_t left; /* torrent -> length */

    int compact; /* Set this to 1 to get compact 6-Byte responses per peer */
    
    char* host; /* tracker.example.com */

} TrackerHTTPGetRequest;

typedef struct {

    /* failure reason */
    char* failure_reason;

    /* warning message */
    char* warning_message;

    /* tracker announce interval */
    uint64_t interval;

    /* optional */
    uint64_t min_interval;

    /* optional */
    char* tracker_id;

    /* seeders */
    uint64_t complete;

    /* leechers */
    uint64_t incomplete;

    /* compact peer list */
    Peer* peers;
    uint64_t num_peers;

} TrackerHTTPGetResponse;


typedef struct {
    uint64_t protocol_id; /* 0x41727101980 */

    /* 0 = connect
    1 = announce
    2 = scrape
    3 = error */
    uint32_t action;

    uint32_t transaction_id; /* Random Number */

} TrackerUDPConnectRequest;

typedef struct {
    uint32_t action;
    uint32_t transaction_id;
    
    /* Important for announce request */
    uint64_t connection_id;

} TrackerUDPConnectResponse;

typedef struct {
    /* From Tracker's response */
    uint64_t connection_id;

    /* */
    uint32_t action;
    uint32_t transaction_id;

    unsigned char info_hash[20];
    unsigned char peer_id[20];

    uint64_t downloaded;
    uint64_t left;
    uint64_t uploaded;

    uint32_t event;

    uint32_t ip_address;

    uint32_t key;

    int32_t num_want;

    uint16_t port;

} TrackerUDPAnnounceRequest;

typedef struct {
    uint32_t action;
    uint32_t transaction_id;

    uint32_t interval;
    uint32_t leechers;
    uint32_t seeders;

    Peer peers[];

} TrackerUDPAnnounceResponse;
