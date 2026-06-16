/**
 * @brief   : This is the first Torrent-specific layer, that will be
 *          hard coded according to the torrent-protocol. By hard coding I mean, 
 *          the info-hash contents, etc will now be added
 * @cite    : https://wiki.theory.org/BitTorrentSpecification#Info_Dictionary
 */

#ifndef TORRENT_H
#define TORRENT_H

#include <stdint.h>
#include "btypes.h"


// Allocated as : torrent.piece_length / 20 and thus, piece hash
// is easily accessed as torrent.pieces.hash[i]
typedef struct {
    unsigned char hash[20];
} PieceHash;

typedef struct {
    // Length of the file in bytes
    uint64_t length;

    // a list containing one or more string elements that together 
    // represent the path and filename. Each element in the list corresponds 
    // to either a directory name or (in the case of the final element) the filename.
    // For example, a the file "dir1/dir2/file.ext" would consist of three string 
    // elements: "dir1", "dir2", and "file.ext".
    char** path;
    // Number of path components
    uint64_t path_count;

    /* OPTIONAL PARAMETERS */
    char* md5sum;

} TorrentFile;


typedef struct {

    // The announce URL of the tracker
    char* announce;

    // Number of bytes in each piece 
    uint64_t piece_length;

    // String consisting of the concatenation of all 20-byte SHA1 hash values, one per piece
    PieceHash* pieces;

    // The filename
    char* name;

    // Length of the file in bytes
    uint64_t length;

    // Multi-File Mode (More common)
    TorrentFile* files;
    uint64_t num_files;

    /* EXTRA ADDED PARAMETERS BY ME */

    uint64_t num_pieces;

    /* OPTIONAL PARAMETERS */

    // This is an extention to the official specification, 
    // offering backwards-compatibility
    char** announce_list;

    // The creation time of the torrent, in standard UNIX epoch format 
    // (integer, seconds since 1-Jan-1970 00:00:00 UTC)
    long long creation_date;
    
    // Free-form textual comments of the author (string)
    char* comment;

    // Name and version of the program used to create the .torrent (string)
    char* created_by;

    // The string encoding format used to generate the pieces part of the info 
    // dictionary in the .torrent metafile
    char* encoding;

    // This field is an integer. If it is set to "1", the client MUST publish its presence 
    // to get other peers ONLY via the trackers explicitly described in the metainfo file. 
    // If this field is set to "0" or is not present, the client may obtain peer from other means, 
    // e.g. PEX peer exchange, dht. Here, "private" may be read as "no external peer source".
    uint64_t is_private; 

} Torrent;

Torrent* torrent_parse(BValue* root);
void torrent_destroy(Torrent *torrent);
void torrent_print(const Torrent* torrent);


#endif