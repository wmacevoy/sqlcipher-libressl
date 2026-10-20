/*
** walseal_codec_test.c -- walseal keyed from SQLCipher's derived key, opened
** the way the WASM worker opens a database (REQ1/DUR-2, REQ6/ENC-3,
** REQ1/ENC-1, REQ2/ENC-2), natively on SQLite's unix VFS.
**
**   walseal_codec_test <dir>
**
** One translation unit with the amalgamation, as sqlcipher_wasm.c is, because
** walseal_key_from_codec reaches SQLCipher's static codec functions.  A
** "crash" is a copy of the database and its WAL taken while the connection is
** still open and nothing has been checkpointed.
*/
#include "sqlite3.c"
#include "walseal_sqlcipher.c"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, msg) do{ if(!(c)){ fprintf(stderr, "FAIL: %s\n", msg); fails++; } }while(0)

static unsigned char *slurp(const char *path, long *n){
  FILE *f = fopen(path, "rb");
  unsigned char *b;
  *n = 0;
  if(!f) return 0;
  fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
  b = malloc(*n ? *n : 1);
  if(fread(b, 1, *n, f) != (size_t)*n) *n = 0;
  fclose(f);
  return b;
}
static void copy(const char *from, const char *to){
  long n;
  unsigned char *b = slurp(from, &n);
  FILE *f = fopen(to, "wb");
  if(n) fwrite(b, 1, n, f);
  fclose(f);
  free(b);
}
static uint32_t be32(const unsigned char *a){ return (uint32_t)a[0] << 24 | (uint32_t)a[1] << 16 | (uint32_t)a[2] << 8 | a[3]; }

/* As the worker does: open, key, walseal from the derived key (and following it),
** locking_mode=EXCLUSIVE, journal_mode=WAL.  A key that cannot read the
** database (SQLITE_NOTADB) keeps the WAL on close, and is given again so the
** caller's first read fails afresh.  Returns "wal", "notadb", or "" on any
** other outcome. */
static const char *worker_open(const char *path, const char *key, sqlite3 **pdb){
  static char mode[16];
  sqlite3_stmt *s;
  int rc;
  mode[0] = 0;
  if(sqlite3_open(path, pdb) != SQLITE_OK) return mode;
  if(key && sqlite3_key(*pdb, key, (int)strlen(key)) != SQLITE_OK) return mode;
  if(walseal_key_from_codec(*pdb, "main") != SQLITE_OK) return mode;
  if(walseal_follow_codec(*pdb, "main") != SQLITE_OK) return mode;
  if(sqlite3_exec(*pdb, "PRAGMA locking_mode=EXCLUSIVE", 0, 0, 0) != SQLITE_OK) return mode;
  /* Preparing reads the schema, so a key that cannot read fails here or at the step. */
  rc = sqlite3_prepare_v2(*pdb, "PRAGMA journal_mode=WAL", -1, &s, 0);
  if(rc == SQLITE_OK){
    rc = sqlite3_step(s);
    if(rc == SQLITE_ROW) snprintf(mode, sizeof(mode), "%s", (const char*)sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
  }
  if(rc != SQLITE_ROW && sqlite3_errcode(*pdb) == SQLITE_NOTADB){
    sqlite3_db_config(*pdb, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, (int*)0);
    if(key) sqlite3_key(*pdb, key, (int)strlen(key));
    snprintf(mode, sizeof(mode), "notadb");
  }
  return mode;
}

static int count_rows(sqlite3 *db){
  sqlite3_stmt *s;
  int n = -1;
  if(sqlite3_prepare_v2(db, "SELECT count(*) FROM t", -1, &s, 0) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
    n = sqlite3_column_int(s, 0);
  sqlite3_finalize(s);
  return n;
}
static int integrity_ok(sqlite3 *db){
  sqlite3_stmt *s;
  int ok = 0;
  if(sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &s, 0) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
    ok = !strcmp((const char*)sqlite3_column_text(s, 0), "ok");
  sqlite3_finalize(s);
  return ok;
}

/* Header fields of a stored WAL in the clear: the magic or page size, and per
** frame a page number of this small database or the header's salts.  An
** enciphered page number is <= 1000 with probability 2^-22, and matches 8
** bytes of salt with 2^-64. */
static int clear_fields(const unsigned char *w, long nw){
  long off;
  int clear = nw >= 32 && ((be32(w) & 0xfffffffe) == 0x377f0682 || be32(w + 8) == 4096);
  for(off = 32; off + 24 + 4096 <= nw; off += 24 + 4096){
    uint32_t pg = be32(w + off);
    if((pg >= 1 && pg <= 1000) || !memcmp(w + off + 8, w + 16, 8)) clear++;
  }
  return clear;
}

/* One exec, as the worker runs it: the SQL, then settle a key change. */
static int run(sqlite3 *db, const char *sql){
  int rc = sqlite3_exec(db, sql, 0, 0, 0);
  return walseal_codec_settle(db) < 0 ? SQLITE_ERROR : rc;
}

static void crash_copy(const char *live, const char *crashed){
  char lw[600], cw[600];
  snprintf(lw, sizeof(lw), "%s-wal", live);
  snprintf(cw, sizeof(cw), "%s-wal", crashed);
  remove(crashed); remove(cw);
  copy(live, crashed);
  copy(lw, cw);
}

/* Opens path under key: "wal" with rows rows and integrity_check ok, or
** what went wrong. */
static const char *reopen(const char *path, const char *key, int rows){
  sqlite3 *db;
  const char *mode = worker_open(path, key, &db);
  const char *r = strcmp(mode, "wal") ? (strcmp(mode, "notadb") ? "error" : "notadb")
                : count_rows(db) != rows ? "wrong rows" : !integrity_ok(db) ? "integrity" : "ok";
  sqlite3_close(db);
  return r;
}

/* PRAGMA rekey under WAL (REQ1/DUR-2, REQ6/ENC-3): the WAL written after it
** must be sealed under the new key, so a crash right after the rekey, or
** after later commits, reopens under the new key with every committed row,
** and not under the old one. */
static void rekey_test(const char *dir){
  char live[512], lw[600], crashed[512], cw[600];
  unsigned char *w;
  long nw;
  sqlite3 *db;
  const char *r;
  int i, ok;
  snprintf(live, sizeof(live), "%s/rekey-live.db", dir);
  snprintf(lw, sizeof(lw), "%s-wal", live);
  snprintf(crashed, sizeof(crashed), "%s/rekey-crashed.db", dir);
  snprintf(cw, sizeof(cw), "%s-wal", crashed);
  remove(live); remove(lw);

  ok = !strcmp(worker_open(live, "key A", &db), "wal");
  ok = ok && run(db, "CREATE TABLE t(x TEXT)") == SQLITE_OK;
  for(i = 0; ok && i < 10; i++) ok = run(db, "INSERT INTO t VALUES ('a row under A')") == SQLITE_OK;
  /* inside a transaction the WAL cannot be emptied first: refused */
  ok = ok && run(db, "BEGIN; INSERT INTO t VALUES ('in the transaction')") == SQLITE_OK;
  CHECK(ok && run(db, "PRAGMA rekey = 'key B'") != SQLITE_OK && strstr(sqlite3_errmsg(db), "not authorized"),
        "rekey: refused inside a transaction");
  ok = ok && run(db, "ROLLBACK") == SQLITE_OK;
  /* the statement, then a crash before the worker settles it */
  ok = ok && sqlite3_exec(db, "PRAGMA rekey = 'key B'", 0, 0, 0) == SQLITE_OK;
  CHECK(ok, "rekey: write 10 rows under A, then PRAGMA rekey");
  crash_copy(live, crashed);
  CHECK(!strcmp(reopen(crashed, "key B", 10), "ok"), "rekey: crash right after it, reopen under B: 10 rows, integrity ok");
  r = reopen(crashed, "key A", 10);
  CHECK(!strcmp(r, "ok") || !strcmp(r, "notadb"), "rekey: crash right after it, under A: the old state or nothing");
  CHECK(walseal_codec_settle(db) == 1, "rekey: settled");
  crash_copy(live, crashed);
  CHECK(!strcmp(reopen(crashed, "key B", 10), "ok"), "rekey: settled, crash, reopen under B: 10 rows");
  CHECK(!strcmp(reopen(crashed, "key A", 10), "notadb"), "rekey: settled, the old key reads nothing");

  for(i = 0; ok && i < 10; i++) ok = run(db, "INSERT INTO t VALUES ('a row under B')") == SQLITE_OK;
  CHECK(ok, "rekey: 10 more rows under B");
  crash_copy(live, crashed);
  sqlite3_close(db);
  w = slurp(cw, &nw);
  CHECK(nw >= 32 + 24 + 4096 && clear_fields(w, nw) == 0, "rekey: the WAL after it holds frames, no header field in the clear");
  free(w);
  CHECK(!strcmp(reopen(crashed, "key B", 20), "ok"), "rekey: crash after more commits, reopen under B: 20 rows, integrity ok");
  CHECK(!strcmp(reopen(crashed, "key A", 20), "notadb"), "rekey: the old key reads nothing: file is not a database");
}

/* Commit n rows, one transaction each, then copy the database and its WAL
** to crashed while the connection is still open. */
static int write_then_crash(const char *live, const char *key, int n, const char *crashed){
  char lw[600], cw[600];
  sqlite3 *db;
  int i, ok;
  snprintf(lw, sizeof(lw), "%s-wal", live);
  snprintf(cw, sizeof(cw), "%s-wal", crashed);
  remove(live); remove(lw); remove(crashed); remove(cw);
  ok = !strcmp(worker_open(live, key, &db), "wal");
  if(ok && key){
    /* derived once, here: the first page read finds nothing left to derive */
    codec_ctx *ctx = sqlcipherPagerGetCodec(sqlite3BtreePager(db->aDb[0].pBt));
    CHECK(ctx && !ctx->read_ctx->derive_key && !ctx->write_ctx->derive_key, "key derived at open");
  }
  ok = ok && sqlite3_exec(db, "CREATE TABLE t(x TEXT)", 0, 0, 0) == SQLITE_OK;
  for(i = 0; ok && i < n; i++)
    ok = sqlite3_exec(db, "INSERT INTO t VALUES ('a committed row')", 0, 0, 0) == SQLITE_OK;
  copy(live, crashed);
  copy(lw, cw);
  sqlite3_close(db);
  return ok;
}

int main(int argc, char **argv){
  char live[512], crashed[512], cw[600];
  unsigned char *w, *w2;
  long nw, nw2;
  sqlite3 *db;

  if(argc < 2){ fprintf(stderr, "usage: walseal_codec_test <dir>\n"); return 2; }
  sqlite3_initialize();
  if(walseal_register(0, 1) != SQLITE_OK){ fprintf(stderr, "walseal_register failed\n"); return 2; }
  snprintf(live, sizeof(live), "%s/codec-live.db", argv[1]);
  snprintf(crashed, sizeof(crashed), "%s/codec-crashed.db", argv[1]);
  snprintf(cw, sizeof(cw), "%s-wal", crashed);

  /* Encrypted: the WAL on storage shows no header field (REQ6/ENC-3). */
  CHECK(write_then_crash(live, "passphrase", 50, crashed), "write 50 rows in WAL mode");
  w = slurp(cw, &nw);
  CHECK(nw >= 32 + 24 + 4096, "the crashed WAL holds frames");
  CHECK(clear_fields(w, nw) == 0, "no WAL magic, page size, page number or salt in the clear");

  /* A wrong key fails its first read (REQ1/ENC-1) and leaves the WAL alone. */
  CHECK(!strcmp(worker_open(crashed, "wrong passphrase", &db), "notadb"), "wrong key: file is not a database");
  CHECK(count_rows(db) == -1 && !strcmp(sqlite3_errmsg(db), "file is not a database"),
        "wrong key: the caller's first read fails as REQ1/ENC-1 says");
  sqlite3_close(db);
  CHECK(!strcmp(worker_open(crashed, 0, &db), "notadb"), "no key: file is not a database");
  CHECK(count_rows(db) == -1 && !strcmp(sqlite3_errmsg(db), "file is not a database"),
        "no key: the caller's first read fails as REQ1/ENC-1 says");
  sqlite3_close(db);
  w2 = slurp(cw, &nw2);
  CHECK(nw2 == nw && w2 && !memcmp(w, w2, nw), "wrong key leaves the WAL as it was");
  free(w); free(w2);

  /* PRAGMA key after a key that could not read: SQLite took the WAL for
  ** empty, so a new WAL would overwrite its commits.  Refused (REQ1/DUR-2). */
  CHECK(!strcmp(worker_open(crashed, "wrong passphrase", &db), "notadb"), "wrong key again");
  CHECK(run(db, "PRAGMA key = 'passphrase'") != SQLITE_OK && strstr(sqlite3_errmsg(db), "not authorized"),
        "PRAGMA key after a failed read: refused");
  run(db, "CREATE TABLE u(x)");
  sqlite3_close(db);

  /* The right key recovers every committed row from the WAL (REQ1/DUR-2). */
  CHECK(!strcmp(worker_open(crashed, "passphrase", &db), "wal"), "reopen in WAL mode");
  CHECK(count_rows(db) == 50, "50 committed rows after the crash");
  CHECK(integrity_ok(db), "integrity_check ok");
  sqlite3_close(db);

  /* No key: an unencrypted database's WAL passes through (REQ2/ENC-2). */
  CHECK(write_then_crash(live, 0, 5, crashed), "write 5 rows unencrypted");
  w = slurp(cw, &nw);
  CHECK(nw >= 32 && (be32(w) & 0xfffffffe) == 0x377f0682, "unencrypted WAL is stock");
  free(w);
  CHECK(!strcmp(worker_open(crashed, 0, &db), "wal"), "reopen unencrypted");
  CHECK(count_rows(db) == 5, "5 rows after the crash, unencrypted");
  sqlite3_close(db);

  rekey_test(argv[1]);

  printf("walseal codec: %s\n", fails ? "FAIL" : "ok");
  return fails != 0;
}
