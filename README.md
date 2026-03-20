# sqlcipher-libressl

Fork of [SQLCipher](https://github.com/sqlcipher/sqlcipher) v4.14.0
patched for **LibreSSL** compatibility and **Emscripten/WASM** builds.

Two patches, three files changed, zero new dependencies.

## Patches

### 1. HMAC: EVP_MAC &rarr; legacy HMAC API

**File:** `src/crypto_openssl.c`

SQLCipher 4.14.0 uses `EVP_MAC` (OpenSSL 3.0+ API).  LibreSSL
doesn't implement it.  This patch replaces `EVP_MAC_fetch` /
`EVP_MAC_init` / `EVP_MAC_update` / `EVP_MAC_final` with the legacy
`HMAC_CTX_new` / `HMAC_Init_ex` / `HMAC_Update` / `HMAC_Final` API,
which works across all LibreSSL versions and all OpenSSL versions
(1.1.x through 3.x).

The patch also simplifies the error path (single `goto error` instead
of `goto cleanup`), removing ~40 lines with no behavior change.

### 2. WASM: atexit instead of .fini_array

**File:** `src/sqlcipher.c`

Emscripten doesn't support `.fini_array` ELF sections.  This patch
adds an `__EMSCRIPTEN__` guard that registers `sqlcipher_fini` via
`atexit()` instead.  Key zeroing behavior is preserved:

- **Native Linux/macOS:** original `.fini_array` / `__DATA,__mod_term_func`
- **WASM:** `atexit(sqlcipher_fini)` registered via `__attribute__((constructor))`

## Usage

### As a git submodule (recommended)

```bash
git submodule add git@github.com:wmacevoy/sqlcipher-libressl.git vendor/sqlcipher
```

### As a patch over upstream

```bash
git clone git@github.com:sqlcipher/sqlcipher.git
cd sqlcipher
git checkout v4.14.0
# Apply the two patches:
git diff 778ab890..0fced25d -- src/crypto_openssl.c src/sqlcipher.c | git apply
```

### Building the amalgamation (for WASM)

```bash
cd vendor/sqlcipher
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -I$HOME/libressl/include" \
  LDFLAGS="-L$HOME/libressl/lib -lssl -lcrypto"
make sqlite3.c   # produces amalgamation: sqlite3.c + sqlite3.h
```

### Compiling to WASM with Emscripten

```bash
emcc sqlite3.c wyatt_wasm.c \
  -I$HOME/libressl/include \
  -L$HOME/libressl-wasm/lib \
  -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
  -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 \
  -DSQLITE_TEMP_STORE=2 -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1 \
  -s WASM=1 -s MODULARIZE=1 -s FILESYSTEM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  $HOME/libressl-wasm/lib/libcrypto.a \
  -o sqlcipher.js
```

Requires LibreSSL built separately for WASM with `emcmake cmake`.

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
