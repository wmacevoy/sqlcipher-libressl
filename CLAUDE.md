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

## Commands

```bash
# Build (requires LibreSSL installed at $HOME/libressl)
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make -j$(nproc)

# Build amalgamation
make sqlite3.c

# Smoke test
gcc -O2 -o basic examples/basic.c -I. -I$HOME/libressl/include \
  -L.libs -L$HOME/libressl/lib -lsqlcipher $HOME/libressl/lib/libcrypto.a -lpthread -ldl -lm
LD_LIBRARY_PATH=.libs:$HOME/libressl/lib ./basic

# Full test suite (requires tcl)
make testfixture
cd test && ../testfixture sqlcipher.test
```

## CI

`.github/workflows/build-test.yml` runs on every push to master:
1. Downloads and builds LibreSSL 4.0.0
2. Configures and builds SQLCipher
3. Builds the amalgamation
4. Runs the smoke test (`examples/basic.c`)
5. Runs the SQLCipher TCL test suite

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
