# tnerroT — A BitTorrent Client Built From Scratch in C

A from-scratch, dependency-free implementation of the core BitTorrent protocol in C: bencoding, `.torrent` parsing, HTTP/UDP tracker communication, the peer wire protocol, and a thread-per-peer concurrent downloader. No libtorrent, no libcurl — just sockets, threads, and the spec.

This started as a way to actually understand BitTorrent instead of just using it: how a `.torrent` file becomes a SHA1-verified pile of bytes on disk, what a tracker really does, and what it takes to talk to sixty peers at once without either a race condition or a deadlock. It's a personal systems-programming project, built incrementally, and this README tries to be honest about what's solid and what's still missing rather than presenting it as more finished than it is.

---

## Table of Contents

1. [The Idea](#the-idea)
2. [Architecture at a Glance](#architecture-at-a-glance)
3. [Project Structure](#project-structure)
4. [File-by-File Breakdown](#file-by-file-breakdown)
   - [Bencoding & Data Types](#bencoding--data-types)
   - [URL Parsing](#url-parsing)
   - [SHA1](#sha1)
   - [Torrent Metadata](#torrent-metadata)
   - [Tracker Communication](#tracker-communication)
   - [Peer Wire Protocol](#peer-wire-protocol)
   - [Piece Manager](#piece-manager)
   - [File Writer](#file-writer)
   - [Downloader (Concurrency)](#downloader-concurrency)
   - [Logger](#logger)
   - [main.c](#mainc)
5. [Building and Running](#building-and-running)
6. [Usage](#usage)
7. [Current Capabilities](#current-capabilities)
8. [Known Limitations & Roadmap](#known-limitations--roadmap)
   - [Magnet links & block-level thread ownership](#a-magnet-links--block-level-thread-ownership)
   - [No resume support (the big one)](#b-no-resume-support-the-big-one)
   - [A terminal UI](#c-a-terminal-ui)
   - [Cross-platform sockets](#d-cross-platform-sockets)
9. [Closing Notes](#closing-notes)

---

## The Idea

BitTorrent looks simple from the outside — you open a `.torrent` file and a download starts — but it's actually four or five separate protocols stacked on top of each other, each with its own quirks:

- **Bencoding**, a terse, self-describing serialization format (think a much stricter JSON) used for both `.torrent` files and tracker responses.
- **The info-dictionary**, a bencoded structure inside the `.torrent` whose SHA1 hash *is* the torrent's identity (the "info hash") — get the byte-for-byte re-encoding wrong and you're a different swarm.
- **Tracker announces**, over either plain HTTP (a GET request, bencoded response) or a surprisingly fiddly binary UDP protocol (BEP 15) with its own connect/announce handshake and retry backoff.
- **The peer wire protocol**, a length-prefixed binary message protocol for actually exchanging pieces of the file with other peers.
- **Piece selection and verification** — deciding what to download next (rarest-first, in a healthy client) and proving each 16KB block assembles into a piece whose SHA1 matches what the `.torrent` promised.

The goal of this project was to implement all of that from first principles — parsing bytes off the wire by hand, managing raw sockets, writing the SHA1 implementation rather than linking one in — and to do it as a real, concurrent, multi-peer client rather than a toy that talks to one seed. It's built and tested against a Windows development environment throughout, which shows up in a few places below.

---

## Architecture at a Glance

```
 .torrent file
      │
      ▼
 bencoder.c ──► decodes the raw bencoded bytes into a generic BValue tree
      │
      ▼
 torrent.c ───► walks that tree into a typed Torrent struct, computes info_hash
      │
      ▼
 tracker.c ───► announces to the tracker (HTTP GET or UDP connect/announce),
                gets back a compact list of peer{ip, port}
      │
      ▼
 downloader.c ─► spawns one thread per peer, each running the same session:
      │
      ├─► peer.c ────► handshake, then length-prefixed message framing
      │                (bitfield, have, choke/unchoke, request, piece)
      │
      └─► piece_manager.c ─► shared state across all threads: rarest-first
                              piece selection, block bookkeeping, SHA1
                              verification, all writing into one shared
                              in-memory buffer
      │
      ▼
 file_writer.c ─► once every piece is verified, splits the buffer back
                  into the real file(s) on disk

 log.c ──────────► thread-safe logging, called from peer.c and downloader.c
                    throughout the above
```

The one idea that shapes most of the design: **the whole torrent lives in a single buffer in memory**, and each piece owns a fixed, non-overlapping byte range within it. Because a piece is only ever "in progress" under one thread at a time, that thread can write its blocks into the buffer with no locking at all — the only things that need a mutex are the small shared bookkeeping fields (piece state, rarity counts, completion counter). That constraint made the concurrency model tractable to write correctly by hand, but it's also directly responsible for the biggest limitation this README covers below (no resume support).

---

## Project Structure

The Makefile expects headers under `include/` and implementation files under `src/` and `utils/` (it globs both with `wildcard` and links everything it finds). The rough split used here is: protocol- and session-specific logic in `src/`, and generic, reusable building blocks in `utils/`.

```
include/     all .h files (-Iinclude)
src/         main.c, torrent.c, tracker.c, peer.c, downloader.c,
             piece_manager.c, file_writer.c
utils/       bencoder.c, btypes.c, sha1.c, RFC1738_url_parser.c, log.c
```

If your local layout differs, either move the files to match or adjust `SRC` in the Makefile — the split above is a convention, not something the code enforces.

One thing to watch out for: `test_piece_manager.c` (a standalone sanity check for the piece manager, with its own `main()`) should **not** sit inside `src/` or `utils/` — the Makefile will happily try to link two `main()`s together and fail. Keep it outside those directories, or compile it as its own separate target.

---

## File-by-File Breakdown

### Bencoding & Data Types

**`btypes.h` / `btypes.c`** define `BValue` — a tagged union covering all four bencode types (integer, string, list, dictionary) — plus the constructors, accessors (`dict_get`, `list_get`), and a recursive `destroy_value` that frees an entire parsed tree. Every piece of parsed `.torrent` data — the announce URL, the info dictionary, the piece hashes — passes through this type on its way to somewhere more specific.

**`bencoder.h` / `bencoder.c`** are the actual bencoding parser and encoder: a `Parser` struct (a cursor over a byte buffer) plus `decode_value`/`decode_int`/`decode_string`/`decode_list`/`decode_dict`, each hand-rolled with overflow and malformed-input checks (a crafted length prefix can't overflow the length accumulator or ask for a size bigger than what's left in the buffer). `encode_value` and friends do the reverse, mostly useful for round-tripping and debugging. Every decoded `BValue` also records the exact byte range it came from (`encoded_begin`/`encoded_end`), which is how `torrent.c` later gets the *exact* original bytes of the info-dictionary for hashing — bencoding a dictionary yourself and hoping it matches byte-for-byte is a common source of "wrong info hash" bugs, so this sidesteps that entirely.

### URL Parsing

**`RFC1738_url_parser.h` / `RFC1738_url_parser.c`** split an announce URL (`http://host:port/path` or `udp://host:port`) into its host/port/path components, and separately handle percent-encoding the 20-byte info hash the way trackers expect it in a query string.

### SHA1

**`sha1.h` / `sha1.c`** is a self-contained SHA1 implementation (FIPS 180-4), written rather than linked, since it's used constantly and for two different reasons: computing the info hash (identity of the torrent) and verifying every downloaded piece (integrity of the data). Context is zeroed after `sha1_final()` so intermediate hash state doesn't linger in memory longer than it needs to.

### Torrent Metadata

**`torrent.h` / `torrent.c`** turn a parsed `BValue` tree into a typed `Torrent` struct: name, announce URL, piece length, the concatenated piece hashes, single- or multi-file layout, and the computed 20-byte `info_hash`. This is also where `calculate_info_hash()` lives, hashing the *original* bencoded bytes of the info-dictionary (via the `encoded_begin`/`encoded_end` pointers mentioned above) rather than re-encoding it.

### Tracker Communication

**`tracker.h` / `tracker.c`** announce to the tracker and normalize the response regardless of transport. It dispatches on the announce URL's scheme: `http://` does a GET request and parses a bencoded response (BEP 3); `udp://` does the binary connect/announce datagram exchange (BEP 15), including the random transaction IDs and exponential-backoff retry schedule the spec requires. Both paths return the same `TrackerHTTPGetResponse` shape (interval, seeder/leecher counts, and a compact peer list) so nothing downstream needs to care which transport was used. This file also owns the client's hardcoded `PEER_ID` and listening `CLIENT_PORT` (6881).

### Peer Wire Protocol

**`peer.h` / `peer.c`** implement BEP 3 at the wire level: the 68-byte handshake, length-prefixed message framing (`<4-byte length><1-byte id><payload>`), and senders for every fixed-shape message (`interested`, `unchoke`, `have`, `request`, `cancel`, ...). This layer is deliberately "dumb" — it knows how to move bytes and shape messages, but has no opinion on *when* to request what; that's `piece_manager.c` and `downloader.c`'s job. `peer_connect()` uses a bounded, `select()`-based timeout rather than the OS default, so a dead or firewalled peer can't tie up a whole thread indefinitely.

### Piece Manager

**`piece_manager.h` / `piece_manager.c`** hold the state every peer thread shares: a bitfield of which pieces are missing/in-progress/complete, a rarity count per piece (for rarest-first selection), and the single in-memory download buffer. `piece_manager_select_piece()` atomically claims a piece for one thread; `piece_manager_store_block()` writes a received block and, once a piece's last block lands, SHA1-verifies the whole piece against the hash from the `.torrent` file — resetting it back to "missing" on a mismatch so another peer can retry it. Block-level writes need no locking, by design (see [Architecture](#architecture-at-a-glance)); only the small bookkeeping fields go through a mutex.

### File Writer

**`file_writer.h` / `file_writer.c`** run once, after every piece is verified: they split the single in-memory buffer back into the torrent's real file layout on disk, creating directories as needed for multi-file torrents and writing each file's byte range as one pass.

### Downloader (Concurrency)

**`downloader.h` / `downloader.c`** are the concurrency layer. `peer_download_session()` is the complete logic for *one* peer — connect, handshake, wait for unchoke, then loop: pick the rarest piece this peer has, download its blocks one at a time (no pipelining yet), hand each to the piece manager. `start_download()` just runs that once per `CreateThread`, capped at `MAX_CONCURRENT_PEERS` (61), and blocks on `WaitForMultipleObjects` until every thread finishes.

### Logger

**`log.h` / `log.c`** are a small thread-safe logger — one mutex around the actual `printf`, so many peer threads logging concurrently can't interleave garbage mid-line. Every line is tagged `[ip:port]` and timestamped (seconds since program start), at one of four levels (`ERROR`/`WARN`/`INFO`/`DEBUG`). It's wired into `peer.c` (connection lifecycle, handshake outcomes) and `downloader.c` (bitfield/choke state, block requests, piece completion and SHA1 failures, per-peer and overall session summaries). Set `BT_LOG_LEVEL=debug` as an environment variable for full per-message tracing; the default `INFO` level gives you the narrative without the noise.

### main.c

Wires all of the above together: reads the `.torrent` file, decodes and parses it, announces to the tracker, creates the piece manager, runs the threaded download, and — on success — flushes the result to disk via the file writer. It's also where `MY_PEER_ID` lives, a copy of the same peer ID `tracker.c` announces with (kept in sync by hand, since `tracker.c`'s is file-local — worth eventually replacing with a proper accessor instead of two constants that have to match).

---

## Building and Running

The project now builds with `make` instead of a hand-typed `gcc` invocation. The Makefile:

```makefile
CC = gcc
CFLAGS = -Iinclude

SRC = $(wildcard src/*.c utils/*.c)
OBJ = $(patsubst %.c, %.o, $(SRC))

TARGET = main

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lws2_32

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)
```

A few practical notes on using it:

- **Requires MinGW-w64's `gcc`** on the PATH (or an equivalent that understands `-lws2_32`) — this links directly against Winsock, so building with MSVC's `cl` won't work without changes to the link step.
- `make` builds everything; `make clean` removes all object files and the binary; `make run` builds and then runs `./main` — but note the `run` target doesn't currently accept arguments, and this program requires a `.torrent` file path. Either run the binary directly after building (see [Usage](#usage) below), or extend the Makefile with something like `run: $(TARGET) ; ./$(TARGET) $(ARGS)` if you want `make run ARGS=some.torrent` to work.
- The build produces a single `main.exe` (or `main` without an extension, depending on your MinGW setup) at the project root.

## Usage

```
main.exe <path-to-torrent-file>
```

On a successful run, the client parses the `.torrent`, announces to its tracker, connects to peers concurrently, downloads and verifies every piece, and writes the result under a `downloads/` directory (named after the torrent, with the original file layout preserved for multi-file torrents).

Set `BT_LOG_LEVEL=debug` in the environment beforehand for verbose per-message logging if something needs debugging.

---

## Current Capabilities

- Full bencode decode/encode with overflow-safe, malformed-input-resistant parsing.
- `.torrent` parsing for both single-file and multi-file layouts, with correct info-hash computation from the original bytes.
- Tracker announces over both HTTP (BEP 3) and UDP (BEP 15), with bounded connect/receive timeouts.
- Full peer wire protocol handshake and message framing.
- Thread-per-peer concurrent downloading (up to 61 peers at once) against a shared, rarest-first piece manager.
- Per-piece SHA1 verification, with automatic re-queueing of any piece that fails verification.
- Thread-safe logging across the whole download lifecycle.

## Known Limitations & Roadmap

This section is deliberately blunt about what isn't done yet — the plan from here, roughly in the order it matters.

### A) Magnet links & block-level thread ownership

Two separate gaps worth naming together since they're both "the next layer of the protocol that isn't built yet":

- **No magnet link support.** Every download currently requires an actual `.torrent` file in hand — there's no DHT, no peer exchange, and no metadata-exchange extension (BEP 9) to bootstrap a download from just an info hash and a list of trackers/peers, which is how magnet links actually work. Right now, no `.torrent` file means no download.
- **Threads own whole pieces, not individual blocks.** The current model gives each peer thread exclusive ownership of a *piece* (typically a few hundred KB) for its whole duration — clean for locking, but it means a single slow or stalled peer can sit on a piece it's only half-downloaded while other, faster peers sit idle with nothing to do for that piece. A proper implementation would let multiple peers contribute blocks to the same piece, which needs finer-grained (block-level) ownership tracking instead of the current piece-level state machine.

### B) No resume support (the big one)

This is the most important thing missing, and it's a direct consequence of the current architecture: the entire torrent lives in a single in-memory buffer for the whole download, and is only ever written to disk *once*, at the very end, after every piece has already been verified. That means:

- If the program crashes, is closed, or the system restarts at 99% completion, **none of that progress is recoverable** — the buffer is gone, and the next run starts from zero.
- There's currently no concept of a "partial download" on disk at all to resume *from*.

The fix is really two changes that go together:

1. **Write pieces (or chunks of pieces) to disk as they're verified**, instead of holding the whole torrent in RAM until the end. This also has the side benefit of bounding memory usage for very large torrents, which the current single-buffer design doesn't do.
2. **On startup, before announcing anything, check for an existing partial download** for the given `.torrent` file. If one exists, read it back, re-verify each piece against its SHA1 (the "ground truth" from the `.torrent`), and use that to populate the piece manager's missing-pieces state directly — so the download resumes from wherever it actually left off instead of re-fetching data that's already sitting correctly on disk. If no partial download exists, fall back to the current from-scratch behavior.

This is the next real feature to build, ahead of anything else in this list.

### C) A terminal UI

Right now the client's only interface is scrolling log lines. A proper TUI is in progress — most likely built with Python's [Textual](https://textual.textualize.io/), though C/C++ (ncurses or similar) or Rust (ratatui) are also on the table depending on how it ends up integrating with the C backend. The likely shape is a thin process boundary: the C client keeps doing the actual downloading and exposes progress (per-piece status, per-peer state, transfer rate) through some simple channel — a local socket, a status file, or similar — that the TUI polls or subscribes to, rather than embedding a scripting runtime inside the C binary itself.

### D) Cross-platform compatibility

The project has been Windows-only throughout, and that was a deliberate, if temporary, choice — the first priority was getting a client that actually connects to real trackers and real peers and fetches real data, not portability. That shows up unevenly across the codebase:

- **Threading is already mostly cross-platform.** `piece_manager.c` and `log.c` use a small macro shim (`CRITICAL_SECTION` on Windows, `pthread_mutex_t` elsewhere) for their locks, and `downloader.c` has a working (if simplified, single-threaded) non-Windows fallback path. This part of the port is largely done.
- **Socket code is not.** `peer.c` and `tracker.c` are written directly against Winsock (`WinSock2.h`, `WSAStartup`, `ioctlsocket`, `SOCKET`/`INVALID_SOCKET`, `closesocket`, etc.). `peer.h` has the bones of a POSIX fallback (`typedef int socket_t`, `close()` aliased to `closesocket()`) for syntax-checking on other platforms, but the actual connect-timeout logic, `WSAGetLastError()` error handling, and DNS resolution in `tracker.c` are Windows-specific and haven't been exercised on Linux/macOS at all.

Making this genuinely cross-platform means finishing that translation layer properly — abstracting the handful of Windows-specific socket calls behind the same kind of shim already used for threading — rather than the current state, where the POSIX branches exist mostly to let the portable logic compile-check on this sandbox.

---

## Closing Notes

Nothing here is pretending to be a production BitTorrent client — there's no DHT, no encryption, no upload/seeding logic, no endgame mode, and (per above) no resume support yet. What it is: a working, concurrent, protocol-correct client built from raw sockets and bencoded bytes up, with each layer built and reasoned about deliberately rather than pulled in as a dependency. The roadmap above is the honest next-steps list, not a wish list — resume support and cross-platform sockets in particular are overdue rather than optional.