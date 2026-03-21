# sqlcipher-libressl

[![build-test](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml/badge.svg)](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml)

Encrypted SQLite — in the browser, on the server, everywhere.

Fork of [SQLCipher](https://github.com/sqlcipher/sqlcipher) v4.14.0
patched for **LibreSSL** and **WASM**.  Two patches, two files changed.

## Browser: encrypted SQLite that just works

Auto-detects the best persistence backend — OPFS or IndexedDB page
cache.  Both are durable per statement.  Same API either way.

```html
<script src="sqlcipher.js"></script>
<script src="sqlcipher-api.js"></script>
<script>
(async function() {
  var db = await SQLCipher.open({filename: "app.db", key: "secret"});
  console.log("Backend:", db.mode);  // "opfs" or "indexeddb"

  await db.exec("CREATE TABLE IF NOT EXISTS t (x TEXT)");
  await db.exec("INSERT INTO t VALUES (?)", ["hello"]);

  var rows = await db.select("SELECT * FROM t");
  console.log(rows);  // [{x: "hello"}]

  await db.flush();   // manual flush (auto-flush happens on every exec)

  // Export encrypted blob (for backup, server sync, etc.)
  var backup = await db.export();

  await db.close();   // indexeddb: auto-saves. both: close.
})();
</script>
```

### Release files

Download from [Releases](https://github.com/wmacevoy/sqlcipher-libressl/releases):

| File | Description |
|------|-------------|
| `sqlcipher.js` | Emscripten glue |
| `sqlcipher.wasm` | Compiled binary (~1.4MB) |
| `sqlcipher-api.js` | Unified API — auto-detects OPFS / IndexedDB |
| `sqlcipher-oo1.js` | [oo1 API](https://sqlite.org/wasm/doc/trunk/api-oo1.md) shim (main thread, IndexedDB blob) |
| `sqlcipher-worker.js` | Web Worker — OPFS or IndexedDB page cache |
| `sqlcipher-wasm-static.tar.gz` | Static libs for custom WASM builds |

### Run the example

```bash
cd examples/web
# Download release files into this directory:
gh release download v0.2.0 --repo wmacevoy/sqlcipher-libressl
python3 -m http.server 8000
# open http://localhost:8000
```

### Unified API

| Method | Returns | Description |
|--------|---------|-------------|
| `SQLCipher.open({filename, key})` | `Promise<Handle>` | Open/create database. Auto-detects backend. |
| `db.exec(sql, bind?)` | `Promise<{changes}>` | Execute DDL / DML. |
| `db.select(sql, bind?)` | `Promise<Object[]>` | Query rows as objects. |
| `db.flush()` | `Promise<void>` | Manual flush (auto-flush happens on every exec). |
| `db.export()` | `Promise<Uint8Array>` | Full encrypted blob for transport. |
| `db.import(bytes)` | `Promise<void>` | Restore from encrypted blob. |
| `db.shred()` | `Promise<void>` | Overwrite all storage with random data + delete. |
| `db.shredOnClose()` | `Promise<void>` | Flag: `close()` will shred instead of save. |
| `db.close()` | `Promise<void>` | Auto-saves (IndexedDB), then closes. Shreds if flagged. |
| `db.mode` | `string` | `"opfs"` or `"indexeddb"` |

### Persistence

| | OPFS (auto, preferred) | IndexedDB page cache (auto, fallback) |
|-|----------------------|--------------------------------------|
| **Durability** | Every COMMIT (via VFS xSync) | Every exec (auto-flush) |
| **Write cost** | 4KB per changed page | 4KB per dirty block |
| **Tab crash** | Data safe | Data safe (flushed per exec) |
| **Browser** | Chrome 108+, Safari 16.4+, Firefox 111+ | All browsers |

### Worker message protocol

| Message | Fields | Response |
|---------|--------|----------|
| `init` | — | `{ok, mode}` |
| `open` | `filename`, `key` | `{ok}` |
| `exec` | `sql`, `bind?` | `{ok, changes}` |
| `select` | `sql`, `bind?` | `{ok, rows, names}` |
| `flush` | — | `{ok}` |
| `export` | — | `{ok, bytes}` |
| `import` | `bytes` | `{ok}` |
| `shred` | — | `{ok}` |
| `shredOnClose` | — | `{ok}` |
| `close` | — | `{ok}` |

## Native C

```c
#include "sqlite3.h"
int sqlite3_key(sqlite3 *db, const void *pKey, int nKey);

sqlite3 *db;
sqlite3_open("encrypted.db", &db);
sqlite3_key(db, "secret", 6);

sqlite3_exec(db, "CREATE TABLE t (k TEXT, v REAL)", 0, 0, 0);
sqlite3_exec(db, "INSERT INTO t VALUES ('temp', 22.5)", 0, 0, 0);
sqlite3_close(db);

// Reopen with same key — data intact
// Without key — SQLITE_NOTADB
```

See `examples/basic.c` for the full round-trip test (create, reopen,
reject wrong key).

## Build

### Prerequisites

- C compiler, cmake, LibreSSL v4.2.1

### Install LibreSSL

```bash
curl -sL https://github.com/libressl/portable/releases/download/v4.2.1/libressl-4.2.1.tar.gz | tar xz
cd libressl-4.2.1 && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/libressl \
  -DLIBRESSL_APPS=OFF -DLIBRESSL_TESTS=OFF -DBUILD_SHARED_LIBS=OFF
make -j$(nproc) && make install
cd ../..
```

### Build SQLCipher

Note: `-DSQLCIPHER_CRYPTO_OPENSSL` is SQLCipher's name for the
OpenSSL-compatible crypto provider.  LibreSSL implements this API.

```bash
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make -j$(nproc)
```

### Test

```bash
# Smoke test
gcc -O2 -o basic examples/basic.c -I. -I$HOME/libressl/include \
  libsqlite3.a $HOME/libressl/lib/libcrypto.a -lpthread -ldl -lm
./basic

# Full SQLCipher TCL test suite
make testfixture
cd test && ../testfixture sqlcipher.test
```

## Patches

### 1. HMAC: EVP_MAC &rarr; legacy HMAC API (`src/crypto_openssl.c`)

Upstream uses `EVP_MAC` (OpenSSL 3.0+ only, not in LibreSSL).
Replaced with `HMAC_CTX_new`/`HMAC_Init_ex`/`HMAC_Update`/`HMAC_Final`
which LibreSSL implements.

### 2. WASM: atexit instead of .fini_array (`src/sqlcipher.c`)

Emscripten doesn't support `.fini_array`.  Added `__EMSCRIPTEN__`
guard using `atexit()` for key zeroing.

## Project layout

```
wasm/
  sqlcipher_wasm.c      C helpers for JS<->WASM boundary
  opfs_vfs.c            SQLite VFS — dispatches to OPFS or IndexedDB page cache
  sqlcipher-api.js      Unified API (recommended)
  sqlcipher-oo1.js      oo1 API shim (main thread, IndexedDB blob)
  sqlcipher-worker.js   Web Worker — auto-detects OPFS / IndexedDB

examples/
  basic.c               Native C encrypted round-trip
  web/index.html        Browser demo (unified API)

docs/
  oo1-api.md            Full API reference + persistence docs
```

## Documentation

| Doc | Scope |
|-----|-------|
| [docs/oo1-api.md](docs/oo1-api.md) | Unified API, oo1 API, persistence (OPFS + IndexedDB page cache), worker protocol, shred |

## Upstream

- **SQLCipher:** https://github.com/sqlcipher/sqlcipher (v4.14.0, SQLite 3.51.3)
- **LibreSSL:** v4.2.1 — https://github.com/libressl/portable

## Why LibreSSL

- Smaller than OpenSSL (~1/3 the code)
- Simpler build (no Perl dependency)
- `getentropy()` maps cleanly to Web Crypto in WASM
- BSD license (matches SQLCipher)

## License

BSD (same as upstream SQLCipher).  See [LICENSE.md](LICENSE.md).
