# Requirements — v10

What must be observably true of sqlcipher-libressl, and what is not being built. A change is a new
version (`REQUIREMENTS_v11.md`), never an edit to this one. See the guide's
[SE1/R1–R11](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/04-requirements.md).

Client (the one voice who can decide): Warren MacEvoy (maintainer) · Proxy for the users
([SE1/H5](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/09-people.md#h5)):
the maintainer, for the downstream projects (all the maintainer's own, all in development) · Date sent:
2026-10-20 · Supersedes [v9](REQUIREMENTS_v9.md) · *Tell us what is wrong with it.*

v1 was written after the fact, from the code, the tests, the README, and the commit history at
`bef26b74`. Every statement that came from inference rather than from the client is in
**Assumptions**, or marked *(inferred)*, until read back
([SE1/W12](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/08-work.md#w12)).
Requirements are cited with the version that last changed them: `REQ1/ENC-1`, `REQ2/PLAT-1`.

## Changes from v9

- **REC-1 (new):** a native recovery tool turns a database and its walseal-sealed WAL into a plain
  SQLCipher database — the maintainer, 2026-10-20, for sqlcipher-embedded's devices (its
  REQUIREMENTS_v2, EMB2/REC-1): a failed device cannot checkpoint, and SQLCipher without walseal
  takes a sealed WAL for empty and deletes it on close.

v9 took `import` under the current key and refused `textkey` / `textrekey`. v8 narrowed DUR-2 to default cipher settings and accepted refusing a key pragma inside a
transaction or after a failed read. v7 named the browsers WASM-1 is tested in and made the README's minimums an untested claim. v6 kept DUR-2 (a WAL on storage) and added ENC-3 (its headers enciphered). v5 added INT-1 (64-bit integers). v4 added OO1-2 (the oo1 shim does what its reference says). v3 added OO1-1 (BLOBs through the oo1 shim). v2 dropped Intel macOS (PLAT-1), added ENC-2 (no key, no encryption) and API-2 (BLOBs through the
unified API), and named the proxy for the users.

## Glossary

| Term | Meaning |
| ---- | ------- |
| upstream | [sqlcipher/sqlcipher](https://github.com/sqlcipher/sqlcipher) at tag `v4.16.0` (SQLite 3.53.1), the current baseline |
| the patches | the fork's only changes under `src/`: the HMAC rewrite in `crypto_openssl.c` and the WASM `atexit` in `sqlcipher.c` |
| LibreSSL | the crypto library this fork links, v4.2.1, as `libcrypto.a` by full path |
| key | the passphrase given to `sqlite3_key` / `SQLCipher.open({key})`; SQLCipher derives the encryption key from it |
| native build | `libsqlite3.a` from `./configure && make`, released as `libsqlcipher-{platform}.a` |
| WASM build | `sqlcipher.js` + `sqlcipher.wasm` from `build-sqlcipher-libressl.sh` |
| unified API | `sqlcipher-api.js`: `SQLCipher.open()` and the handle it returns; the recommended browser interface |
| worker | `sqlcipher-worker.js`: runs the WASM build in a Web Worker and answers the unified API's messages |
| backend | where the worker keeps the database file: `opfs` or `indexeddb`, chosen at `init` |
| OPFS backend | the file lives in the Origin Private File System, written through a `FileSystemSyncAccessHandle` |
| IndexedDB backend | the file lives in worker memory and is copied to IndexedDB as 4 KB blocks. The README calls this the "page cache"; it is not SQLite's own page cache |
| flush | copy the worker's dirty blocks to IndexedDB (IndexedDB backend); nothing on OPFS |
| WAL | SQLite's write-ahead log, `<database>-wal`: committed pages not yet copied into the database file. SQLCipher encrypts the pages in it; SQLite writes its 32-byte file header and each frame's 24-byte header (page number, database size at commit, salts, checksums) in the clear |
| export / import | the whole encrypted file as bytes, out of / into a database |
| shred | overwrite the stored file with random bytes, then delete it |
| oo1 shim | `sqlcipher-oo1.js`: the sqlite3 oo1 API on the main thread, persisted by explicit `save()` of the whole file to IndexedDB |
| fixture | `sqlcipher-resources/sqlcipher-*-testkey.db`: databases written by official SQLCipher releases, key `testkey` |
| recovery tool | `walseal/walseal_recover.c`: a native program that copies a database and its sealed WAL, opens the copies through walseal, checkpoints, and writes a plain SQLCipher database (REC-1) |
| integrator | a person or project building on this fork: linking a release `.a`, loading the WASM files, or calling the build script |

## The pain

Upstream SQLCipher's HMAC code requires OpenSSL 3, so it does not build against LibreSSL, and it does
not build for WASM. An integrator who wants encrypted SQLite on small or unusual targets, or in a
browser with no server, has no lighter option. *(Inferred — see Assumptions.)*

## People this touches

- **Downstream projects** — today only the maintainer's own, all in development. They are the users
  this document speaks for.
- **Integrators, native** — link `libsqlcipher-{platform}.a` with LibreSSL's `libcrypto.a`.
- **Integrators, browser** — load `sqlcipher.js`, `sqlcipher.wasm`, `sqlcipher-api.js` and the worker
  from a release.
- **Downstream builds** — call `build-sqlcipher-libressl.sh` from another repository (qjson, one
  of the maintainer's projects).
- **End users of those apps** — never see the library; expect their data to be private at rest and
  still there tomorrow.
- **sqlcipher-embedded** — the maintainer's library for ESP32-class devices; compiles `walseal/` with
  a PSA backend, and needs REC-1 to read a failed device's storage.
- **Upstream SQLCipher** — the source of every merge; never receives these patches *(inferred)*.

## Requirements

| ID | Requirement |
| -- | ----------- |
| **ENC-1** | *(since v1)* A database written with key *K* opens without error under any other key, or none, but its first read fails with `file is not a database`; under *K* it reads back what was written. Native and WASM. |
| **ENC-2** | *(since v2)* With no key — `SQLCipher.open()` without `key`, or `sqlite3_key` never called — a database is opened or created unencrypted, as in SQLCipher: its file begins with `SQLite format 3\0`. |
| **FMT-1** | *(since v1)* A database written by official SQLCipher 4.0 with default settings opens here under its key, `PRAGMA integrity_check` returns `ok`, and its rows read back — native and WASM. Natively, databases from SQLCipher 1.1.8, 2.0 and 3.0 open as upstream's tests require. |
| **UP-1** | *(since v1)* Upstream's `test/sqlcipher.test`, built with upstream's test recipe, reports 0 errors on linux-glibc-x64. |
| **UP-2** | *(since v1)* Under `src/`, the diff from upstream's tag touches only `crypto_openssl.c` and `sqlcipher.c`. |
| **LIB-1** | *(since v1)* The native and WASM builds link LibreSSL 4.2.1's `libcrypto.a` and nothing else from OpenSSL or LibreSSL: no `libssl`, no `-lcrypto`, no OpenSSL-3-only API. |
| **PLAT-1** | *(since v2)* Each release has a static library for linux-glibc-x64, linux-glibc-arm64, linux-musl-x64 and macos-arm64, each built on that platform and passing `examples/basic.c` there. |
| **WASM-1** | *(since v7)* The WASM build runs in a browser Web Worker with no server-side code: open, write, read, close — in the Chromium, Firefox and WebKit builds of the Playwright version `tests/wasm/package-lock.json` locks, on either backend. |
| **API-1** | *(since v1)* The unified API behaves as the README's method table says: `open`, `exec`, `select`, `flush`, `export`, `import`, `shred`, `shredOnClose`, `close`, `mode`. Published since v1.0.0. |
| **API-2** | *(since v2)* Through the unified API, a `Uint8Array` bound to a statement is stored as a BLOB, and a BLOB column reads back as a `Uint8Array` with the same bytes, on either backend. |
| **OO1-1** | *(since v3)* Through the oo1 shim, a `Uint8Array` given to `bind()` or `bindAsBlob()` is stored as a BLOB, and a string given to `bindAsBlob()` is stored as its UTF-8 bytes; a BLOB column reads back as a `Uint8Array` with the same bytes from `get(ndx)`, `get([])`, `get({})` and `getBlob(ndx)`. |
| **OO1-2** | *(since v4)* Every `DB` and `Stmt` method, constructor form and option that `docs/oo1-api.md` documents exists in the oo1 shim and behaves as the reference describes, and the reference documents nothing the shim does not do. A string `key` is a passphrase, as in the unified API, so a database written through one opens through the other. |
| **INT-1** | *(since v5)* Through the unified API and the oo1 shim, every 64-bit integer SQLite stores reads back exactly: as a `number` when it is a safe integer (\|v\| ≤ 2^53 − 1), otherwise as a `BigInt`. A `number` that is a safe integer, or a `BigInt` within 64 bits, is stored as an INTEGER; a `BigInt` beyond 64 bits is an error, not a rounded value. In the oo1 shim the same holds for SQL function arguments and results. |
| **ENC-3** | *(since v6)* Without the key, a WAL the WASM build keeps on storage reveals no more than the database file does: not which pages a transaction wrote, not where commits fall, not the database's size at each commit. Its headers are no easier to attack than its pages: recovering them takes the same key derivation. |
| **ERR-1** | *(since v1)* Every SQL statement that fails rejects its promise with SQLite's error message, and reports nothing as changed *(inferred)*. |
| **DUR-1** | *(since v1)* Once `exec` resolves, its changes survive closing and reopening the database, on either backend. |
| **DUR-2** | *(since v8)* If the tab or worker dies at any point, the next `open` finds the database at its last committed state, and `integrity_check` returns `ok` (the README's "Tab crash: Data safe"). This holds for a database that opens with SQLCipher's default cipher settings; one that needs non-default settings is not covered (*Scope decisions*). |
| **EXP-1** | *(since v9)* `export()` returns the encrypted file; `import(bytes)` of a file under the database's current key — the key given to `open`, or the one a key pragma through `exec` changed it to — replaces its contents with exactly that file's. |
| **REC-1** | *(since v10)* Given a database and its WAL sealed by walseal — from the WASM build, or from sqlcipher-embedded with any backend that passes `walseal.sh crypto` — the recovery tool, under the database's key (a passphrase, or a raw key with or without the salt), writes a new file that opens in this fork's native build with no WAL beside it, `integrity_check` = `ok`, holding every commit the WAL held. It never writes to its input files. Under a wrong key it exits non-zero and writes nothing. |
| **SHR-1** | *(since v1)* After `shred()`, or `close()` following `shredOnClose()`, reopening that filename gives an empty database, and the stored bytes were overwritten with random data before deletion. |

## Inputs: absent, late, wrong, lying

| Input | Absent | Late | Wrong | Lying |
| ----- | ------ | ---- | ----- | ----- |
| key | unencrypted database, by decision (ENC-2) | a key pragma (`key`, `rekey`, …) through `exec`: the WAL follows the new key (DESIGN *Decisions*); refused with `not authorized` inside a transaction, or on a connection whose key could not read its database at `open` (either would lose committed data); `textkey` and `textrekey` always refused (they crash SQLCipher 4.16.0) | ENC-1: first read fails `file is not a database`. oo1 shim: two of `key`/`textkey`/`hexkey`, or a raw key that is not 32 bytes (48 with salt), throws before opening — SQLCipher itself would take a malformed raw key as a passphrase, silently | n/a (a guessed key is ENC-1's job, and the KDF's) |
| imported bytes | n/a | n/a | not a SQLCipher file, or another key: next read fails (assumed; untested) | a crafted file: SQLite's own defenses; nothing added here |
| browser storage | no OPFS: IndexedDB backend. No IndexedDB either: behavior unstated | no timeouts; a stalled storage call stalls the worker | quota exceeded on flush: `exec` rejects *after* the statement applied in worker memory, so memory and storage differ (from the code; untested) | storage the browser evicts, or another tab writes: unstated |
| bind values | `null`/`undefined` bind as NULL | n/a | a `Uint8Array` is a BLOB (API-2, OO1-1); a safe-integer `number` or a 64-bit `BigInt` is an INTEGER, a `BigInt` beyond 64 bits throws (INT-1); any other object binds as its text. oo1 shim: a parameter name the statement does not have throws | n/a, caller is trusted code |
| the worker | a worker script that fails to load, or throws while loading: `open` rejects and terminates the worker (tested) | no timeout on any message | a failed `init` or `open`: `open` rejects and terminates the worker (tested) | n/a |

## Operating envelope

- **Scale:** no stated limit. The IndexedDB backend holds the whole file in worker memory
  (`ALLOW_MEMORY_GROWTH`, starting at 16 MB), so a database is bounded by what one worker can
  allocate. Largest tested database: the 4.0 fixture, 956 KB.
- **Speed:** no requirement stated.
- **Resources:** `sqlcipher.wasm` about 1.4 MB (README). No other budget stated.
- **Interruption:** a tab can close at any moment; DUR-2 is the promise, tested by killing the
  worker before each write of a transaction on both backends. Power loss is not tested (DESIGN
  *Known hazards*: a torn page 1).
- **Recovery:** none automatic. `export()` is the backup; `import()` restores one.
- **Availability:** not applicable — a library.

## Security, law, and ethics

- **Protects:** the database file at rest, from anyone who can read the storage (disk, OPFS,
  IndexedDB, an exported blob) but does not have the key. Strength is SQLCipher 4's defaults
  (AES-256, PBKDF2-HMAC-SHA512) and the passphrase the integrator chooses.
- **Does not protect** *(inferred)*: against code running in the same origin (XSS, extensions); the
  key passes through JavaScript and the worker keeps it as a string, which cannot be wiped. Not
  against someone with the running device's memory. Not the key itself: storing and recovering it
  is the integrator's job.
- **Shred is best-effort** *(inferred)*: the browser's storage engine and the disk may keep older
  copies the overwrite never reaches. It removes the live copy; it does not erase the medium.
- **Data about people:** the library stores none of its own. What an app stores is the app's.
- **License:** BSD, same as upstream.

## Assumptions (K4s), read back to the client

- "The pain is OpenSSL 3 lock-in plus no WASM build, and the users are integrators targeting small
  devices and browsers. True?" — _answer, date_
- "A missing key creating an unencrypted database is acceptable, or at least known." — yes, for
  compatibility; a hard call (2026-10-20). Now ENC-2.
- "BLOBs are out of scope for the unified API." — no, they seem important (2026-10-20). Now API-2.
- "The oo1 shim is still supported, though the unified API is recommended." — yes: its BLOBs
  (OO1-1) and then "complete the oo1 shim" (OO1-2), 2026-10-20.
- "In the oo1 shim, a string `key` stays a passphrase, as the shim has always applied it and as the
  unified API does — not, as the reference used to say, UTF-8 bytes applied as a raw key, which
  would make a database written by one API unreadable by the other." — _inferred, 2026-10-20; …_
- "qjson is the main downstream of `build-sqlcipher-libressl.sh`; are there others?" — the
  downstreams are the maintainer's own projects, all in development (2026-10-20).
- "So until an outside project depends on this, the API, the worker protocol and the IndexedDB
  layout can still change without expand-then-contract (DESIGN *One-way doors*). True?" —
  yes, cheap to change for now (2026-10-20).
- "These patches are not meant to go upstream." — _…_
- "The README's minimum browsers (Chrome 108+, Safari 16.4+, Firefox 111+) hold." — untested; CI
  runs only current engines, and no real Safari (maintainer, 2026-10-20: an untested claim).
- "Refusing a key pragma inside a transaction, or after an `open` whose key could not read the
  database, is acceptable behavior for the published API, since allowing it loses committed data."
  — yes (maintainer, 2026-10-20)

## Scope decisions

- **Not tested: stock SQLCipher opening files written here** — decided by the maintainer,
  2026-10-20. Reading upstream's files already shows our HMAC, KDF and cipher output match upstream's,
  and writing uses the same code; a second OpenSSL 3 build of upstream in CI would add little.
  Revisit if the patches start touching the write path, or if a user reports a file upstream can't
  open. Tested instead: the native build (upstream's code plus the patches) opens every file the
  WASM build exports, in CI.
- **Crash safety only with default cipher settings** (DUR-2) — decided by the maintainer,
  2026-10-20. A database that needs `PRAGMA cipher_compatibility` or other non-default settings
  keeps its rollback journal in memory, so a crash can corrupt it. Rejected: an `open` option for
  cipher settings, so the worker could read such a database and switch it to the WAL. Revisit if
  a user needs old-format databases in the browser.
- **Two patched files under `src/`** (UP-2): everything else is upstream, so each merge stays cheap.
- **No Intel macOS** (PLAT-1) — decided by the maintainer, 2026-10-20. The x64 artifact was an
  arm64 build all along (FINDINGS 2026-10-20); dropped rather than moved to an Intel runner.
- **No key means no encryption** (ENC-2) — decided by the maintainer, 2026-10-20, for compatibility
  with SQLCipher; a hard call. Rejected: rejecting `open()` without a key.
- **LibreSSL only** (LIB-1). The HMAC rewrite also compiles against OpenSSL, but that is not
  tested or promised.

## Non-goals

- Changing SQLCipher's file format or cryptographic defaults.
- A server, or sync between devices. `export()` produces a blob; moving it is the integrator's job.
- Key management: deriving, storing, or recovering keys *(inferred)*.
- `libssl`, TLS, or any network code.

## Not yet answered

- WASM-1: do the README's minimum browser versions hold, and does Apple's Safari behave as
  Playwright's WebKit does? — whoever tests an old browser, or Safari
- WASM-1: does the WASM build need COOP/COEP headers? The test server sends them; the README says no
  SharedArrayBuffer is needed. — whoever tests it without them
