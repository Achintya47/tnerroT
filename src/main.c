#include <stdio.h>
#include <stdlib.h>

#include "btypes.h"
#include "torrent.h"
#include "bencoder.h"

char* read_file(const char* path, size_t* size_out) {
    FILE* fp = fopen(path, "rb");

    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char* buffer = malloc(size);

    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    if (fread(buffer, 1, size, fp) != (size_t)size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }

    fclose(fp);

    *size_out = (size_t)size;

    return buffer;
}

int main(int argc, char* argv[]) {

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <torrent-file>\n", argv[0]); 
        return EXIT_FAILURE;
    }
    size_t file_size;

    char* buffer =
        read_file(argv[1], &file_size);

    if (!buffer)
        return EXIT_FAILURE;

    Parser parser = {
        .data = buffer,
        .size = file_size,
        .pos = 0
    };

    BValue* root =
        decode_value(&parser);

    if (!root) {
        destroy_value(root);
        fprintf(stderr, "Failed to decode torrent file\n");
        return EXIT_FAILURE;
    }

    Torrent* torrent = torrent_parse(root);

    if (!torrent) {
        fprintf(stderr, "Failed to parse torrent metadata\n");
        destroy_value(root);
        free(buffer);
        return EXIT_FAILURE;
    }


    /* since w're now testing info_hash cretion, this shall be removed */
    // torrent_print(torrent);

    // connect_tracker(torrent);
    
    destroy_value(root);

    torrent_destroy(torrent);

    free(buffer);

    return 0;
}