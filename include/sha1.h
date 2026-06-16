#ifndef SHA1_H
#define SHA1_H

/*
 * sha1.h — SHA-1 (Secure Hash Algorithm 1) interface
 *
 * Produces a 160-bit (20-byte) digest as specified in FIPS PUB 180-4.
 *
 * Usage (streaming):
 *   SHA1_CTX ctx;
 *   sha1_init(&ctx);
 *   sha1_update(&ctx, data, len);   // call as many times as needed
 *   sha1_final(&ctx, hash);         // writes 20 bytes into hash[]
 *
 * Usage (one-shot):
 *   sha1_digest(data, len, hash);   // writes 20 bytes into hash[]
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Output size of SHA-1 in bytes. */
#define SHA1_DIGEST_SIZE 20

/** Internal block size of SHA-1 in bytes. */
#define SHA1_BLOCK_SIZE  64

/**
 * SHA1_CTX - incremental hashing context.
 *
 * Treat as opaque; initialise with sha1_init() before use.
 */
typedef struct {
    uint8_t  buf[SHA1_BLOCK_SIZE]; /* partial input block            */
    uint32_t state[5];             /* running hash state (H0..H4)    */
    uint64_t bitlen;               /* total message length in bits   */
    uint32_t buflen;               /* bytes currently held in buf[]  */
} SHA1_CTX;

/**
 * sha1_init() - initialise a fresh hashing context.
 * @ctx: context to initialise; must not be NULL.
 */
void sha1_init(SHA1_CTX *ctx);

/**
 * sha1_update() - feed message bytes into the hash.
 * @ctx:  context previously initialised with sha1_init().
 * @data: pointer to input bytes; may be NULL only when @len == 0.
 * @len:  number of bytes to process.
 *
 * May be called any number of times between sha1_init() and sha1_final().
 */
void sha1_update(SHA1_CTX *ctx, const void *data, size_t len);

/**
 * sha1_final() - finalise the hash and write the digest.
 * @ctx:  context previously fed with sha1_update().
 * @hash: output buffer; must be at least SHA1_DIGEST_SIZE (20) bytes.
 *
 * The context is zeroed after this call and must not be reused without
 * a fresh sha1_init().
 */
void sha1_final(SHA1_CTX *ctx, uint8_t hash[SHA1_DIGEST_SIZE]);

/**
 * sha1_digest() - one-shot convenience wrapper.
 * @data: pointer to input bytes.
 * @len:  number of bytes to hash.
 * @hash: output buffer; must be at least SHA1_DIGEST_SIZE (20) bytes.
 */
void sha1_digest(const void *data, size_t len, uint8_t hash[SHA1_DIGEST_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SHA1_H */