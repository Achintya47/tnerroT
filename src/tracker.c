#include "tracker.h"
#include "torrent.h"
#include <stdlib.h>
#include<string.h>
#include<curl/curl.h>


typedef struct
{
    char *data;
    size_t size;
} ResponseBuffer;  /*would be using libcurl ... so need to store the trackers response*/


int tracker_get_peers(Torrent *torrent,Peer *peers,int max_peers){

    ResponseBuffer response;
    response.data = malloc(1);
    if (response.data == NULL) {
        fprintf(stderr, "Failed to allocate initial response buffer.\n");
        return -1;
    }
    response.size = 0;
    response.data[0] = '\0';




}

/*libcurl would call this function each time*/

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb; /*the no of bytes received from curl can b diff each time */
    ResponseBuffer *mem = (ResponseBuffer *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    /*expand the buffer dynamically keepin a null terminator in mind*/

    memcpy(&(mem->data[mem->size]), contents, realsize);

    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}


