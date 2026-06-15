#include <stdio.h>
#include <stdlib.h>

#include "btypes.h"
#include "torrent.h"
#include "bencoder.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <torrent-file>\n", argv[0]); 
        return EXIT_FAILURE;
    }
    
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("fopen");
        return EXIT_FAILURE;
    }
    
    BValue* root = decode_value(fp);

    fclose(fp);

    if (!root) {
        destroy_value(root);
        fprintf(stderr, "Failed to decode torrent file\n");
        return EXIT_FAILURE;
    }

    Torrent* torrent = torrent_parse(root);

    if (!torrent) {
        destroy_value(root);
        torrent_destroy(torrent);
        fprintf(stderr, "Failed to parse torrent metadata\n");
    }

    torrent_print(torrent);
    
    destroy_value(root);
    torrent_destroy(torrent);

    return 0;
}