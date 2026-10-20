# Design — v1

How sqlcipher-libressl meets [REQUIREMENTS_v1](REQUIREMENTS_v1.md), and what was rejected. The
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
| WASM helpers | 4 | `wasm/sqlcipher_wasm.c` | thin `wasm_db_*` / `wasm_stmt_*` wrappers, so JS never manages WASM memory | no |
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

Reviewed by: _nobody independent yet_.

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
- **`-s FILESYSTEM=1`** (`future-plan.md`) — LibreSSL's `libcrypto.a` references BIO socket symbols
  at link time; about 50 KB of dead code.
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
- **The TCL suite has its own configure, after the release `.a` is uploaded** (PR #1) — REQ1/UP-1
  — `-DSQLCIPHER_TEST` must not ship.
- **No test of stock SQLCipher reading our files** (2026-10-20, maintainer) — REQ1/FMT-1 — see
  [Scope decisions](REQUIREMENTS_v1.md#scope-decisions).

## Known hazards

From reading the code on 2026-10-20; none of them has been run. Each is a QUEUE item with a test to
write first.

- **No journal survives a crash.** SQLite writes changed pages into the database file during commit.
  With the journal in memory and `xAccess` always "no", a worker that dies partway through leaves a
  half-written file and nothing to roll it back. On OPFS that is any commit. On IndexedDB each flush
  is one IndexedDB transaction after a whole `exec`, so the exposure is narrower: a transaction left
  open across `exec` calls that grows large enough to spill SQLite's cache, whose uncommitted pages
  the next flush then stores. This contradicts REQ1/DUR-2.
- **`exec` with bind values never reports failure.** `wasm_stmt_step` returns 1 for a row and 0 for
  anything else, error included; the worker loops until 0 and resolves `ok`. A constraint violation
  succeeds silently (contradicts REQ1/ERR-1). `select` likewise returns the rows read before an
  error.
- **No BLOBs.** Binding a `Uint8Array` stores its text; reading a BLOB column gives `null`.
- **A missing key means an unencrypted database**, silently.
- **A failed flush leaves memory ahead of storage.** The statement has already applied in the
  worker; the promise rejects; nothing rolls the memory copy back.

## Deferred

- _Not yet: a GitHub Pages demo_ (`future-plan.md`) — trigger: the maintainer wants a public demo.
- _Not yet: multiple databases per worker_ — one worker per handle is simpler; trigger: memory cost
  of many workers.
