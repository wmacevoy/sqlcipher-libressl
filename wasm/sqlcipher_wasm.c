/* ============================================================
 * sqlcipher_wasm.c — C helpers for SQLCipher WASM
 *
 * Includes the SQLCipher amalgamation and adds thin wrappers
 * that reduce JS<->WASM boundary crossings.  Handles string
 * copying (SQLITE_TRANSIENT) so JS never manages WASM memory.
 *
 * The JS shim (oo1.js) matches the official sqlite3 WASM oo1 API:
 *   https://sqlite.org/wasm/doc/trunk/api-oo1.md
 *
 * Compiled by Emscripten:
 *   emcc sqlcipher_wasm.c -O2 -s WASM=1 ... -o sqlcipher.js
 * ============================================================ */

#include "sqlite3.c"
#include "walseal_sqlcipher.c"   /* needs the amalgamation's static codec functions */
#include <emscripten.h>
#include <stdint.h>
#include <stdlib.h>

/* ── Database ──────────────────────────────────────────────── */

sqlite3 *wasm_db_open(const char *filename) {
    sqlite3 *db = 0;
    sqlite3_open(filename, &db);
    return db;
}

/* Returns the handle even on failure, so the caller can read wasm_db_errmsg, then close it. */
sqlite3 *wasm_db_open_v2(const char *filename, int flags, const char *vfs) {
    sqlite3 *db = 0;
    sqlite3_open_v2(filename, &db, flags, vfs);
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

int wasm_db_errcode(sqlite3 *db) {
    return sqlite3_errcode(db);
}

int wasm_db_changes(sqlite3 *db) {
    return sqlite3_changes(db);
}

int wasm_db_total_changes(sqlite3 *db) {
    return sqlite3_total_changes(db);
}

const char *wasm_db_filename(sqlite3 *db, const char *zDb) {
    return sqlite3_db_filename(db, zDb);
}

const char *wasm_db_name(sqlite3 *db, int n) {
    return sqlite3_db_name(db, n);
}

const char *wasm_db_vfs_name(sqlite3 *db, const char *zDb) {
    sqlite3_vfs *pVfs = 0;
    sqlite3_file_control(db, zDb, SQLITE_FCNTL_VFS_POINTER, &pVfs);
    return pVfs ? pVfs->zName : 0;
}

/* ── Statements ────────────────────────────────────────────── */

sqlite3_stmt *wasm_db_prepare(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = 0;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    return rc == SQLITE_OK ? stmt : 0;
}

/* One statement from zSql; *pzTail is where the next begins. *ppStmt is 0 for whitespace or a comment. */
int wasm_db_prepare_tail(sqlite3 *db, const char *zSql, sqlite3_stmt **ppStmt, const char **pzTail) {
    return sqlite3_prepare_v2(db, zSql, -1, ppStmt, pzTail);
}

int wasm_stmt_finalize(sqlite3_stmt *stmt) {
    return sqlite3_finalize(stmt);
}

int wasm_stmt_reset(sqlite3_stmt *stmt) {
    return sqlite3_reset(stmt);
}

void wasm_stmt_clear_bindings(sqlite3_stmt *stmt) {
    sqlite3_clear_bindings(stmt);
}

int wasm_stmt_step(sqlite3_stmt *stmt) {
    return sqlite3_step(stmt) == SQLITE_ROW ? 1 : 0;
}

/* wasm_stmt_step hides errors as "no more rows"; this returns SQLITE_ROW, SQLITE_DONE, or the error. */
int wasm_stmt_step_rc(sqlite3_stmt *stmt) {
    return sqlite3_step(stmt);
}

const char *wasm_stmt_sql(sqlite3_stmt *stmt) {
    return sqlite3_sql(stmt);
}

int wasm_stmt_readonly(sqlite3_stmt *stmt) {
    return sqlite3_stmt_readonly(stmt);
}

/* ── Bind ──────────────────────────────────────────────────── */

void wasm_stmt_bind_text(sqlite3_stmt *stmt, int i, const char *v) {
    sqlite3_bind_text(stmt, i, v, -1, SQLITE_TRANSIENT);
}

void wasm_stmt_bind_int(sqlite3_stmt *stmt, int i, int v) {
    sqlite3_bind_int(stmt, i, v);
}

/* 64-bit integers cross the JS boundary as a double when they fit in 53 bits, and as
 * decimal text otherwise; the build has no BigInt support (WASM_BIGINT) to pass them whole.
 * The JS side checks the range before calling the _text forms. */
void wasm_stmt_bind_int64(sqlite3_stmt *stmt, int i, double v) {
    sqlite3_bind_int64(stmt, i, (sqlite3_int64)v);
}

void wasm_stmt_bind_int64_text(sqlite3_stmt *stmt, int i, const char *dec) {
    sqlite3_bind_int64(stmt, i, strtoll(dec, 0, 10));
}

void wasm_stmt_bind_double(sqlite3_stmt *stmt, int i, double v) {
    sqlite3_bind_double(stmt, i, v);
}

void wasm_stmt_bind_null(sqlite3_stmt *stmt, int i) {
    sqlite3_bind_null(stmt, i);
}

void wasm_stmt_bind_blob(sqlite3_stmt *stmt, int i, const void *p, int n) {
    /* sqlite3_bind_blob with a NULL pointer binds NULL; an empty array must stay a BLOB. */
    if (n == 0) sqlite3_bind_zeroblob(stmt, i, 0);
    else sqlite3_bind_blob(stmt, i, p, n, SQLITE_TRANSIENT);
}

int wasm_stmt_bind_parameter_count(sqlite3_stmt *stmt) {
    return sqlite3_bind_parameter_count(stmt);
}

int wasm_stmt_bind_parameter_index(sqlite3_stmt *stmt, const char *name) {
    return sqlite3_bind_parameter_index(stmt, name);
}

const char *wasm_stmt_bind_parameter_name(sqlite3_stmt *stmt, int i) {
    return sqlite3_bind_parameter_name(stmt, i);
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

/* The double is exact while |v| < 2^53; past that, read wasm_stmt_int64_text. */
double wasm_stmt_int64(sqlite3_stmt *stmt, int i) {
    return (double)sqlite3_column_int64(stmt, i);
}

/* The decimal text of an int64, in a buffer the next call reuses. */
static char int64_buf[24];
static const char *int64_text(sqlite3_int64 v) {
    sqlite3_snprintf(sizeof int64_buf, int64_buf, "%lld", v);
    return int64_buf;
}

const char *wasm_stmt_int64_text(sqlite3_stmt *stmt, int i) {
    return int64_text(sqlite3_column_int64(stmt, i));
}

double wasm_stmt_double(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_double(stmt, i);
}

const char *wasm_stmt_text(sqlite3_stmt *stmt, int i) {
    return (const char *)sqlite3_column_text(stmt, i);
}

/* Call wasm_stmt_blob before wasm_stmt_bytes, so the size is of the blob form. */
const void *wasm_stmt_blob(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_blob(stmt, i);
}

int wasm_stmt_bytes(sqlite3_stmt *stmt, int i) {
    return sqlite3_column_bytes(stmt, i);
}

/* ── User-defined functions ───────────────────────────────────
 * Every SQL function registered from JS shares these trampolines. The function's
 * user data is an id into the oo1 shim's registry on Module; kind says which
 * callback to run: 0 xFunc, 1 xStep, 2 xFinal, 3 xValue, 4 xInverse.
 * SQLite calls xDestroy when the function is replaced, or the database closes.
 *
 * key is what the JS callback gets as its first argument. For a scalar it is ctx.
 * For an aggregate or window function it is sqlite3_aggregate_context(): SQLite
 * calls xFinal and xValue with a different ctx from xStep's, so ctx cannot key
 * per-group state, but the aggregate context is the same for every call of one group. */

EM_JS(void, js_fn_call, (int id, int kind, sqlite3_context *ctx, int key, int argc, sqlite3_value **argv), {
    Module._oo1_fn_call(id, kind, ctx, key, argc, argv);
});

EM_JS(void, js_fn_destroy, (int id), {
    Module._oo1_fn_destroy(id);
});

static int fn_id(sqlite3_context *ctx) { return (int)(intptr_t)sqlite3_user_data(ctx); }
static int fn_key(sqlite3_context *ctx) { return (int)(intptr_t)sqlite3_aggregate_context(ctx, 8); }
static void fn_func(sqlite3_context *ctx, int argc, sqlite3_value **argv) { js_fn_call(fn_id(ctx), 0, ctx, (int)(intptr_t)ctx, argc, argv); }
static void fn_step(sqlite3_context *ctx, int argc, sqlite3_value **argv) { js_fn_call(fn_id(ctx), 1, ctx, fn_key(ctx), argc, argv); }
static void fn_final(sqlite3_context *ctx) { js_fn_call(fn_id(ctx), 2, ctx, fn_key(ctx), 0, 0); }
static void fn_value(sqlite3_context *ctx) { js_fn_call(fn_id(ctx), 3, ctx, fn_key(ctx), 0, 0); }
static void fn_inverse(sqlite3_context *ctx, int argc, sqlite3_value **argv) { js_fn_call(fn_id(ctx), 4, ctx, fn_key(ctx), argc, argv); }
static void fn_destroy(void *p) { js_fn_destroy((int)(intptr_t)p); }

/* kind: 0 scalar, 1 aggregate, 2 window. On failure SQLite still calls xDestroy. */
int wasm_db_create_function(sqlite3 *db, const char *name, int nArg, int flags, int id, int kind) {
    void *p = (void *)(intptr_t)id;
    if (kind == 2)
        return sqlite3_create_window_function(db, name, nArg, flags, p,
                                              fn_step, fn_final, fn_value, fn_inverse, fn_destroy);
    if (kind == 1)
        return sqlite3_create_function_v2(db, name, nArg, flags, p, 0, fn_step, fn_final, fn_destroy);
    return sqlite3_create_function_v2(db, name, nArg, flags, p, fn_func, 0, 0, fn_destroy);
}

int wasm_value_type(sqlite3_value **argv, int i) { return sqlite3_value_type(argv[i]); }
double wasm_value_double(sqlite3_value **argv, int i) { return sqlite3_value_double(argv[i]); }
double wasm_value_int64(sqlite3_value **argv, int i) { return (double)sqlite3_value_int64(argv[i]); }
const char *wasm_value_int64_text(sqlite3_value **argv, int i) { return int64_text(sqlite3_value_int64(argv[i])); }
const char *wasm_value_text(sqlite3_value **argv, int i) { return (const char *)sqlite3_value_text(argv[i]); }
/* Call wasm_value_blob before wasm_value_bytes, as with columns. */
const void *wasm_value_blob(sqlite3_value **argv, int i) { return sqlite3_value_blob(argv[i]); }
int wasm_value_bytes(sqlite3_value **argv, int i) { return sqlite3_value_bytes(argv[i]); }

void wasm_result_null(sqlite3_context *ctx) { sqlite3_result_null(ctx); }
void wasm_result_int(sqlite3_context *ctx, int v) { sqlite3_result_int(ctx, v); }
void wasm_result_double(sqlite3_context *ctx, double v) { sqlite3_result_double(ctx, v); }
void wasm_result_int64(sqlite3_context *ctx, double v) { sqlite3_result_int64(ctx, (sqlite3_int64)v); }
void wasm_result_int64_text(sqlite3_context *ctx, const char *dec) { sqlite3_result_int64(ctx, strtoll(dec, 0, 10)); }
void wasm_result_text(sqlite3_context *ctx, const char *v) { sqlite3_result_text(ctx, v, -1, SQLITE_TRANSIENT); }
void wasm_result_blob(sqlite3_context *ctx, const void *p, int n) {
    if (n == 0) sqlite3_result_zeroblob(ctx, 0);   /* a NULL pointer would give NULL, as in wasm_stmt_bind_blob */
    else sqlite3_result_blob(ctx, p, n, SQLITE_TRANSIENT);
}
void wasm_result_error(sqlite3_context *ctx, const char *msg) { sqlite3_result_error(ctx, msg, -1); }

/* ── SQLCipher key ─────────────────────────────────────────── */

int wasm_db_key(sqlite3 *db, const char *key) {
#ifdef SQLITE_HAS_CODEC
    return sqlite3_key(db, key, (int)strlen(key));
#else
    (void)db; (void)key;
    return SQLITE_OK;
#endif
}

/* ── The WAL (the worker) ──────────────────────────────────── */

/* walseal's key from SQLCipher's derived key; with no key, the WAL passes
   through (walseal_sqlcipher.c).  Call after wasm_db_key, before any read. */
int wasm_db_walseal_key(sqlite3 *db) {
    return walseal_key_from_codec(db, "main");
}

/* Close without checkpointing or deleting the WAL.  Under a key that cannot
   read the database, walseal deciphers the WAL to noise, SQLite takes it for
   empty, and a normal close would delete its committed frames. */
int wasm_db_keep_wal(sqlite3 *db) {
    return sqlite3_db_config(db, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, (int *)0);
}
