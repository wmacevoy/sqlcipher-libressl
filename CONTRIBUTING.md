# Contributing to sqlcipher-libressl

The local mechanics: how to build, test, and reset. The principles behind them are in
[AGENTS.md](AGENTS.md) (copied unchanged from the
[software-engineering guide, v1.1](https://github.com/wmacevoy/software-engineering/tree/v1.1),
whose principle IDs it cites as `SE1/…`); where the two disagree, this file wins. Agents read both
before acting.

Fork of SQLCipher v4.16.0 (SQLite 3.53.1), patched for LibreSSL and WASM: two patched files under
`src/`, plus a WASM layer in `wasm/`.

## The working set

| Document | Answers |
| -------- | ------- |
| [docs/REQUIREMENTS_v6.md](docs/REQUIREMENTS_v6.md) | what must observably be true, and what is not being built |
| [docs/DESIGN_v1.md](docs/DESIGN_v1.md) | how the fork meets them, what was rejected, and the known hazards |
| [docs/QUEUE.md](docs/QUEUE.md) | what is being worked, in what order, and what would make it done |
| [docs/FINDINGS.md](docs/FINDINGS.md) | what was learned the expensive way |
| [docs/oo1-api.md](docs/oo1-api.md) | the API reference: unified API, oo1 shim, persistence, worker protocol |

## Key files

| File | Role |
| ---- | ---- |
| `src/crypto_openssl.c` | patched: legacy HMAC API, for LibreSSL |
| `src/sqlcipher.c` | patched: `atexit` under Emscripten (~line 406) |
| `wasm/sqlcipher_wasm.c` | C helpers for the JS↔WASM boundary |
| `wasm/opfs_vfs.c` | SQLite VFS whose file calls go to the worker (`EM_JS`) |
| `wasm/sqlcipher-api.js` | unified API |
| `wasm/sqlcipher-worker.js` | Web Worker: OPFS or IndexedDB backend |
| `wasm/sqlcipher-oo1.js` | oo1 shim (main thread, IndexedDB blob) |
| `build-sqlcipher-libressl.sh` | WASM build: amalgamation, `emcc`, JS helpers |
| `examples/basic.c` | native smoke test |
| `tests/wasm/` | browser tests (`test.html`) and their Playwright runner |

## Build

Needs LibreSSL 4.2.1 installed at `$HOME/libressl` (the README shows how).

```sh
./configure --with-tempstore=yes \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make -j8              # libsqlite3.a
make sqlite3.c        # amalgamation
```

WASM needs Emscripten 3.1.74 and a second LibreSSL built with `emcmake` (the `wasm` job in
`.github/workflows/build-test.yml` shows the commands), then:

```sh
LIBRESSL_NATIVE=$HOME/libressl LIBRESSL_WASM=$HOME/libressl-wasm ./build-sqlcipher-libressl.sh
```

Output goes to `wasm/dist/`.

To run the browser demo from a release instead:

```sh
cd examples/web
gh release download v1.0.0 --repo wmacevoy/sqlcipher-libressl
python3 -m http.server 8000     # open http://localhost:8000
```

`make distclean` returns the tree to a known state. Browser state lives in the browser: clear the
site's data, or `shred()` the database.

## Test

```sh
# Smoke test (REQ1/ENC-1, REQ2/ENC-2)
gcc -O2 -o basic examples/basic.c -I. -I$HOME/libressl/include \
  libsqlite3.a $HOME/libressl/lib/libcrypto.a -lpthread -ldl -lm
./basic

# Upstream's suite (REQ1/UP-1, REQ1/FMT-1). Needs its own configure; never ship this build.
make distclean
./configure --with-tempstore=yes --enable-fts5 \
  CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown -DSQLCIPHER_TEST -I$HOME/libressl/include" \
  LDFLAGS="$HOME/libressl/lib/libcrypto.a"
make testfixture
cd test && ../testfixture sqlcipher.test     # ends "0 errors out of 208 tests"

# Browser tests: copy the build and the fixture next to the runner, then run it
cp wasm/dist/sqlcipher.js wasm/dist/sqlcipher.wasm wasm/dist/sqlcipher-api.js \
   wasm/dist/sqlcipher-worker.js wasm/dist/sqlcipher-oo1.js \
   sqlcipher-resources/sqlcipher-4.0-testkey.db docs/oo1-api.md tests/wasm/
(cd tests/wasm && npm init -y >/dev/null && npm install playwright && npx playwright install chromium)
node tests/wasm/run-tests.mjs                # ends "Result: N/N passed, 0 failed -- PASS"
```

On macOS with Tcl from conda or Homebrew, `testfixture` may need
`DYLD_LIBRARY_PATH=<that prefix>/lib` to find `libtcl8.6.dylib`.

CI (`.github/workflows/build-test.yml`) builds and smoke-tests on all four platforms, runs
upstream's suite on linux-glibc-x64, and builds and runs the browser tests in headless Chromium. On
a `v*` tag it publishes the release (`.a` per platform, the WASM files, the JS helpers,
`sqlcipher-wasm-static.tar.gz`). Pushes to branches do not run it; open a PR, or start it with
`gh workflow run build-test.yml --repo wmacevoy/sqlcipher-libressl --ref <branch>`.

**The trace.** Browser test names, the smoke test's steps, and CI step names start with the
requirement IDs they test (`REQ1/ENC-1 wrong key rejection`), so `grep -rn REQ1/ENC-1 tests/
examples/ .github/` finds a requirement's tests. Upstream's `test/*.test` files are not renamed:
`sqlcipher.test` is REQ1/UP-1, and its `sqlcipher-compatibility.test` is REQ1/FMT-1 natively. A
requirement with no test is a [QUEUE](docs/QUEUE.md) item.

**Instruments**, each seen to fire:

| Instrument | Fires on | Seen |
| ---------- | -------- | ---- |
| upstream's `sqlcipher.test` in CI | any change that breaks SQLCipher's behavior, including file compatibility | 12 errors, exit 1, with the release flags (locally, 2026-10-19) |
| `shell: bash` (pipefail) on the suite step | the suite failing behind `| tail` | `bash -e` vs `bash -eo pipefail` on a failing pipe, 2026-10-19 |
| WASM "upstream compat" test | the WASM build unable to read an upstream-written file | wrong key: "file is not a database", run 39367518601 |
| `examples/basic.c` | a database written unencrypted, readable without the key, or readable with the wrong one; one written without a key that is not plain SQLite (step 5, REQ2/ENC-2) | `sqlite3_key` removed at create: exit 1 at step 2; `sqlite3_key` planted in step 5: exit 1 (locally, 2026-10-20) |
| WASM "no key" test | the worker encrypting a database opened without a key | worker planted to key every database: run 39465323702 |

## Syncing with upstream

Remotes: `origin` is git@github.com:wmacevoy/sqlcipher-libressl.git, `upstream` is
git@github.com:sqlcipher/sqlcipher.git.

```sh
git fetch upstream
git merge upstream/master     # or the next release tag
git diff <new tag> --stat -- src/   # must list only the two patched files
```

The patches are `sqlcipher_openssl_hmac` in `crypto_openssl.c` and the `sqlcipher_fini`
registration in `sqlcipher.c`. Update the baseline version here, in the README, and in
REQUIREMENTS' glossary (a new version) when it moves.

## Rules specific to this repository

- **LibreSSL, not OpenSSL.** No OpenSSL-3-only API. Link `libcrypto.a` by full path, never
  `-lssl` or `-lcrypto` (REQ1/LIB-1).
- **WASM-safe.** No `.fini_array`, no `fork()`, no signals.
- **Two patched files.** Do not modify other `src/` files, and do not edit upstream's `test/`
  (REQ1/UP-2).
- **The JS API, the worker protocol, and the IndexedDB layout are published** (DESIGN *One-way
  doors*). Change them expand-then-contract, or not at all.
- **`gh` needs `--repo wmacevoy/sqlcipher-libressl`.** This clone has an `upstream` remote and no
  default repository; without the flag, `gh` can act on sqlcipher/sqlcipher.

## License

BSD, same as upstream SQLCipher.
