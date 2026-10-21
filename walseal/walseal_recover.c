/*
** walseal_recover.c -- the recovery tool (REQ10/REC-1): a database and its
** walseal-sealed WAL in, a plain SQLCipher database out.
**
**   walseal_recover [-p PRAGMA]... <database> <output>  < key
**
** The key is the first line of standard input, not an argument, so it never
** shows in a process listing: a passphrase, or a raw key x'<64 hex>', or a
** raw key with the salt x'<96 hex>' (needed when page 1 may be torn).  Each
** -p runs as "PRAGMA <PRAGMA>" after the key, for databases written with
** settings other than SQLCipher's defaults (-p "cipher_page_size = 4096").
**
** It copies <database> and <database>-wal into a new directory beside
** <output> and works on the copies only, because SQLite without walseal (or
** under a wrong key) takes a sealed WAL for empty and deletes it on close.
** It opens the copy through walseal keyed from SQLCipher's derived key, as
** the WASM worker and sqlcipher-embedded do, checkpoints, leaves WAL mode,
** and requires integrity_check = ok.  Only then is the copy renamed to
** <output>: same key, salt and settings, no WAL.  On any failure, including
** a wrong key, it exits 1 and writes nothing.  It refuses an <output> that
** exists.
**
** One translation unit with the amalgamation, as walseal_sqlcipher.c needs.
*/
#include "sqlite3.c"
#include "walseal_sqlcipher.c"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail(const char *what, const char *why){
  fprintf(stderr, "walseal_recover: %s%s%s\n", what, why ? ": " : "", why ? why : "");
  return 0;
}

static int copy(const char *from, const char *to){
  FILE *in = fopen(from, "rb"), *out;
  char b[65536];
  size_t n;
  int ok = 1;
  if(!in) return fail(from, strerror(errno));
  if(!(out = fopen(to, "wb"))){ fclose(in); return fail(to, strerror(errno)); }
  while((n = fread(b, 1, sizeof(b), in)) > 0) if(fwrite(b, 1, n, out) != n) ok = 0;
  if(ferror(in)) ok = 0;
  fclose(in);
  if(fflush(out) != 0 || fsync(fileno(out)) != 0) ok = 0;
  if(fclose(out) != 0) ok = 0;
  return ok ? 1 : fail(to, "copy failed");
}

static int exists(const char *path){
  struct stat st;
  return stat(path, &st) == 0;
}

/* The single text value sql returns, into out; 0 on any error. */
static int one(sqlite3 *db, const char *sql, char *out, int nOut){
  sqlite3_stmt *s;
  int ok = 0;
  if(sqlite3_prepare_v2(db, sql, -1, &s, 0) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW){
    snprintf(out, nOut, "%s", sqlite3_column_text(s, 0) ? (const char*)sqlite3_column_text(s, 0) : "");
    ok = 1;
  }
  if(!ok) fail(sql, sqlite3_errmsg(db));
  sqlite3_finalize(s);
  return ok;
}

/* Checkpoint the copy at path into a database with no WAL, under key. */
static int recover(const char *path, const char *key, char **pragmas, int nPragma){
  sqlite3 *db = 0;
  char sql[512], v[64];
  int i, ok;

  if(sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, "walseal") != SQLITE_OK){
    fail(path, sqlite3_errmsg(db));
    sqlite3_close(db);
    return 0;
  }
  ok = sqlite3_key(db, key, (int)strlen(key)) == SQLITE_OK || fail("key", sqlite3_errmsg(db));
  for(i = 0; ok && i < nPragma; i++){
    snprintf(sql, sizeof(sql), "PRAGMA %s", pragmas[i]);
    ok = sqlite3_exec(db, sql, 0, 0, 0) == SQLITE_OK || fail(sql, sqlite3_errmsg(db));
  }
  ok = ok && (walseal_key_from_codec(db, "main") == SQLITE_OK || fail("walseal key", sqlite3_errmsg(db)));
  /* Before the first read, so the WAL index lives in memory and no -shm is used. */
  ok = ok && (sqlite3_exec(db, "PRAGMA locking_mode = EXCLUSIVE", 0, 0, 0) == SQLITE_OK || fail("locking_mode", sqlite3_errmsg(db)));
  /* A wrong key fails here: "file is not a database". */
  ok = ok && one(db, "SELECT count(*) FROM sqlite_schema", v, sizeof(v));
  ok = ok && one(db, "PRAGMA wal_checkpoint(TRUNCATE)", v, sizeof(v)) && (!strcmp(v, "0") || fail("checkpoint", "busy"));
  ok = ok && one(db, "PRAGMA journal_mode = DELETE", v, sizeof(v)) && (!strcmp(v, "delete") || fail("journal_mode", v));
  ok = ok && one(db, "PRAGMA integrity_check", v, sizeof(v)) && (!strcmp(v, "ok") || fail("integrity_check", v));
  if(sqlite3_close(db) != SQLITE_OK) ok = fail("close", "busy");
  return ok;
}

int main(int argc, char **argv){
  char *pragmas[16], key[1024], dir[1024], copyDb[1100], copyWal[1110], wal[1100], j[1100];
  const char *in, *out;
  int nPragma = 0, i, ok;
  size_t n;
  struct stat st;

  for(i = 1; i + 1 < argc && !strcmp(argv[i], "-p") && nPragma < 16; i += 2) pragmas[nPragma++] = argv[i + 1];
  if(argc - i != 2){
    fprintf(stderr, "usage: walseal_recover [-p PRAGMA]... <database> <output>  < key\n");
    return 2;
  }
  in = argv[i]; out = argv[i + 1];

  if(!fgets(key, sizeof(key), stdin)) key[0] = 0;
  n = strlen(key);
  if(n && key[n - 1] == '\n') key[--n] = 0;
  if(n && key[n - 1] == '\r') key[--n] = 0;
  if(!n){ fail("no key on standard input", 0); return 1; }

  snprintf(wal, sizeof(wal), "%s-wal", out);
  snprintf(j, sizeof(j), "%s-journal", out);
  if(exists(out) || exists(wal) || exists(j)){ fail(out, "exists"); return 1; }
  /* An empty file has no page 1 to check the key against: under a wrong key
  ** the WAL would read as empty and the output as an empty database. */
  if(stat(in, &st) != 0){ fail(in, strerror(errno)); return 1; }
  if(st.st_size == 0){ fail(in, "empty: nothing to check the key against"); return 1; }

  /* Beside the output, so the rename below stays on one file system. */
  snprintf(dir, sizeof(dir), "%s.recover-XXXXXX", out);
  if(!mkdtemp(dir)){ fail(dir, strerror(errno)); return 1; }
  snprintf(copyDb, sizeof(copyDb), "%s/db", dir);
  snprintf(copyWal, sizeof(copyWal), "%s/db-wal", dir);
  snprintf(wal, sizeof(wal), "%s-wal", in);

  sqlite3_initialize();
  ok = walseal_register(0, 0) == SQLITE_OK || fail("walseal_register", 0);
  ok = ok && copy(in, copyDb);
  ok = ok && (!exists(wal) || copy(wal, copyWal));
  ok = ok && recover(copyDb, key, pragmas, nPragma);
  sqlcipher_memset(key, 0, sizeof(key));
  ok = ok && (!exists(copyWal) || fail("a WAL is left beside the copy", 0));
  ok = ok && (rename(copyDb, out) == 0 || fail(out, strerror(errno)));

  if(!ok){
    unlink(copyDb); unlink(copyWal);
    snprintf(j, sizeof(j), "%s/db-journal", dir);
    unlink(j);
  }
  rmdir(dir);
  return ok ? 0 : 1;
}
