# Requirements — v2

What must be observably true of sqlcipher-libressl, and what is not being built. A change is a new
version (`REQUIREMENTS_v3.md`), never an edit to this one. See the guide's
[SE1/R1–R11](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/04-requirements.md).

Client (the one voice who can decide): Warren MacEvoy (maintainer) · Proxy for the users
([SE1/H5](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/09-people.md#h5)):
the maintainer, for the downstream projects (all the maintainer's own, all in development) · Date sent:
2026-10-20 · Supersedes [v1](REQUIREMENTS_v1.md) · *Tell us what is wrong with it.*

v1 was written after the fact, from the code, the tests, the README, and the commit history at
`bef26b74`. Every statement that came from inference rather than from the client is in
**Assumptions**, or marked *(inferred)*, until read back
([SE1/W12](https://github.com/wmacevoy/software-engineering/blob/v1.1/guide/08-work.md#w12)).
Requirements are cited with the version that last changed them: `REQ1/ENC-1`, `REQ2/PLAT-1`.

## Changes from v1

The maintainer's answers to v1, 2026-10-20.

- **PLAT-1 changed:** no Intel macOS. Four platforms, not five.
- **ENC-2 added:** a missing key gives an unencrypted database, as in SQLCipher — kept for
  compatibility. The maintainer called it a hard call.
- **API-2 added:** BLOBs through the unified API.
- **Proxy for the users named:** the maintainer, speaking for the downstream projects, which are
  today all the maintainer's own and all in development.

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
| export / import | the whole encrypted file as bytes, out of / into a database |
| shred | overwrite the stored file with random bytes, then delete it |
| oo1 shim | `sqlcipher-oo1.js`: the sqlite3 oo1 API on the main thread, persisted by explicit `save()` of the whole file to IndexedDB |
| fixture | `sqlcipher-resources/sqlcipher-*-testkey.db`: databases written by official SQLCipher releases, key `testkey` |
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
| **WASM-1** | *(since v1)* The WASM build runs in a browser Web Worker with no server-side code: open, write, read, close. Browsers: Chrome 108+, Safari 16.4+, Firefox 111+ (the README's claim). |
| **API-1** | *(since v1)* The unified API behaves as the README's method table says: `open`, `exec`, `select`, `flush`, `export`, `import`, `shred`, `shredOnClose`, `close`, `mode`. Published since v1.0.0. |
| **API-2** | *(since v2)* Through the unified API, a `Uint8Array` bound to a statement is stored as a BLOB, and a BLOB column reads back as a `Uint8Array` with the same bytes, on either backend. |
| **ERR-1** | *(since v1)* Every SQL statement that fails rejects its promise with SQLite's error message, and reports nothing as changed *(inferred)*. |
| **DUR-1** | *(since v1)* Once `exec` resolves, its changes survive closing and reopening the database, on either backend. |
| **DUR-2** | *(since v1)* If the tab or worker dies at any point, the next `open` finds the database at its last committed state, and `integrity_check` returns `ok` (the README's "Tab crash: Data safe"). |
| **EXP-1** | *(since v1)* `export()` returns the encrypted file; `import(bytes)` into a database opened under the same key replaces its contents with exactly that file's. |
| **SHR-1** | *(since v1)* After `shred()`, or `close()` following `shredOnClose()`, reopening that filename gives an empty database, and the stored bytes were overwritten with random data before deletion. |

## Inputs: absent, late, wrong, lying

| Input | Absent | Late | Wrong | Lying |
| ----- | ------ | ---- | ----- | ----- |
| key | unencrypted database, by decision (ENC-2) | n/a | ENC-1: first read fails `file is not a database` | n/a (a guessed key is ENC-1's job, and the KDF's) |
| imported bytes | n/a | n/a | not a SQLCipher file, or another key: next read fails (assumed; untested) | a crafted file: SQLite's own defenses; nothing added here |
| browser storage | no OPFS: IndexedDB backend. No IndexedDB either: behavior unstated | no timeouts; a stalled storage call stalls the worker | quota exceeded on flush: `exec` rejects *after* the statement applied in worker memory, so memory and storage differ (from the code; untested) | storage the browser evicts, or another tab writes: unstated |
| bind values | `null`/`undefined` bind as NULL | n/a | a `Uint8Array` must be a BLOB (API-2); today it binds as its text and BLOB columns read back as `null` | n/a, caller is trusted code |
| the worker | `open` rejects if the worker fails to load (assumed) | no timeout on any message | n/a | n/a |

## Operating envelope

- **Scale:** no stated limit. The IndexedDB backend holds the whole file in worker memory
  (`ALLOW_MEMORY_GROWTH`, starting at 16 MB), so a database is bounded by what one worker can
  allocate. Largest tested database: the 4.0 fixture, 956 KB.
- **Speed:** no requirement stated.
- **Resources:** `sqlcipher.wasm` about 1.4 MB (README). No other budget stated.
- **Interruption:** a tab can close at any moment; DUR-2 is the promise, and it is untested.
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
- "The oo1 shim is still supported, though the unified API is recommended." — _…_
- "qjson is the main downstream of `build-sqlcipher-libressl.sh`; are there others?" — the
  downstreams are the maintainer's own projects, all in development (2026-10-20).
- "So until an outside project depends on this, the API, the worker protocol and the IndexedDB
  layout can still change without expand-then-contract (DESIGN *One-way doors*). True?" —
  yes, cheap to change for now (2026-10-20).
- "These patches are not meant to go upstream." — _…_

## Scope decisions

- **Not tested: stock SQLCipher opening files written here** — decided by the maintainer,
  2026-10-20. Reading upstream's files already shows our HMAC, KDF and cipher output match upstream's,
  and writing uses the same code; a second OpenSSL 3 build of upstream in CI would add little.
  Revisit if the patches start touching the write path, or if a user reports a file upstream can't
  open.
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

- WASM-1: does the WASM build need COOP/COEP headers? The test server sends them; the README says no
  SharedArrayBuffer is needed. — whoever tests it without them
- DUR-2: is "Tab crash: Data safe" a promise to keep, given the journal lives in memory (FINDINGS
  2026-10-20)? — client, after the crash test (QUEUE #1)
