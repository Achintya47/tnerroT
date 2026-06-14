# tnerroT

A BitTorrent metainfo parser written in C. Give it a `.torrent` file, get back the tracker URL, file name, size, piece length, and all SHA-1 piece hashes.

```
$ ./tnerroT big-buck-bunny.torrent

Torrent Information
-------------------
Name         : Big Buck Bunny
Announce     : udp://tracker.leechers-paradise.org:6969
Length       : 276445467
Piece Length : 262144
Pieces       : 1055
Hash[0] : 2020a7789d6f8b18623b2dca1de527df3f27230c
...
```

## Project Structure

```
.
├── include/
│   ├── btypes.h      # Core data types (BValue, BInt, BString, BList, BDict)
│   ├── bencoder.h    # Encoder / decoder declarations
│   └── torrent.h     # Torrent struct and parser declarations
├── src/
│   ├── btypes.c      # BValue constructors, list/dict operations, destroy
│   ├── bencoder.c    # Bencode encoder and decoder
│   ├── torrent.c     # .torrent metainfo parser and printer
│   └── main.c        # Entry point
├── test/
│   ├── test_utils.h      # Minimal CHECK / SECTION / SUMMARY test macros
│   ├── test_bencoder.c   # Unit + round-trip tests for bencoder
│   └── test_torrent.c    # Unit tests for the torrent parser
└── big-buck-bunny.torrent
```

## How It Works

`.torrent` files are encoded in **bencode** — a simple format with four types:

| Type       | Example              | Meaning            |
|------------|----------------------|--------------------|
| Integer    | `i42e`               | 42                 |
| String     | `4:spam`             | "spam"             |
| List       | `l4:spami1ee`        | ["spam", 1]        |
| Dictionary | `d3:key5:valuee`     | { "key": "value" } |

The parser reads the file in two passes:

1. **`bencoder.c`** decodes the raw bytes into a generic `BValue` tree.
2. **`torrent.c`** walks that tree and pulls out the known torrent fields (announce URL, name, length, pieces, etc.) into a typed `Torrent` struct.

All four bencode types share a single `BValue` container (a tagged union), so lists and dicts can hold mixed types without losing type information.

## Build

Requires GCC or Clang and `make` / `build-essential`.

```bash
gcc -Wall -Wextra -Iinclude src/bencoder.c src/btypes.c src/torrent.c src/main.c -o tnerroT
./tnerroT your-file.torrent
```

## Tests

```bash
# Bencoder tests
gcc -Iinclude -Itest src/bencoder.c src/btypes.c test/test_bencoder.c -o test_bencoder
./test_bencoder

# Torrent parser tests
gcc -Iinclude -Itest src/bencoder.c src/btypes.c src/torrent.c test/test_torrent.c -o test_torrent
./test_torrent big-buck-bunny.torrent
```

## CI

Every push and pull request runs:

- **Build matrix** — compiled with both GCC and Clang, in debug and release mode
- **Unit tests** — logic correctness
- **AddressSanitizer** — heap overflows, use-after-free, double-free
- **UndefinedBehaviorSanitizer** — integer overflow, null deref, bad casts
- **Valgrind** — memory leak detection

See `.github/workflows/ci.yml` for the full pipeline.

## Supported Torrent Fields

| Field          | Required | Notes                                   |
|----------------|----------|-----------------------------------------|
| `announce`     | yes      | Tracker URL                             |
| `name`         | yes      | Suggested filename                      |
| `length`       | yes*     | File size in bytes (single-file mode)   |
| `files`        | yes*     | File list with lengths (multi-file mode)|
| `piece length` | yes      | Bytes per piece                         |
| `pieces`       | yes      | Concatenated 20-byte SHA-1 hashes       |

*One of `length` or `files` must be present.

## References

- [BitTorrent Specification](https://wiki.theory.org/BitTorrentSpecification)
- [Bencode Format](https://wiki.theory.org/BitTorrentSpecification#Bencoding)