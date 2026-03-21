/* ============================================================
 * basic.c — SQLCipher + LibreSSL: encrypted database round-trip
 *
 * Build (after ./configure && make):
 *   gcc -O2 -o basic examples/basic.c \
 *     -I. -I$HOME/libressl/include \
 *     -L.libs -L$HOME/libressl/lib \
 *     -lsqlcipher -lcrypto -lpthread -ldl -lm
 *
 * Or with the amalgamation:
 *   gcc -O2 -o basic examples/basic.c sqlite3.c \
 *     -I$HOME/libressl/include -L$HOME/libressl/lib \
 *     -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
 *     -lcrypto -lpthread -ldl -lm
 *
 * Run:
 *   ./basic
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sqlite3.h"

static void check(int rc, sqlite3 *db, const char *msg) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        fprintf(stderr, "FAIL: %s: %s\n", msg, db ? sqlite3_errmsg(db) : "");
        exit(1);
    }
}

static void exec(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "FAIL: %s: %s\n", sql, err ? err : "unknown");
        sqlite3_free(err);
        exit(1);
    }
}

int main(void) {
    const char *dbfile = "/tmp/sqlcipher_example.db";
    const char *key = "correct-horse-battery-staple";
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int rc;

    unlink(dbfile);

    /* ── 1. Create encrypted database ─────────────────────── */

    printf("1. Creating encrypted database...\n");
    rc = sqlite3_open(dbfile, &db);
    check(rc, db, "open");

    rc = sqlite3_key(db, key, (int)strlen(key));
    check(rc, db, "key");

    exec(db, "CREATE TABLE sensors (id TEXT PRIMARY KEY, value REAL, ts INTEGER)");
    exec(db, "INSERT INTO sensors VALUES ('temp_1', 22.5, 1710000000)");
    exec(db, "INSERT INTO sensors VALUES ('temp_2', 18.3, 1710000001)");
    exec(db, "INSERT INTO sensors VALUES ('humidity', 0.65, 1710000002)");

    printf("   Wrote 3 rows.\n");
    sqlite3_close(db);

    /* ── 2. Reopen with correct key — data intact ─────────── */

    printf("2. Reopening with correct key...\n");
    rc = sqlite3_open(dbfile, &db);
    check(rc, db, "reopen");

    rc = sqlite3_key(db, key, (int)strlen(key));
    check(rc, db, "rekey");

    rc = sqlite3_prepare_v2(db, "SELECT id, value FROM sensors ORDER BY id", -1, &stmt, NULL);
    check(rc, db, "prepare");

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        printf("   %s = %.1f\n", sqlite3_column_text(stmt, 0),
               sqlite3_column_double(stmt, 1));
        count++;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (count != 3) {
        fprintf(stderr, "FAIL: expected 3 rows, got %d\n", count);
        return 1;
    }
    printf("   Read %d rows — data survived encrypted restart.\n", count);

    /* ── 3. Try without key — must fail ───────────────────── */

    printf("3. Reopening WITHOUT key (must fail)...\n");
    rc = sqlite3_open(dbfile, &db);
    check(rc, db, "open-nokey");

    /* No sqlite3_key call — database should be unreadable */
    char *err = NULL;
    rc = sqlite3_exec(db, "SELECT * FROM sensors", NULL, NULL, &err);
    if (rc == SQLITE_OK) {
        fprintf(stderr, "FAIL: database readable without key — encryption broken!\n");
        sqlite3_close(db);
        return 1;
    }
    printf("   Correctly rejected: %s\n", err ? err : "not a database");
    sqlite3_free(err);
    sqlite3_close(db);

    /* ── 4. Try with wrong key — must fail ────────────────── */

    printf("4. Reopening with WRONG key (must fail)...\n");
    rc = sqlite3_open(dbfile, &db);
    check(rc, db, "open-wrongkey");

    rc = sqlite3_key(db, "wrong-key", 9);
    check(rc, db, "wrongkey");

    rc = sqlite3_exec(db, "SELECT * FROM sensors", NULL, NULL, &err);
    if (rc == SQLITE_OK) {
        fprintf(stderr, "FAIL: database readable with wrong key!\n");
        sqlite3_close(db);
        return 1;
    }
    printf("   Correctly rejected: %s\n", err ? err : "not a database");
    sqlite3_free(err);
    sqlite3_close(db);

    /* ── Done ─────────────────────────────────────────────── */

    unlink(dbfile);
    printf("\nAll tests passed. SQLCipher + LibreSSL encryption works.\n");
    return 0;
}
