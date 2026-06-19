#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define BT_PROTOCOL_LENGTH 19

#define BT_PROTOCOL_STRING "BitTorrent protocol"       //The very first thing a BitTorrent peer expects to see is the literal string "BitTorrent protocol". 

typedef struct {
 
    uint8_t pstrlen; //PROTOCOL INDENTIFIER

    char protocol[19];

    /*These are bits masks which basically works like toggle buttons
    for DHT support byte 7 is toggled
    for transfering meta data 5th bit is toggled*/
    uint8_t reserved[8]; //Used for extension 
    

    uint8_t info_hash[20];

    uint8_t peer_id[20];

} BTHandshake;


// parse -> peer.connect() -> send handshake -> receive_handshake -> verify_handshake

int bt_send_handshake(int sockfd, const unsigned char info_hash[20],const char peer_id[20]);

int bt_receive_handshake(int sockfd,BTHandshake* hs);

int bt_verify_handshake(const BTHandshake* hs,const unsigned char info_hash[20]);

#endif