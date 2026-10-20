/*
** walseal_sqlcipher.c -- key walseal from SQLCipher's derived key
** (REQ6/ENC-3).
**
** #include this after sqlite3.c, in the same translation unit: it reaches
** SQLCipher's codec through functions the amalgamation keeps static.
*/
#include "walseal.h"

/* Give walseal the key for database zDb's WAL headers: SQLCipher's derived
** encryption key for that database (PBKDF2 of the passphrase, or the raw
** key), which walseal turns into its own with HMAC-SHA256 and a fixed label.
** So the headers cost an attacker the same key derivation as the pages.
**
** If SQLCipher has not derived the key yet, it does so here, exactly as the
** first page read would, and that read then finds it derived: one PBKDF2,
** not two.  Call after sqlite3_key and before anything reads the database,
** because the first read can open the WAL, and walseal refuses to open a WAL
** without a key.
**
** With no codec (no key: an unencrypted database, REQ2/ENC-2) the WAL passes
** through: its pages are in the clear anyway.
**
** A wrong passphrase still derives a key, so this succeeds; the first read
** then fails as REQ1/ENC-1 requires.  Returns an SQLite result code. */
int walseal_key_from_codec(sqlite3 *db, const char *zDb){
  int i = sqlcipher_find_db_index(db, zDb);
  codec_ctx *ctx;
  unsigned char k[64];
  int n = 0, rc = SQLITE_OK;
  if(i < 0 || i >= db->nDb || !db->aDb[i].pBt) return SQLITE_MISUSE;

  sqlite3_mutex_enter(db->mutex);
  ctx = (codec_ctx*)sqlcipherPagerGetCodec(sqlite3BtreePager(db->aDb[i].pBt));
  if(ctx){
    n = ctx->key_sz;
    if(n > (int)sizeof(k)) rc = SQLITE_ERROR;
    else rc = sqlcipher_codec_key_derive(ctx);
    if(rc == SQLITE_OK){
      /* SQLCipher keeps keys masked in memory (sqlcipher_shield is an XOR):
      ** unmask a copy. */
      memcpy(k, ctx->read_ctx->key, n);
      sqlcipher_shield(k, n);
    }
  }
  sqlite3_mutex_leave(db->mutex);
  if(rc != SQLITE_OK) return rc;

  rc = ctx ? walseal_key(db, zDb, k, n) : walseal_key(db, zDb, NULL, 0);
  sqlcipher_memset(k, 0, sizeof(k));
  return rc;
}
