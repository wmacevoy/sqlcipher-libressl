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
| [docs/REQUIREMENTS_v5.md](docs/REQUIREMENTS_v5.md) | what must observably be true, and what is not being built |
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
| `tests/wasm/` | browser tests (`test.html`), their Playwright runner, and test-only worker wrappers (`backend-worker.js` forces the backend) |

## Build

Needs a C compiler, `make`, `cmake` and `curl`; for the TCL suite, Tcl and its headers.

```sh
./check.sh            # LibreSSL, the release library, the smoke test, the TCL suite
```

`check.sh` is the native build and test, the same one CI's `native` job runs. It downloads the
LibreSSL release pinned in `libressl.env`, checks its SHA-256, and installs it at
`$LIBRESSL_PREFIX` (default `$HOME/libressl`); builds `libsqlite3.a` and `sqlite3.c` and keeps a copy
of the release library as `libsqlcipher.a`; runs the smoke test against it; then reconfigures with
upstream's test flags and runs the TCL suite. It exits non-zero at the first failure. Because the
suite reconfigures, the tree ends in the test build: ship `libsqlcipher.a`, never a `libsqlite3.a`
built after it. `CHECK_TCL=no ./check.sh` stops after the smoke test; CI sets it everywhere but
linux-glibc-x64, the one platform REQ1/UP-1 names.

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
# Native: smoke test (REQ1/ENC-1, REQ2/ENC-2) and upstream's suite (REQ1/UP-1, REQ1/FMT-1)
./check.sh                                   # ends "0 errors out of 208 tests" and "PASS: check.sh"

# Browser tests: copy the build and the fixture next to the runner, then run it
cp wasm/dist/sqlcipher.js wasm/dist/sqlcipher.wasm wasm/dist/sqlcipher-api.js \
   wasm/dist/sqlcipher-worker.js wasm/dist/sqlcipher-oo1.js \
   sqlcipher-resources/sqlcipher-4.0-testkey.db docs/oo1-api.md tests/wasm/
(cd tests/wasm && npm ci && npx playwright install chromium firefox webkit)   # Playwright as locked
node tests/wasm/run-tests.mjs                # ends "Result: 6/6 browser/backend runs passed -- PASS"
```

On macOS with Tcl from conda or Homebrew, `testfixture` may not find `libtcl8.6.dylib`
("Library not loaded: @rpath/libtcl8.6.dylib"). Name its directory in `TCL_LIBDIR`:
`TCL_LIBDIR=$HOME/miniforge3/lib ./check.sh`, for example. `DYLD_LIBRARY_PATH` does not work
here: macOS drops it before `/bin/sh` starts, so it never reaches `testfixture`.

CI (`.github/workflows/build-test.yml`) checks that `src/` differs from upstream's tag only in the
two patched files, runs `check.sh` on all four platforms (the TCL suite on linux-glibc-x64 only),
checks that each `.a` is the architecture its name says (`objdump -f`, or `lipo -info` on macOS)
before uploading it, and builds and runs the browser tests in headless Chromium, Firefox and
WebKit, once per backend in each (`?backend=opfs`, `?backend=indexeddb`). On a `v*` tag it
publishes the release (`.a` per platform, the WASM files, the JS helpers,
`sqlcipher-wasm-static.tar.gz`). Pushes to branches do not run it; open a PR, or start it with
`gh workflow run build-test.yml --repo wmacevoy/sqlcipher-libressl --ref <branch>`.

**The trace.** Browser test names, the smoke test's steps, and CI step names start with the
requirement IDs they test (`REQ1/ENC-1 wrong key rejection`), so `grep -rn REQ1/ENC-1 tests/
examples/ .github/ check.sh` finds a requirement's tests. Upstream's `test/*.test` files are not renamed:
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

## Pinned and floating inputs

What CI builds from, and whether it can change under us. The rule: every tag names a release, never
`latest`, so a new release arrives as a commit here; inside a release, updates float.

| Input | Where | Pinned to | Floats | Why |
| ----- | ----- | --------- | ------ | --- |
| LibreSSL tarball | `LIBRESSL_VERSION`, `LIBRESSL_SHA256` in `libressl.env` (read by `check.sh` and the wasm job) | 4.2.1, by SHA-256 | nothing | the crypto we ship; a changed tarball must stop the build (REQ1/LIB-1) |
| Playwright and its Chromium | `tests/wasm/package.json` and `package-lock.json`, installed with `npm ci` | 1.64.1, which fixes its browser build | nothing | a new browser arrives as a reviewed change, not as a red run |
| Emscripten | `setup-emsdk`'s `version:` | 3.1.74 | nothing | it decides the WASM output |
| upstream SQLCipher | `UPSTREAM_TAG` | v4.16.0 | nothing | the two-file diff is measured against it |
| containers | `debian:bookworm`, `alpine:3.19` | the distribution release | point and patch updates | the release fixes the libc the `.a` targets; the updates are security fixes |
| runners | `ubuntu-24.04`, `ubuntu-24.04-arm`, `macos-26` | the OS release | GitHub's image updates | `macos-26` is what `macos-latest` ran on 2026-10-20 and builds the shipped macOS `.a`; the Linux runners host a container, or (WASM) supply only `tcl` and `cmake` |
| actions | `actions/*@v4`, `mymindstorm/setup-emsdk@v14`, `softprops/action-gh-release@v2` | the major version | minor and patch releases | the publisher's compatibility promise; pinning by commit SHA is an open choice |
| Node.js | `setup-node`'s `node-version` | 22 | minor and patch | runs only the test runner |

To bump LibreSSL, download the new tarball, check its hash against the `SHA256` file OpenBSD
publishes (`https://cdn.openbsd.org/pub/OpenBSD/LibreSSL/SHA256`), and change both values in
`libressl.env` together. To bump Playwright, change its version in `tests/wasm/package.json`, run
`npm install --package-lock-only` there, and commit both files. A runner or container its
publisher retires fails the job by name; move it to the next release then.

## Syncing with upstream

Remotes: `origin` is git@github.com:wmacevoy/sqlcipher-libressl.git, `upstream` is
git@github.com:sqlcipher/sqlcipher.git.

```sh
git fetch upstream
git merge upstream/master     # or the next release tag
git diff <new tag> --stat -- src/   # must list only the two patched files
```

The patches are `sqlcipher_openssl_hmac` in `crypto_openssl.c` and the `sqlcipher_fini`
registration in `sqlcipher.c`. CI's REQ1/UP-2 step fails if any other `src/` file differs from
the tag in `UPSTREAM_TAG` at the top of `.github/workflows/build-test.yml`. Update that tag, the
baseline version here, in the README, and in REQUIREMENTS' glossary (a new version) when it moves.

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
