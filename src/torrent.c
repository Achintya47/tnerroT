#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "btypes.h"
#include "torrent.h"
#include "sha1.h"


#define FAIL(msg) do { \
    fprintf(stderr, "torrent_parse: %s\n", msg); \
    return NULL; \
} while (0)


/**
 * @brief   : Helper function to simply copy bencoded strings. They might
 *           not also be null terminated
 */
static char* copy_bstring(BValue* str) {
    if (!str || str->type != BSTRING)
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

    /* This is the reason we've shifted from FILE* to char*, the whole torrent file now lives
    inside the buffer with us, 'LOL' */
    BValue *info =
        dict_get(root, "info", 4);

    if (!announce || announce->type != BSTRING ||
        !info || info->type != BDICT)
    {
        free(torrent);
        FAIL("Failed to fetch announce or info field, or wrong type");
    }

    calculate_info_hash(info->encoded_begin, info->encoded_end,
        torrent->info_hash);
    
    /* for debugging, remove later */
    printf("Info Hash: ");

    for (int i = 0; i < 20; i++)
        printf("%02x", torrent->info_hash[i]);

    printf("\n");

    /* DELETE PLEASE */

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

    if (!name || name->type != BSTRING ||
        !piece_length || piece_length->type != BINT ||
        !pieces || pieces->type != BSTRING) {
        torrent_destroy(torrent);
        FAIL("Missing name, piece_length or pieces field, or wrong type");
    }

    if (length && length->type == BINT) {
        torrent->length =
            length->value.integer.value;
    }
    else if (files && files->type == BLIST) {

        BList* list = &files->value.list;

        torrent->num_files = list->count;
        torrent->files = calloc(list->count, sizeof(TorrentFile));
            
        uint64_t total = 0;

        for (int i = 0; i < list->count; i++) {
            BValue* file_dict = list->items[i];

            if (!file_dict || file_dict->type != BDICT)
                continue;

            BValue* file_length =
                dict_get(file_dict, "length", 6);
            
            BValue* path = 
                dict_get(file_dict, "path", 4);
            
            if (!file_length || file_length->type != BINT ||
                !path || path->type != BLIST)
                continue;

            TorrentFile* tf = &torrent->files[i];

            tf->length = file_length->value.integer.value;
            total += tf->length;

            BList* path_list = &path->value.list;

            tf->path_count = path_list->count;
            tf->path = calloc(path_list->count, sizeof(char*));

            for (int j = 0; j < path_list->count; j++) {
                BValue* component = path_list->items[j];

                tf->path[j] = copy_bstring(component);
            }

            BValue* md5 =
                dict_get(file_dict, "md5sum", 6);

            if (md5 && md5->type == BSTRING)
                tf->md5sum = copy_bstring(md5);

        }

        torrent->length = total;
    }
    else {
        torrent_destroy(torrent);
        FAIL("Neither length nor files present, or wrong type");
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


void calculate_info_hash(const char* start,const char* end,
    unsigned char digest[20]) {

        if (!start || !end || end < start)
            return;

        size_t len = (size_t)(end - start);

        SHA1_CTX ctx;

        sha1_init(&ctx);
        sha1_update(
            &ctx,
            (const unsigned char*)start,
            len
        );
        sha1_final(&ctx, digest);
}


void torrent_destroy(Torrent *torrent) {
    if (!torrent)
        return;

    free(torrent->announce);

    free(torrent->name);

    free(torrent->pieces);

    if (torrent->files) {

        for (uint64_t i = 0; i < torrent->num_files; i++) {

            TorrentFile* tf = &torrent->files[i];

            if (tf->path) {

                for (uint64_t j = 0; j < tf->path_count; j++)
                    free(tf->path[j]);

                free(tf->path);
            }

            /* free(NULL) is safe */
            free(tf->md5sum);
        }

        free(torrent->files);
    }

    free(torrent->comment);
    free(torrent->created_by);
    free(torrent->encoding);

    if (torrent->announce_list) {
        /* free individual announce URLs when implemented */
    }

    free(torrent);
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