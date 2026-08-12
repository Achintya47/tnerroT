#ifndef LOG_H
#define LOG_H

#include <stdint.h>

/* Ordered so "log everything up to level X" is a simple <= comparison. */
typedef enum {
    LOG_ERROR = 0,  /* internal/unexpected failures: bad allocations, protocol
                        invariants broken — things that shouldn't happen */
    LOG_WARN  = 1,  /* normal-in-a-swarm failures: a peer refused, choked us,
                        sent a bad piece, disconnected mid-transfer */
    LOG_INFO  = 2,  /* the "what's happening" narrative: connected, handshake
                        ok, unchoked, piece N verified, session summary */
    LOG_DEBUG = 3   /* per-message chatter: bitfield/have received, every
                        block requested — off by default, noisy but useful */
} LogLevel;

/* Call once, early in main(), before any peer threads are spawned. Safe to
   call more than once (subsequent calls are no-ops). Also called lazily
   and defensively by log_msg() itself if a caller forgets, so logging
   never silently does nothing — it just loses a bit of elapsed-time
   accuracy in that case. */
void log_init(void);

/* Only messages at this level or more severe (lower enum value = more
   severe) are printed. Default is LOG_INFO. */
void log_set_level(LogLevel level);

/**
 * @brief Thread-safe, timestamped log line. All peer threads share one
 *        lock around the actual printf, so concurrent logging from many
 *        peers never interleaves mid-line.
 * @param peer_ip, peer_port  Identify which peer this line is about, for
 *        readability when many threads are logging at once. Both network
 *        byte order (peer_ip) / host byte order (peer_port), matching
 *        PeerConnection's own fields. Pass (0, 0) for lines that aren't
 *        about a specific peer (e.g. overall download progress).
 */
void log_msg(LogLevel level, uint32_t peer_ip, uint16_t peer_port, const char* fmt, ...);

#endif