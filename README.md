# tnerroT — Build Your Own BitTorrent Client in C

A from-scratch, dependency-free implementation of the BitTorrent protocol in C: bencoding, `.torrent` parsing, HTTP/UDP tracker communication, the peer wire protocol, and a thread-per-peer concurrent downloader. No libtorrent, no libcurl, no third-party networking or crypto libraries — just raw sockets, threads, and the specs, built and explained one protocol layer at a time.

This document isn't a project pitch — it's a tutorial. BitTorrent looks simple from the outside ("open a `.torrent`, a download starts"), but it's really four or five separate protocols stacked on top of each other, and most of what makes a client *work* is getting the small, easy-to-get-wrong details right: hashing the exact original bytes of a dictionary, framing a length-prefixed binary message, deciding which of sixty peers gets which piece next. Each section below takes one of those layers, explains the spec, and then walks through the actual C that implements it — so you can build (or at least deeply understand) your own client by the end.

---

## Table of Contents

1. [Why Build a BitTorrent Client](#1-why-build-a-bittorrent-client)
2. [How the Pieces Fit Together](#2-how-the-pieces-fit-together)
3. [Bencoding: BitTorrent's Serialization Format](#3-bencoding-bittorrents-serialization-format)
   - 3.1 [The Four Bencode Types](#31-the-four-bencode-types)
   - 3.2 [Representing Bencoded Data in C: `BValue`](#32-representing-bencoded-data-in-c-bvalue)
   - 3.3 [Writing the Decoder](#33-writing-the-decoder)
   - 3.4 [Hardening the Decoder Against Malformed Input](#34-hardening-the-decoder-against-malformed-input)
   - 3.5 [Writing the Encoder](#35-writing-the-encoder)
4. [The `.torrent` File and the Info Hash](#4-the-torrent-file-and-the-info-hash)
   - 4.1 [Anatomy of a `.torrent`](#41-anatomy-of-a-torrent)
   - 4.2 [Single-File vs. Multi-File Torrents](#42-single-file-vs-multi-file-torrents)
   - 4.3 [The Info Hash Gotcha](#43-the-info-hash-gotcha)
   - 4.4 [Writing SHA1 From Scratch](#44-writing-sha1-from-scratch)
5. [Talking to a Tracker](#5-talking-to-a-tracker)
   - 5.1 [What a Tracker Actually Does](#51-what-a-tracker-actually-does)
   - 5.2 [HTTP Trackers (BEP 3)](#52-http-trackers-bep-3)
   - 5.3 [UDP Trackers (BEP 15)](#53-udp-trackers-bep-15)
   - 5.4 [The Compact Peer Format](#54-the-compact-peer-format)
6. [The Peer Wire Protocol](#6-the-peer-wire-protocol)
   - 6.1 [The Handshake](#61-the-handshake)
   - 6.2 [Length-Prefixed Message Framing](#62-length-prefixed-message-framing)
   - 6.3 [The Message Types](#63-the-message-types)
   - 6.4 [The Choke/Interested State Machine](#64-the-chokeinterested-state-machine)
7. [Pieces, Blocks, and Verification](#7-pieces-blocks-and-verification)
   - 7.1 [Pieces vs. Blocks](#71-pieces-vs-blocks)
   - 7.2 [Rarest-First Piece Selection](#72-rarest-first-piece-selection)
   - 7.3 [Requesting and Storing Blocks](#73-requesting-and-storing-blocks)
   - 7.4 [Verifying a Completed Piece](#74-verifying-a-completed-piece)
8. [Concurrency: One Thread Per Peer](#8-concurrency-one-thread-per-peer)
   - 8.1 [The Ownership Trick That Avoids Locking](#81-the-ownership-trick-that-avoids-locking)
   - 8.2 [What Actually Needs a Mutex](#82-what-actually-needs-a-mutex)
   - 8.3 [The Single-Peer Session Loop](#83-the-single-peer-session-loop)
   - 8.4 [Spawning and Joining Threads](#84-spawning-and-joining-threads)
9. [From Buffer to Disk](#9-from-buffer-to-disk)
10. [Thread-Safe Logging](#10-thread-safe-logging)
11. [Putting It All Together: `main.c`](#11-putting-it-all-together-mainc)
12. [Project Structure](#12-project-structure)
13. [File-by-File Reference](#13-file-by-file-reference)
14. [Current Capabilities](#14-current-capabilities)
15. [Known Limitations & Roadmap](#15-known-limitations--roadmap)
    - 15.1 [Magnet Links & Block-Level Thread Ownership](#151-magnet-links--block-level-thread-ownership)
    - 15.2 [No Resume Support (the big one)](#152-no-resume-support-the-big-one)
    - 15.3 [A Terminal UI](#153-a-terminal-ui)
    - 15.4 [Cross-Platform Sockets](#154-cross-platform-sockets)
16. [Further Reading](#16-further-reading)
17. [Building and Running](#17-building-and-running)
18. [Usage](#18-usage)
19. [Closing Notes](#19-closing-notes)

---

## 1. Why Build a BitTorrent Client

Every popular torrent client (qBittorrent, Transmission, libtorrent-based tools) hides the protocol behind a polished UI, and every tutorial on "how BitTorrent works" tends to stop at a diagram of peers exchanging pieces. Neither actually shows you the bytes.

This project exists to answer, in working code, the questions a diagram can't:

- What do the raw bytes of a `.torrent` file actually look like, and how do you parse them without a library?
- Why does one wrong byte in how you re-serialize a dictionary silently put you in the wrong swarm entirely?
- What does a tracker request look like on the wire, for both the HTTP and UDP variants?
- How is "I want piece 41" and "here are the 16KB of piece 41 you asked for" actually encoded between two peers?
- How do you download from many peers *concurrently* without either a data race or a giant global lock that serializes everything anyway?

The approach taken here is the same one this document uses to explain it: build one protocol layer, get it correct against the spec, then build the layer on top. By the end you have bencoding → torrent metadata → tracker announce → peer handshake → piece exchange → verified file on disk, each piece testable and reasoned about on its own.

---

## 2. How the Pieces Fit Together

Before diving into any one layer, it helps to see the whole pipeline at once — this is the shape every section below fills in:

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

The single idea that shapes most of the design, and that comes up repeatedly below: **the whole torrent lives in one buffer in memory**, and each piece owns a fixed, non-overlapping byte range within it. Because a piece is only ever "in progress" under one thread at a time, that thread can write its blocks into the buffer with *no locking at all* — the only things that need a mutex are small shared bookkeeping fields (piece state, rarity counts, completion counter). Keep that constraint in mind; it explains both why the concurrency model in Section 8 is simple to reason about, and why Section 15.2 (no resume support) is the biggest thing still missing.

---

## 3. Bencoding: BitTorrent's Serialization Format

### 3.1 The Four Bencode Types

Bencoding ("Be-encoding", after Bram Cohen) is BitTorrent's serialization format — think a much stricter, more terse JSON. It supports exactly four types, and every `.torrent` file and every tracker response is built out of nothing but these:

| Type | Encoding | Example | Meaning |
|---|---|---|---|
| Integer | `i<base-10 ASCII>e` | `i42e` | the integer `42` |
| Byte string | `<length>:<bytes>` | `4:spam` | the 4-byte string `"spam"` |
| List | `l<bencoded values>e` | `l4:spam4:eggse` | `["spam", "eggs"]` |
| Dictionary | `d<bencoded string><bencoded value>...e` | `d3:cow3:mooe` | `{"cow": "moo"}` |

Two details matter more than they look like they should:

- **Strings are byte strings, not text strings.** There's no encoding declared or assumed — a bencoded string is a length prefix and that many raw bytes, which might be UTF-8 text (a file name) or might be 20-byte SHA1 hashes concatenated together (the `pieces` field). Code that treats every bencoded string as a null-terminated C string will silently corrupt binary fields.
- **Dictionary keys must be sorted** (by raw byte value) in a well-formed bencoded dictionary. This project's dictionary type doesn't currently enforce or re-sort on encode — see [File-by-File Reference](#13-file-by-file-reference) — because it only decodes existing, already-sorted `.torrent`/tracker data rather than re-encoding an info-dict from scratch. If you extend the encoder to produce new `.torrent` files or metadata-exchange payloads, sorting on insert (or on encode) is worth adding.

### 3.2 Representing Bencoded Data in C: `BValue`

Since the four bencode types are heterogeneous (a list can contain integers, strings, other lists, and dictionaries all at once), the natural C representation is a tagged union — one struct that can *be* any of the four types, plus a tag saying which one it currently is:

```c
typedef enum { BINT, BSTRING, BLIST, BDICT } BType;

typedef struct BValue {
    BType type;
    union {
        BInteger integer;
        BString  string;
        BList    list;
        BDict    dict;
    } value;

    /* the exact byte range in the original buffer this value was
       decoded from — see Section 4.3 for why this matters */
    const char* encoded_begin;
    const char* encoded_end;
} BValue;
```

`BList` and `BDict` are themselves small growable-array structs (`items`/`count`/`capacity` for a list, `entries`/`count`/`capacity` for a dict), so a decoded `.torrent` file ends up as one recursive tree of `BValue` nodes rooted at a top-level dictionary. `btypes.c` supplies the constructors (`create_int`, `create_string`, `create_list`, `create_dict`), the accessors (`dict_get`, `list_get`), the mutators (`list_append`, `dict_insert`), and a recursive `destroy_value()` that walks the whole tree and frees it — the only teardown function every other layer needs to know about.

One easy-to-miss bug worth calling out here since it's a good general C lesson: every constructor uses `calloc`, not `malloc`, specifically so the `encoded_begin`/`encoded_end` pointers start life as `NULL` rather than garbage. If you hand-construct a `BValue` for a unit test (rather than getting one back from the decoder) and something later dereferences those pointers uninitialized, you get a segfault that only reproduces with hand-crafted test data — exactly the kind of bug that's cheap to prevent and annoying to debug after the fact.

### 3.3 Writing the Decoder

The decoder is built around a `Parser` — nothing more than a cursor over an in-memory byte buffer:

```c
typedef struct {
    const char* data;
    size_t size;
    size_t pos;
} Parser;

int parser_get(Parser* p)  { /* returns byte at pos, advances pos, or EOF */ }
int parser_peek(Parser* p) { /* returns byte at pos, does NOT advance */ }
int parser_read(Parser* p, void* dst, size_t length); /* bulk copy + advance */
```

`decode_value()` is the dispatcher: peek at (well, consume) one byte, and that byte alone tells you which of the four decoders to hand off to — `i` means an integer, `l` a list, `d` a dictionary, and any ASCII digit `0`–`9` means a string (whose length prefix has already started). This is what makes bencoding pleasant to parse by hand: the grammar is unambiguous one token ahead, no backtracking required.

```c
BValue* decode_value(Parser* in) {
    const char* start = in->data + in->pos;
    int c = parser_get(in);
    BValue* value = NULL;

    if (c == EOF) return NULL;
    if (c == 'i') value = decode_int(in);
    if (c == 'l') value = decode_list(in);
    if (c == 'd') value = decode_dict(in);
    if (c >= '0' && c <= '9') value = decode_string(in, c);

    if (!value) return NULL;

    value->encoded_begin = start;
    value->encoded_end   = in->data + in->pos;
    return value;
}
```

Notice `encoded_begin`/`encoded_end` are stamped on *every* decoded value, not just the top-level dictionary — that's what lets `torrent.c` later grab the exact original bytes of just the `info` sub-dictionary (Section 4.3).

`decode_list()` and `decode_dict()` are both simple loops that keep calling `decode_value()` (or, for a dict, `decode_string()` for the key followed by `decode_value()` for the value) until they hit the terminating `e`:

```c
BValue* decode_list(Parser* in) {
    BValue* list = create_list();
    while (1) {
        int c = parser_peek(in);
        if (c == EOF) { destroy_value(list); return NULL; }
        if (c == 'e') { parser_get(in); break; }

        BValue* item = decode_value(in);
        if (!item) { destroy_value(list); return NULL; }
        list_append(list, item);
    }
    return list;
}
```

### 3.4 Hardening the Decoder Against Malformed Input

A `.torrent` file is untrusted input the moment it comes from anywhere other than your own encoder — someone else authored it, or it arrived over the network as part of a tracker response. Two failure modes get handled explicitly in `decode_string()`:

**Non-digit characters mid-length.** `4a:spam` should be rejected, not silently parsed as length `4` (stopping at the first non-digit and hoping for the best) or, worse, accumulated as if `a` were a valid digit:

```c
while ((c = parser_get(in)) != ':') {
    if (c == EOF) return NULL;

    if (c < '0' || c > '9')
        return NULL;   /* reject malformed length prefix outright */

    length = length * 10 + (c - '0');
}
```

**Integer overflow in the length prefix.** A crafted (or corrupted) 20-digit length prefix would overflow a 32-bit `int` accumulator and hand `malloc()` a garbage or wraparound size — a classic path to either a crash or, in the worst case, a heap overflow if the wrapped value is small but the actual read length used elsewhere isn't. The fix is to bound-check *before* each multiply-and-add, and separately to reject any length that couldn't possibly fit in what's left of the buffer:

```c
if (length > (INT_MAX - 9) / 10)
    return NULL;                      /* would overflow on the next digit */

length = length * 10 + (c - '0');

if ((size_t)length > in->size - in->pos)
    return NULL;                      /* claims more bytes than we have */
```

`decode_int()` applies the same discipline to its own buffer (bounded at 32 digits, checked before every write) rather than assuming a well-behaved integer literal. The general lesson generalizes past bencoding: **any parser reading a length-prefixed field from untrusted bytes needs to validate the length against remaining buffer size before it's used for an allocation or a `memcpy`**, not after.

### 3.5 Writing the Encoder

The encoder is the decoder's mirror image and much simpler, since it operates on an already-valid, in-memory `BValue` tree rather than untrusted bytes — there's nothing to reject, only bytes to emit:

```c
void encode_int(FILE* out, const BValue* value) {
    fprintf(out, "i%llde", value->value.integer.value);
}

void encode_string(FILE* out, const BValue* value) {
    const BString* str = &value->value.string;
    fprintf(out, "%d:", str->length);
    fwrite(str->data, 1, str->length, out);
}
```

`encode_list()` and `encode_dict()` just wrap their contents in `l`...`e` / `d`...`e` and recurse through `encode_value()`, the dispatcher that switches on `value->type`. In this project the encoder is used mainly for round-tripping and debugging rather than producing new `.torrent` files or metadata payloads — but it's the piece you'd extend first if you wanted to add magnet-link metadata exchange (BEP 9, see [Section 15.1](#151-magnet-links--block-level-thread-ownership)), since that protocol involves *constructing* bencoded dictionaries on the fly rather than only reading them.

---

## 4. The `.torrent` File and the Info Hash

### 4.1 Anatomy of a `.torrent`

A `.torrent` file is a single bencoded dictionary at the top level, with (at minimum) two fields:

```
d
  8:announce  <tracker URL as a bencoded string>
  4:info
    d
      4:name         <suggested file/folder name>
      12:piece length <bytes per piece, e.g. i262144e>
      6:pieces        <concatenated 20-byte SHA1 hashes, one per piece>
      6:length        <total size, for single-file torrents>
      ... or ...
      5:files         <list of {length, path} dicts, for multi-file torrents>
    e
e
```

`torrent_parse()` in `torrent.c` walks this with `dict_get()` calls straight off the decoded `BValue` tree — `dict_get(root, "announce", 8)`, `dict_get(info, "piece length", 12)`, and so on — validating the type of each field it pulls out (`BSTRING` for `name`, `BINT` for `piece length`) and failing the parse cleanly if a required field is missing or the wrong type, rather than dereferencing a union member that isn't actually populated.

### 4.2 Single-File vs. Multi-File Torrents

The `info` dictionary is shaped differently depending on whether the torrent describes one file or many, and `torrent_parse()` branches on which of two mutually-exclusive fields is present:

- **Single-file**: `info` has a `length` integer directly — the whole torrent *is* one file, named by `info.name`.
- **Multi-file**: `info` has a `files` list instead, where each entry is its own dictionary with a `length` and a `path` (itself a list of path components, so nested directories round-trip correctly across operating systems with different path separators) and an optional `md5sum`. `info.name` becomes the shared root directory all of those files live under.

```c
if (length && length->type == BINT) {
    torrent->length = length->value.integer.value;
}
else if (files && files->type == BLIST) {
    /* sum every file's length to get torrent->length, and keep each
       file's own length + path components for later */
    ...
}
```

This distinction resurfaces in [Section 9](#9-from-buffer-to-disk), where `file_writer.c` has to reverse it — splitting one flat in-memory buffer back into either a single file or a directory tree of many.

### 4.3 The Info Hash Gotcha

The **info hash** — the 20-byte SHA1 hash of the *bencoded* `info` dictionary — is a torrent's identity. It's what you tell the tracker you want peers for, and it's what both sides of a peer handshake compare to confirm they're talking about the same torrent. Get it wrong and you're not talking to the wrong peer for one torrent — you're silently participating in (or being rejected from) an entirely different swarm.

The trap: it is extremely tempting to decode the `info` dictionary into your `BValue` tree and then **re-encode it** to compute the hash. Don't. Bencoded dictionaries are only well-formed with keys in sorted order, but nothing stops a real-world `.torrent` file from having extra whitespace conventions, a different internal ordering quirk, or simply an encoder elsewhere in the ecosystem that serializes semantically-identical data as different bytes. If your re-encoding doesn't reproduce the *exact original byte sequence*, byte for byte, your info hash is wrong — and it'll be wrong in a way that's very hard to notice, because the torrent will still parse and print fine; it just won't find any peers, or will fail every handshake.

The fix used throughout this project is the `encoded_begin`/`encoded_end` pointers stamped onto every `BValue` during decode (Section 3.3): they point directly into the *original* file buffer, so hashing the info dictionary means hashing those exact original bytes — no re-serialization, no chance of drift:

```c
BValue* info = dict_get(root, "info", 4);
...
calculate_info_hash(info->encoded_begin, info->encoded_end, torrent->info_hash);
```

```c
void calculate_info_hash(const char* start, const char* end, unsigned char digest[20]) {
    if (!start || !end || end < start) return;

    size_t len = (size_t)(end - start);

    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (const unsigned char*)start, len);
    sha1_final(&ctx, digest);
}
```

This is the single biggest reason the decoder keeps a pointer back into the original buffer at all, rather than just building a clean detached tree — and it's a pattern worth remembering any time you parse a format where a hash or signature is computed over a sub-region of the *original* serialized bytes (JWTs, signed XML, and Git's own object format all have a version of the same trap).

### 4.4 Writing SHA1 From Scratch

SHA1 gets used twice in this project — computing the info hash above, and verifying every downloaded piece against the hashes listed in `info.pieces` ([Section 7.4](#74-verifying-a-completed-piece)) — so it's implemented directly per FIPS PUB 180-4 rather than pulled in as a dependency. The implementation follows the spec's four stages:

1. **Message schedule expansion**: each 64-byte block is unpacked into sixteen big-endian 32-bit words, then expanded to eighty words via `W[i] = ROTL32(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1)`.
2. **Eighty compression rounds**, split into four groups of twenty, each using a different mixing function (`Ch`, `Parity`, `Maj`, `Parity` again) and round constant.
3. **Streaming updates**: `sha1_update()` can be called repeatedly with arbitrary-sized chunks — it buffers up to 63 bytes internally and only calls the block-compression function once a full 64-byte block has accumulated, which is what lets `sha1_digest()` hash something as large as a whole piece without needing it all statically sized at compile time.
4. **Padding and finalization**: `sha1_final()` appends the mandatory `0x80` padding byte, zero-pads out to a 64-bit length field, appends the total bit-length in big-endian, compresses one last time, and serializes the five 32-bit state words into the 20-byte digest.

One small defensive habit worth copying into your own crypto code: `sha1_final()` ends with `memset(ctx, 0, sizeof(*ctx))`, scrubbing the whole context — including the message schedule and intermediate state — so that mid-computation hash state doesn't linger in memory on the stack or heap longer than it has to.

```c
void sha1_digest(const void *data, size_t len, uint8_t hash[SHA1_DIGEST_SIZE]) {
    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, hash);
}
```

---

## 5. Talking to a Tracker

### 5.1 What a Tracker Actually Does

A tracker is deliberately a small, almost boring service: you send it your info hash, your peer ID, and a few stats (bytes uploaded/downloaded/remaining), and it hands back a list of other peers currently in that swarm, plus an interval telling you how often to check back in. It does not relay any file data itself — it's a directory, not a data path. Modern swarms often supplement or replace trackers with DHT and peer exchange, but the tracker announce is still the simplest, most universally-supported way to bootstrap a swarm, and it's the one this project implements.

`connect_tracker()` in `tracker.c` is the single entry point everything else calls, and it does nothing but dispatch on URL scheme:

```c
TrackerHTTPGetResponse* connect_tracker(Torrent* torrent) {
    if (strncmp(torrent->announce, "udp://", 6) == 0)
        return connect_tracker_udp(torrent);

    if (strncmp(torrent->announce, "http://", 7) == 0)
        return connect_tracker_http(torrent);

    fprintf(stderr, "connect_tracker: unsupported announce scheme: %s\n", torrent->announce);
    return NULL;
}
```

Both branches normalize their very different wire formats into the exact same `TrackerHTTPGetResponse` struct (interval, seeder/leecher counts, compact peer list), so nothing downstream — not `main.c`, not `downloader.c` — ever needs to know or care which transport was actually used.

### 5.2 HTTP Trackers (BEP 3)

The HTTP variant (BEP 3) is a plain GET request with the announce parameters URL-encoded into the query string:

```
GET /announce?info_hash=%01%02...&peer_id=-CB0001-000...&port=6881
    &uploaded=0&downloaded=0&left=<bytes-remaining>&compact=1 HTTP/1.1
Host: tracker.example.com
Connection: close
```

`build_http_get_request()` assembles exactly this, hex-percent-encoding the raw 20-byte info hash and peer ID via `encode_info_hash()` (in `RFC1738_url_parser.c` — the raw bytes of a SHA1 hash are essentially never valid unescaped URL characters, so every byte gets `%XX`-encoded unconditionally rather than trying to special-case the "safe" ones). `Connection: close` is sent deliberately, so the response can be read to EOF instead of needing to parse a `Content-Length` header or handle chunked transfer encoding:

```c
static int tracker_roundtrip(SOCKET sock, const char* request, char** out, size_t* out_len) {
    send(sock, request, (int)strlen(request), 0);

    /* growable buffer, doubles capacity as needed */
    while ((bytes = recv(sock, chunk, sizeof(chunk), 0)) > 0) {
        /* ...append chunk to buffer, growing capacity if needed... */
    }

    *out = buffer;
    *out_len = length;
    return 1;
}
```

`http_body()` then splits the raw response on the first blank line (`\r\n\r\n`) to separate HTTP headers from the bencoded body, and `decode_value()` — the same bencode decoder from Section 3 — parses that body straight into a `BValue` dictionary, which `parse_tracker_dict()` walks with the same `dict_get()` pattern used for `.torrent` files: `interval`, `complete` (seeders), `incomplete` (leechers), and `peers` (Section 5.4), plus an optional `failure reason` string that, if present, means the announce was rejected and nothing else in the response should be trusted.

### 5.3 UDP Trackers (BEP 15)

The UDP variant (BEP 15) trades HTTP's simplicity for a much smaller wire format — a real concern back when it was designed, since a busy tracker fields enormous numbers of announces — at the cost of needing to implement your own reliability on top of UDP's fire-and-forget delivery. It's a two-step binary handshake:

**Step 1 — Connect.** Establishes a `connection_id` the tracker will accept on the following announce, proving (loosely) that you can receive replies from the address you claim to be sending from:

```c
put_u64(req,      UDP_PROTOCOL_ID);   /* magic constant: 0x41727101980 */
put_u32(req + 8,  UDP_ACTION_CONNECT);
put_u32(req + 12, random_transaction_id());
```

**Step 2 — Announce.** Sends the same stats an HTTP announce would (info hash, peer ID, uploaded/downloaded/left, port), but packed into a fixed 98-byte binary struct instead of a query string, using the `connection_id` from step 1:

```c
put_u64(req,      connection_id);
put_u32(req + 8,  UDP_ACTION_ANNOUNCE);
put_u32(req + 12, random_transaction_id());
memcpy(req + 16, torrent->info_hash, 20);
memcpy(req + 36, PEER_ID, 20);
put_u64(req + 64, torrent->length);   /* left */
...
```

Both steps share the same reliability pattern, since UDP guarantees neither delivery nor ordering: generate a random **transaction ID**, send, and wait with a timeout that **doubles on every retry** (the spec's prescribed backoff — 15s, 30s, 60s, ... up to a capped number of attempts). Any reply is checked against the transaction ID before being trusted at all, since nothing stops an unrelated stray UDP datagram from arriving on the same socket:

```c
for (int n = 0; n <= UDP_MAX_RETRIES; n++) {
    send(sock, (const char*)req, sizeof(req), 0);
    set_recv_timeout(sock, UDP_TIMEOUT_BASE_SEC * (1 << n));  /* exponential backoff */

    int got = recv(sock, (char*)resp, sizeof(resp), 0);

    if (got < 8) continue;                       /* timed out or short read — retry */
    if (get_u32(resp + 4) != txn) continue;       /* mismatched/stray reply — retry */

    /* got >= 8 and transaction ID matches: safe to trust the payload */
}
```

This connect-then-announce dance, done correctly with matching transaction IDs and real backoff, is most of what makes BEP 15 fiddlier to implement than the HTTP variant despite having a smaller wire format — the format is trivial; the *reliability logic wrapped around it* is where the real work is.

### 5.4 The Compact Peer Format

Both tracker transports return their peer list in the same **compact format**: a flat byte string, 6 bytes per peer — 4 bytes of IPv4 address followed by 2 bytes of port, both big-endian/network byte order, with no delimiters needed since every entry is a fixed size:

```c
BValue* peers = dict_get(dict, "peers", 5);

if (peers && peers->type == BSTRING) {
    size_t count = (size_t)peers->value.string.length / sizeof(Peer);   /* sizeof(Peer) == 6 */
    out->peers = malloc(sizeof(Peer) * count);
    memcpy(out->peers, peers->value.string.data, count * sizeof(Peer));
    out->num_peers = count;
}
```

Because `Peer` is declared as a 6-byte packed `{uint32_t ip; uint16_t port;}` in network byte order, this whole "parse" is a single `memcpy` — the wire format and the in-memory struct are bit-for-bit identical, which is exactly the point of a compact format. (The alternative, older "dictionary model" — a bencoded list of `{ip, port, peer id}` dictionaries per peer — is far more verbose and isn't handled by this project's tracker parsing; every modern tracker supports compact, requested here via `compact=1`.) The UDP variant's peer list works the same way, just without ever having been bencoded in the first place — it's simply the tail of the announce response, sliced into 6-byte chunks directly.

---

## 6. The Peer Wire Protocol

Once you have a list of `{ip, port}` pairs from the tracker, the peer wire protocol (also defined in BEP 3) is how you actually talk to them — a small, length-prefixed binary protocol layered directly over a raw TCP socket, implemented in `peer.c`.

### 6.1 The Handshake

Every peer connection opens with a fixed 68-byte handshake, sent by both sides before any other message is valid:

```
byte 0        : 19 (length of the protocol name string that follows)
bytes 1–19    : "BitTorrent protocol"
bytes 20–27   : 8 reserved bytes (extension flags — all zero here, no extensions negotiated)
bytes 28–47   : 20-byte info hash
bytes 48–67   : 20-byte peer ID
```

```c
int peer_send_handshake(PeerConnection* pc, const unsigned char info_hash[20], const unsigned char peer_id[20]) {
    unsigned char buf[HANDSHAKE_LEN];   /* 68 */

    buf[0] = 19;
    memcpy(buf + 1, "BitTorrent protocol", 19);
    memset(buf + 20, 0, 8);
    memcpy(buf + 28, info_hash, 20);
    memcpy(buf + 48, peer_id, 20);

    return send_all(pc->sock, buf, HANDSHAKE_LEN);
}
```

Receiving one back is the mirror image, plus the one check that actually matters: **the returned info hash must equal the one you sent**. If it doesn't, you're connected to a peer that's part of a different swarm — usually because the address came from a stale tracker response or a misbehaving/malicious peer — and the honest move is to disconnect immediately rather than trying to proceed:

```c
if (memcmp(buf + 28, expected_info_hash, INFO_HASH_LEN) != 0) {
    log_msg(LOG_WARN, pc->ip, pc->port, "handshake info_hash mismatch — wrong swarm, disconnecting");
    return 0;
}
```

### 6.2 Length-Prefixed Message Framing

Every message *after* the handshake shares one shape: a 4-byte big-endian length prefix, a 1-byte message ID, and an ID-dependent payload — except the special zero-length **keep-alive**, which is just four zero bytes and nothing else (no ID byte at all):

```
<4-byte length prefix><1-byte message ID><payload, length-1 bytes>
```

Sending is a straightforward pack-and-write:

```c
int peer_send_message(PeerConnection* pc, int id, const uint8_t* payload, uint32_t length) {
    if (id < 0) {
        unsigned char zero[4] = {0,0,0,0};       /* keep-alive */
        return send_all(pc->sock, zero, 4);
    }

    uint32_t msg_len = 1 + length;
    unsigned char header[5];
    header[0] = (unsigned char)(msg_len >> 24);
    header[1] = (unsigned char)(msg_len >> 16);
    header[2] = (unsigned char)(msg_len >> 8);
    header[3] = (unsigned char)(msg_len);
    header[4] = (unsigned char)id;

    send_all(pc->sock, header, 5);
    return length > 0 ? send_all(pc->sock, payload, length) : 1;
}
```

Receiving is the same shape read back, with one deliberate safety valve: a sanity cap on `msg_len` before trusting it enough to `malloc()` a buffer for the payload. A real `piece` message caps out around 16KB + a few bytes of overhead; anything wildly larger means either a desynced TCP stream (framing has drifted and every subsequent read is now garbage) or a misbehaving peer, and the right move in either case is to bail rather than attempt a huge, attacker-influenced allocation:

```c
if (msg_len > (1u << 20))   /* 1MB — nothing legitimate is ever this big */
    return 0;
```

`recv_all()`/`send_all()` underneath both loop until the full requested length has been transferred or the socket fails, since a single `recv()`/`send()` call is never guaranteed to move the whole buffer in one shot over TCP — a detail that's easy to get away with skipping against `localhost` and immediately breaks against a real, slower network peer.

### 6.3 The Message Types

| ID | Name | Payload | Meaning |
|---|---|---|---|
| — | `keep-alive` | none (zero-length message) | "still here", resets the peer's idle timeout |
| 0 | `choke` | none | "I will not send you data right now" |
| 1 | `unchoke` | none | "you may now request pieces from me" |
| 2 | `interested` | none | "I want something you have" |
| 3 | `not interested` | none | "I don't want anything from you right now" |
| 4 | `have` | 4-byte piece index | "I just finished downloading/verifying this piece" |
| 5 | `bitfield` | variable, one bit per piece | sent once, right after the handshake: "here's everything I already have" |
| 6 | `request` | index, begin, length (4 bytes each) | "send me this block" |
| 7 | `piece` | index, begin (4+4 bytes), then raw block data | "here's the block you asked for" |
| 8 | `cancel` | index, begin, length (4 bytes each) | "never mind, don't bother sending that block" |

`wait_until_unchoked()` in `downloader.c` is where most of these get consumed on the receiving end — it's a dispatch loop that runs until the connection either fails or transitions to unchoked, updating local state (bitfield, choke status) and re-declaring interest as needed along the way:

```c
switch (msg.id) {
    case MSG_BITFIELD:
        peer_set_bitfield(pc, msg.payload, msg.length);
        piece_manager_add_peer_bitfield(pm, pc->bitfield, pc->bitfield_len);
        break;

    case MSG_HAVE:
        peer_mark_have(pc, idx);   /* idx parsed from the 4-byte payload */
        break;

    case MSG_UNCHOKE:
        pc->peer_choking = 0;
        break;
    /* ... */
}
```

### 6.4 The Choke/Interested State Machine

Both directions of a peer connection carry two independent boolean flags — `choking` and `interested` — giving four combinations per direction, but the one that actually gates data transfer is simple: **you may request blocks from a peer only once you are interested *and* that peer has unchoked you.** Every connection starts choked and not-interested on both sides (the spec's conservative default), and the client's side of the negotiation in this project is deliberately minimal — it never chokes anyone back, since it doesn't implement uploading/seeding yet (see [Section 15](#15-known-limitations--roadmap)):

```c
/* Peer Wire Protocol starts with both sides choked/not-interested. */
pc->am_choking      = 1;
pc->peer_choking    = 1;
pc->am_interested   = 0;
pc->peer_interested = 0;
```

```c
if (!pc->am_interested) {
    peer_send_interested(pc);
}

if (!pc->peer_choking)
    return 1;   /* ready to start requesting blocks */
```

A real, well-behaved client also periodically re-sends `interested`/`not-interested` as its own local piece-availability needs change, and implements tit-for-tat choking algorithms to decide who *it* unchokes when uploading — both are natural next steps once seeding support exists.

---

## 7. Pieces, Blocks, and Verification

### 7.1 Pieces vs. Blocks

A torrent's `piece length` (from the `.torrent` file, typically 256KB–4MB) is the unit the SHA1 hashes in `info.pieces` are computed over — but the peer wire protocol never requests or transfers a whole piece in one message. Instead, each piece is split into fixed-size **blocks** (conventionally 16KB, `BLOCK_SIZE` in `piece_manager.h`) that get requested and transferred one at a time. A piece is "complete" once every one of its blocks has arrived; only then is it meaningful to verify.

### 7.2 Rarest-First Piece Selection

A healthy BitTorrent client doesn't request pieces in file order — it requests the **rarest** piece available first. The reasoning is swarm-health, not just personal speed: if only one peer in the whole swarm has a given piece and that peer disappears, that piece becomes unobtainable for everyone unless someone grabs it early and starts re-sharing it. `piece_manager.c` tracks a simple per-piece rarity counter, incremented whenever a peer announces having that piece (via `bitfield` or `have`) and decremented when that peer disconnects:

```c
void piece_manager_add_peer_bitfield(PieceManager* pm, const uint8_t* bitfield, uint32_t bitfield_len) {
    for (uint32_t i = 0; i < pm->num_pieces; i++) {
        if (/* bit i is set in bitfield */)
            pm->rarity[i]++;
    }
}
```

`piece_manager_select_piece()` then does exactly what the name says: given one peer's bitfield, find the still-missing piece with the lowest rarity count that this specific peer actually has, claim it (`PIECE_MISSING → PIECE_IN_PROGRESS`), and hand back its index:

```c
for (uint32_t i = 0; i < pm->num_pieces; i++) {
    if (pm->pieces[i].state != PIECE_MISSING) continue;
    if (!peer_has_piece(i)) continue;

    if (pm->rarity[i] < best_rarity) {
        best_rarity = pm->rarity[i];
        best = (int)i;
    }
}

if (best >= 0)
    pm->pieces[best].state = PIECE_IN_PROGRESS;
```

This claim-and-mark-in-progress happens under a lock (see [Section 8.2](#82-what-actually-needs-a-mutex)), which is what guarantees two threads can never walk away believing they each own the same piece.

### 7.3 Requesting and Storing Blocks

Once a peer thread owns a piece, `download_piece()` in `downloader.c` requests its blocks one at a time — no pipelining yet, deliberately (see [Section 15](#15-known-limitations--roadmap)) — asking `piece_manager_next_block()` for the next block that isn't marked as received:

```c
while (piece_manager_next_block(pm, piece_index, &begin, &length)) {
    peer_send_request(pc, piece_index, begin, length);

    /* block until this exact block arrives (msg.id == MSG_PIECE
       with matching index/begin), ignoring/handling anything else
       (keep-alives, HAVEs, a stale/mismatched piece message) that
       shows up while waiting */
}
```

Each arriving block is handed to `piece_manager_store_block()`, which bounds-checks `begin`/`length` against the piece's actual size before touching memory, then writes directly into the shared buffer at that piece's fixed offset:

```c
uint64_t abs_offset = piece_offset + begin;

/* No lock needed: piece_index is owned by exactly one thread while
   IN_PROGRESS, and [abs_offset, abs_offset+length) is unique to this
   block, so no two threads ever touch overlapping bytes. */
memcpy(pm->file_buffer + abs_offset, data, length);
```

That comment is the whole concurrency story for the hot path, and it's worth internalizing before Section 8: block writes need no synchronization purely because of how ownership is structured, not because of anything clever happening at the `memcpy` call site itself.

### 7.4 Verifying a Completed Piece

The moment a piece's last block lands (`blocks_have == blocks_total`), `piece_manager_store_block()` immediately SHA1-hashes the whole piece and compares it against the hash recorded for that index in the `.torrent` file's `pieces` field:

```c
unsigned char digest[20];
sha1_digest(pm->file_buffer + piece_offset, pi->length, digest);

if (memcmp(digest, pm->torrent->pieces[piece_index].hash, 20) != 0) {
    /* corrupt piece — wipe progress and let someone else retry it */
    memset(pi->block_have, 0, pi->blocks_total);
    pi->blocks_have = 0;
    pi->state = PIECE_MISSING;     /* under lock */
    return -1;
}

pi->state = PIECE_COMPLETE;        /* under lock */
pm->pieces_complete++;
return 2;
```

This single check is BitTorrent's entire integrity guarantee, and it's worth appreciating how strong it actually is: it doesn't matter whether a given peer is malicious, buggy, or just relaying corrupted data from *its* upstream — every piece gets independently verified against a hash that ultimately traces back to the `.torrent` file (which itself, transitively, traces back to the info hash everyone in the swarm agreed to before connecting at all). A failed check doesn't crash the download or blame a peer specifically; it just resets that one piece back to `PIECE_MISSING` so it gets re-selected — potentially from a different, better-behaved peer — on a future call to `piece_manager_select_piece()`.

---

## 8. Concurrency: One Thread Per Peer

### 8.1 The Ownership Trick That Avoids Locking

`start_download()` spawns one OS thread per peer (capped at `MAX_CONCURRENT_PEERS`, 61 — the traditional BitTorrent connection-limit convention), and every one of those threads runs the exact same function, `peer_download_session()`, hammering the exact same `PieceManager`. The naive version of that design needs a lock around essentially everything, since dozens of threads are touching shared state constantly. This project avoids that by making the *unit of ownership* a whole piece:

- A piece is claimed by exactly one thread the moment it transitions to `PIECE_IN_PROGRESS` (Section 7.2).
- That piece's bytes always live at the same fixed, non-overlapping range of `file_buffer` — `[piece_index * piece_length, piece_index * piece_length + piece_length)` — regardless of which thread is currently downloading it.
- Because no other thread can also own that piece at the same time, and no other piece's data can land in that byte range, the owning thread can `memcpy` its blocks into the shared buffer with zero synchronization.

This is the single design decision that makes the whole concurrency model tractable to reason about by hand — it trades a *little* efficiency (Section 15.1 covers the cost: a slow peer can sit on a piece other, faster peers can't help with) for a model where the hot path (writing downloaded bytes) needs no locking whatsoever.

### 8.2 What Actually Needs a Mutex

Everything that *isn't* raw block data — the small bookkeeping fields more than one thread might read or write concurrently — goes through `pm->lock`:

- Piece **state** transitions (`MISSING → IN_PROGRESS → COMPLETE`, or back to `MISSING` on a failed verification).
- The per-piece **rarity** counters, incremented/decremented as peers connect and disconnect.
- The global **pieces_complete** counter.

```c
#ifdef _WIN32
typedef CRITICAL_SECTION log_lock_t;
#define PM_LOCK(l)   EnterCriticalSection(l)
#define PM_UNLOCK(l) LeaveCriticalSection(l)
#else
typedef pthread_mutex_t log_lock_t;
#define PM_LOCK(l)   pthread_mutex_lock(l)
#define PM_UNLOCK(l) pthread_mutex_unlock(l)
#endif
```

Every one of these critical sections is short — a handful of array/counter reads and writes, never a network call or anything else that blocks — which keeps lock contention low even with dozens of threads all calling `piece_manager_select_piece()` and `piece_manager_store_block()` concurrently. The general principle: **lock the bookkeeping, not the data**, and make sure the ownership model doing the heavy lifting is actually airtight (Section 8.1) before deciding what's safe to leave unlocked.

### 8.3 The Single-Peer Session Loop

Every thread's entire job, regardless of which peer it's talking to, is `peer_download_session()`:

```c
peer_connect(&pc, peer_ip, peer_port);
peer_send_handshake(&pc, torrent->info_hash, my_peer_id);
peer_recv_handshake(&pc, torrent->info_hash);

wait_until_unchoked(&pc, pm, &bitfield_registered);

while (1) {
    int piece_index = piece_manager_select_piece(pm, pc.bitfield, pc.bitfield_len);
    if (piece_index < 0) break;                  /* nothing left this peer has that we need */

    if (!download_piece(&pc, pm, piece_index)) {
        piece_manager_release_piece(pm, piece_index);   /* put it back for someone else */
        break;                                    /* connection's bad, or we got choked */
    }
}

peer_close(&pc);
```

Every layer discussed above — handshake (6.1), framing (6.2), rarest-first selection (7.2), block requests and storage (7.3), verification (7.4) — composes into this one straight-line function. That's deliberate: keeping the entire single-peer protocol logic in one readable loop, with no peer-specific state living anywhere else, is what makes "run this on N threads" in the next section trivial rather than its own separate source of bugs.

### 8.4 Spawning and Joining Threads

```c
HANDLE* threads = calloc(count, sizeof(HANDLE));

for (uint64_t i = 0; i < count; i++) {
    PeerThreadArgs* args = malloc(sizeof(PeerThreadArgs));
    args->torrent = torrent;
    args->pm = pm;
    args->peer_ip = peers[i].ip;
    args->peer_port = ntohs(peers[i].port);
    memcpy(args->my_peer_id, my_peer_id, 20);

    threads[spawned++] = CreateThread(NULL, 0, peer_thread_entry, args, 0, NULL);
}

WaitForMultipleObjects((DWORD)spawned, threads, TRUE, INFINITE);
```

Each thread gets its own heap-allocated argument struct (freed by the thread itself once `peer_download_session()` returns) so there's no risk of one thread reading another's in-flight arguments off a shared stack frame. `start_download()` blocks on `WaitForMultipleObjects(..., TRUE, ...)` — wait for *all* handles — until every peer session has finished, one way or another, before deciding whether the overall download is complete. A simplified, single-threaded fallback path (compiled in on non-Windows builds) runs the same `peer_download_session()` against peers sequentially instead — useful for portability/testing, though obviously much slower against a real multi-peer swarm.

---

## 9. From Buffer to Disk

Everything above writes into one contiguous `pm->file_buffer` — deliberately format-agnostic about single-file vs. multi-file torrents until the very end, since a piece's byte range doesn't care about file boundaries; a piece can straddle the end of one file and the start of the next in a multi-file torrent. `file_writer_save()` is what reverses that, once (and only once) every piece has been verified:

```c
if (torrent->num_files == 0) {
    /* single-file: <output_dir>/<name> is the whole buffer */
    return write_range(path, buffer, total_length);
}

/* multi-file: walk torrent->files in order, since each file's
   bytes are laid out back-to-back in the buffer at a running offset */
uint64_t offset = 0;
for (uint64_t i = 0; i < torrent->num_files; i++) {
    const TorrentFile* tf = &torrent->files[i];
    write_range(path_for(tf), buffer + offset, tf->length);
    offset += tf->length;
}
```

Directory components (from each file's `path` list, Section 4.2) get created recursively as needed with a small best-effort `make_dirs_recursive()` helper before any file inside them is opened for writing. This whole layer is intentionally the simplest possible thing that works — one pass, whole-buffer writes — which is exactly why it's also the piece of the design most directly implicated in [the missing resume support](#152-no-resume-support-the-big-one): nothing touches disk until the very end, so a crash at 99% loses everything downloaded so far.

---

## 10. Thread-Safe Logging

With dozens of peer threads all logging concurrently, the one thing that has to be correct is that lines don't interleave mid-print — otherwise you get garbled output that's actively misleading rather than just noisy. `log.c` solves this with a single mutex wrapped tightly around just the `printf`, not around any of the work that produces the log line's arguments:

```c
void log_msg(LogLevel level, uint32_t peer_ip, uint16_t peer_port, const char* fmt, ...) {
    if (level > g_log_level) return;

    LOG_LOCK(&g_log_lock);
    printf("[%8.3f] [%s] [%-21s] ", elapsed_seconds(), level_name(level), peer_buf);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
    fflush(stdout);
    LOG_UNLOCK(&g_log_lock);
}
```

Timestamps are seconds-since-`log_init()` off a monotonic clock (`GetTickCount64()` on Windows, `clock_gettime(CLOCK_MONOTONIC, ...)` elsewhere), not wall-clock time — simpler, portable across the two threading backends already in use elsewhere in the project, and closer to what you actually want when reading logs anyway ("how far into the download did this happen"). Every line is tagged with the peer's `ip:port` (or `-` for session-wide messages with no specific peer) and one of four levels — `ERROR`/`WARN`/`INFO`/`DEBUG` — filtered by `log_set_level()`; the default `INFO` gives you connection lifecycle, handshake outcomes, unchokes, and piece completions without the per-message noise that `DEBUG` adds (every bitfield, every have, every block request).

---

## 11. Putting It All Together: `main.c`

`main.c` is the thin conductor that calls every layer above in order, with a cleanup path at each failure point so partial state never leaks:

```c
char* buffer = read_file(argv[1], &file_size);
Parser parser = { .data = buffer, .size = file_size, .pos = 0 };

BValue* root = decode_value(&parser);              /* §3 */
Torrent* torrent = torrent_parse(root);             /* §4 */

TrackerHTTPGetResponse* response = connect_tracker(torrent);   /* §5 */

PieceManager* pm = piece_manager_create(torrent);
peer_network_init();

int complete = start_download(torrent, pm, response->peers,     /* §6, §7, §8 */
                               response->num_peers, MY_PEER_ID);

if (complete)
    file_writer_save(torrent, pm->file_buffer, pm->total_length, "downloads");   /* §9 */
```

`setvbuf(stdout, NULL, _IONBF, 0)` at the very top is a small but worthwhile habit for any program that might block on network I/O: without it, buffered `printf` output can sit unflushed and never appear if the program later hangs inside a socket call, making a hang look like it produced *zero* output when it actually produced plenty, just not yet visible — a genuinely confusing thing to debug without this line.

---

## 12. Project Structure

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

## 13. File-by-File Reference

A quick-lookup companion to Sections 3–11 above, in case you want the map without the walkthrough:

| File | Covers |
|---|---|
| `btypes.h` / `btypes.c` | `BValue` tagged union, constructors, `dict_get`/`list_get`, recursive `destroy_value` — [§3.2](#32-representing-bencoded-data-in-c-bvalue) |
| `bencoder.h` / `bencoder.c` | Bencode decode/encode, the `Parser` cursor, overflow/malformed-input guards — [§3.3–3.5](#33-writing-the-decoder) |
| `RFC1738_url_parser.h` / `RFC1738_url_parser.c` | Splitting announce URLs into host/port/path; percent-encoding the info hash for HTTP announces — [§5.2](#52-http-trackers-bep-3) |
| `sha1.h` / `sha1.c` | From-scratch FIPS 180-4 SHA1 — [§4.4](#44-writing-sha1-from-scratch) |
| `torrent.h` / `torrent.c` | `BValue` → typed `Torrent` struct, info-hash computation — [§4](#4-the-torrent-file-and-the-info-hash) |
| `tracker.h` / `tracker.c` | HTTP (BEP 3) and UDP (BEP 15) tracker announces, normalized into one response shape — [§5](#5-talking-to-a-tracker) |
| `peer.h` / `peer.c` | Handshake, length-prefixed message framing, message senders — [§6](#6-the-peer-wire-protocol) |
| `piece_manager.h` / `piece_manager.c` | Rarest-first selection, block bookkeeping, SHA1 verification, the shared buffer — [§7](#7-pieces-blocks-and-verification) |
| `downloader.h` / `downloader.c` | Single-peer session logic, thread-per-peer spawning/joining — [§8](#8-concurrency-one-thread-per-peer) |
| `file_writer.h` / `file_writer.c` | Buffer → single-file or multi-file layout on disk — [§9](#9-from-buffer-to-disk) |
| `log.h` / `log.c` | Thread-safe, leveled, timestamped logging — [§10](#10-thread-safe-logging) |
| `main.c` | Wires every layer together end-to-end — [§11](#11-putting-it-all-together-mainc) |

---

## 14. Current Capabilities

- Full bencode decode/encode with overflow-safe, malformed-input-resistant parsing.
- `.torrent` parsing for both single-file and multi-file layouts, with correct info-hash computation from the original bytes.
- Tracker announces over both HTTP (BEP 3) and UDP (BEP 15), with bounded connect/receive timeouts and proper backoff.
- Full peer wire protocol handshake and message framing.
- Thread-per-peer concurrent downloading (up to 61 peers at once) against a shared, rarest-first piece manager.
- Per-piece SHA1 verification, with automatic re-queueing of any piece that fails verification.
- Thread-safe logging across the whole download lifecycle.

## 15. Known Limitations & Roadmap

This section is deliberately blunt about what isn't done yet — the plan from here, roughly in the order it matters.

### 15.1 Magnet Links & Block-Level Thread Ownership

Two separate gaps worth naming together since they're both "the next layer of the protocol that isn't built yet":

- **No magnet link support.** Every download currently requires an actual `.torrent` file in hand — there's no DHT, no peer exchange, and no metadata-exchange extension (BEP 9) to bootstrap a download from just an info hash and a list of trackers/peers, which is how magnet links actually work. Right now, no `.torrent` file means no download.
- **Threads own whole pieces, not individual blocks.** The current model gives each peer thread exclusive ownership of a *piece* (typically a few hundred KB) for its whole duration — clean for locking (Section 8.1), but it means a single slow or stalled peer can sit on a piece it's only half-downloaded while other, faster peers sit idle with nothing to do for that piece. A proper implementation would let multiple peers contribute blocks to the same piece, which needs finer-grained (block-level) ownership tracking instead of the current piece-level state machine.

### 15.2 No Resume Support (the big one)

This is the most important thing missing, and it's a direct consequence of the current architecture: the entire torrent lives in a single in-memory buffer for the whole download, and is only ever written to disk *once*, at the very end, after every piece has already been verified ([§9](#9-from-buffer-to-disk)). That means:

- If the program crashes, is closed, or the system restarts at 99% completion, **none of that progress is recoverable** — the buffer is gone, and the next run starts from zero.
- There's currently no concept of a "partial download" on disk at all to resume *from*.

The fix is really two changes that go together:

1. **Write pieces (or chunks of pieces) to disk as they're verified**, instead of holding the whole torrent in RAM until the end. This also has the side benefit of bounding memory usage for very large torrents, which the current single-buffer design doesn't do.
2. **On startup, before announcing anything, check for an existing partial download** for the given `.torrent` file. If one exists, read it back, re-verify each piece against its SHA1 (the "ground truth" from the `.torrent`, per [§7.4](#74-verifying-a-completed-piece)), and use that to populate the piece manager's missing-pieces state directly — so the download resumes from wherever it actually left off instead of re-fetching data that's already sitting correctly on disk. If no partial download exists, fall back to the current from-scratch behavior.

This is the next real feature to build, ahead of anything else in this list.

### 15.3 A Terminal UI

Right now the client's only interface is scrolling log lines. A proper TUI is in progress — most likely built with Python's [Textual](https://textual.textualize.io/), though C/C++ (ncurses or similar) or Rust (ratatui) are also on the table depending on how it ends up integrating with the C backend. The likely shape is a thin process boundary: the C client keeps doing the actual downloading and exposes progress (per-piece status, per-peer state, transfer rate) through some simple channel — a local socket, a status file, or similar — that the TUI polls or subscribes to, rather than embedding a scripting runtime inside the C binary itself.

### 15.4 Cross-Platform Sockets

The project has been Windows-only throughout, and that was a deliberate, if temporary, choice — the first priority was getting a client that actually connects to real trackers and real peers and fetches real data, not portability. That shows up unevenly across the codebase:

- **Threading is already mostly cross-platform.** `piece_manager.c` and `log.c` use a small macro shim (`CRITICAL_SECTION` on Windows, `pthread_mutex_t` elsewhere) for their locks ([§8.2](#82-what-actually-needs-a-mutex)), and `downloader.c` has a working (if simplified, single-threaded) non-Windows fallback path ([§8.4](#84-spawning-and-joining-threads)). This part of the port is largely done.
- **Socket code is not.** `peer.c` and `tracker.c` are written directly against Winsock (`WinSock2.h`, `WSAStartup`, `ioctlsocket`, `SOCKET`/`INVALID_SOCKET`, `closesocket`, etc.). `peer.h` has the bones of a POSIX fallback (`typedef int socket_t`, `close()` aliased to `closesocket()`) for syntax-checking on other platforms, but the actual connect-timeout logic, `WSAGetLastError()` error handling, and DNS resolution in `tracker.c` are Windows-specific and haven't been exercised on Linux/macOS at all.

Making this genuinely cross-platform means finishing that translation layer properly — abstracting the handful of Windows-specific socket calls behind the same kind of shim already used for threading — rather than the current state, where the POSIX branches exist mostly to let the portable logic compile-check on this sandbox.

## 16. Further Reading

The primary sources this implementation follows, in the order they're most useful while reading the code above:

- [BEP 3 — The BitTorrent Protocol Specification](https://www.bittorrent.org/beps/bep_0003.html) — bencoding, `.torrent` structure, the HTTP tracker protocol, and the peer wire protocol, all in one document.
- [BEP 15 — UDP Tracker Protocol](https://www.bittorrent.org/beps/bep_0015.html) — the connect/announce/scrape binary format and required backoff behavior.
- [BEP 9 — Extension for Peers to Send Metadata Files](https://www.bittorrent.org/beps/bep_0009.html) — relevant background for anyone picking up the magnet-link gap in [§15.1](#151-magnet-links--block-level-thread-ownership).
- [theory.org's unofficial BitTorrent specification](https://wiki.theory.org/BitTorrentSpecification) — a more narrative, example-heavy companion to the terser official BEPs; useful for double-checking edge cases like the compact peer format.
- [FIPS PUB 180-4 — Secure Hash Standard](https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.180-4.pdf) — the SHA1 (and SHA2) specification `sha1.c` implements directly.

---

## 17. Building and Running

The project builds with `make` instead of a hand-typed `gcc` invocation. The Makefile:

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
- `make` builds everything; `make clean` removes all object files and the binary; `make run` builds and then runs `./main` — but note the `run` target doesn't currently accept arguments, and this program requires a `.torrent` file path. Either run the binary directly after building (see [Usage](#18-usage) below), or extend the Makefile with something like `run: $(TARGET) ; ./$(TARGET) $(ARGS)` if you want `make run ARGS=some.torrent` to work.
- The build produces a single `main.exe` (or `main` without an extension, depending on your MinGW setup) at the project root.

## 18. Usage

```
main.exe <path-to-torrent-file>
```

On a successful run, the client parses the `.torrent`, announces to its tracker, connects to peers concurrently, downloads and verifies every piece, and writes the result under a `downloads/` directory (named after the torrent, with the original file layout preserved for multi-file torrents).

Set `BT_LOG_LEVEL=debug` in the environment beforehand for verbose per-message logging if something needs debugging.

---

## 19. Closing Notes

Nothing here is pretending to be a production BitTorrent client — there's no DHT, no encryption, no upload/seeding logic, no endgame mode, and (per Section 15.2) no resume support yet. What it is: a working, concurrent, protocol-correct client built from raw sockets and bencoded bytes up, with each layer built and reasoned about deliberately rather than pulled in as a dependency, and documented here in enough depth that you should be able to build your own version of any single layer — the bencoder, the tracker client, the peer wire protocol, the piece manager — by reading its section and its source side by side. The roadmap in Section 15 is the honest next-steps list, not a wish list: resume support and cross-platform sockets in particular are overdue rather than optional.