#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "torrent.h"

char* tracker_request_string(Torrent* torrent) {
    char request[4096];

    snprintf(
        request,
        sizeof(request),
        "GET %s?info_hash=%s"
        "&peer_id=%s"
        "&port=%d"
        "&uploaded=%llu"
        "&downloaded=%llu"
        "&left=%llu"
        "&compact=1"
        "&event=started"
        " HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, /* After the host, the path will be provided, ususally /announce */
        info_hash, /* URL-Encode this (RFC1738) */
        peer_id, /* Randomly Generated, let's keep it hardcoded */
        port, /* Our port, 6881(for torrent) */
        uploaded, /* 0 */
        downloaded, /* 0 */
        left, /* torrent -> length */
        host /* tracker.example.com */
    );

}

int main() {
    
    /* contains the host name, aliases, mainly the h_addr_list*/
    struct hostent* host = gethostbyname("example.com");

    if (!host) {
        perror("Socket Failed");
        return 1;
    }

    /* AF_INET : IPv4, SOCK_STREAM : TCP, 0 : default -> TCP */
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return 1;
    }

    /* Contains the port, and IP Address */
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;

    /* Network byte order is big-endian, our machine is little endian */
    /* htons : host to network short */
    server.sin_port = htons(80);

    memcpy(&server.sin_addr, 
        host->h_addr_list[0], 
        host->h_length);
    
    if (connect(sock, (struct sockaddr*)&server,sizeof(server)) < 0) {
        perror("Connect");
        return 1;
    }

    const char* request = 
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n";
    
    send(sock, request, strlen(request), 0);

    char buffer[4096];
    ssize_t bytes;

    while((bytes = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    close(sock);
    return 0;

}