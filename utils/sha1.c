/*
 * sha1.c — SHA-1 implementation (FIPS PUB 180-4)
 *
 * Design notes
 * ------------
 *  - All internal words are uint32_t / uint64_t (no assumed int width).
 *  - ROTL32 is written to avoid undefined behaviour on shift-by-zero and
 *    signed-integer overflow; compilers reduce it to a single rotate
 *    instruction on x86/ARM.
 *  - Round constants and the message-schedule expansion are kept local to
 *    this file; nothing leaks into the public header.
 *  - The context is zeroed in sha1_final() so sensitive mid-state data
 *    does not linger on the stack or heap after use.
 */

#include "sha1.h"

#include <string.h>


/*
 * ROTL32 - rotate a 32-bit word left by n bits (0 <= n <= 31).
 *
 * The cast to uint32_t before shifting right guards against
 * implementation-defined behaviour on right-shifting a negative value
 * (would only matter if the compiler promotes to signed int, which it
 * shouldn't for uint32_t, but the explicit cast makes the intent clear).
 */
#define ROTL32(x, n) \
    (((uint32_t)(x) << (n)) | ((uint32_t)(x) >> (32u - (n))))

/*
 * SHA-1 round constants (FIPS 180-4 §4.2.1).
 */
static const uint32_t K[4] = {
    UINT32_C(0x5A827999),   /* rounds  0-19 */
    UINT32_C(0x6ED9EBA1),   /* rounds 20-39 */
    UINT32_C(0x8F1BBCDC),   /* rounds 40-59 */
    UINT32_C(0xCA62C1D6),   /* rounds 60-79 */
};

/*
 * Initial hash values H(0) (FIPS 180-4 §5.3.1).
 */
static const uint32_t H0[5] = {
    UINT32_C(0x67452301),
    UINT32_C(0xEFCDAB89),
    UINT32_C(0x98BADCFE),
    UINT32_C(0x10325476),
    UINT32_C(0xC3D2E1F0),
};

/*
 * sha1_compress() - process one 64-byte block.
 *
 * @state: current hash state H[0..4]; updated in place.
 * @block: exactly 64 bytes of message data.
 */
static void sha1_compress(uint32_t state[5], const uint8_t block[SHA1_BLOCK_SIZE])
{
    uint32_t W[80];
    uint32_t a, b, c, d, e, f, T;
    unsigned int i;

    /* ---- Message schedule (FIPS 180-4 §6.1.2, step 1) ---- */

    /* Words 0-15: unpack big-endian bytes into 32-bit words */
    for (i = 0; i < 16; ++i) {
        W[i] = ((uint32_t)block[i * 4    ] << 24)
             | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] <<  8)
             | ((uint32_t)block[i * 4 + 3]      );
    }

    /* Words 16-79: expand with the SHA-1 recurrence */
    for (i = 16; i < 80; ++i) {
        W[i] = ROTL32(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);
    }

    /* ---- Working variables (FIPS 180-4 §6.1.2, step 2) ---- */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    /* ---- 80 rounds (FIPS 180-4 §6.1.2, step 3) ---- */

    /* Rounds 0-19: f(b,c,d) = Ch(b,c,d) = (b & c) | (~b & d) */
    for (i = 0; i < 20; ++i) {
        f = (b & c) | (~b & d);
        T = ROTL32(a, 5) + f + e + K[0] + W[i];
        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = T;
    }

    /* Rounds 20-39: f(b,c,d) = Parity(b,c,d) = b ^ c ^ d */
    for (; i < 40; ++i) {
        f = b ^ c ^ d;
        T = ROTL32(a, 5) + f + e + K[1] + W[i];
        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = T;
    }

    /* Rounds 40-59: f(b,c,d) = Maj(b,c,d) = (b & c) | (b & d) | (c & d) */
    for (; i < 60; ++i) {
        f = (b & c) | (b & d) | (c & d);
        T = ROTL32(a, 5) + f + e + K[2] + W[i];
        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = T;
    }

    /* Rounds 60-79: f(b,c,d) = Parity(b,c,d) = b ^ c ^ d */
    for (; i < 80; ++i) {
        f = b ^ c ^ d;
        T = ROTL32(a, 5) + f + e + K[3] + W[i];
        e = d;
        d = c;
        c = ROTL32(b, 30);
        b = a;
        a = T;
    }

    /* ---- Intermediate hash value (FIPS 180-4 §6.1.2, step 4) ---- */
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;

    /* Scrub the message schedule from the stack */
    memset(W, 0, sizeof(W));
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void sha1_init(SHA1_CTX *ctx)
{
    memcpy(ctx->state, H0, sizeof(H0));
    ctx->bitlen = 0;
    ctx->buflen = 0;
    /* buf[] content is irrelevant until written by sha1_update */
}

void sha1_update(SHA1_CTX *ctx, const void *data, size_t len)
{
    const uint8_t *src = (const uint8_t *)data;

    while (len > 0) {
        /*
         * How many bytes can we copy into the current partial block?
         * Fill it up, then compress if we have a full block.
         */
        uint32_t room = SHA1_BLOCK_SIZE - ctx->buflen;
        uint32_t take = (len < room) ? (uint32_t)len : room;

        memcpy(ctx->buf + ctx->buflen, src, take);
        ctx->buflen += take;
        src         += take;
        len         -= take;

        if (ctx->buflen == SHA1_BLOCK_SIZE) {
            sha1_compress(ctx->state, ctx->buf);
            ctx->bitlen += SHA1_BLOCK_SIZE * 8u;
            ctx->buflen  = 0;
        }
    }
}

void sha1_final(SHA1_CTX *ctx, uint8_t hash[SHA1_DIGEST_SIZE])
{
    unsigned int i;

    /*
     * Account for the bytes still sitting in the buffer before padding.
     * The total bit length must include all bytes ever fed in.
     */
    uint64_t total_bits = ctx->bitlen + (uint64_t)ctx->buflen * 8u;

    /*
     * Append the mandatory 0x80 padding byte.
     * buflen is at most 63 here, so there is always room.
     */
    ctx->buf[ctx->buflen++] = 0x80;

    /*
     * If the 0x80 byte left fewer than 8 bytes for the length field,
     * pad to the end of the current block, compress it, then start a
     * fresh block that will hold only the length.
     */
    if (ctx->buflen > SHA1_BLOCK_SIZE - 8) {
        memset(ctx->buf + ctx->buflen, 0, SHA1_BLOCK_SIZE - ctx->buflen);
        sha1_compress(ctx->state, ctx->buf);
        ctx->buflen = 0;
    }

    /* Zero-pad up to the 8-byte length field */
    memset(ctx->buf + ctx->buflen, 0, (SHA1_BLOCK_SIZE - 8) - ctx->buflen);

    /*
     * Append the 64-bit big-endian bit length in the last 8 bytes
     * (FIPS 180-4 §5.1.1).
     */
    ctx->buf[56] = (uint8_t)(total_bits >> 56);
    ctx->buf[57] = (uint8_t)(total_bits >> 48);
    ctx->buf[58] = (uint8_t)(total_bits >> 40);
    ctx->buf[59] = (uint8_t)(total_bits >> 32);
    ctx->buf[60] = (uint8_t)(total_bits >> 24);
    ctx->buf[61] = (uint8_t)(total_bits >> 16);
    ctx->buf[62] = (uint8_t)(total_bits >>  8);
    ctx->buf[63] = (uint8_t)(total_bits      );

    sha1_compress(ctx->state, ctx->buf);

    /*
     * Serialise the five 32-bit state words into 20 big-endian bytes
     * (FIPS 180-4 §6.1.2, step 4 / §3.1).
     */
    for (i = 0; i < 5; ++i) {
        hash[i * 4    ] = (uint8_t)(ctx->state[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(ctx->state[i] >>  8);
        hash[i * 4 + 3] = (uint8_t)(ctx->state[i]      );
    }

    /* Zero the context so mid-state data does not linger in memory */
    memset(ctx, 0, sizeof(*ctx));
}

void sha1_digest(const void *data, size_t len, uint8_t hash[SHA1_DIGEST_SIZE])
{
    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, hash);
}