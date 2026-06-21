#include<string.h>
#include "tracker.h"
#include<stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

// Helper function to URL-encode the 20-byte binary arrays (info_hash and peer_id)
void url_encode(const unsigned char* input, int len, char* output) {
    char hex[] = "0123456789ABCDEF";
    int pos = 0;
    for (int i = 0; i < len; i++) {
        output[pos++] = '%';
        output[pos++] = hex[input[i] >> 4];
        output[pos++] = hex[input[i] & 0x0F];
    }
    output[pos] = '\0';
}

const char* locate_bencoded_compact_peers(const char* bencoded_data, int* out_len) {
    // Look for the "5:peers" dictionary key
    const char* peers_key = strstr(bencoded_data, "5:peers");
    if (!peers_key) return NULL;


    const char* length_ptr = peers_key + 7; // Skip past "5:peers"
    int string_len = atoi(length_ptr);
    
    const char* colon = strchr(length_ptr, ':');
    if (!colon) return NULL;
    
    *out_len = string_len;
    return colon + 1; // Return the exact start of the binary peer strings
}

Peer* handle_http_tracker(const char* host, int port, Torrent* torrent, int* out_peer_count) {
    *out_peer_count = 0;

    TrackerHTTPGetRequest req;
    req.host = (char*)host;
    req.path = "/announce"; // Default BitTorrent fallback path
    memcpy(req.info_hash, torrent->info_hash, 20);
    memset(req.peer_id, 'X', 20); // Static peer ID configuration
    req.port = 6881;
    req.uploaded = 0;
    req.downloaded = 0;
    req.left = torrent->length;
    req.compact = 1;

    // 2. Perform DNS Lookup
    struct hostent* server = gethostbyname(req.host);
    if (!server) {
        perror("HTTP Tracker DNS failed");
        return NULL;
    }

    // 3. Create TCP Stream Socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    // Set network timeout guard
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("HTTP Tracker Connection failed");
        close(sock);
        return NULL;
    }

    // 4. URL-encode parameters
    char encoded_hash[61];
    char encoded_peer_id[61];
    url_encode(req.info_hash, 20, encoded_hash);
    url_encode(req.peer_id, 20, encoded_peer_id);

    // 5. Build raw HTTP Request using struct values
    char request_buffer[4096];
    snprintf(request_buffer, sizeof(request_buffer),
        "GET %s?info_hash=%s"
        "&peer_id=%s"
        "&port=%d"
        "&uploaded=%llu"
        "&downloaded=%llu"
        "&left=%llu"
        "&compact=%d"
        "&event=started"
        " HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n",
        req.path, encoded_hash, encoded_peer_id, req.port, 
        (unsigned long long)req.uploaded, (unsigned long long)req.downloaded, 
        (unsigned long long)req.left, req.compact, req.host, port
    );

    // 6. Send across TCP Socket
    send(sock, request_buffer, strlen(request_buffer), 0);

    // 7. Dynamic Read Buffer Loop
    uint8_t response_buf[8192];
    int total_bytes = 0;
    int bytes_read;
    while ((bytes_read = recv(sock, response_buf + total_bytes, sizeof(response_buf) - total_bytes - 1, 0)) > 0) {
        total_bytes += bytes_read;
    }
    response_buf[total_bytes] = '\0';
    close(sock);

    // 8. Separate HTTP Headers from Bencoded Payload Body
    char* bencoded_body = strstr((char*)response_buf, "\r\n\r\n");
    if (!bencoded_body) return NULL;
    bencoded_body += 4; // Advance past headers to body layer

    // 9. Parse using Bencode extractor
    int binary_peers_len = 0;
    const char* raw_peer_bytes = locate_bencoded_compact_peers(bencoded_body, &binary_peers_len);
    if (!raw_peer_bytes || binary_peers_len == 0) {
        return NULL;
    }

    // 10. Map back into  exact TrackerHTTPGetResponse Layout
    TrackerHTTPGetResponse res;
    res.failure_reason = NULL;
    res.warning_message = NULL;
    res.interval = 1800; // Trackers usually fallback to 30 mins if reading fails
    res.num_peers = binary_peers_len / 6;
    *out_peer_count = res.num_peers;

    // Allocate memory into exact Peer structure setup
    res.peers = malloc(res.num_peers * sizeof(Peer));
    if (!res.peers) return NULL;

    for (int i = 0; i < res.num_peers; i++) {
        // Step forward 6 bytes consecutively (4 byte IP + 2 byte Port)
        memcpy(&res.peers[i].ip, raw_peer_bytes + (i * 6), 4);
        memcpy(&res.peers[i].port, raw_peer_bytes + (i * 6) + 4, 2);
    }

    return res.peers;
}


//URL parser
TrackerType parse_tracker_url(const char* url,char* host,int* port,char* path){

    if(strncmp(url,"udp://",6 )==0){
        return TRACKER_UDP;
    }

    if(strncmp(url,"http://",7)==0){
        return TRACKER_HTTP;
    }

    return TRACKER_HTTPS;

}

Peer* handle_udp_tracker(const char* host, int port, Torrent* torrent, int* out_peer_count){
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    //settin a timeout, so that program doesnt wait even after UDP drops

    struct timeval tv; //struct for time elapsed
    tv.tv_sec=5;
    tv.tev_usec= 0;

    // Resolve host address using getaddrinfo()
    struct sockaddr_in server_addr;
    // setup server_addr using host and port

    //  CONNECT 
    TrackerUDPConnectRequest conn_req;
    conn_req.protocol_id = htobe64(0x41727101980U); // Magic constant , hardcoded in BitTorrent,to identify valid tracker req
    conn_req.action = htonl(0); // 0 = Connect
    conn_req.transaction_id = htonl(rand());

    sendto(sock, &conn_req, sizeof(conn_req), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    TrackerUDPConnectResponse conn_res;
    socklen_t addr_len = sizeof(server_addr);
    if (recvfrom(sock, &conn_res, sizeof(conn_res), 0, (struct sockaddr*)&server_addr, &addr_len) < 0) {
        // Handle timeout / error
        close(sock);
        return NULL;
    }

    // ANNOUNCE
    TrackerUDPAnnounceRequest ann_req;
    ann_req.connection_id = conn_res.connection_id; // Already Big Endian from server
    ann_req.action = htonl(1); // 1 = Announce
    ann_req.transaction_id = htonl(rand());
    memcpy(ann_req.info_hash, torrent->info_hash, 20); // Your 20 byte hash from torrent.h
    memset(ann_req.peer_id, 'X', 20); // Hardcoded peer id for now
    ann_req.downloaded = htobe64(0); //htobe64 => host-to-big-endian-64bit
    ann_req.left = htobe64(torrent->length);
    ann_req.uploaded = htobe64(0);
    ann_req.event = htonl(0); // 0: none; 1: completed; 2: started; 3: stopped
    ann_req.ip_address = htonl(0); // Default internal
    ann_req.key = htonl(rand());
    ann_req.num_want = htonl(-1); // Default number of peers (-1)
    ann_req.port = htons(6881);

    sendto(sock, &ann_req, sizeof(ann_req), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    // Read response into a buffer dynamically sized for compact peers
    uint8_t raw_response[2048];
    int bytes_received = recvfrom(sock, raw_response, sizeof(raw_response), 0, NULL, NULL);
    
    // Parse peers out of raw_response (skipping the 20-byte UDP announce header)
    TrackerUDPAnnounceResponse* ann_res = (TrackerUDPAnnounceResponse*)raw_response;
    int num_peers = (bytes_received - 20) / 6; // subtracts 20 byte header , each peer use 6 bytes
    *out_peer_count = num_peers;

    Peer* peer_list = malloc(num_peers * sizeof(Peer));
    for(int i = 0; i < num_peers; i++) {
        
        memcpy(&peer_list[i].ip, &ann_res->peers[i].ip, 4);
        memcpy(&peer_list[i].port, &ann_res->peers[i].port, 2);
    }

    close(sock);
    return peer_list;
}