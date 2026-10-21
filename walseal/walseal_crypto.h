/*
** walseal_crypto.h -- the cryptography walseal uses, from whichever library
** the build links: AES-256 on whole blocks (the PRP's block cipher) and
** HMAC-SHA256 (its key derivation).
**
** Backends: walseal_crypto_libressl.c (LibreSSL's libcrypto, here) and
** walseal_crypto_psa.c (the PSA Crypto API, in sqlcipher-embedded).  Link
** exactly one.  Each function returns SQLITE_OK or SQLITE_ERROR.
*/
#ifndef WALSEAL_CRYPTO_H
#define WALSEAL_CRYPTO_H
#include <stddef.h>
#include "sqlite3.h"

typedef struct walseal_aes walseal_aes;

/* An AES-256 encryption key schedule for key (32 bytes), in *pp. */
int walseal_aes_new(walseal_aes **pp, const unsigned char *key);

/* Encrypt n bytes (a multiple of 16), each 16-byte block on its own (ECB),
** from in to out; in and out may be the same buffer. */
int walseal_aes_encrypt(walseal_aes *p, const unsigned char *in, unsigned char *out, size_t n);

/* Wipe and free p; p may be NULL. */
void walseal_aes_free(walseal_aes *p);

/* out (32 bytes) = HMAC-SHA256 of msg under key. */
int walseal_hmac_sha256(const unsigned char *key, int nKey,
                        const unsigned char *msg, int nMsg, unsigned char *out);

/* Zero n bytes at p, in a way the compiler cannot drop. */
void walseal_cleanse(void *p, size_t n);

#endif
