#include <stdio.h>
#include <stdlib.h>

#include "include/btypes.h"
#include "include/torrent.h"

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
        fprintf(stderr, "Failed to decode torrent file\n");
        return EXIT_FAILURE;
    }

    Torrent* torrent = torrent_parse(root);

    if (!torrent) {
        fprintf(stderr, "Failed to parse torrent metadata\n");
    }
}