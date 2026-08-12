#ifndef FILE_WRITER_H
#define FILE_WRITER_H

#include <stdint.h>
#include "torrent.h"

/**
 * @brief Writes a fully-downloaded, contiguous in-memory buffer out to
 *        disk according to the torrent's file layout.
 *
 * Single-file torrents (torrent->num_files == 0) are written as
 * "<output_dir>/<torrent->name>".
 *
 * Multi-file torrents are written under "<output_dir>/<torrent->name>/",
 * recreating each file's path components (directories are created as
 * needed), with each file's bytes taken from consecutive offsets of
 * 'buffer' in the order torrent->files[] lists them — this matches how
 * the "length"/"files" fields are concatenated to form the piece stream
 * per the BitTorrent info-dictionary spec.
 *
 * @return 1 on success, 0 on any I/O failure.
 */
int file_writer_save(const Torrent* torrent, const uint8_t* buffer,
    uint64_t total_length, const char* output_dir);

#endif
