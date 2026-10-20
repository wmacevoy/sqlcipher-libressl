# Findings

What was learned the expensive way: surprises, dead ends, measurements. Blameless: say what would
have found it, not who should have asked. Newest first. Entries before 2026-10-19 are reconstructed
from commit messages.

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
- **Changed:** Intel macOS dropped by the maintainer (REQ2/PLAT-1); the build is gone from CI. A
  check that each artifact's architecture matches its name is still QUEUE #3.

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
