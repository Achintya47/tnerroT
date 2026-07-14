# tnerroT

A BitTorrent client written in C — currently handles `.torrent` metainfo parsing and tracker
communication. Give it a `.torrent` file, and it decodes the metainfo, computes the info-hash,
announces to the tracker (HTTP or UDP), and prints back the peer list ready for downloading.

```
$ ./tnerroT linux-lite-8.0-64bit.iso.torrent

Torrent Information
-------------------
Name         : linux-lite-8.0-64bit.iso
Announce     : udp://tracker.opentrackr.org:1337/announce
Length       : 2533359616
Piece Length : 262144
Pieces       : 9664
Hash[0] : 2020a7789d6f8b18623b2dca1de527df3f27230c
...
Hash[9663] : d34f7c9fd57ba5f1c3243a41984fffc1f7d25f07

connect_tracker: udp connect attempt 1/3 (waiting up to 5s for a reply)...
connect_tracker: udp announce attempt 1/3 (waiting up to 5s for a reply)...
Tracker OK: interval=1800, seeders=210, leechers=14, peers=61
  peer[0] 45.132.16.211:51413
  peer[1] 88.99.2.42:6881
  ...
```

Downloading the actual file data from peers (the peer wire protocol) isn't implemented yet —
see [Roadmap](#roadmap).

## Project Structure

```
.
├── include/
│   ├── btypes.h              # Core data types (BValue, BInt, BString, BList, BDict)
│   ├── bencoder.h             # Encoder / decoder declarations
│   ├── torrent.h               # Torrent struct and parser declarations
│   ├── sha1.h                    # SHA-1 implementation (FIPS 180-4), used for the info-hash
│   ├── RFC1738_url_parser.h    # Announce-URL splitting (host/port/path) + info-hash URL-encoding
│   └── tracker.h              # HTTP/UDP tracker request & response types
├── src/
│   ├── btypes.c              # BValue constructors, list/dict operations, destroy
│   ├── bencoder.c             # Bencode encoder and decoder
│   ├── torrent.c               # .torrent metainfo parser, info-hash calculation, printer
│   ├── sha1.c                    # SHA-1 implementation
│   ├── RFC1738_url_parser.c    # Announce-URL splitting + %-hex encoding
│   ├── tracker.c              # Tracker client: HTTP GET (BEP 3) and UDP (BEP 15) announces
│   └── main.c                # Entry point
├── test/
│   ├── test_utils.h      # Minimal CHECK / SECTION / SUMMARY test macros
│   ├── test_bencoder.c   # Unit + round-trip tests for bencoder
│   └── test_torrent.c    # Unit tests for the torrent parser
└── big-buck-bunny.torrent
```

## How It Works

### 1. Bencode decoding

`.torrent` files are encoded in **bencode** — a simple format with four types:

| Type       | Example              | Meaning            |
|------------|-----------------------|--------------------|
| Integer    | `i42e`                | 42                 |
| String     | `4:spam`               | "spam"             |
| List       | `l4:spami1ee`           | ["spam", 1]        |
| Dictionary | `d3:key5:valuee`        | { "key": "value" } |

- **`bencoder.c`** decodes the raw bytes into a generic `BValue` tree.
- **`torrent.c`** walks that tree and pulls out the known torrent fields (announce URL, name,
  length, pieces, etc.) into a typed `Torrent` struct.

All four bencode types share a single `BValue` container (a tagged union), so lists and dicts
can hold mixed types without losing type information.

### 2. Info-hash calculation

The tracker identifies a torrent by the SHA-1 hash of its **exact original bencoded `info`
dictionary bytes** — not a re-encoding of the parsed struct, since that could reorder keys or
change whitespace and produce a different hash. To get this right, `decode_value()` records the
`encoded_begin`/`encoded_end` byte offsets of every value as it parses, so `torrent.c` can hash
the *original* slice of the file directly via `sha1.c`.

### 3. Tracker communication

Once the info-hash and announce URL are known, `tracker.c`'s `connect_tracker()` dispatches on
the announce URL's scheme:

- **`http://`** — builds a `GET /announce?info_hash=...&peer_id=...&...` request (info-hash and
  peer-id URL-encoded via `RFC1738_url_parser.c`), sends it over a TCP socket, and decodes the
  bencoded response body with the same `bencoder.c` used for the `.torrent` file itself.
- **`udp://`** — implements [BEP 15](https://www.bittorrent.org/beps/bep_0015.html)'s binary
  connect/announce protocol over a UDP socket: a connect handshake to obtain a `connection_id`
  (anti-spoofing), followed by an announce request, both serialized by hand into big-endian byte
  buffers (see `put_u16/32/64` / `get_u32/64` in `tracker.c`) rather than relying on struct
  layout, which isn't reliably endianness- or padding-safe across platforms/compilers.

Both paths normalize their result into the same `TrackerHTTPGetResponse` struct — interval,
seeder/leecher counts, and a compact peer list — so the rest of the program doesn't need to
know which transport was actually used.

## Build

Requires GCC or Clang and `make` / `build-essential`.

**Metainfo parser only** (portable — Linux/macOS/Windows):

```bash
gcc -Wall -Wextra -Iinclude src/bencoder.c src/btypes.c src/torrent.c src/sha1.c -o parse_test
```

**Full client, including tracker announce (Windows only — uses WinSock):**

```bash
gcc -Wall -Wextra -Iinclude src/bencoder.c src/btypes.c src/torrent.c src/sha1.c \
    src/RFC1738_url_parser.c src/tracker.c src/main.c -o tnerroT.exe -lws2_32
./tnerroT.exe your-file.torrent
```

> **Note:** `tracker.c` and `main.c` currently depend on WinSock (`WinSock2.h`) and only build
> on Windows / MinGW. The bencoder and torrent parser have no such dependency and build
> anywhere — that's why the test suite below only links those.

## Tests

```bash
# Bencoder tests
gcc -Iinclude -Itest src/bencoder.c src/btypes.c test/test_bencoder.c -o test_bencoder
./test_bencoder

# Torrent parser tests
gcc -Iinclude -Itest src/bencoder.c src/btypes.c src/torrent.c src/sha1.c test/test_torrent.c -o test_torrent
./test_torrent big-buck-bunny.torrent
```

## CI

Every push and pull request runs:

- **Build matrix** — compiled with both GCC and Clang, in debug and release mode
- **Unit tests** — logic correctness
- **AddressSanitizer** — heap overflows, use-after-free, double-free
- **UndefinedBehaviorSanitizer** — integer overflow, null deref, bad casts
- **Valgrind** — memory leak detection

This covers the bencoder and torrent parser, which are platform-independent. The tracker client
(WinSock-based) isn't part of this matrix yet since it only builds on Windows.

See `.github/workflows/ci.yml` for the full pipeline.

## Supported Torrent Fields

| Field          | Required | Notes                                     |
|----------------|----------|--------------------------------------------|
| `announce`     | yes      | Tracker URL (`http://` or `udp://`)         |
| `name`         | yes      | Suggested filename                          |
| `length`       | yes*     | File size in bytes (single-file mode)       |
| `files`        | yes*     | File list with lengths (multi-file mode)    |
| `piece length` | yes      | Bytes per piece                             |
| `pieces`       | yes      | Concatenated 20-byte SHA-1 hashes            |

*One of `length` or `files` must be present.

## Roadmap

- [x] Bencode encoder/decoder
- [x] `.torrent` metainfo parsing
- [x] Info-hash calculation (SHA-1)
- [x] HTTP tracker announce (BEP 3)
- [x] UDP tracker announce (BEP 15)
- [ ] Peer wire protocol (handshake, message framing, piece requests)
- [ ] Piece downloading, verification, and file assembly
- [ ] Parallel downloads across multiple peers
- [ ] `announce-list` fallback (try backup trackers if the primary fails)

## References

- [BitTorrent Specification](https://wiki.theory.org/BitTorrentSpecification)
- [Bencode Format](https://wiki.theory.org/BitTorrentSpecification#Bencoding)
- [BEP 3 — The BitTorrent Protocol Specification](https://www.bittorrent.org/beps/bep_0003.html)
- [BEP 15 — UDP Tracker Protocol](https://www.bittorrent.org/beps/bep_0015.html)
- [RFC 1738 — Uniform Resource Locators](https://www.rfc-editor.org/rfc/rfc1738)