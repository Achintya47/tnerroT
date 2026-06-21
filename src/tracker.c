#include<string.h>
#include "tracker.h"
#include<stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>

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
    
}