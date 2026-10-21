# Findings

What was learned the expensive way: surprises, dead ends, measurements. Blameless: say what would
have found it, not who should have asked. Newest first. Entries before 2026-10-19 are reconstructed
from commit messages.

## 2026-10-20 — Following the key for import: what SQLCipher's key pragmas do

- **What happened:** writing the record of the current key (QUEUE #21) found three things. In
  SQLCipher 4.16.0, `PRAGMA textrekey` (and `textkey` before the first read) crashes natively
  with a bus error, because `pragma.c` passes length -1 into `sqlcipher_cipher_ctx_set_pass`; a
  `textkey` after a read does nothing. A deferred `BEGIN` alone takes no lock, so the authorizer's
  checkpoint succeeds and a rekey right after it is not refused; only a transaction that has read
  or written refuses it. And the derived key and its keyspec carry the file's salt, so neither can
  key a file another database wrote under the same passphrase (from reading the code).
- **Kind:** K3 (upstream code nobody here had run); K2 (the `BEGIN`, which the first native test
  assumed otherwise).
- **Cost:** none reported; found before release.
- **Findable by:** the native codec check, run with each key pragma form.
- **Changed:** the worker keeps the last allowed `key`/`hexkey`/`rekey`/`hexrekey` pragma's argument
  for import, and leaves the text forms out (PR #24). The text forms' crash is QUEUE #23, upstream
  code, so any fix goes in this repository's own layer.

## 2026-10-20 — Changing the key under the WAL: key pragmas act at prepare

- **What happened:** two things, found while keeping walseal on the key across `PRAGMA rekey`
  (QUEUE #17). SQLCipher's key pragmas act while the statement is prepared, not when it steps, so a
  hook after the statement is too late: rekey's own commit was still sealed under the old key, and
  a worker killed before the next checkpoint left a database neither key opens (18 of 28 kill
  points on OPFS, run 39483632951). And after a wrong-key `open`, SQLite's in-heap WAL index
  records an empty WAL; a later `PRAGMA key` with the right key reads stale pages, and the next
  commit overwrites the WAL (reproduced natively: 50 committed rows lost).
- **Kind:** K3 — SQLCipher's `pragma.c` and `sqlite3_rekey_v2` say so; nothing here did.
- **Cost:** none reported; caught by the rekey crash test before release.
- **Findable by:** the rekey crash test; the native codec check.
- **Changed:** an authorizer checkpoints before any key pragma and refuses one inside a transaction
  or after a failed read (DESIGN *Decisions*, PR #21).

## 2026-10-20 — Building the WAL on storage: what walseal turned up

- **What happened:** under walseal a wrong key deciphers the WAL to noise; SQLite took it for
  empty, and a normal close checkpointed nothing and deleted it, losing every uncheckpointed
  commit. Once `open` does the first read itself, SQLCipher answers every later read on that
  connection with "out of memory", so the caller's first read no longer said `file is not a
  database` (REQ1/ENC-1). Both found writing the native codec test.
- **Kind:** K3, found before release.
- **Cost:** none reported.
- **Findable by:** the native codec test's wrong-key checks.
- **Changed:** a wrong-key `open` sets `SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE`, and the worker keys again
  after `SQLITE_NOTADB` (PR #20).

## 2026-10-20 — Shred skipped every file over 64 KB

- **What happened:** `crypto.getRandomValues` throws above 65536 bytes. Shred caught the throw, so
  a database larger than 64 KB was neither overwritten nor removed, silently (REQ1/SHR-1). Found
  when the `-wal` shred test hit it (run 39476552244); the overwrite test (PR #10) used a 32 KB
  file.
- **Kind:** K3 — a caught exception hid it.
- **Cost:** unknown; any database over 64 KB that was shredded before the fix stayed in place.
- **Findable by:** a shred test on a file over 64 KB.
- **Changed:** random bytes filled in 64 KB pieces (PR #20).

## 2026-10-20 — The README's "Tab crash: Data safe" was false on both backends

- **What happened:** killing the worker before each VFS write (QUEUE #1) gave: on OPFS, a kill
  during a commit's main-file writes left "database disk image is malformed", and a kill after a
  cache spill reopened with `integrity_check` ok and the uncommitted rows visible — silent. On
  IndexedDB a single-exec transaction was always safe; one spanning execs leaked uncommitted rows
  once a spilled page had been flushed. All three browsers, run 39473646953.
- **Kind:** K3 — the README promised it (REQ1/DUR-2); no test had ever killed a worker.
- **Cost:** none reported; possible silent corruption in users' browsers since v1.0.0.
- **Findable by:** the crash tests (`crash-worker.js`).
- **Changed:** a WAL on storage, its headers enciphered by walseal (maintainer, 2026-10-20; PR
  #20). The crash tests are green on all six runs.

## 2026-10-20 — Restarting a worker: held handles and leaked workers

- **What happened:** in Chromium an OPFS access handle stays held for about 2 s after its worker is
  terminated: reopening failed with "Access Handles cannot be created if there is another open
  Access Handle" for about 18 tries 100 ms apart. Firefox and WebKit release it at once. And a
  failed `init` or `open` left its worker running, with no handle returned to end it.
- **Kind:** K2 (Chromium), K3 (the leak).
- **Cost:** any app that restarts a crashed worker on OPFS hits the first; the second leaks a
  worker and its handles per failed open.
- **Findable by:** the crash tests' reopen; the fail-worker ping test (run 39484171208).
- **Changed:** the crash test retries; a failed open terminates its worker (PR #21).

## 2026-10-20 — The browser runner's timeout was never 60 s

- **What happened:** `run-tests.mjs` passed `{timeout: 60000}` as `waitForFunction`'s second
  argument, which is the function's argument, not its options, so Playwright's 30 s default
  applied.
- **Kind:** K3 — a limit nobody saw take effect.
- **Findable by:** a test that runs longer than 30 s (the crash tests).
- **Changed:** the options are the third argument, at 600 s (PR #19). A hang now costs up to 10
  minutes per run instead of 30 s.

## 2026-10-20 — Three browsers, two backends: what the first runs showed

- **What happened:** CI had only ever run the browser tests in Chromium on OPFS; the IndexedDB
  backend never ran. Forced, all 27 tests passed on it. In Playwright's WebKit an ephemeral context
  has no OPFS (`getDirectory()` rejects with UnknownError, as in Safari private browsing), so the
  worker fell back to IndexedDB without saying so and the forced-OPFS run reported `indexeddb`. The
  engines are current builds (Chromium 156, Firefox 157, WebKit 27.2), so the README's minimum
  versions are untested, as is Apple's Safari. One local run (macOS, Firefox 157, OPFS) hung after
  REQ5/INT-1 and passed on rerun; cause not found.
- **Kind:** K3 (coverage gap, no defect behind it), K2 (WebKit).
- **Findable by:** asserting the reported `mode` in each run.
- **Changed:** six runs, a fresh persistent profile each (PRs #12, #15). In Safari private windows
  the API uses IndexedDB, the documented fallback; `db.mode` says which. REQ7/WASM-1 names what
  is tested.

## 2026-10-20 — CI inputs that moved, and checks that could not fail

- **What happened:** `macos-latest` had moved to `macos-26-arm64` with no commit here (run
  39469920483). LibreSSL was fetched with `curl -sL | tar xz`: without `-f` an HTTP error page went
  into `tar`, and nothing checked the bytes of a good download. GitHub's default `run` shell drops a
  failure inside `$(... | grep ... || true)`. Fail-fast cancelled the macOS job once the Linux arm64
  job failed, so a planted failure showed on one platform.
- **Kind:** K2.
- **Cost:** none seen; noticed while pinning (PR #17) and enforcing the diff (PR #14). One extra CI
  attempt.
- **Findable by:** the "Image:" line of a job's set-up log; reading the steps.
- **Changed:** runners named by OS release; the tarball downloaded with `-f` and checked by
  SHA-256; the diff captured before filtering; `fail-fast: false`.

## 2026-10-20 — `check.sh` on macOS, and the suite's error count

- **What happened:** `DYLD_LIBRARY_PATH` never reaches `testfixture` through a `#!/bin/sh` script:
  SIP strips `DYLD_*` when a protected binary starts, so the old hint worked only from the user's
  own shell (dyld "Library not loaded: @rpath/libtcl8.6.dylib", exit 134). Without
  `-DSQLCIPHER_TEST` the suite reported 10 errors on linux-glibc-x64 (run 39473124410), where the
  2026-10-19 entry recorded 12: red, but not a stable signature. `make distclean` leaves `*.dSYM`
  directories and `test/testdir` behind on macOS.
- **Kind:** K2.
- **Cost:** one local run each.
- **Changed:** `check.sh` takes `TCL_LIBDIR` and sets `DYLD_LIBRARY_PATH` itself (PR #18). The
  leftovers are removed by hand.

## 2026-10-20 — The v1.0.0 release is older than the README said

- **What happened:** v1.0.0 was built from SQLCipher 4.14.0 / SQLite 3.51.3 (`git show
  v1.0.0:VERSION`). The README says 4.16.0 and tells readers to download v1.0.0, which has none of
  the BLOB, 64-bit integer or oo1 work.
- **Kind:** K3.
- **Cost:** a reader gets an older release than the version the README states.
- **Findable by:** `git show <tag>:VERSION`.
- **Changed:** v2.0.0 is released from this baseline, and the downloads name it.

## 2026-10-20 — Completing the oo1 shim: what the missing methods were hiding

- **What happened:** writing a test per behavior in `docs/oo1-api.md` (REQ4/OO1-2) found more than
  the ten missing methods. `step()` reported a failed statement as "no more rows", so
  `exec({sql, bind})` and `stepReset()` dropped errors silently (the oo1 side of REQ1/ERR-1).
  `exec()` with any option ran only the first statement. A statement left open at `close()` kept
  its pointer, and a later `finalize()` freed it twice. The statement list grew by one entry per
  `prepare()` for the life of the DB. `bind({$name: v})` threw "not yet supported". A byte-array
  `key` was applied as the text `"1,2,3..."`. And `pCtx`, which upstream tells aggregate callbacks
  to key state by, is a different pointer in `xFinal` than in `xStep`.
- **Kind:** K3 — the reference described upstream's oo1, and no test read it.
- **Cost:** none reported; the shim's only user is the maintainer's own projects.
- **Findable by:** the check that every documented method exists, now a test; a test per
  documented behavior.
- **Changed:** all fixed in PR #5. Still open: `get()` and `getInt()` read integers as 32-bit
  (`sqlite3_column_int`), so a larger integer comes back wrong — QUEUE #15.

## 2026-10-20 — The oo1 reference documents methods the shim does not have

- **What happened:** `docs/oo1-api.md` describes the official sqlite3 oo1 API, including BLOBs
  ("Blobs are returned as `Uint8Array` instances", `getBlob`, `bindAsBlob`). The shim reads every
  BLOB column as `null`, binds a `Uint8Array` as its text, and has neither method — nor ten
  others the reference lists (REQUIREMENTS_v3 *Not yet answered*). Found while planning REQ2/API-2.
- **Kind:** K3 — a reference written from the API being matched, not from the code.
- **Cost:** none reported; any caller following the reference gets `null` or a `TypeError`.
- **Findable by:** a check that every documented method exists (a loop over the headings, run
  once by hand on 2026-10-20); a test per documented behavior.
- **Changed:** REQ3/OO1-1 and its test cover the BLOB part; the other ten are a question for the
  maintainer (QUEUE #14).

## 2026-10-20 — The macos-x64 library is an arm64 build

- **What happened:** the matrix builds `libsqlcipher-macos-x64.a` on `macos-15`, which is an arm64
  runner: the log reads `Image: macos-15-arm64` and `Host System...aarch64-apple-darwin24.6.0` (run
  39367882243, job 118448772745). Every release since v1.0.0 has shipped two arm64 libraries for
  macOS, one of them named x64. The maintainer's private notes recorded this in March; the
  repository did not.
- **Kind:** K3 — the name said x64, and nothing checked.
- **Cost:** unknown; any Intel-Mac integrator gets a link failure.
- **Findable by:** asserting each artifact's architecture (`lipo -info`, `file`) in CI; writing the
  known problem where the next person reads.
- **Changed:** Intel macOS dropped by the maintainer (REQ2/PLAT-1); the build is gone from CI. CI
  checks that each artifact is the architecture its name says (the REQ2/PLAT-1 step, seen red in
  run 39469916280).

## 2026-10-20 — Reading the worker: the crash-safety and error claims are unproven

- **What happened:** while rebuilding the design, reading (not running) the worker showed that the
  journal lives in memory on both backends, `xAccess` always says "no file", and `exec` with bind
  values never reports a failed step. See DESIGN_v1 *Known hazards*.
- **Kind:** K3, suspected — the README says "Tab crash: Data safe"; no test has ever killed a tab.
- **Cost:** not yet known. Possible silent corruption (REQ1/DUR-2) and silent write failures
  (REQ1/ERR-1) in users' browsers.
- **Findable by:** a test that kills the worker mid-commit (SE1/T2); a test that expects a
  constraint violation to reject.
- **Changed:** QUEUE #1, #2: tests first, to confirm or refute.

## 2026-10-19 — The TCL suite never gated CI

- **What happened:** the step ran `testfixture sqlcipher.test | tail -30`. The debian container's
  step shell is `sh -e` (dash), with no `pipefail`, so the step's status was `tail`'s. Behind it, 12
  of 208 tests failed: CI built the testfixture without upstream's test recipe (`-DSQLCIPHER_TEST`,
  `--enable-fts5`). The README and CLAUDE.md gave the same failing command.
- **Kind:** K3 — a green check that could not go red.
- **Cost:** since 2026-03-20, the fork's only full test of the patches proved nothing.
- **Findable by:** watching the suite fail once (SE1/T3); reading upstream's test instructions.
- **Changed:** PR #1 — `shell: bash` (pipefail), a separate test configure after the release upload,
  docs fixed. Seen red locally (12 errors, exit 1) and green in CI (0 of 208).

## 2026-03-21 — OPFS mode could not write at all

- **What happened:** "unable to open database file" on every write in OPFS mode: SQLite opens a
  journal, and the VFS only had handles for pre-opened main files (`d68bba71`). The OPFS backend had
  shipped in `106be6d4` the same day.
- **Kind:** K3 — "every COMMIT is durable" was written before a commit had succeeded.
- **Cost:** one release with a backend that only read.
- **Findable by:** any test that writes; the first Playwright tests arrived in `e12e2185`.
- **Changed:** journal, WAL and SHM handles are created in memory on demand, for both backends —
  which is the root of the crash-safety question above.

## 2026-03-21 — Shred overwrote nothing

- **What happened:** `shred()` closed the database first, and closing nulls the handles, so there
  was nothing left to overwrite (`e12e2185`).
- **Kind:** K3
- **Cost:** shred shipped in `3033477f` and was a delete until the first browser tests ran.
- **Findable by:** the "reopen gives an empty database" test, written the same day.
- **Changed:** handle type and size are captured before close; OPFS is reopened to overwrite. The
  overwrite itself is still untested (REQ1/SHR-1).

## 2026-03-20/21 — CI and release, by trial

- **What happened:** eight "Fix CI" and two "Fix release" commits. LibreSSL source: OpenBSD mirror
  unreliable, git repository lacks generated CMake files, `gh` needs a token — settled on `curl` of
  the GitHub release tarball. autosetup names its output `libsqlite3.a`, not `libsqlcipher.a`.
  `testfixture` needs `tcl-dev`. `SQLITE_EXTRA_INIT`/`SHUTDOWN` are required. Playwright must be
  installed locally for ES modules. A directory did not survive artifact upload/download.
- **Kind:** K2 — each a surprise found by CI.
- **Cost:** about a day of round trips through CI.
- **Findable by:** one script that runs locally the way CI does (SE1/O3), so each surprise costs a
  local run rather than a push.
- **Changed:** QUEUE #6 (`check.sh`).
