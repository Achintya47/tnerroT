#ifndef PEER_H
#define PEER_H

#include <stdint.h>

typedef struct {

    char ip[64];

    uint16_t port;

} Peer;


int peer_connect(const Peer* peer);/*Create socket, Connect socket ,Return socket descriptor*/

void peer_disconnect(int sockfd); //closes the socket

#endif