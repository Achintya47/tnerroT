#ifndef TRACKER_H /*include guards*/
#define TRACKER_H

/*basic workin is the client sends GET request with info_hash nd peer id 
tracker looks who else has this info_hash
gets replied with the list of bencoded ports and ips

and finally parses into the array of peers*/

#include<stdint.h>

typedef struct{
    char ip[16];
    uint16_t port;
}Peer;

int get_peers(
    const char *announce_url, /*web address of the tracker*/
    unsigned char info_hash[20], /*unique sha1 code for the specfic torrent being searched*/
    const char *peer_id, /*20 byte identification id*/
    uint64_t left,/*no of bytes left to b downloaded*/
    Peer *peers,
    int max_peers
);
)

#endif