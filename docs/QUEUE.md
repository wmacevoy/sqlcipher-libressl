# Queue

What is being worked, in what order, and what would make it done. Nothing enters without a *done*
sentence that can be checked from outside. An item that contradicts the requirements is a question
for the maintainer, not a task. Only a human sets *done*.

Ordered by cost of being wrong once: the things that can silently lose or expose a user's data
first.

| # | Item | Requirement IDs (or *chore*) | Owner | Done when | Estimate | Status | Since |
| - | ---- | ---------------------------- | ----- | --------- | -------- | ------ | ----- |
| 1 | Crash test: kill the worker after write *N* of a commit, for every *N*, on both backends; reopen | REQ1/DUR-2 | | a WASM test terminates the worker at each write of a multi-page transaction, reopens, and requires `integrity_check` = `ok` and the rows equal to before or after the transaction. Expected red on OPFS today (DESIGN *Known hazards*); then the maintainer decides: journal on OPFS, or narrow DUR-2 | time box: 2 days, then decide | ready | 2026-10-20 |
| 2 | `exec` with bind values rejects on a failed step | REQ1/ERR-1 | | a WASM test inserts a duplicate primary key with bind values and expects a rejection carrying SQLite's message; seen red first | 0.5 day | ready | 2026-10-20 |
| 3 | Each artifact is the architecture its name says | REQ2/PLAT-1 | | CI fails unless `file` (or `lipo -info`) reports the architecture in each `.a`'s name; macos-x64 is gone from the matrix (maintainer, 2026-10-20) | 0.5 day | ready | 2026-10-20 |
| 4 | Run the WASM tests on the IndexedDB backend too | REQ1/DUR-1, REQ1/API-1 | | a test-only way to force the backend; CI runs `test.html` once per backend and both report their `mode` | 1 day | ready | 2026-10-20 |
| 5 | Run the WASM tests in Firefox and WebKit | REQ1/WASM-1 | | Playwright runs `test.html` in chromium, firefox and webkit; a failure in any one fails CI | 0.5–1 day | ready | 2026-10-20 |
| 6 | One check command, run locally and by CI | chore | | `./check.sh` builds LibreSSL (pinned), the native library, the smoke test and the TCL suite, and exits non-zero on any failure; CI's native job calls it | 1 day | ready | 2026-10-20 |
| 7 | Pin the inputs | chore | | the LibreSSL tarball is checked against a committed SHA-256; `tests/wasm/package.json` and its lock are committed and CI uses `npm ci`; container and runner tags are pinned or written down as floating on purpose | 0.5 day | ready | 2026-10-20 |
| 8 | Enforce the two-file diff | REQ1/UP-2 | | a CI step fails if `git diff <upstream tag> -- src/` names any file but `crypto_openssl.c` and `sqlcipher.c`; seen red with a planted change | 0.5 day | ready | 2026-10-20 |
| 9 | Shred overwrite actually overwrites | REQ1/SHR-1 | | a WASM test reads the stored bytes between overwrite and delete (or through a test hook) and finds them not equal to the encrypted file | 0.5 day | ready | 2026-10-20 |
| 10 | Stale version references | chore | | README says v4.16.0 / SQLite 3.53.1 and downloads `v1.0.0`; `grep -rn '4\.14\|3\.51' README.md docs/` finds nothing | 0.25 day | ready | 2026-10-20 |
| 11 | Retire `future-plan.md` | chore | | its remaining idea (a Pages demo) is in DESIGN *Deferred*; the file is deleted | 0.25 day | ready | 2026-10-20 |
| 12 | BLOBs through the unified API and the oo1 shim | REQ2/API-2, REQ3/OO1-1 | Claude | a WASM test binds a `Uint8Array` with every byte value 0–255 and an empty one, and reads both back as `Uint8Array`s with the same bytes and `typeof` = `blob` — through the unified API (on both backends once #4 lands) and through every oo1 form REQ3/OO1-1 names; seen red first (today they come back `null`) | 1 day | in progress: `feat/blobs` | 2026-10-20 |
| 14 | The oo1 reference's ten missing methods | none yet | | the maintainer has chosen per method: implement (with a test each) or remove from `docs/oo1-api.md`; then a check that every documented method exists | — | blocked: maintainer's answer | 2026-10-20 |
| 13 | No key means an unencrypted database | REQ2/ENC-2 | | a WASM test opens without `key`, writes, and `export()` starts with `SQLite format 3\0`; a native step does the same without `sqlite3_key` | 0.5 day | ready | 2026-10-20 |
