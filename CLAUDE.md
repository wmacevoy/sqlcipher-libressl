# CLAUDE.md

## Project

sqlcipher-libressl — Fork of SQLCipher v4.14.0 (SQLite 3.51.3) patched
for LibreSSL compatibility and Emscripten/WASM builds.  Two patches over
upstream, three files changed.

## Patches

Only two files are modified from upstream:

1. **`src/crypto_openssl.c`** — HMAC function: replaced OpenSSL 3 `EVP_MAC`
   API with legacy `HMAC_CTX_new`/`HMAC_Init_ex`/`HMAC_Update`/`HMAC_Final`.
   Works with LibreSSL and all OpenSSL versions.

2. **`src/sqlcipher.c`** — WASM cleanup: `__EMSCRIPTEN__` guard replaces
   `.fini_array` with `atexit()` for key zeroing in WASM builds.

Everything else is upstream SQLCipher.  Do not modify other files.

## Remotes

- `origin` — git@github.com:wmacevoy/sqlcipher-libressl.git (this fork)
- `upstream` — git@github.com:sqlcipher/sqlcipher.git

## Syncing with upstream

```bash
git fetch upstream
git merge upstream/master
# Re-check that the two patches still apply cleanly to crypto_openssl.c and sqlcipher.c
```

The patches are in the function `sqlcipher_openssl_hmac` (crypto_openssl.c)
and the `sqlcipher_fini` registration block (sqlcipher.c ~line 406).
If upstream changes these areas, the patches may need manual rebasing.

## Building

This is a standard SQLCipher checkout.  The amalgamation build:

```bash
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -I/path/to/libressl/include" \
  LDFLAGS="-L/path/to/libressl/lib -lssl -lcrypto"
make sqlite3.c
```

For WASM builds, see the README.

## Constraints

- **LibreSSL, not OpenSSL.** All crypto must work with LibreSSL.
  Do not use OpenSSL 3-only APIs (EVP_MAC, OSSL_PARAM, providers).
- **WASM-safe.** No `.fini_array`, no `fork()`, no signals.
  Use `atexit()` behind `__EMSCRIPTEN__` guards.
- **Minimal diff.** Keep the patch footprint small.  Two files changed
  is the target.  Do not refactor unrelated code.

## License

BSD (same as upstream SQLCipher)
