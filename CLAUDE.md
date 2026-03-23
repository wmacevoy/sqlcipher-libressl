# CLAUDE.md

## Project

sqlcipher-libressl — Encrypted SQLite everywhere.  Fork of SQLCipher
v4.14.0 (SQLite 3.51.3) patched for LibreSSL and WASM.  Two patches
over upstream.  Ships a WASM build with unified API — auto-detects OPFS
(durable/commit) or IndexedDB page cache (durable/exec) for
encrypted persistence in the browser.

## Architecture

```
Native:   SQLCipher + LibreSSL libcrypto.a → encrypted SQLite
Browser:  SQLCipher WASM + VFS → encrypted, durable, no server
          Worker auto-detects: OPFS (durable/commit) or IndexedDB page cache (durable/exec)
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
| `build-sqlcipher-libressl.sh` | WASM build script (amalgamation + emcc + JS helpers) |
| `examples/web/index.html` | Browser demo (unified API) |
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
gh release download v1.0.0 --repo wmacevoy/sqlcipher-libressl
python3 -m http.server 8000
```

## CI

`.github/workflows/build-test.yml`:
- **native job** (5-platform matrix): build with LibreSSL v4.2.1, smoke test on all;
  TCL test suite on linux-glibc-x64.  Platforms: debian-glibc-x64,
  debian-glibc-arm64, alpine-musl-x64, macos-arm64, macos-x64.
- **wasm job**: calls `build-sqlcipher-libressl.sh`, Playwright browser tests
- **release job** (on `v*` tags): creates GitHub release via
  `softprops/action-gh-release@v2` with native `.a` libs + WASM artifacts:
  `sqlcipher.js`, `sqlcipher.wasm`, `sqlcipher-api.js`, `sqlcipher-oo1.js`,
  `sqlcipher-worker.js`, `sqlcipher-wasm-static.tar.gz`,
  `libsqlcipher-{platform}.a` (5 platforms)

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
