/*
** walseal.h -- a VFS shim that encrypts the WAL's headers in place.
** From sqlcipher-embedded, branch experiment/wal-seal, commit 5d50c676
** (walseal.c says what changed).
**
** SQLCipher encrypts WAL page images, but SQLite writes the 32-byte WAL
** header and each frame's 24-byte header (page number, commit size, salts,
** checksums) in the clear: without the key, the WAL shows which pages each
** transaction wrote and where commits fall.  walseal enciphers those bytes
** where they are (a length-preserving tweaked PRP over AES-256), so the WAL
** keeps SQLite's layout, write pattern and crash behaviour.  Other files
** pass through.
*/
#ifndef WALSEAL_H
#define WALSEAL_H
#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register "walseal" over the VFS named zUnder (NULL: the default). */
int walseal_register(const char *zUnder, int makeDefault);
/* The same under another VFS name (zName must outlive the registration). */
int walseal_register_as(const char *zName, const char *zUnder);

/* Give database zDb ("main") of db the key (>= 16 bytes) for its WAL
** headers.  Call after PRAGMA key and before the WAL is first opened
** (journal_mode=WAL or the first read of a WAL database).  Without it,
** opening the WAL fails.  key NULL and nKey 0: the WAL passes through,
** not enciphered (for an unencrypted database). */
int walseal_key(sqlite3 *db, const char *zDb, const void *key, int nKey);

#ifdef __cplusplus
}
#endif
#endif
