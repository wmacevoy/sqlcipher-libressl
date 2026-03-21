# sqlcipher-libressl

[![build-test](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml/badge.svg)](https://github.com/wmacevoy/sqlcipher-libressl/actions/workflows/build-test.yml)

Fork of [SQLCipher](https://github.com/sqlcipher/sqlcipher) v4.14.0
patched for **LibreSSL** compatibility and **Emscripten/WASM** builds.

Two patches, three files changed, zero new dependencies.

## Quick start

```bash
# Install LibreSSL (if not already available)
curl -sL https://github.com/libressl/portable/releases/download/v4.2.1/libressl-4.2.1.tar.gz | tar xz
cd libressl-4.2.1 && mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/libressl \
  -DLIBRESSL_APPS=OFF -DLIBRESSL_TESTS=OFF -DBUILD_SHARED_LIBS=OFF
make -j$(nproc) && make install
cd ../..

# Build SQLCipher with LibreSSL
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make -j$(nproc)

# Run the example
gcc -O2 -o basic examples/basic.c \
  -I. -I$HOME/libressl/include \
  -L.libs -L$HOME/libressl/lib \
  -lsqlcipher $HOME/libressl/lib/libcrypto.a -lpthread -ldl -lm
LD_LIBRARY_PATH=.libs:$HOME/libressl/lib ./basic
```

## Web example

`examples/web/index.html` — encrypted SQLite running entirely in the
browser.  No server, no extensions, no plugins.

1. Build the WASM module (CI does this automatically — download the
   artifact, or build locally)
2. Copy `sqlcipher.js` and `sqlcipher.wasm` into `examples/web/`
3. Serve the directory: `python3 -m http.server 8000`
4. Open `http://localhost:8000/examples/web/`

The page lets you create an encrypted database, insert rows, query them,
close and reopen with the same key, and verify that a wrong key is rejected.

## Native C example

`examples/basic.c` — encrypted database round-trip from C:

```c
#include "sqlite3.h"

sqlite3 *db;
sqlite3_open("encrypted.db", &db);
sqlite3_key(db, "secret", 6);

sqlite3_exec(db, "CREATE TABLE t (k TEXT, v REAL)", 0, 0, 0);
sqlite3_exec(db, "INSERT INTO t VALUES ('temp', 22.5)", 0, 0, 0);
sqlite3_close(db);

// Reopen with same key — data intact
sqlite3_open("encrypted.db", &db);
sqlite3_key(db, "secret", 6);
// SELECT * FROM t → temp, 22.5

// Without key — SQLITE_NOTADB
```

Tests all four cases: create, reopen with correct key, reject without key,
reject wrong key.  Used as the CI smoke test.

## Patches

### 1. HMAC: EVP_MAC &rarr; legacy HMAC API

**File:** `src/crypto_openssl.c`

Upstream SQLCipher 4.14.0 uses `EVP_MAC` (an API only available in
OpenSSL 3.0+, not in LibreSSL).  This patch replaces it with the
legacy `HMAC_CTX_new` / `HMAC_Init_ex` / `HMAC_Update` / `HMAC_Final`
API, which LibreSSL implements.

The patch also simplifies the error path (single `goto error` instead
of `goto cleanup`), removing ~40 lines with no behavior change.

### 2. WASM: atexit instead of .fini_array

**File:** `src/sqlcipher.c`

Emscripten doesn't support `.fini_array` ELF sections.  This patch
adds an `__EMSCRIPTEN__` guard that registers `sqlcipher_fini` via
`atexit()` instead.  Key zeroing behavior is preserved:

- **Native Linux/macOS:** original `.fini_array` / `__DATA,__mod_term_func`
- **WASM:** `atexit(sqlcipher_fini)` registered via `__attribute__((constructor))`

## Build

### Prerequisites

- C compiler (gcc or clang)
- cmake
- LibreSSL (built from source or installed)
- tcl (for the full test suite)

### Native build

Note: `-DSQLCIPHER_CRYPTO_OPENSSL` is SQLCipher's name for the
OpenSSL-compatible crypto provider.  LibreSSL implements this API.

```bash
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make -j$(nproc)
```

### Amalgamation (for embedding or WASM)

```bash
make sqlite3.c   # produces sqlite3.c + sqlite3.h
```

### Test

```bash
# Smoke test (build + run the example)
gcc -O2 -o basic examples/basic.c \
  -I. -I$HOME/libressl/include \
  -L.libs -L$HOME/libressl/lib \
  -lsqlcipher $HOME/libressl/lib/libcrypto.a -lpthread -ldl -lm
LD_LIBRARY_PATH=.libs:$HOME/libressl/lib ./basic

# Full SQLCipher test suite (requires tcl)
make testfixture
cd test && ../testfixture sqlcipher.test
```

## Usage

### As a git submodule

```bash
git submodule add git@github.com:wmacevoy/sqlcipher-libressl.git vendor/sqlcipher
```

### As a patch over upstream

```bash
git clone git@github.com:sqlcipher/sqlcipher.git
cd sqlcipher
git checkout v4.14.0
git diff 778ab890..0fced25d -- src/crypto_openssl.c src/sqlcipher.c | git apply
```

## WASM

### Compiling to WASM with Emscripten

```bash
# Build LibreSSL for WASM first
mkdir -p libressl-wasm && cd libressl-wasm
emcmake cmake /path/to/libressl -DCMAKE_INSTALL_PREFIX=$HOME/libressl-wasm \
  -DLIBRESSL_APPS=OFF -DLIBRESSL_TESTS=OFF -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-DHAVE_TIMEGM -DHAVE_GETENTROPY -D__STDC_NO_ATOMICS__"
emmake make -j$(nproc) crypto
cd ..

# Build amalgamation, then compile to WASM
make sqlite3.c
emcc sqlite3.c your_wasm_wrapper.c \
  -I$HOME/libressl/include -L$HOME/libressl-wasm/lib \
  -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
  -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 \
  -s WASM=1 -s MODULARIZE=1 -s FILESYSTEM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  libressl-wasm/lib/libcrypto.a \
  -o sqlcipher.js
```

### Entropy in WASM

LibreSSL uses `getentropy()`.  Emscripten maps this to
`crypto.getRandomValues()` when compiled with `-DHAVE_GETENTROPY`.
No `/dev/urandom` needed.

### FILESYSTEM=1

LibreSSL's `libcrypto.a` references BIO socket symbols at link time
(dead code in this use case).  `FILESYSTEM=1` provides the stubs.
Overhead: ~50KB.  Future work: strip BIO from the WASM libcrypto build.

## Upstream

- **SQLCipher:** https://github.com/sqlcipher/sqlcipher (v4.14.0, SQLite 3.51.3)
- **LibreSSL:** https://www.libressl.org

## Why LibreSSL

- Smaller than OpenSSL (~1/3 the code)
- Simpler build (no Perl dependency)
- `getentropy()` maps cleanly to Web Crypto in WASM
- BSD license (matches SQLCipher)

## License

BSD (same as upstream SQLCipher).  See [LICENSE.md](LICENSE.md).
