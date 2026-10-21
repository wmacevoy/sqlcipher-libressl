# sqlcipher-libressl

[![build-test](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml/badge.svg)](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml)

Encrypted SQLite — in the browser, on the server, everywhere.

Fork of [SQLCipher](https://github.com/sqlcipher/sqlcipher) v4.16.0 (SQLite 3.53.1)
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
gh release download v2.0.0 --repo wmacevoy/sqlcipher-libressl
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
| `db.import(bytes)` | `Promise<void>` | Restore from encrypted blob, opened under the database's current key (`open`'s, or the last `PRAGMA rekey`'s). |
| `db.shred()` | `Promise<void>` | Overwrite all storage with random data + delete. |
| `db.shredOnClose()` | `Promise<void>` | Flag: `close()` will shred instead of save. |
| `db.close()` | `Promise<void>` | Auto-saves (IndexedDB), then closes. Shreds if flagged. |
| `db.mode` | `string` | `"opfs"` or `"indexeddb"` |

Bind values: numbers, `BigInt`s, strings, booleans, `null`, and `Uint8Array` (stored as a BLOB).
BLOB columns read back as `Uint8Array`; integers as numbers, or as `BigInt`s beyond 2^53 − 1.

### Persistence

| | OPFS (auto, preferred) | IndexedDB page cache (auto, fallback) |
|-|----------------------|--------------------------------------|
| **Durability** | Every COMMIT, synced to the write-ahead log (`-wal`) | Every exec (checkpoint, then auto-flush) |
| **Write cost** | 4KB per changed page to the WAL, again at each checkpoint | 4KB per dirty block |
| **Tab crash** | Data safe (the next open replays the WAL's commits)\* | Data safe (the stored copy is the last flushed commit)\* |
| **Browser** | Chrome 108+, Safari 16.4+, Firefox 111+ (untested: CI runs current Chromium, Firefox and WebKit) | All browsers |

\* With SQLCipher's default cipher settings. A database that needs `PRAGMA cipher_compatibility` or
other non-default settings keeps its journal in memory, and a crash can corrupt it.

The worker opens every database in `locking_mode=EXCLUSIVE` and `journal_mode=WAL`. On OPFS the
`-wal` file sits next to the database; SQLCipher encrypts the pages in it, and walseal enciphers
the headers SQLite would write in the clear (page numbers, commit sizes, salts), keyed from
SQLCipher's derived key. On IndexedDB the WAL stays in worker memory and only committed,
checkpointed states are stored. `export()` checkpoints first, so it returns the whole database.
`PRAGMA rekey` through `exec` keeps every commit: the worker checkpoints before and after it, and
the WAL that follows is sealed under the new key. A key pragma inside a transaction, or after an
`open` whose key could not read the database, is refused with `not authorized`.

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

`./check.sh` does all of the steps below — LibreSSL, the library, the smoke
test, the TCL suite — and leaves the release library in `libsqlcipher.a`
(see [CONTRIBUTING.md](CONTRIBUTING.md#build)). By hand:

### Install LibreSSL

```bash
curl -fsSLO https://github.com/libressl/portable/releases/download/v4.2.1/libressl-4.2.1.tar.gz
sha256sum libressl-4.2.1.tar.gz   # must equal LIBRESSL_SHA256 in libressl.env
tar xzf libressl-4.2.1.tar.gz
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

# Full TCL test suite — needs its own configure (test-only flags, never ship this build)
make distclean
./configure --with-tempstore=yes --enable-fts5 \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -DSQLCIPHER_TEST -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make testfixture
cd test && ../testfixture sqlcipher.test
```

### Compatibility with upstream SQLCipher

**Tested:** files written by official SQLCipher open here, both natively
(upstream's `sqlcipher-compatibility.test`, inside `sqlcipher.test`:
1.1.8 through 4.0 files from `sqlcipher-resources/`) and in WASM (the
`REQ1/FMT-1` test in `tests/wasm/test.html`, which opens the 4.0 file).
Files the WASM build exports open in this repository's native build: CI
opens each browser run's `export()` under its key (`tests/wasm/open-export.c`).

**Not tested, by decision:** stock SQLCipher opening files written here.
The reason and when to revisit are under *Scope decisions* in
[docs/REQUIREMENTS_v8.md](docs/REQUIREMENTS_v8.md#scope-decisions).

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
| [CONTRIBUTING.md](CONTRIBUTING.md) | Build, test, and the working set: requirements, design, queue, findings |

## Upstream

- **SQLCipher:** https://github.com/sqlcipher/sqlcipher (v4.16.0, SQLite 3.53.1)
- **LibreSSL:** v4.2.1 — https://github.com/libressl/portable

## Why LibreSSL

- Smaller than OpenSSL (~1/3 the code)
- Simpler build (no Perl dependency)
- `getentropy()` maps cleanly to Web Crypto in WASM
- BSD license (matches SQLCipher)

## License

BSD (same as upstream SQLCipher).  See [LICENSE.md](LICENSE.md).
