/* ============================================================
 * sqlcipher_wasm.c — C helpers for SQLCipher WASM
 *
 * Includes the SQLCipher amalgamation and adds thin wrappers
 * that reduce JS↔WASM boundary crossings.  Handles string
 * copying (SQLITE_TRANSIENT) so JS never manages WASM memory.
 *
 * Compiled by Emscripten:
 *   emcc sqlcipher_wasm.c -O2 -s WASM=1 ... -o sqlcipher.js
 * ============================================================ */

#include "sqlite3.c"

/* ── Database ──────────────────────────────────────────────── */

sqlite3 *wasm_db_open(const char *filename) {
    sqlite3 *db = 0;
    sqlite3_open(filename, &db);
    return db;
}

void wasm_db_close(sqlite3 *db) {
    if (db) sqlite3_close(db);
}

int wasm_db_exec(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, 0, 0, 0);
}

const char *wasm_db_errmsg(sqlite3 *db) {
    return sqlite3_errmsg(db);
}

int wasm_db_changes(sqlite3 *db) {
    return sqlite3_changes(db);
}

/* ── Statements ────────────────────────────────────────────── */

sqlite3_stmt *wasm_db_prepare(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = 0;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    return rc == SQLITE_OK ? stmt : 0;
}

void wasm_stmt_finalize(sqlite3_stmt *stmt) {
    sqlite3_finalize(stmt);
}

void wasm_stmt_reset(sqlite3_stmt *stmt) {
    sqlite3_reset(stmt);
}

int wasm_stmt_step(sqlite3_stmt *stmt) {
    return sqlite3_step(stmt) == SQLITE_ROW ? 1 : 0;
}

/* ── Bind ──────────────────────────────────────────────────── */

void wasm_stmt_bind_text(sqlite3_stmt *stmt, int i, const char *v) {
    sqlite3_bind_text(stmt, i, v, -1, SQLITE_TRANSIENT);
}

void wasm_stmt_bind_int(sqlite3_stmt *stmt, int i, int v) {
    sqlite3_bind_int(stmt, i, v);
}

void wasm_stmt_bind_double(sqlite3_stmt *stmt, int i, double v) {
    sqlite3_bind_double(stmt, i, v);
}

void wasm_stmt_bind_null(sqlite3_stmt *stmt, int i) {
    sqlite3_bind_null(stmt, i);
}

/* ── Column access ─────────────────────────────────────────── */

int wasm_stmt_columns(sqlite3_stmt *stmt) {
    return sqlite3_column_count(stmt);
}

const char *wasm_stmt_colname(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_name(stmt, i);
}

int wasm_stmt_coltype(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_type(stmt, i);
}

int wasm_stmt_int(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_int(stmt, i);
}

double wasm_stmt_double(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_double(stmt, i);
}

const char *wasm_stmt_text(sqlite3_stmt *stmt, int i) {
    return (const char *)sqlite3_column_text(stmt, i);
}

/* ── SQLCipher key ─────────────────────────────────────────── */

int wasm_db_key(sqlite3 *db, const char *key) {
#ifdef SQLITE_HAS_CODEC
    return sqlite3_key(db, key, (int)strlen(key));
#else
    (void)db; (void)key;
    return SQLITE_OK;
#endif
}
