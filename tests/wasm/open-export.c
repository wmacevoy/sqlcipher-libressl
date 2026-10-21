/*
** open-export.c -- open a file the WASM build's export() returned, with this
** repository's native build (REQ9/EXP-1, REQ1/FMT-1).
**
**   open-export <file> <key> <rows>
**
** test.html writes table t (id, x = 'row ' || id) with <rows> rows under
** <key>, all of them still in the WAL, and exports; run-tests.mjs saves the
** bytes. This opens the file alone, with no -wal beside it, under the key,
** and exits 0 if integrity_check is ok, the file is in WAL mode (as the worker
** leaves every database), and every row reads back. Any other outcome prints
** FAIL and exits 1.
**
** Build with the amalgamation, as CI's wasm job does:
**   gcc -O2 -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_TEMP_STORE=2 \
**     -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown \
**     -I. -I$HOME/libressl/include -o open-export tests/wasm/open-export.c sqlite3.c \
**     $HOME/libressl/lib/libcrypto.a -lpthread -lm
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sqlite3.h"

/* sqlite3_key is defined in sqlcipher.c but not in sqlite3.h */
int sqlite3_key(sqlite3 *db, const void *pKey, int nKey);

/* The first column of the first row of sql, as text ("" if none). */
static const char *one(sqlite3 *db, const char *sql, char *buf, int n){
  sqlite3_stmt *s;
  buf[0] = 0;
  if(sqlite3_prepare_v2(db, sql, -1, &s, 0) == SQLITE_OK){
    if(sqlite3_step(s) == SQLITE_ROW && sqlite3_column_text(s, 0))
      snprintf(buf, n, "%s", (const char*)sqlite3_column_text(s, 0));
    sqlite3_finalize(s);
  }
  if(!buf[0]) snprintf(buf, n, "error: %s", sqlite3_errmsg(db));
  return buf;
}

int main(int argc, char **argv){
  char wal[4096], want[32], got[256];
  const char *file, *key;
  sqlite3 *db;
  int fails = 0;

  if(argc != 4){ fprintf(stderr, "usage: open-export <file> <key> <rows>\n"); return 2; }
  file = argv[1]; key = argv[2];
  snprintf(wal, sizeof(wal), "%s-wal", file);
  if(access(wal, F_OK) == 0){ printf("FAIL: %s: a -wal is beside it\n", file); return 1; }

  if(sqlite3_open_v2(file, &db, SQLITE_OPEN_READWRITE, 0) != SQLITE_OK   /* never create it */
     || sqlite3_key(db, key, (int)strlen(key)) != SQLITE_OK){
    printf("FAIL: %s: open: %s\n", file, sqlite3_errmsg(db));
    return 1;
  }
#define EXPECT(sql, value) do{ \
    one(db, sql, got, sizeof(got)); \
    if(strcmp(got, value)){ printf("FAIL: %s: %s gave \"%s\", want \"%s\"\n", file, sql, got, value); fails++; } \
  }while(0)
  EXPECT("PRAGMA integrity_check", "ok");
  EXPECT("PRAGMA journal_mode", "wal");
  snprintf(want, sizeof(want), "%s", argv[3]);
  EXPECT("SELECT count(*) FROM t", want);
  EXPECT("SELECT count(*) FROM t WHERE x = 'row ' || id", want);
  sqlite3_close(db);
  if(!fails) printf("PASS: REQ9/EXP-1 REQ1/FMT-1 %s opens natively: integrity ok, wal, %s rows\n", file, argv[3]);
  return fails ? 1 : 0;
}
