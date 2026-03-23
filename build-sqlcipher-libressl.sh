#!/usr/bin/env bash
set -euo pipefail

# Build SQLCipher WASM + JS helpers.
#
# Required env:
#   LIBRESSL_NATIVE  native LibreSSL install prefix (headers for configure)
#   LIBRESSL_WASM    WASM LibreSSL install prefix   (libcrypto.a)
#
# Optional env:
#   OUTPUT_DIR       output directory (default: wasm/dist, relative to this script)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

: "${LIBRESSL_NATIVE:?Set LIBRESSL_NATIVE to native LibreSSL install prefix}"
: "${LIBRESSL_WASM:?Set LIBRESSL_WASM to WASM LibreSSL install prefix}"
OUTPUT_DIR="${OUTPUT_DIR:-wasm/dist}"

# ── Amalgamation ─────────────────────────────────────────────
echo "==> Building SQLCipher amalgamation"
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
          -DSQLITE_EXTRA_INIT=sqlcipher_extra_init \
          -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown \
          -I${LIBRESSL_NATIVE}/include" \
  LDFLAGS="${LIBRESSL_NATIVE}/lib/libcrypto.a"
make sqlite3.c

# ── WASM compilation ────────────────────────────────────────
echo "==> Compiling WASM"
mkdir -p "$OUTPUT_DIR"
cp sqlite3.c sqlite3.h wasm/sqlcipher_wasm.c wasm/opfs_vfs.c "$OUTPUT_DIR/"

EXPORTED='["_wasm_db_open","_wasm_db_close","_wasm_db_exec","_wasm_db_errmsg","_wasm_db_changes","_wasm_db_total_changes","_wasm_db_key","_wasm_db_prepare","_wasm_stmt_finalize","_wasm_stmt_reset","_wasm_stmt_clear_bindings","_wasm_stmt_step","_wasm_stmt_bind_text","_wasm_stmt_bind_int","_wasm_stmt_bind_double","_wasm_stmt_bind_null","_wasm_stmt_bind_parameter_count","_wasm_stmt_columns","_wasm_stmt_colname","_wasm_stmt_coltype","_wasm_stmt_int","_wasm_stmt_double","_wasm_stmt_text","_sqlite3_opfs_init","_malloc","_free"]'
RUNTIME='["cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8"]'

(
  cd "$OUTPUT_DIR"
  emcc sqlcipher_wasm.c opfs_vfs.c \
    -I. -I"${LIBRESSL_NATIVE}/include" \
    -O2 \
    -DSQLITE_HAS_CODEC \
    -DSQLCIPHER_CRYPTO_OPENSSL \
    -DSQLITE_EXTRA_INIT=sqlcipher_extra_init \
    -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown \
    -DSQLITE_OMIT_LOAD_EXTENSION \
    -DSQLITE_THREADSAFE=1 \
    -DSQLITE_TEMP_STORE=2 \
    -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_JSON1 \
    -s WASM=1 \
    -s EXPORTED_FUNCTIONS="$EXPORTED" \
    -s EXPORTED_RUNTIME_METHODS="$RUNTIME" \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=16777216 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="initSqlcipher" \
    -s ENVIRONMENT='web,worker,node' \
    -s FILESYSTEM=1 \
    "${LIBRESSL_WASM}/lib/libcrypto.a" \
    -o sqlcipher.js
  rm -f sqlite3.c sqlite3.h sqlcipher_wasm.c opfs_vfs.c
)

# ── JS helpers ──────────────────────────────────────────────
echo "==> Copying JS helpers"
cp wasm/sqlcipher-api.js wasm/sqlcipher-worker.js wasm/sqlcipher-oo1.js "$OUTPUT_DIR/"

echo "==> Build complete"
ls -lh "$OUTPUT_DIR"
