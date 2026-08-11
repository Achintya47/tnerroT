/**
 * @brief : Flushes the piece manager's single contiguous download buffer
 *          out to the file(s) described by the torrent's info-dictionary.
 *          Kept intentionally simple: one pass, whole-buffer writes, no
 *          incremental/streaming writes yet (that's a natural follow-up
 *          once endgame/streaming-to-disk is on the table).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_writer.h"

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

#define FW_PATH_MAX 1024

/* Creates every directory component in 'path' (best-effort — MKDIR on an
   already-existing directory just fails harmlessly, which we ignore). */
static void make_dirs_recursive(const char* path) {
    char buf[FW_PATH_MAX];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(buf))
        return;

    strcpy(buf, path);

    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/' || buf[i] == '\\') {
            char saved = buf[i];
            buf[i] = '\0';
            MKDIR(buf);
            buf[i] = saved;
        }
    }

    MKDIR(buf);
}

static int write_range(const char* filepath, const uint8_t* data, uint64_t length) {
    FILE* fp = fopen(filepath, "wb");

    if (!fp) {
        fprintf(stderr, "file_writer: failed to open '%s' for writing\n", filepath);
        return 0;
    }

    size_t written = fwrite(data, 1, (size_t)length, fp);
    fclose(fp);

    if (written != (size_t)length) {
        fprintf(stderr, "file_writer: short write on '%s' (%zu/%llu bytes)\n",
            filepath, written, (unsigned long long)length);
        return 0;
    }

    return 1;
}

int file_writer_save(const Torrent* torrent, const uint8_t* buffer,
    uint64_t total_length, const char* output_dir) {

    if (!torrent || !buffer || !output_dir)
        return 0;

    make_dirs_recursive(output_dir);

    if (torrent->num_files == 0) {
        /* single-file torrent: <output_dir>/<name> */
        char path[FW_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", output_dir, torrent->name);

        return write_range(path, buffer, total_length);
    }

    /* multi-file torrent: everything lives under <output_dir>/<name>/... */
    char root[FW_PATH_MAX];
    snprintf(root, sizeof(root), "%s/%s", output_dir, torrent->name);
    make_dirs_recursive(root);

    uint64_t offset = 0;

    for (uint64_t i = 0; i < torrent->num_files; i++) {
        const TorrentFile* tf = &torrent->files[i];

        if (offset + tf->length > total_length) {
            fprintf(stderr, "file_writer: file[%llu] exceeds buffer bounds\n",
                (unsigned long long)i);
            return 0;
        }

        char path[FW_PATH_MAX];
        strncpy(path, root, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';

        for (uint64_t j = 0; j < tf->path_count; j++) {
            strncat(path, "/", sizeof(path) - strlen(path) - 1);
            strncat(path, tf->path[j], sizeof(path) - strlen(path) - 1);

            /* every component except the final one (the filename) is a
               directory that needs to exist before we fopen() into it */
            if (j + 1 < tf->path_count)
                make_dirs_recursive(path);
        }

        if (!write_range(path, buffer + offset, tf->length))
            return 0;

        offset += tf->length;
    }

    return 1;
}
