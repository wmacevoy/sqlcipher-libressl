# Design — v1

How sqlcipher-libressl meets [REQUIREMENTS_v5](REQUIREMENTS_v5.md), and what was rejected. The
database engine and its crypto are upstream's and are described here only as far as a change needs
them; the fork's own work is the patches, the WASM layer, and the build.

## Components and the dependency rule

Higher rows call lower rows, never the reverse. Not enforced by a check.

| Component | Layer | Files | Contract (one line) | Holds state? |
| --------- | ----- | ----- | ------------------- | ------------ |
| unified API | 6 | `wasm/sqlcipher-api.js` | `SQLCipher.open()` → a handle; one worker per handle; each method is one message and one promise | pending requests |
| oo1 shim | 6 | `wasm/sqlcipher-oo1.js` | sqlite3's oo1 API on the main thread; file in MEMFS; `save()` writes the whole file to IndexedDB | open DB, statements |
| worker | 5 | `wasm/sqlcipher-worker.js` | answers the message protocol; chooses the backend; keeps the handle table the VFS reads and writes | handle table, current filename and key, IndexedDB buffer |
| VFS | 4 | `wasm/opfs_vfs.c` | SQLite VFS whose file calls go to the worker's handle table through `EM_JS` | no |
| WASM helpers | 4 | `wasm/sqlcipher_wasm.c` | thin `wasm_db_*` / `wasm_stmt_*` wrappers, so JS never manages WASM memory; one set of SQL-function trampolines that call the oo1 shim through `EM_JS` | no |
| SQLCipher | 3 | `src/` (upstream + the patches), built as `sqlite3.c` | SQLite with page encryption; `sqlite3_key` | SQLite's own |
| crypto provider | 2 | `src/crypto_openssl.c` (patched) | HMAC, PBKDF2, AES and random bytes through LibreSSL | key material in SQLCipher's context |
| LibreSSL | 1 | `libcrypto.a` (native and an Emscripten build) | the OpenSSL 1.1-style API | no |
| build | — | `configure`, `build-sqlcipher-libressl.sh`, `.github/workflows/build-test.yml` | native `.a` per platform; WASM files; the release | no |

## State

| State | Owner (authoritative) | Copies | Max staleness | While wrong, the reader sees |
| ----- | --------------------- | ------ | ------------- | ---------------------------- |
| database file, OPFS backend | the OPFS file | SQLite's page cache | until `xSync` at commit | next open: the file as last synced |
| database file, IndexedDB backend | worker memory (`h.buf`) while open | IndexedDB blocks `[filename, n]`, meta `[filename, -1]` | until the end of the current `exec` (flushed after each one) | next open: the blocks as last flushed |
| journal, WAL, SHM files | worker memory, **both backends** (`_opfs_open_sync` → `openIdb`) | none | — | lost if the worker dies; `xAccess` always reports "no file", so a hot journal is never seen |
| database file, oo1 shim | MEMFS (memory) | one IndexedDB blob per store name | until `save()` | next load: the blob as last saved |
| key | the caller | `_currentKey` in the worker (a JS string, kept for `import`); SQLCipher's derived key in the WASM heap | for the worker's lifetime | — |

**Memory is the danger.** Every backend keeps something only in memory, and a browser can stop a
worker at any moment. DUR-2 depends on what is in memory when that happens; see Known hazards.

## Essential complexity

1. Byte-for-byte SQLCipher file compatibility, with someone else's format (REQ1/FMT-1).
2. Upstream's crypto calls, satisfied by LibreSSL, which has no `EVP_MAC` (REQ1/LIB-1).
3. SQLite's synchronous VFS over browser storage: OPFS's `SyncAccessHandle` is synchronous but only
   in workers; IndexedDB is asynchronous, so its file lives in memory and is flushed after each
   statement (REQ1/DUR-1).
4. One API over two backends that differ in durability (REQ1/API-1).
5. Crash consistency with no journal on disk (REQ1/DUR-2).
6. Keeping the diff from upstream small enough that merging stays cheap (REQ1/UP-2).

## One-way doors

- **The file format.** Not ours; upstream's. A file a user writes today must open in every later
  release (REQ1/FMT-1).
- **The unified API and the worker protocol**, published since v1.0.0 (REQ1/API-1). A rename like
  `save()` → `flush()` (`087b6ec5`, before v1.0.0) now needs expand-then-contract.
- **The IndexedDB layout:** database `sqlcipher_pages`, store `blocks`, keys `[filename, n]`, meta at
  `[filename, -1]`, 4 KB blocks. Users' data lives under it; changing it without a migration orphans
  that data. The OPFS path layout is the same kind of door.
- **Release artifact names.** Downstream builds fetch them by name.

Today every downstream is the maintainer's own project, in development, so these doors are cheap to
change for now (maintainer, 2026-10-20; REQUIREMENTS_v5 *Assumptions*). They close when an outside
project depends on them. Reviewed by: _nobody independent yet_.

## Decisions

Reasons are from the commit messages unless marked *(inferred)*
([SE1/W12](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/08-work.md#w12)).

- **Legacy HMAC API instead of `EVP_MAC`** (`671a294e`) — REQ1/LIB-1 — `EVP_MAC` is OpenSSL 3 only;
  `HMAC_CTX_new` / `HMAC_Init_ex` / `HMAC_Update` / `HMAC_Final` exist in LibreSSL and every
  OpenSSL. Everything else in `crypto_openssl.c` already used APIs both share.
- **`atexit` instead of `.fini_array` under Emscripten** (`0fced25d`) — WASM-safe — Emscripten has
  no `.fini_array`; `atexit` keeps the key zeroing when `Module.exit()` runs. When a tab closes
  without it, the memory goes with the tab.
- **Link `libcrypto.a` by full path** (`8e5a3d6b`) — REQ1/LIB-1 — never `-lcrypto`, so a system
  OpenSSL cannot be picked up instead. SQLCipher needs only libcrypto.
- **LibreSSL from its GitHub release tarball** (`609e0a2d`, `4802d60d`, `6b8acee4`) — REQ1/LIB-1 —
  the OpenBSD mirror was unreliable from CI; the git repository lacks the generated CMake files;
  `curl`, because `gh` needs a token. Rejected: the mirror; `git clone`.
- **CI inputs name a release; the tarball and Playwright are exact** (QUEUE #7) — REQ1/LIB-1 —
  the tarball is checked against a committed SHA-256 and Playwright comes from a committed lock;
  runners and containers name an OS release and take its updates. Which input floats, and why, is
  in CONTRIBUTING *Pinned and floating inputs*. Rejected: image digests and action commit SHAs,
  which need a bot or regular hand edits to stay current.
- **`-s FILESYSTEM=1`** (`fd2f7ef1`, in the plan file it added, since retired) — LibreSSL's
  `libcrypto.a` references BIO socket symbols at link time; about 50 KB of dead code. The oo1 shim
  also keeps its file in MEMFS through `Module.FS` *(inferred)*.
- **The browser backends live in a Worker** (`106be6d4`) — REQ1/DUR-1 — `FileSystemSyncAccessHandle`
  exists only in workers *(inferred)*, and SQLite's VFS is synchronous.
- **IndexedDB as 4 KB blocks, only dirty ones written** (`f5582a32`) — REQ1/DUR-1 — Rejected:
  saving the whole file each time, as the oo1 shim does (write cost grows with the file)
  *(inferred)*.
- **Flush after every `exec`** (`ba34611b`) — REQ1/DUR-1 — matches OPFS, which is durable per
  commit. Rejected: explicit `save()` (renamed `flush()`, `087b6ec5`, and kept for manual use).
- **Journal, WAL and SHM in memory on both backends** (`d68bba71`) — "transient and don't need
  persistence"; this fixed "unable to open database file" on every OPFS write. **Contested:** see
  Known hazards.
- **Shred overwrites with `crypto.getRandomValues` before deleting** (`3033477f`, fixed `e12e2185`)
  — REQ1/SHR-1 — defense in depth on top of encryption.
- **The TCL suite has its own configure, after the release `.a` is kept** (PR #1) — REQ1/UP-1
  — `-DSQLCIPHER_TEST` must not ship. `check.sh` copies the release build to `libsqlcipher.a`
  before reconfiguring, and CI uploads that copy.
- **One native check, `check.sh`, run locally and by CI** (QUEUE #6) — every CI surprise in
  FINDINGS 2026-03-20/21 could have cost a local run instead of a push. Plain POSIX `sh`, because
  the Alpine container has no bash; the suite's status is read from a log file, not a pipe, since
  `sh` has no `pipefail`. The LibreSSL pin moved to `libressl.env` so `check.sh` and the wasm job
  read one place. Rejected: keeping the native steps in the workflow and documenting them twice.
- **No test of stock SQLCipher reading our files** (2026-10-20, maintainer) — REQ1/FMT-1 — see
  [Scope decisions](REQUIREMENTS_v5.md#scope-decisions).
- **Drop the macos-x64 build** (maintainer, 2026-10-20) — REQ2/PLAT-1 — it was built on an arm64
  runner. Rejected: moving it to an Intel runner.
- **No key means an unencrypted database** (maintainer, 2026-10-20) — REQ2/ENC-2 — compatible with
  SQLCipher; a hard call. Rejected: rejecting `open()` without a key.
- **oo1 SQL functions through fixed C trampolines and a registry on `Module`** (PR #5) — REQ4/OO1-2
  — every function registered from JS uses the same five C callbacks; the function's user data is
  an id into `Module._oo1_fns`, and SQLite's destructor removes it on replace or close. Rejected:
  Emscripten's `addFunction`, which needs `ALLOW_TABLE_GROWTH` and a signature per callback.
  Aggregate and window callbacks get a key from `sqlite3_aggregate_context()` where upstream passes
  `pCtx`, because SQLite calls `xFinal` and `xValue` with a context of their own
  (`sqlite3VdbeMemFinalize`), so `pCtx` cannot key per-group state.
- **64-bit integers cross the WASM boundary as a double or as decimal text** (PR #6) — REQ5/INT-1
  — the C helpers return `sqlite3_column_int64` as a double, exact below 2^53, and the JS side
  asks for the decimal text only when the double is not a safe integer; a `BigInt` goes in as
  text. Rejected: `-s WASM_BIGINT`, which changes how every `i64` crosses `cwrap` for both the
  worker and the shim, for a case the text path already covers.
- **The worker steps with `wasm_stmt_step_rc`** (PR #7) — REQ1/ERR-1 — `wasm_stmt_step` reports a
  failed step as "no more rows", so `exec` with bind values resolved after a constraint violation,
  and `select` returned the rows read before an error. `wasm_stmt_step` stays for compatibility; the
  worker and the oo1 shim no longer call it.
- **The oo1 shim's string `key` stays a passphrase** (PR #5, *inferred*; REQUIREMENTS_v4
  *Assumptions*) — REQ4/OO1-2 — the shim always applied it with `sqlite3_key`, as the unified API
  does. Rejected: the reference's old description (UTF-8 bytes as a raw key), under which a
  database written by one API would not open in the other. Raw keys are the `hexkey` option, or a
  byte-array `key`.

## Known hazards

From reading the code on 2026-10-20. Each is a QUEUE item with a test to write first; the first
has now been run.

- **No journal survives a crash.** SQLite writes changed pages into the database file during commit.
  With the journal in memory and `xAccess` always "no", a worker that dies partway through leaves a
  half-written file and nothing to roll it back. On OPFS that is any commit. On IndexedDB each flush
  is one IndexedDB transaction after a whole `exec`, so the exposure is narrower: a transaction left
  open across `exec` calls that grows large enough to spill SQLite's cache, whose uncommitted pages
  the next flush then stores. This contradicts REQ1/DUR-2. **Confirmed** by the REQ1/DUR-2 crash
  tests in `tests/wasm/test.html` (`crash-worker.js` kills the worker before each VFS write), the
  same in Chromium, Firefox and WebKit: on OPFS, a kill during a commit's main-file writes leaves
  `database disk image is malformed`, and a kill after a cache spill leaves the uncommitted rows
  visible with `integrity_check` ok; on IndexedDB a one-`exec` transaction always reopens at its
  last commit, and a transaction spanning `exec`s shows its uncommitted rows once a spilled page has
  been flushed.
- **A failed flush leaves memory ahead of storage.** The statement has already applied in the
  worker; the promise rejects; nothing rolls the memory copy back.

## Deferred

- _Not yet: a GitHub Pages demo_ (`fd2f7ef1`) — `examples/web` served from Pages, built by a
  `pages.yml` workflow (the `wasm` job's build, plus `.nojekyll`); trigger: the maintainer wants a
  public demo.
- _Not yet: multiple databases per worker_ — one worker per handle is simpler; trigger: memory cost
  of many workers.
