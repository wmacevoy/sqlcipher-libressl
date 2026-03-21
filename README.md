# sqlcipher-libressl

[![build-test](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml/badge.svg)](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml)

Encrypted SQLite — in the browser, on the server, everywhere.

Fork of [SQLCipher](https://github.com/sqlcipher/sqlcipher) v4.14.0
patched for **LibreSSL** and **WASM**.  Two patches, three files changed.

## Browser: encrypted SQLite with OPFS persistence

Every COMMIT is durable.  No manual save.  Survives tab close, crash,
browser restart.  Data encrypted at rest in the Origin Private File System.

```html
<script src="sqlcipher.js"></script>
<script>
var worker = new Worker("sqlcipher-worker.js");
var _id = 0, _pending = {};

function send(msg) {
  return new Promise(function(resolve, reject) {
    msg.id = ++_id;
    _pending[msg.id] = {resolve: resolve, reject: reject};
    worker.postMessage(msg);
  });
}
worker.onmessage = function(e) {
  var p = _pending[e.data.id]; delete _pending[e.data.id];
  if (e.data.ok) p.resolve(e.data); else p.reject(new Error(e.data.error));
};

(async function() {
  await send({type: "init"});
  await send({type: "open", filename: "/app.db", key: "secret"});
  await send({type: "exec", sql: "CREATE TABLE IF NOT EXISTS t (x TEXT)"});
  await send({type: "exec", sql: "INSERT INTO t VALUES (?)", bind: ["hello"]});

  var result = await send({type: "select", sql: "SELECT * FROM t"});
  console.log(result.rows);  // [{x: "hello"}]

  // Export encrypted blob (for backup, server sync, etc.)
  var backup = await send({type: "export"});
  // backup.bytes is a Uint8Array — the encrypted SQLite file
})();
</script>
```

### Release files

Download from [Releases](https://github.com/wmacevoy/sqlcipher-libressl/releases):

| File | Description |
|------|-------------|
| `sqlcipher.js` | Emscripten glue |
| `sqlcipher.wasm` | Compiled binary (~1.4MB) |
| `sqlcipher-oo1.js` | [oo1 API](https://sqlite.org/wasm/doc/trunk/api-oo1.md) shim (main thread, IndexedDB fallback) |
| `sqlcipher-worker.js` | Web Worker with OPFS VFS (durable persistence) |

### Run the example

```bash
cd examples/web
# Download release files into this directory:
gh release download v0.1.0 --repo wmacevoy/sqlcipher-libressl
python3 -m http.server 8000
# open http://localhost:8000
```

### Two persistence modes

| | OPFS VFS (Worker) | IndexedDB (main thread) |
|-|-------------------|------------------------|
| **Durability** | Every COMMIT | On `db.save()` call |
| **Write cost** | 4KB per changed page | Entire database blob |
| **Tab crash** | Data safe | Data since last save lost |
| **Requires** | Web Worker | Nothing extra |
| **Browser** | Chrome 108+, Safari 16.4+, Firefox 111+ | All browsers |

### Worker message protocol

| Message | Fields | Response |
|---------|--------|----------|
| `init` | — | `{ok}` |
| `open` | `filename`, `key` | `{ok}` |
| `exec` | `sql`, `bind?` | `{ok, changes}` |
| `select` | `sql`, `bind?` | `{ok, rows, names}` |
| `export` | — | `{ok, bytes}` |
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
  opfs_vfs.c            OPFS-backed SQLite VFS (durable persistence)
  sqlcipher-oo1.js      oo1 API shim (main thread, IndexedDB)
  sqlcipher-worker.js   Web Worker (OPFS VFS, postMessage protocol)

examples/
  basic.c               Native C encrypted round-trip
  web/index.html         Browser demo (Worker + OPFS)

docs/
  oo1-api.md            Full oo1 API reference + persistence docs
```

## Documentation

| Doc | Scope |
|-----|-------|
| [docs/oo1-api.md](docs/oo1-api.md) | oo1 API reference, persistence (OPFS + IndexedDB), worker protocol |

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
