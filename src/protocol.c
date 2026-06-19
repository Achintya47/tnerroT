#include "protocol.h"
#include<string.h>
#include<sys/socket.h> //need WSL

int bt_send_handshake(int sockfd, const unsigned char info_hash[20],const char peer_id[20]){
    
    BTHandshake hs;
    
    memset(&hs,0 , sizeof(hs)); //used to clear the buffer to zero
    hs.pstrlen = BT_PROTOCOL_LENGTH; //sets the fixed protocol length
    
    memcpy(hs.protocol, BT_PROTOCOL_STRING, BT_PROTOCOL_LENGTH); 
    memcpy(hs.info_hash,info_hash,20);
    memcpy(hs.peer_id, peer_id, 20); //copies the peer id

    ssize_t bytes_sent = send(sockfd, &hs, sizeof(hs), 0); //pushes the struct 


    if (bytes_sent != sizeof(hs)) {
        return -1; // Network failure or partial send occurred
    }

    return 0; // Success

}

int bt_receive_handshake(int sockfd, BTHandshake* hs) {
    if (hs == NULL) {
        return -1;
    }

    size_t total_received = 0;
    size_t handshake_size = sizeof(BTHandshake); // 68 bytes
    uint8_t* buffer = (uint8_t*)hs;

    // Loop until we read exactly 68 bytes
    while (total_received < handshake_size) {
        ssize_t bytes_left = handshake_size - total_received;
        ssize_t received = recv(sockfd, buffer + total_received, bytes_left, 0);

        if (received < 0) {
            // Error reading from socket (e.g., interrupted or connection issue)
            return -1; 
        } else if (received == 0) {
            // Peer closed the connection prematurely
            return -1; 
        }

        total_received += received;
    }

    return 0; // Success: The struct is fully populated
}



int bt_verify_handshake(const BTHandshake* hs, const unsigned char info_hash[20]) {
    if (hs == NULL || info_hash == NULL) {
        return -1;
    }

    // 1. Verify the protocol string length is 19
    if (hs->pstrlen != BT_PROTOCOL_LENGTH) {
        return -1;
    }

    // 2. Verify the protocol identifier string matches exactly
    if (memcmp(hs->protocol, BT_PROTOCOL_STRING, BT_PROTOCOL_LENGTH) != 0) {
        return -1;
    }

    // 3. Verify the info_hash matches the torrent we want
    if (memcmp(hs->info_hash, info_hash, 20) != 0) {
        return -1; 
    }
    
    //could potentially verify peer id , to check blacklisted or banned peers
    
    // If all checks pass, the handshake is valid
    return 0;
}