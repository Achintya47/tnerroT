#include "include/btypes.h"
#include "include/torrent.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#define FAIL(msg) do { \
    fprintf(stderr, "torrent_parse: %s\n", msg); \
    return NULL; \
} while (0)


/**
 * @brief   : Helper function to simply copy bencoded strings. They might
 *           not also be null terminated
 */
static char* copy_bstring(BValue* str) {
    if (!str)
        return NULL;

    char* copy = malloc(str->value.string.length + 1);

    memcpy(
        copy, 
        str->value.string.data,
        str->value.string.length
    );

    copy[str->value.string.length] = '\0';

    return copy;
}


Torrent *torrent_parse(BValue *root) {
    if (!root || root->type != BDICT)
        return NULL;

    Torrent *torrent = calloc(1, sizeof(Torrent));

    if (!torrent)
        FAIL("Torrent calloc Failed");

    BValue *announce =
        dict_get(root, "announce", 8);

    BValue *info =
        dict_get(root, "info", 4);

    if (!announce || !info)
    {
        free(torrent);
        FAIL("Failed to fetch announce or info field");
    }

    torrent->announce =
        copy_bstring(announce);

    BValue *name =
        dict_get(info, "name", 4);

    BValue *length =
        dict_get(info, "length", 6);

    BValue *files =
        dict_get(info, "files", 5);

    BValue *piece_length =
        dict_get(info, "piece length", 12);

    BValue *pieces =
        dict_get(info, "pieces", 6);

    if (!name || !piece_length || !pieces) {
        torrent_destroy(torrent);
        FAIL("Failed to fetch name, length, piece_length or pieces field");
    }

    if (length) {
        torrent->length =
            length->value.integer.value;
    }
    else if (files) {
        uint64_t total = 0;

        BList* list = &files->value.list;

        for (int i = 0; i < list->count; i++) {
            BValue* file_dict = list->items[i];

            BValue* file_length =
                dict_get(file_dict, "length", 6);

            if (file_length)
                total += file_length->value.integer.value;
        }

        torrent->length = total;
    }
    else {
        torrent_destroy(torrent);
        FAIL("Neither length nor files present");
    }


    if (pieces->value.string.length % 20 != 0) {
        torrent_destroy(torrent);
        FAIL("Number of Pieces aren't divisible by 20");
    }

    int piece_count = pieces->value.string.length / 20;

    torrent->num_pieces = piece_count;

    torrent->name =
        copy_bstring(name);

    torrent->piece_length =
        piece_length->value.integer.value;

    torrent->pieces =
        malloc(sizeof(PieceHash) * piece_count);

    if (!torrent->pieces) {
        torrent_destroy(torrent);
        FAIL("Failed to allocate pieces");
    }

    memcpy(
        torrent->pieces,
        pieces->value.string.data,
        pieces->value.string.length
    );

    return torrent;
}

static void print_piece_hash(const PieceHash* hash) {
    
}

void torrent_print(const Torrent* torrent) {
    if (!torrent)
        return;

    printf("\n");
    printf("Torrent Information\n");
    printf("-------------------\n");

    printf("Name         : %s\n",
           torrent->name);

    printf("Announce     : %s\n",
           torrent->announce);

    printf("Length       : %llu\n",
           (unsigned long long)
           torrent->length);

    printf("Piece Length : %llu\n",
           (unsigned long long)
           torrent->piece_length);

    printf("Pieces       : %zu\n", torrent->num_pieces);

    for (size_t i = 0; i < torrent->num_pieces; i++) {
        printf("Hash[%zu] : ", i);

        for (int j = 0; j < 20; j++)
            printf("%02x", torrent->pieces[i].hash[j]);

        printf("\n");
    }

    printf("\n");   
}

void torrent_destroy(Torrent *torrent) {
    if (!torrent)
        return;

    free(torrent->announce);

    free(torrent->name);

    free(torrent->pieces);

    free(torrent);
}