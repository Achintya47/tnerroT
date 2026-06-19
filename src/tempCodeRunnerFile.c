#include <arpa/inet.h> //required to convert ip to binary
#include <sys/socket.h>
#include <unistd.h>
#include "peer.h"


int peer_connect(const Peer* peer) //returns the socket descriptor
                                    /*stdin  = 0
                                    stdout = 1
                                    stderr = 2
                                    socket = 3*/
{
    int sockfd;

    struct sockaddr_in addr;

    sockfd =
        socket(AF_INET,SOCK_STREAM,0);  //tcp            //function returns the file descriptor

    if (sockfd < 0)
        return -1;

    addr.sin_family = AF_INET;

    addr.sin_port = htons(peer->port); /*htons is used to correctly switch between little nd big endian
                                            Routers nd Network devices work with Big Endian*/

    if (inet_pton(AF_INET,peer->ip,&addr.sin_addr) <= 0)       //inet_pton handles conversion of ip to bytes so that the kernel can work with it
    {
        close(sockfd);
        return -1;
    }

    if (connect(sockfd,(struct sockaddr*)&addr,sizeof(addr)) < 0) //intiaties the handshake
    {
        close(sockfd);
        return -1;
    }

    return sockfd;
}


void peer_disconnect(int sockfd)
{
    close(sockfd);
}