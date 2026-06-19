#include "protocol.h"
#include<string.h>
#include<sys/socket.h> //need WSL

int bt_send_handshake(int sockfd, const unsigned char info_hash[20],const char peer_id[20]){
    
    BTHandshake hs;
    
    memset(&hs,0 , sizeof(hs)); //used to clear the buffer to zero
    hs.pstrlen = BT_PROTOCOL_LENGTH //sets the fixed protocol length
    
    memcpy(hs.protocol, BT_PROTOCOL_STRING, BT_PROTOCOL_LENGTH); //copies the memory directly includin null terminator
    memcpy(hs.peer_id, peer_id, 20); //copies the peer id

    ssize_t bytes_sent = send(sockfd, &hs, sizeof(hs), 0); //pushes the struct 


    if (bytes_sent != sizeof(hs)) {
        return -1; // Network failure or partial send occurred
    }

    return 0; // Success

}