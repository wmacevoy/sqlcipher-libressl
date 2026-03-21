# SQLCipher WASM -- oo1 API Reference

This API matches the official sqlite3 WASM oo1 API
(<https://sqlite.org/wasm/doc/trunk/api-oo1.md>) with SQLCipher
encryption added via the `key` constructor option.

The oo1 layer wraps the low-level C API (`sqlite3_open_v2`,
`sqlite3_prepare_v2`, etc.) in two JavaScript classes -- `DB` and `Stmt`
-- exposed as `sqlite3.oo1.DB` and `sqlite3.oo1.Stmt` after the WASM
module is initialized.  All methods are synchronous and must run in the
same thread as the WASM instance.

---

## DB

### Constructor

```js
// In-memory database, default flags "c" (create + read/write)
var db = new sqlite3.oo1.DB();

// Named file (in whichever VFS is active)
var db = new sqlite3.oo1.DB("/mydata.db");

// Positional: filename, flags, vfs
var db = new sqlite3.oo1.DB("/mydata.db", "c", "opfs");

// Options object -- the preferred form for encrypted databases
var db = new sqlite3.oo1.DB({
  filename: ":memory:",
  flags:    "c",
  vfs:      null,
  key:      "correct-horse-battery-staple"   // SQLCipher encryption key
});
```

**Parameters (positional form):**

| # | Name | Default | Description |
|---|------|---------|-------------|
| 1 | `filename` | `":memory:"` | Database path. Special names: `":memory:"`, `""` (temp on-disk), `":localStorage:"`, `":sessionStorage:"` (kvvfs, main thread only). |
| 2 | `flags` | `"c"` | One or more of: `c` (create + read/write), `w` (read/write), `r` (read-only), `t` (trace SQL to console). |
| 3 | `vfs` | `null` | VFS name string, or falsy for the default VFS. |

**Options object properties** (all optional except `filename`):

| Property | Type | Description |
|----------|------|-------------|
| `filename` | string | Same as positional. |
| `flags` | string | Same as positional. Default `"c"`. |
| `vfs` | string \| null | Same as positional. |
| `key` | string \| ArrayBuffer \| Uint8Array | Encryption key. Strings are UTF-8-encoded then applied as a hex key. Byte arrays are applied as hex keys directly. |
| `textkey` | string \| ArrayBuffer \| Uint8Array | Encryption key applied verbatim as `PRAGMA textkey`. |
| `hexkey` | string \| ArrayBuffer \| Uint8Array | Encryption key applied as `PRAGMA hexkey`. If a byte array, converted to its hex representation. |

Only **one** of `key`, `textkey`, or `hexkey` may be provided.  An
exception is thrown if more than one is set.  The key pragma is executed
immediately after opening, before any VFS post-open callbacks run.

The read-only `pointer` property holds the underlying `sqlite3*` WASM
pointer.  It becomes `undefined` after `close()`.

---

### DB.prototype.exec(sql)
### DB.prototype.exec(sql, options)
### DB.prototype.exec(options)

Executes one or more SQL statements.  By default returns `this` (the DB
object), enabling chaining.

```js
// Simple string form
db.exec("CREATE TABLE t (a TEXT, b REAL)");

// With bindings (applied to the first bindable statement)
db.exec("INSERT INTO t VALUES (?, ?)", { bind: ["hello", 3.14] });

// Full options form
var rows = db.exec({
  sql:         "SELECT a, b FROM t WHERE b > ?",
  bind:        [1.0],
  callback:    function(row, stmt) { console.log(row); },
  rowMode:     "object",     // "array" (default), "object", "stmt", integer, "$colName"
  resultRows:  [],           // collect rows here
  columnNames: [],           // collect column names here
  returnValue: "resultRows"  // "this" (default), "resultRows", "saveSql"
});
```

**Options:**

| Property | Type | Description |
|----------|------|-------------|
| `sql` | string \| Uint8Array \| string[] | SQL to execute. Arrays are concatenated without separators. |
| `bind` | any | Value(s) for the first bindable statement. Accepts the same types as `Stmt.bind()`. |
| `callback` | function(row, stmt) | Called for each result row. Return `false` to stop iteration. |
| `rowMode` | string \| integer | Shape of the first callback argument: `"array"`, `"object"`, `"stmt"`, a 0-based column index, or `"$columnName"`. Default: `"array"`. |
| `resultRows` | array | If provided, each result row is pushed here. |
| `columnNames` | array | If provided, column names are stored here before the first row. |
| `saveSql` | array | If provided, the SQL text of each executed statement is appended. |
| `returnValue` | string | What `exec()` returns: `"this"` (default), `"resultRows"`, or `"saveSql"`. |

---

### DB.prototype.prepare(sql)

Compiles SQL into a prepared statement.  Returns a `Stmt` object.  This
is the only way to create `Stmt` instances.  Throws if the SQL is empty
or invalid.

```js
var stmt = db.prepare("SELECT a, b FROM t WHERE b > ?");
```

The `sql` argument may be a string, a `Uint8Array` of SQL bytes, a WASM
pointer to a NUL-terminated C string, or an array of strings
(concatenated without separators).

---

### Select shortcuts

These convenience methods prepare a statement internally, step it, and
finalize it.  The optional `bind` argument is passed to `Stmt.bind()`.

#### DB.prototype.selectValue(sql [, bind [, asType]])

Returns the value of the first column of the first result row, or
`undefined` if there are no results.  The optional `asType` is a
`SQLITE_*` type constant to coerce the result.

```js
var count = db.selectValue("SELECT count(*) FROM t");
```

#### DB.prototype.selectValues(sql [, bind [, asType]])

Returns an array of first-column values from all result rows.

```js
var names = db.selectValues("SELECT a FROM t ORDER BY a");
// ["hello", "world"]
```

#### DB.prototype.selectArray(sql [, bind])

Returns the first result row as an array, or `undefined` if empty.

```js
var row = db.selectArray("SELECT a, b FROM t LIMIT 1");
// ["hello", 3.14]
```

#### DB.prototype.selectArrays(sql [, bind])

Returns all result rows, each as an array.

```js
var rows = db.selectArrays("SELECT a, b FROM t");
// [["hello", 3.14], ["world", 2.72]]
```

#### DB.prototype.selectObject(sql [, bind])

Returns the first result row as a plain object (keys = column names), or
`undefined` if empty.

```js
var obj = db.selectObject("SELECT a, b FROM t LIMIT 1");
// {a: "hello", b: 3.14}
```

#### DB.prototype.selectObjects(sql [, bind])

Returns all result rows, each as a plain object.

```js
var objs = db.selectObjects("SELECT a, b FROM t");
// [{a: "hello", b: 3.14}, {a: "world", b: 2.72}]
```

---

### DB.prototype.transaction([qualifier,] callback)

Wraps `callback` in a `BEGIN` / `COMMIT` pair.  If the callback throws,
`ROLLBACK` is executed and the exception propagates.  Returns the
callback's return value on success.

The optional `qualifier` string is appended to `BEGIN`, e.g.
`"IMMEDIATE"` or `"EXCLUSIVE"`.

```js
db.transaction("IMMEDIATE", function(db) {
  db.exec("INSERT INTO t VALUES ('a', 1)");
  db.exec("INSERT INTO t VALUES ('b', 2)");
});
```

Transactions cannot be nested.  For nesting, use `savepoint()`.

---

### DB.prototype.savepoint(callback)

Like `transaction()` but uses `SAVEPOINT` / `RELEASE`, allowing
nesting.

```js
db.savepoint(function(db) {
  db.exec("INSERT INTO t VALUES ('outer', 1)");
  db.savepoint(function(db) {
    db.exec("INSERT INTO t VALUES ('inner', 2)");
  });
});
```

---

### DB.prototype.changes(total, sixtyFour)

Returns the number of rows changed by the most recent statement.

| Argument | Default | Effect |
|----------|---------|--------|
| `total` | `false` | If `true`, returns `sqlite3_total_changes()` instead. |
| `sixtyFour` | `false` | If `true`, uses the 64-bit variant (requires BigInt support in the build). |

---

### DB.prototype.createFunction(name, xFunc [, options])
### DB.prototype.createFunction(options)

Registers a user-defined function accessible from SQL.  Supports scalar,
aggregate, and window functions.  Returns `this`.

```js
db.createFunction("double_it", function(pCtx, val) {
  return val * 2;
});
db.selectValue("SELECT double_it(21)"); // 42
```

See the source for the full options object shape (`xFunc`, `xStep`,
`xFinal`, `xValue`, `xInverse`, `arity`, `deterministic`, `directOnly`,
`innocuous`).

---

### DB.prototype.isOpen()

Returns `true` if the database handle is open, `false` otherwise.

### DB.prototype.affirmOpen()

Throws if the database has been closed.  Returns `this`.

### DB.prototype.close()

Finalizes all open statements and closes the database.  A no-op if
already closed.  After calling `close()`, the `pointer` property
becomes `undefined`.

If `this.onclose.before` and/or `this.onclose.after` are functions, they
are called around the close operation (exceptions are silently ignored).

### DB.prototype.checkRc(resultCode)

Throws an `SQLite3Error` if `resultCode` is non-zero, using
`sqlite3_errmsg()` for the error message.  Returns `this` on success.

### DB.prototype.dbFilename(dbName)

Returns `sqlite3_db_filename()` for the given database name (default
`"main"`).

### DB.prototype.dbName(dbNumber)

Returns the name of the given 0-based attached database (default `0`).

### DB.prototype.dbVfsName(dbName)

Returns the VFS name string for the given attached database.

### DB.prototype.openStatementCount()

Returns the number of `Stmt` objects currently open on this DB (only
those created via `prepare()`).

---

## Stmt

`Stmt` objects are created exclusively via `DB.prepare()`.  Calling the
`Stmt` constructor directly throws.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `pointer` | number \| undefined | Read-only. The `sqlite3_stmt*` WASM pointer. `undefined` after `finalize()`. |
| `db` | DB | The parent DB that created this statement. |
| `columnCount` | number | Read-only. Number of result columns (`sqlite3_column_count()`). |
| `parameterCount` | number | Read-only. Number of bindable parameters (`sqlite3_bind_parameter_count()`). |

---

### Stmt.prototype.bind(value)
### Stmt.prototype.bind(index, value)
### Stmt.prototype.bind(array)
### Stmt.prototype.bind(object)

Binds one or more values to the statement's parameters.  Returns `this`.

**Single value (1 arg):** binds to parameter index 1.

**Two arguments:** `index` is a 1-based integer or a named parameter
string (e.g. `"$name"`, `":name"`, `"@name"`).

**Array:** each element is bound at its array-index + 1.

**Object:** each key is treated as a named parameter (must include the
prefix, e.g. `{$a: 1, $b: 2}`).

**Bindable types:** `null`, `number`, `string`, `boolean` (bound as
0/1), `BigInt` (if enabled), `Uint8Array` / `Int8Array` / `ArrayBuffer`
(bound as blob).  Passing `undefined` is a no-op.

---

### Stmt.prototype.step()

Steps the statement.  Returns `true` if a row of data is available (the
`get()` family becomes legal), `false` if the statement is done.  Throws
on error.

### Stmt.prototype.stepFinalize()

Calls `step()`, then `finalize()` (even if `step()` throws).  Returns
the boolean result of `step()`.

```js
db.prepare("INSERT INTO t VALUES (?, ?)").bind([val1, val2]).stepFinalize();
```

### Stmt.prototype.stepReset()

Calls `step()`, then `reset()`.  Returns `this`.  Useful for repeated
bind-step-reset loops.

```js
var stmt = db.prepare("INSERT INTO t VALUES (?, ?)");
for (var i = 0; i < 100; i++) {
  stmt.bind([i, "row " + i]).stepReset();
}
stmt.finalize();
```

---

### Stmt.prototype.get([])

Returns the current row as an array.

### Stmt.prototype.get({})

Returns the current row as a plain object (keys = column names).

### Stmt.prototype.get(columnIndex)

Returns the value at the given 0-based column index, with the type
auto-detected from `sqlite3_column_type()`.

### Stmt.prototype.get(columnIndex, asType)

Returns the value at the given column, coerced to the given
`SQLITE_*` type constant (`SQLITE_INTEGER`, `SQLITE_FLOAT`,
`SQLITE_TEXT`, `SQLITE_BLOB`).

Blobs are returned as `Uint8Array` instances.

---

### Stmt.prototype.getInt(columnIndex)

Equivalent to `get(columnIndex, SQLITE_INTEGER)`.

### Stmt.prototype.getFloat(columnIndex)

Equivalent to `get(columnIndex, SQLITE_FLOAT)`.

### Stmt.prototype.getString(columnIndex)

Equivalent to `get(columnIndex, SQLITE_TEXT)`.

### Stmt.prototype.getBlob(columnIndex)

Equivalent to `get(columnIndex, SQLITE_BLOB)`.  Returns a `Uint8Array`.

### Stmt.prototype.getJSON(columnIndex)

Fetches the column as a string and passes it through `JSON.parse()`.
Returns `null` if the column is NULL.

---

### Stmt.prototype.getColumnName(columnIndex)

Returns the result column name at the given 0-based index.

### Stmt.prototype.getColumnNames([target])

Returns an array of all result column names.  If passed an array, names
are appended to it and it is returned.

---

### Stmt.prototype.reset(alsoClearBindings)

Resets the statement for re-execution.  If `alsoClearBindings` is truthy,
also calls `clearBindings()`.  Returns `this`.  Throws on error
(including the `INSERT ... RETURNING` lock case).

### Stmt.prototype.clearBindings()

Clears all bound parameter values.  Returns `this`.

### Stmt.prototype.finalize()

Finalizes the statement, releasing all resources.  Returns the result of
`sqlite3_finalize()` (0 on success), or `undefined` if already
finalized.  After this call, most methods will throw.

---

### Stmt.prototype.bindAsBlob(value)
### Stmt.prototype.bindAsBlob(index, value)

Like `bind()` but forces the BLOB binding mechanism.  The value must be a
string, `null`/`undefined`, or a typed array.

### Stmt.prototype.getParamIndex(name)

Returns the 1-based bind index for the given named parameter, or `0` if
not found.

### Stmt.prototype.getParamName(index)

Returns the name of the given 1-based bind parameter, or `null`.

### Stmt.prototype.isBusy()

Returns `true` if the statement has been stepped but not yet reset or
finalized.

### Stmt.prototype.isReadOnly()

Returns `true` if the statement makes no direct changes to the database.

---

## Quick start

```html
<script src="sqlcipher.js"></script>
<script>
(async function() {
  // 1. Load the WASM module
  var sqlite3 = await initSqlcipher();

  // 2. Open an encrypted in-memory database
  var db = new sqlite3.oo1.DB({
    filename: ":memory:",
    key: "correct-horse-battery-staple"
  });

  // 3. Create a table and insert data with exec()
  db.exec("CREATE TABLE sensors (id TEXT PRIMARY KEY, value REAL, ts INTEGER)");
  db.exec({
    sql:  "INSERT INTO sensors VALUES (?, ?, ?)",
    bind: ["temp_kitchen", 22.5, 1710000000]
  });
  db.exec({
    sql:  "INSERT INTO sensors VALUES (?, ?, ?)",
    bind: ["humidity", 0.65, 1710000002]
  });

  // 4. Query with select shortcuts
  var count = db.selectValue("SELECT count(*) FROM sensors");
  console.log("rows:", count);  // 2

  var row = db.selectObject("SELECT * FROM sensors WHERE id = ?", ["humidity"]);
  console.log(row);  // {id: "humidity", value: 0.65, ts: 1710000002}

  var all = db.selectArrays("SELECT id, value FROM sensors ORDER BY id");
  console.log(all);  // [["humidity", 0.65], ["temp_kitchen", 22.5]]

  // 5. Prepare / step / get loop
  var stmt = db.prepare("SELECT id, value FROM sensors WHERE value > ?");
  stmt.bind([1.0]);
  while (stmt.step()) {
    var row = stmt.get([]);
    console.log(row[0], "=", row[1]);
  }
  stmt.finalize();

  // 6. Transaction
  db.transaction(function(db) {
    db.exec({ sql: "INSERT INTO sensors VALUES (?, ?, ?)", bind: ["co2", 412, 1710000003] });
    db.exec({ sql: "INSERT INTO sensors VALUES (?, ?, ?)", bind: ["pm25", 8.2, 1710000004] });
  });

  // 7. Clean up
  db.close();
})();
</script>
```

---

## Persistence

### Unified API (recommended)

`sqlcipher-api.js` provides a single async interface that auto-detects
the best persistence backend.  Prefer OPFS, fall back to IndexedDB
page cache.  Both are durable per statement (auto-flush on every exec).

```html
<script src="sqlcipher.js"></script>
<script src="sqlcipher-api.js"></script>
<script>
(async function() {
  var db = await SQLCipher.open({filename: "app.db", key: "secret"});
  console.log("Backend:", db.mode);  // "opfs" or "indexeddb"

  await db.exec("CREATE TABLE IF NOT EXISTS t (x TEXT)");
  await db.exec("INSERT INTO t VALUES (?)", ["hello"]);

  var rows = await db.select("SELECT * FROM t");
  console.log(rows);  // [{x: "hello"}]

  await db.save();    // manual flush (auto-flush happens on every exec)
  await db.close();   // closes (auto-flushed already)
})();
</script>
```

#### SQLCipher.open(opts) → Promise\<Handle\>

| Option | Default | Description |
|--------|---------|-------------|
| `filename` | `"/app.db"` | Database name. Leading `/` added if missing. |
| `key` | — | Encryption passphrase. |
| `workerUrl` | `"sqlcipher-worker.js"` | Path to the Worker script. |

Returns a `Handle` with `mode` set to `"opfs"` or `"indexeddb"`.

#### Handle methods

| Method | Returns | Description |
|--------|---------|-------------|
| `exec(sql, bind?)` | `Promise<{changes}>` | Execute DDL / DML. `bind` is a positional array. |
| `select(sql, bind?)` | `Promise<Object[]>` | Query rows as objects (keys = column names). |
| `save()` | `Promise<void>` | Manual flush. Both backends auto-flush on every `exec()`. |
| `export()` | `Promise<Uint8Array>` | Full encrypted database blob (for download / transport). |
| `import(bytes)` | `Promise<void>` | Replace database from an encrypted blob. Same key is reused. |
| `shred()` | `Promise<void>` | Overwrite all stored blocks with random data, then delete. Handle is unusable after. |
| `shredOnClose()` | `Promise<void>` | Set flag: `close()` will shred instead of save. Call early to ensure destruction on exit. |
| `close()` | `Promise<void>` | Auto-saves (IndexedDB), then closes. If `shredOnClose()` was called, shreds instead. |

#### Handle properties

| Property | Type | Description |
|----------|------|-------------|
| `mode` | `"opfs"` \| `"indexeddb"` | Which persistence backend is active. |
| `filename` | string | The database filename. |

---

### How persistence works

Both backends use the same SQLite VFS (`opfs_vfs.c`).  The VFS calls
dispatch to different JS backing stores per handle:

```
VFS xRead/xWrite
  ├── OPFS:      SyncAccessHandle   (sync I/O, durable per commit)
  └── IndexedDB: in-memory buffer   (sync I/O, persisted on save/close)
```

**OPFS** — `FileSystemSyncAccessHandle` provides synchronous read/write
in a Web Worker.  Every `xSync` (triggered by `COMMIT`) flushes to
disk.  Zero data loss on tab crash.

**IndexedDB page cache** — The entire file is loaded into memory on
open.  VFS reads/writes operate on the buffer.  Dirty 4KB blocks are
tracked and auto-flushed to IndexedDB after every `exec()` (checkpoint
WAL + write only changed blocks).  `save()` is still available for
manual control but is no longer required for durability.

IndexedDB schema (`sqlcipher_pages` database, `blocks` object store):

| Key | Value |
|-----|-------|
| `[filename, -1]` | `{fileSize}` (meta) |
| `[filename, 0]` | `Uint8Array(4096)` (block 0) |
| `[filename, 1]` | `Uint8Array(4096)` (block 1) |
| ... | ... |

A 10 MB database with 1 changed page writes 4 KB on save, not 10 MB.

---

### Worker message protocol

| Message | Fields | Response |
|---------|--------|----------|
| `init` | — | `{ok, mode}` |
| `open` | `filename`, `key` | `{ok}` |
| `exec` | `sql`, `bind?` | `{ok, changes}` |
| `select` | `sql`, `bind?` | `{ok, rows, names}` |
| `save` | — | `{ok}` |
| `export` | — | `{ok, bytes}` (Uint8Array, transferable) |
| `import` | `bytes` | `{ok}` |
| `shred` | — | `{ok}` |
| `shredOnClose` | — | `{ok}` |
| `close` | — | `{ok}` |

---

### OPFS VFS (direct Worker usage)

For direct Worker control without the unified API:

```html
<script>
var worker = new Worker("sqlcipher-worker.js");
var _id = 0, _pending = {};

function send(msg) {
  return new Promise(function(resolve, reject) {
    msg.id = ++_id;
    _pending[msg.id] = {resolve: resolve, reject: reject};
    worker.postMessage(msg);
  });
}

worker.onmessage = function(e) {
  var p = _pending[e.data.id];
  delete _pending[e.data.id];
  if (e.data.ok) p.resolve(e.data);
  else p.reject(new Error(e.data.error));
};

(async function() {
  var r = await send({type: "init"});
  console.log("mode:", r.mode);
  await send({type: "open", filename: "/app.db", key: "secret"});
  await send({type: "exec", sql: "CREATE TABLE IF NOT EXISTS t (x TEXT)"});
  await send({type: "exec", sql: "INSERT INTO t VALUES (?)", bind: ["hello"]});
  var result = await send({type: "select", sql: "SELECT * FROM t"});
  console.log(result.rows);
})();
</script>
```

### oo1 API (main thread, IndexedDB blob)

For synchronous main-thread access (no Worker), use the oo1 shim with
explicit `db.save()`.  Stores the entire database blob in IndexedDB.

```javascript
var Module = await initSqlcipher();
var db = await DB.load(Module, {
  filename: "/app.db",
  key: "secret",
  store: "myapp"
});
db.exec({sql: "INSERT INTO t VALUES (?)", bind: ["hello"]});
await db.save();   // exports entire encrypted blob → IndexedDB
```

### Comparison

| | OPFS (unified/worker) | IndexedDB page cache (unified/worker) | IndexedDB blob (oo1) |
|-|----------------------|--------------------------------------|---------------------|
| Durability | Every COMMIT (VFS xSync) | Every exec (auto-flush) | On `save()` call |
| Write cost | 4KB per changed page | 4KB per dirty block | Entire database blob |
| Tab crash | Data safe | Data safe (flushed per exec) | Data since last `save()` lost |
| Thread | Worker (auto) | Worker (auto) | Main thread |
| Browser support | Chrome 108+, Safari 16.4+, Firefox 111+ | All browsers | All browsers |

### What's stored

All modes store encrypted bytes.  Without the key, the data is
opaque.  This is encryption at rest in the browser.

### Export / Import (transport)

Use `export()` to get a full encrypted database blob as a `Uint8Array`.
This is suitable for download, upload, backup, or transfer between
devices.

Use `import(bytes)` to restore from a blob.  The database is replaced
in-place; the same encryption key is reused.

```js
// Export
var blob = await db.export();
// blob is a Uint8Array — save to file, upload, etc.

// Import
var blob = /* fetch from server, file input, etc. */;
await db.import(blob);
// Database is now restored from the blob
```

### Shred (secure delete)

`shred()` overwrites every stored block with `crypto.getRandomValues()`
random data, flushes to storage, then deletes the file.  The handle is
unusable after shred.

```js
await db.shred();
// All OPFS/IndexedDB blocks overwritten with random, then deleted.
// db is now closed and cannot be reused.
```

This is defense-in-depth — the data is already encrypted, but shred
ensures the ciphertext itself is destroyed in storage.

### shredOnClose

Set a flag so that `close()` will shred instead of save. Useful for
session-scoped databases that should always be destroyed on exit.

```js
var db = await SQLCipher.open({filename: "session.db", key: "secret"});
await db.shredOnClose();  // set the flag early

// ... use the database normally ...

await db.close();  // this now shreds instead of saving
```

---

## Differences from official sqlite3 WASM oo1

| Feature | Official oo1 | SQLCipher WASM oo1 |
|---------|-------------|-------------------|
| Encryption | Not available (plain SQLite) | `key`, `textkey`, `hexkey` constructor options via SQLCipher / LibreSSL |
| `OpfsDb` subclass | Available | Not yet implemented |
| `JsStorageDb` subclass | Available (main thread) | Available (main thread) |
| `createFunction()` | Full support | Full support |
| OPFS SAH Pool VFS | Available | Not yet implemented |
| Worker1 API | Available | Not yet implemented |
| `PRAGMA key` | N/A | Applied automatically when `key`/`textkey`/`hexkey` is passed to the constructor; can also be run manually via `exec("PRAGMA key='...'")`  |

The core `DB` and `Stmt` classes are functionally identical to upstream.
The only addition is the key-application logic that runs immediately
after `sqlite3_open_v2()`, before any VFS post-open callbacks.  This
means encrypted databases work transparently -- the rest of the oo1 API
(exec, prepare, select shortcuts, transactions) behaves exactly as
documented upstream.
