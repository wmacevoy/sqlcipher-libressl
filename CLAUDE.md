# CLAUDE.md

## Project

sqlcipher-libressl — Encrypted SQLite everywhere.  Fork of SQLCipher
v4.14.0 (SQLite 3.51.3) patched for LibreSSL and WASM.  Two patches
over upstream.  Ships a WASM build with OPFS VFS for durable encrypted
persistence in the browser.

## Architecture

```
Native:   SQLCipher + LibreSSL libcrypto.a → encrypted SQLite
Browser:  SQLCipher WASM + VFS → encrypted, durable, no server
          Worker auto-detects: OPFS (durable/commit) or IndexedDB page cache (durable/save)
          Unified API: sqlcipher-api.js — same interface, best backend
          oo1 API shim still available for main-thread use
```

## Key files

| File | Role |
|------|------|
| `src/crypto_openssl.c` | Patched: HMAC legacy API for LibreSSL |
| `src/sqlcipher.c` | Patched: atexit for WASM |
| `wasm/sqlcipher_wasm.c` | C helpers for JS↔WASM boundary |
| `wasm/opfs_vfs.c` | OPFS-backed SQLite VFS (EM_JS callbacks) |
| `wasm/sqlcipher-api.js` | Unified API — auto-detects OPFS / IndexedDB |
| `wasm/sqlcipher-oo1.js` | oo1 API shim (main thread, IndexedDB blob) |
| `wasm/sqlcipher-worker.js` | Web Worker — OPFS or IndexedDB page cache |
| `examples/basic.c` | Native C smoke test |
| `examples/web/index.html` | Browser demo (Worker + OPFS) |
| `examples/web/unified.html` | Browser demo (unified API) |
| `docs/oo1-api.md` | Full API reference + persistence docs |

## Commands

```bash
# Build (requires LibreSSL at $HOME/libressl)
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make -j$(nproc)

# Amalgamation
make sqlite3.c

# Native smoke test
gcc -O2 -o basic examples/basic.c -I. -I$HOME/libressl/include \
  libsqlite3.a $HOME/libressl/lib/libcrypto.a -lpthread -ldl -lm
./basic

# Full TCL test suite
make testfixture
cd test && ../testfixture sqlcipher.test

# Web example (download release artifacts first)
cd examples/web
gh release download v0.1.0 --repo wmacevoy/sqlcipher-libressl
python3 -m http.server 8000
```

## CI

`.github/workflows/build-test.yml`:
- **native job**: build with LibreSSL v4.2.1, smoke test, TCL test suite
- **wasm job**: build amalgamation, LibreSSL for WASM, compile with OPFS VFS
- **release job** (on `v*` tags): creates GitHub release with
  `sqlcipher.js`, `sqlcipher.wasm`, `sqlcipher-api.js`, `sqlcipher-oo1.js`, `sqlcipher-worker.js`

## Remotes

- `origin` — git@github.com:wmacevoy/sqlcipher-libressl.git
- `upstream` — git@github.com:sqlcipher/sqlcipher.git

## Syncing with upstream

```bash
git fetch upstream
git merge upstream/master
```

Patches are in `sqlcipher_openssl_hmac` (crypto_openssl.c) and
`sqlcipher_fini` registration (sqlcipher.c ~line 406).

## Constraints

- **LibreSSL, not OpenSSL.** Do not use OpenSSL 3-only APIs.
  Link `libcrypto.a` by full path, never `-lssl` or `-lcrypto`.
- **WASM-safe.** No `.fini_array`, no `fork()`, no signals.
- **Minimal diff.** Two patched files over upstream.  Do not modify other src/ files.

## License

BSD (same as upstream SQLCipher)
