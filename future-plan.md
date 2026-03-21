# Future Plan: GitHub Pages Demo

## Goal

A working "Secure Notes" web page on GitHub Pages demonstrating
lightweight encrypted browser storage with SQLCipher WASM + LibreSSL.

## What needs to happen

### 1. GitHub Actions CI (`.github/workflows/pages.yml`)

Build pipeline on push to master:

1. **Install Emscripten SDK** (e.g. 3.1.68 via `mymindstorm/setup-emsdk`)
2. **Build LibreSSL for WASM** — download source from openbsd.org,
   cross-compile with `emcmake cmake` / `emmake make` to produce
   `libcrypto.a`.  Cache the result keyed on LibreSSL + EMSDK version.
3. **Build SQLCipher amalgamation** — `./configure --with-tempstore=yes`
   then `make sqlite3.c` (native build, produces amalgamation).
4. **Compile to WASM with emcc** — compile `sqlite3.c` against
   LibreSSL's `libcrypto.a`, producing `sqlcipher.js` + `sqlcipher.wasm`.
   Key flags:
   - `-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL` (upstream
     SQLCipher's name for the OpenSSL-compatible API — LibreSSL provides it)
   - `-sFILESYSTEM=1` (LibreSSL's libcrypto.a references BIO socket
     symbols at link time; dead code but linker needs stubs, ~50KB overhead)
   - `-sMODULARIZE=1 -sEXPORT_NAME=initSqlCipher`
   - `-sALLOW_MEMORY_GROWTH=1`
   - Exported functions: `sqlite3_open`, `sqlite3_close_v2`, `sqlite3_exec`,
     `sqlite3_prepare_v2`, `sqlite3_step`, `sqlite3_finalize`, `sqlite3_reset`,
     `sqlite3_column_*`, `sqlite3_bind_*`, `sqlite3_errmsg`,
     `sqlite3_changes`, `sqlite3_last_insert_rowid`, `malloc`, `free`
   - Exported runtime methods: `cwrap`, `UTF8ToString`, `stringToUTF8`,
     `stackSave`, `stackRestore`, `stackAlloc`, `setValue`, `getValue`, `HEAPU8`
5. **Deploy** — upload `docs/` as GitHub Pages artifact.

### 2. JavaScript driver (`docs/sqlcipher-driver.js`)

Thin wrapper around the Emscripten module using `cwrap`:

- `SQLCipherDriver.init(wasmJsPath)` — loads the module
- `SQLCipherDriver.open(filename)` — returns a `Database`
- `Database.exec(sql, params?)` — execute DDL/DML
- `Database.query(sql, params?)` — returns `[{col: val, ...}, ...]`
- `Database.scalar(sql, params?)` — single value
- `Database.prepare(sql)` — returns `Statement`
- `Statement.bind(params)`, `.step()`, `.getRow()`, `.finalize()`
- `Database.close()` — closes DB, SQLCipher zeros keys

### 3. Demo page (`docs/index.html` + `docs/secure-notes.js`)

"Secure Notes" app:

- Passphrase entry form
- `PRAGMA key = '<passphrase>'` to encrypt the in-memory database
- Create / read / delete notes (stored in WASM linear memory, encrypted)
- Export notes as decrypted SQL
- Lock button (closes DB, zeros keys)
- Status bar showing SQLite version and encryption state
- Collapsible log panel showing SQLCipher operations

### 4. Cleanup

- Remove `wyatt_wasm.c` reference from README (cruft from migration)
- Add `.nojekyll` to `docs/` for GitHub Pages

## Key constraints

- **LibreSSL only** — no OpenSSL. `-DSQLCIPHER_CRYPTO_OPENSSL` is
  SQLCipher's flag name for the OpenSSL-compatible API that LibreSSL provides.
- **Minimal diff** — CI and docs are new files; do not modify upstream sources
  beyond the existing two patches.
- **WASM-safe** — `atexit()` cleanup, no `.fini_array`, no `fork()`.
