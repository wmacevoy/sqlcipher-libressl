# Design — v1

How sqlcipher-libressl meets [REQUIREMENTS_v6](REQUIREMENTS_v6.md), and what was rejected. The
database engine and its crypto are upstream's and are described here only as far as a change needs
them; the fork's own work is the patches, the WASM layer, and the build.

## Components and the dependency rule

Higher rows call lower rows, never the reverse. Not enforced by a check.

| Component | Layer | Files | Contract (one line) | Holds state? |
| --------- | ----- | ----- | ------------------- | ------------ |
| unified API | 6 | `wasm/sqlcipher-api.js` | `SQLCipher.open()` → a handle; one worker per handle; each method is one message and one promise | pending requests |
| oo1 shim | 6 | `wasm/sqlcipher-oo1.js` | sqlite3's oo1 API on the main thread; file in MEMFS; `save()` writes the whole file to IndexedDB | open DB, statements |
| worker | 5 | `wasm/sqlcipher-worker.js` | answers the message protocol; chooses the backend; keeps the handle table the VFS reads and writes | handle table, current filename and key, IndexedDB buffer |
| walseal | 4 | `wasm/walseal.c`, `wasm/walseal_sqlcipher.c` | VFS shim over the VFS, the default in the worker: enciphers the `-wal` file's headers in place, passes every other file through; keyed from SQLCipher's derived key, and from its new key for each WAL after a key change | walseal's AES key, per open database |
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
| WAL, OPFS backend | the `<database>-wal` OPFS file, pre-opened with the database; headers enciphered by walseal | SQLite's WAL index, in the heap (`locking_mode=EXCLUSIVE`) | until `xSync` at commit | next open: SQLite recovers the frames up to the last synced commit |
| WAL, IndexedDB backend | worker memory, never stored | none | checkpointed (`TRUNCATE`) into the database before each flush | lost if the worker dies, but the stored database already holds every flushed commit |
| rollback journal | worker memory (`_opfs_open_sync` → `openIdb`), only while SQLite switches a database to WAL | none | — | lost if the worker dies during that one page-1 write; `xAccess` reports no journal |
| database file, oo1 shim | MEMFS (memory) | one IndexedDB blob per store name | until `save()` | next load: the blob as last saved |
| key | the caller | `_currentKey` in the worker (a JS string, kept for `import`); SQLCipher's derived key in the WASM heap; walseal's key (HMAC of the derived key) in the WASM heap | for the worker's lifetime | — |

**Memory is the danger.** A browser can stop a worker at any moment. On OPFS every commit is
synced to the `-wal` file; the IndexedDB backend keeps the WAL and the unflushed database in
memory, and stores only flushed, committed states; the oo1 shim keeps everything in memory until
`save()`. See Known hazards.

## Essential complexity

1. Byte-for-byte SQLCipher file compatibility, with someone else's format (REQ1/FMT-1).
2. Upstream's crypto calls, satisfied by LibreSSL, which has no `EVP_MAC` (REQ1/LIB-1).
3. SQLite's synchronous VFS over browser storage: OPFS's `SyncAccessHandle` is synchronous but only
   in workers; IndexedDB is asynchronous, so its file lives in memory and is flushed after each
   statement (REQ1/DUR-1).
4. One API over two backends that differ in durability (REQ1/API-1).
5. Crash consistency through a journal on storage that must not show what SQLite writes in the
   clear (REQ1/DUR-2, REQ6/ENC-3).
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
change for now (maintainer, 2026-10-20; REQUIREMENTS_v6 *Assumptions*). They close when an outside
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
  in CONTRIBUTING *Pinned and floating inputs*. The two third-party actions are pinned
  by commit SHA (maintainer, 2026-10-20): a moved tag elsewhere would run with the release job's
  write access. Rejected: image digests and SHAs for GitHub's own actions, which need a bot or
  regular hand edits to stay current.
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
  persistence"; this fixed "unable to open database file" on every OPFS write. **Superseded** by the
  next decision (QUEUE #16); only the rollback journal SQLite uses to switch a database to WAL is
  still in memory.
- **A write-ahead log on storage, its headers enciphered by walseal** (maintainer, 2026-10-20) —
  REQ1/DUR-2, REQ6/ENC-3 — the worker opens every database with `locking_mode=EXCLUSIVE` then
  `journal_mode=WAL`: one connection per worker already holds the file exclusively, and exclusive
  mode keeps the WAL index in heap memory, so the VFS needs no shared memory. walseal, a VFS shim
  from the maintainer's `sqlcipher-embedded` (branch `experiment/wal-seal`, commit `5d50c676`),
  enciphers the WAL's file and frame headers in place with a length-preserving tweaked PRP,
  keeping SQLite's layout, writes and syncs; it is ported from PSA Crypto to LibreSSL's
  `libcrypto` (EVP AES-256-ECB, `HMAC()`), the algorithm unchanged, and `sqlite3_opfs_init`
  registers it over `opfs` as the default VFS. Rejected: a rollback journal in `TRUNCATE` mode
  (simpler, no crypto, but every page written twice, page numbers in the clear in the journal,
  and the IndexedDB spill hazard stays); the passphrase as walseal's key (brute-forced without
  PBKDF2's cost); a separate WAL key option (new API surface and key management for integrators).
  - *On OPFS* the `-wal` is a real OPFS file, pre-opened with the database
    (`createSyncAccessHandle` is asynchronous; SQLite's `xOpen` is not). Its handle outlives
    SQLite's `xClose`, so SQLite can reopen it, and the worker closes it after the database.
    `xDelete` empties it instead of removing it (`removeEntry` is asynchronous too), and `xAccess`
    answers from the pre-opened handles: a file exists if it is non-empty. So recovery finds a WAL
    with frames, and an emptied one reads as none. A `-wal` that was not pre-opened is an error,
    not a memory file.
  - *On IndexedDB* the WAL stays in worker memory and is never stored. In WAL mode only a
    checkpoint writes the database file, and it copies committed pages only; each `exec` ends with
    a `TRUNCATE` checkpoint and one IndexedDB transaction for the dirty blocks, so what is stored
    is always a committed state, and a dead worker loses at most the `exec` in flight, which never
    resolved. Storing the WAL too would add a second write per commit and a change to the
    IndexedDB layout (a one-way door) for no gain in DUR-2. This also ends the old spill hazard:
    uncommitted pages now go to the WAL, not the database file.
  - *The key* is SQLCipher's derived encryption key for the database (PBKDF2 of the passphrase,
    or the raw key), which walseal turns into its own with HMAC-SHA256 and the label
    `walseal hdr`; so the headers cost the same PBKDF2 work to attack as the pages.
    `walseal_key_from_codec` (`wasm/walseal_sqlcipher.c`, compiled in the amalgamation's
    translation unit because it calls SQLCipher's static codec functions) runs right after
    `sqlite3_key` and before the first read: it asks SQLCipher to derive the key now, as the first
    page read would, so the read then finds it derived (one PBKDF2, not two), unmasks a copy,
    hands it to walseal and wipes it. It must run before the first read because that read can
    open the WAL, and walseal refuses a WAL without a key. Rejected: walseal pulling the key
    lazily when it opens the WAL (the VFS cannot reach the pager's codec from a file; it would need
    a callback and the database handle). With no key (REQ2/ENC-2) there is no codec and the WAL
    passes through: an unencrypted database's pages are in the clear anyway.
  - *A key changed in a session* (`PRAGMA rekey`, or `key`, `hexkey`, `textkey` and their
    `re…` forms, through `exec` or `select`) — REQ1/DUR-2, REQ6/ENC-3. All frames of one WAL
    must be sealed under one key, the one the next `open` derives; SQLCipher applies these
    pragmas while the statement is prepared, and rekey rewrites every page in one commit. So
    `walseal_follow_codec` (`wasm/walseal_sqlcipher.c`, run with `walseal_key_from_codec` at
    open) installs two things: an authorizer that checkpoints (`TRUNCATE`) before a key pragma
    runs, so its commit starts a new WAL; and a key source walseal calls each time a new WAL
    starts, which hands it the codec's *write* key (the new key while rekey writes). After every
    `exec` and `select` the worker calls `walseal_codec_settle`, which checkpoints again if a key
    pragma ran, so the database file is under the new key and the old key opens nothing. The
    authorizer refuses a key pragma (SQLite's `not authorized`) inside a transaction, where it
    cannot checkpoint, and after an `open` whose key could not read the database: SQLite then
    took the WAL for empty, and a new WAL would overwrite its commits; close and open again with
    the right key. Rejected: checkpointing and re-keying walseal only after the statement, as
    first proposed — rekey's own commit would be sealed under the old key, and a crash before
    the checkpoint would leave a WAL the new key cannot read over pages the old key cannot read,
    so neither key would open the database; and spotting key pragmas in the SQL text from
    JavaScript (every statement naming a `key` column would checkpoint). The authorizer replaces
    any other on the worker's connection; the unified API exposes none.
  - *A key that cannot read the database* (wrong, or none for an encrypted file) still derives a
    walseal key, so the WAL's headers decipher to noise and SQLite takes the WAL for empty. A
    normal close would then checkpoint nothing and delete the WAL, losing its commits. So when
    `journal_mode=WAL` fails with `SQLITE_NOTADB`, the worker sets
    `SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE`, and `open` still resolves (REQ1/ENC-1: the first read
    fails, not the open). It then gives SQLCipher the key again, which resets its codec: after a
    failed read SQLCipher answers every later read "out of memory", so without this the caller's
    first read would not fail with `file is not a database`.
  - *export* checkpoints (`TRUNCATE`) before reading the file; *import* empties the `-wal` before
    reopening, so no frame of the old database is replayed over the new one; *shred* overwrites
    the `-wal`'s frames with random bytes (closing without a checkpoint, so they are still there)
    and removes it, as it does the database.
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
  [Scope decisions](REQUIREMENTS_v6.md#scope-decisions).
- **The native build opens what the WASM build exports** (maintainer, 2026-10-20) — REQ1/EXP-1,
  REQ1/FMT-1 — under WAL the newest rows sit in the `-wal` until `export()` checkpoints, and the
  exported file is a WAL-mode file with no `-wal` beside it. `run-tests.mjs` saves each run's
  export (`NATIVE_EXPORT_DIR`), and the wasm job compiles `tests/wasm/open-export.c` with the
  amalgamation and native LibreSSL it already has and opens every file: `integrity_check`, WAL
  mode, every row. This is the fork's own native code (upstream plus the two patches), not stock
  SQLCipher, so the scope decision above stands. Rejected: a test hook in the worker to dump the
  file (the API already returns the bytes); a separate CI job (it would rebuild LibreSSL).
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

From reading the code on 2026-10-20; none of them has been run unless it says so. Each is a QUEUE
item with a test to write first. (The first one recorded here, "No journal survives a crash", was
confirmed by the REQ1/DUR-2 crash tests and is gone with the WAL on storage, QUEUE #16; the second,
"Changing the key inside a session can lose the WAL's commits", is fixed and tested, see
*Decisions*.)

- **A worker killed inside the `exec` that changes the key** leaves the database readable under
  either key (the rekey crash test). Until that `exec` checkpoints, the database file is still
  under the old key, so the old key opens the state before the change, and closing that
  connection discards the WAL holding the change and anything after it in the same `exec`. The
  `exec` never resolved, so nothing acknowledged is lost; once it resolves, only the new key
  opens the database.
- **After a key pragma, `import` reopens under the key given to `open`** (from reading the code;
  untested). The worker keeps that passphrase (`_currentKey`) to reopen the database after
  `import`; a `PRAGMA rekey` through `exec` does not change it, so `import` of a file under the
  new key opens it under the old one, and its first read fails.
- **Non-default cipher settings and the WAL.** A database that needs them (`PRAGMA
  cipher_compatibility` and the like, given after `open`) fails the read `open` makes, so the
  worker never switches it to WAL: unless it already is one, its rollback journal stays in
  memory, and DUR-2 does not hold for it. A key pragma on such a connection is refused (it could
  not read its database at `open`).
- **The switch to WAL uses a rollback journal in memory.** The first time a database is opened,
  SQLite writes page 1 to mark it WAL, journaled in memory. Killing a worker cannot tear that one
  write; a power loss might (as below).
- **A torn page 1 during a checkpoint makes the database unopenable** (found in
  `sqlcipher-embedded`, its README): SQLCipher reads the KDF salt from page 1 of the database file
  before it consults the WAL. Killing a worker cannot tear a write, since each `write()` on a sync access
  handle completes or never starts, but a power loss or an OS crash might. A raw key that includes the salt
  (`x'<key><salt>'`) recovers it. Not addressed; a question for the maintainer if power loss
  matters.
- **A failed flush leaves memory ahead of storage.** The statement has already applied in the
  worker; the promise rejects; nothing rolls the memory copy back.

## Deferred

- _Not yet: a GitHub Pages demo_ (`fd2f7ef1`) — `examples/web` served from Pages, built by a
  `pages.yml` workflow (the `wasm` job's build, plus `.nojekyll`); trigger: the maintainer wants a
  public demo.
- _Not yet: multiple databases per worker_ — one worker per handle is simpler; trigger: memory cost
  of many workers.
