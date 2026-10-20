/*
** walseal_sqlcipher.c -- key walseal from SQLCipher's derived key
** (REQ6/ENC-3), and keep it on that key when a statement changes it
** (REQ1/DUR-2).
**
** #include this after sqlite3.c, in the same translation unit: it reaches
** SQLCipher's codec through functions the amalgamation keeps static.
*/
#include "walseal.h"

/* SQLCipher's derived encryption key for the database of pPager, unmasked
** into k (64 bytes): the read context's, or (bWrite) the write context's,
** which while PRAGMA rekey writes the database out is the new key.  *pn is
** 0 when there is no codec.  If SQLCipher has not derived the key yet, it
** does so here, exactly as the first page read or write would. */
static int codec_key(Pager *pPager, int bWrite, unsigned char *k, int *pn){
  codec_ctx *ctx = (codec_ctx*)sqlcipherPagerGetCodec(pPager);
  int rc;
  *pn = 0;
  if(!ctx) return SQLITE_OK;
  if(ctx->key_sz > 64) return SQLITE_ERROR;
  rc = sqlcipher_codec_key_derive(ctx);
  if(rc != SQLITE_OK) return rc;
  /* SQLCipher keeps keys masked in memory (sqlcipher_shield is an XOR):
  ** unmask a copy. */
  memcpy(k, bWrite ? ctx->write_ctx->key : ctx->read_ctx->key, ctx->key_sz);
  sqlcipher_shield(k, ctx->key_sz);
  *pn = ctx->key_sz;
  return SQLITE_OK;
}

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
  unsigned char k[64];
  int n = 0, rc;
  if(i < 0 || i >= db->nDb || !db->aDb[i].pBt) return SQLITE_MISUSE;

  sqlite3_mutex_enter(db->mutex);
  rc = codec_key(sqlite3BtreePager(db->aDb[i].pBt), 0, k, &n);
  sqlite3_mutex_leave(db->mutex);
  if(rc != SQLITE_OK) return rc;

  rc = n ? walseal_key(db, zDb, k, n) : walseal_key(db, zDb, NULL, 0);
  sqlcipher_memset(k, 0, sizeof(k));
  return rc;
}

/* ---- a key changed during the session -----------------------------------
**
** PRAGMA key, rekey, hexkey, hexrekey, textkey and textrekey act while their
** statement is prepared; rekey then rewrites every page under the new key in
** one transaction.  Frames of one WAL must all be sealed under one key, and
** that must be the key the next open derives.  So:
**   1. before such a pragma runs, the authorizer checkpoints (TRUNCATE), so
**      the WAL is empty and the next commit starts a new one;
**   2. walseal takes its key from the codec's write key each time a new WAL
**      starts (walseal_key_source), so rekey's commit is sealed under the
**      new key: a crash after it reopens under the new key;
**   3. after the statement, walseal_codec_settle checkpoints again, so the
**      database file is under the new key too and the old key opens nothing.
** Sealing rekey's frames under the old key instead would leave, after a
** crash, a WAL the new key cannot read over a file the new key cannot read:
** neither key would open the database. */

static const char *const key_pragmas[] = {
  "key", "rekey", "hexkey", "hexrekey", "textkey", "textrekey"
};

typedef struct Follow { sqlite3 *db; int iDb; int changed; } Follow;
#define FOLLOW "walseal_follow"            /* the connection's client data */

/* walseal's key source: the codec's write key (the read key, except while
** rekey writes).  Called inside SQLite's commit, under the database mutex. */
static int follow_key(void *pPager, unsigned char *k, int *pn){
  return codec_key((Pager*)pPager, 1, k, pn);
}

/* The authorizer, while a statement is prepared: before a key pragma on our
** database, empty the WAL.  Refused (SQLITE_DENY, "not authorized") when
** that cannot be done safely:
**   - inside a transaction, where the checkpoint cannot run, so rekey would
**     add its frames to a WAL sealed under the old key;
**   - when the connection could not read its database (the worker set
**     SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE): SQLite then took the WAL for empty,
**     and checkpointing or starting a new WAL would erase its commits.
**     Close, and open again with the right key. */
static int follow_auth(void *pArg, int op, const char *zName, const char *zValue,
                       const char *zSchema, const char *zTrigger){
  Follow *f = pArg;
  int i, n = (int)(sizeof(key_pragmas)/sizeof(key_pragmas[0]));
  int nLog = -1, nCkpt = -1;
  (void)zTrigger;
  if(op != SQLITE_PRAGMA || !zValue) return SQLITE_OK;
  for(i = 0; i < n && sqlite3_stricmp(zName, key_pragmas[i]); i++){}
  if(i == n || sqlcipher_find_db_index(f->db, zSchema) != f->iDb) return SQLITE_OK;
  if(f->db->flags & SQLITE_NoCkptOnClose) return SQLITE_DENY;
  /* sqlite3Checkpoint, not sqlite3_wal_checkpoint_v2: we are inside
  ** sqlite3_prepare, which holds the mutex and owns the error state. */
  if(sqlite3Checkpoint(f->db, f->iDb, SQLITE_CHECKPOINT_TRUNCATE, &nLog, &nCkpt) != SQLITE_OK
     || nLog > 0) return SQLITE_DENY;
  f->changed = 1;
  return SQLITE_OK;
}

/* Keep walseal on database zDb's key when a statement changes it: install
** the authorizer and walseal's key source above (this replaces any
** authorizer the connection had).  Call after walseal_key_from_codec. */
int walseal_follow_codec(sqlite3 *db, const char *zDb){
  int i = sqlcipher_find_db_index(db, zDb);
  Follow *f;
  int rc;
  if(i < 0 || i >= db->nDb || !db->aDb[i].pBt) return SQLITE_MISUSE;
  if(!(f = sqlite3_malloc(sizeof(*f)))) return SQLITE_NOMEM;
  f->db = db;
  f->iDb = i;
  f->changed = 0;
  /* freed when the connection closes; SQLite calls no authorizer after that */
  rc = sqlite3_set_clientdata(db, FOLLOW, f, sqlite3_free);
  if(rc != SQLITE_OK){ sqlite3_free(f); return rc; }
  rc = sqlite3_set_authorizer(db, follow_auth, f);
  if(rc == SQLITE_OK) rc = walseal_key_source(db, zDb, follow_key, sqlite3BtreePager(db->aDb[i].pBt));
  return rc;
}

/* After each statement: if it changed the key, checkpoint (TRUNCATE), so the
** database file is under the new key and the old one opens nothing.  Until
** then, after a crash, the old key opens the database as it was before the
** change (each key at a committed state).  Returns 1 if it checkpointed, 0 if
** no key changed, -1 if the checkpoint failed (sqlite3_errmsg says why). */
int walseal_codec_settle(sqlite3 *db){
  Follow *f = sqlite3_get_clientdata(db, FOLLOW);
  if(!f || !f->changed) return 0;
  f->changed = 0;
  return sqlite3_wal_checkpoint_v2(db, db->aDb[f->iDb].zDbSName, SQLITE_CHECKPOINT_TRUNCATE, 0, 0)
         == SQLITE_OK ? 1 : -1;
}
