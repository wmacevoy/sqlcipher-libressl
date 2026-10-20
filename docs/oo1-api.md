# SQLCipher WASM -- oo1 API Reference

The oo1 shim (`sqlcipher-oo1.js`) follows the official sqlite3 WASM oo1 API
(<https://sqlite.org/wasm/doc/trunk/api-oo1.md>), with SQLCipher
encryption added by the `key` constructor options.  Where it differs, this
reference says so, and [Differences](#differences-from-official-sqlite3-wasm-oo1)
lists the differences together.

The shim wraps the WASM build's C helpers (`sqlcipher_wasm.c`) in two
JavaScript classes, `DB` and `Stmt`.  `DB` is a global (or
`require("./sqlcipher-oo1.js").DB`) and takes the module from
`initSqlcipher()` as its first argument; `Stmt` objects come only from
`DB.prototype.prepare()`.  All methods are synchronous and run on the
thread that loaded the WASM module.  Errors are thrown as `Error`s
carrying SQLite's message.

---

## DB

### Constructor

```js
var Module = await initSqlcipher();

// In-memory database, flags "c" (create + read/write)
var db = new DB(Module);

// Named file, in the module's in-memory file system (MEMFS)
var db = new DB(Module, "/mydata.db");

// Positional: filename, flags, vfs
var db = new DB(Module, "/mydata.db", "c", "unix-none");

// Options object -- the form for encrypted databases
var db = new DB(Module, {
  filename: "/mydata.db",
  flags:    "c",
  vfs:      null,
  key:      "correct-horse-battery-staple"   // SQLCipher passphrase
});
```

**Parameters (positional form, after `Module`):**

| # | Name | Default | Description |
|---|------|---------|-------------|
| 1 | `filename` | `":memory:"` | Database path. Special names: `":memory:"`, `""` (a temporary database). |
| 2 | `flags` | `"c"` | One or more of: `c` (create + read/write), `w` (read/write; the file must exist), `r` (read-only), `t` (log each statement's SQL to the console as `SQL TRACE: ...` when it is prepared). `c` outranks `w`, which outranks `r`. Any other letter throws. |
| 3 | `vfs` | `null` | VFS name, or falsy for the default. This build has SQLite's `unix` VFSes (`unix`, `unix-none`, `unix-dotfile`, `unix-excl`) over MEMFS. |

**Options object properties** (all optional):

| Property | Type | Description |
|----------|------|-------------|
| `filename` | string | Same as positional. |
| `flags` | string | Same as positional. Default `"c"`. |
| `vfs` | string \| null | Same as positional. |
| `key` | string \| ArrayBuffer \| typed array | A string is a passphrase, run through SQLCipher's key derivation as `PRAGMA key='...'` does -- the same as the unified API's `key`, so a database written by one opens in the other. Bytes are a raw 256-bit key (32 bytes), or a raw key and its salt (48 bytes), used without derivation. |
| `textkey` | string \| ArrayBuffer \| typed array | A passphrase, as a string or as UTF-8 bytes. Same as a string `key`. |
| `hexkey` | string \| ArrayBuffer \| typed array | A raw key: 64 hex digits (or 96, with the salt), or the bytes they stand for. Same as a byte-array `key`. |
| `bytes` | Uint8Array | Written to `filename` in MEMFS before opening -- a database to restore. |
| `store` | string | The IndexedDB name `save()` and `DB.load()` use (see [Persistence](#oo1-api-main-thread-indexeddb-blob)). |

Only **one** of `key`, `textkey`, or `hexkey` may be given; more throws.
A raw key of any length but 32 or 48 bytes throws (SQLCipher itself would
silently treat a malformed raw key as a passphrase).  The key is applied
immediately after opening.  An open that fails -- no such file under
`w` or `r`, no such VFS -- throws, and leaves nothing open.

The `pointer` property holds the underlying `sqlite3*` WASM pointer.  It
becomes `undefined` after `close()`.  `filename` holds the name it was
opened with.

---

### DB.prototype.exec(sql)
### DB.prototype.exec(sql, options)
### DB.prototype.exec(options)

Executes every SQL statement in `sql`, in order.  By default returns
`this` (the DB object), enabling chaining.  A statement that fails throws
with SQLite's message; the statements before it have run.

```js
// Simple string form
db.exec("CREATE TABLE t (a TEXT, b REAL); CREATE INDEX t_b ON t (b)");

// With bindings (applied to the first statement that has parameters)
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
| `sql` | string \| string[] \| Uint8Array | SQL to execute. Arrays are joined without separators; bytes are UTF-8. |
| `bind` | any | Value(s) for the first statement that has parameters. Accepts the same types as `Stmt.bind()`. |
| `callback` | function(row, stmt) | Called for each result row of every statement that has result columns. Returning `false` stops: no more rows, and no more statements. |
| `rowMode` | string \| integer | Shape of each row: `"array"`, `"object"`, `"stmt"` (the `Stmt` itself, callback only), a 0-based column index, or `"$columnName"` (that column's value). Default: `"array"`. |
| `resultRows` | array | If provided, each result row is pushed here. Throws with `rowMode: "stmt"`. |
| `columnNames` | array | If provided, emptied and filled with the column names of the first statement that has result columns. |
| `saveSql` | array | If provided, the SQL text of each executed statement is appended, as written (with its `;`), trimmed. |
| `returnValue` | string | What `exec()` returns: `"this"` (default), `"resultRows"`, or `"saveSql"` (each creates its array if not given). |

---

### DB.prototype.prepare(sql)

Compiles the first statement in `sql` into a prepared statement and
returns a `Stmt` object.  This is the only way to create `Stmt`
instances.  Throws if `sql` holds no statement or is invalid.  `sql` may
be a string, an array of strings (joined without separators), or a
`Uint8Array` of UTF-8.

```js
var stmt = db.prepare("SELECT a, b FROM t WHERE b > ?");
```

---

### Select shortcuts

These convenience methods prepare a statement internally, step it, and
finalize it.  The optional `bind` argument is passed to `Stmt.bind()`.

#### DB.prototype.selectValue(sql [, bind [, asType]])

Returns the value of the first column of the first result row, or
`undefined` if there are no results.  The optional `asType` coerces the
result, as in `Stmt.get(columnIndex, asType)`.

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

### DB.prototype.changes(total)

Returns the number of rows changed by the most recent statement, or, if
`total` is `true`, `sqlite3_total_changes()`: every row changed since the
database was opened.

---

### DB.prototype.createFunction(name, xFunc [, options])
### DB.prototype.createFunction(name, options)
### DB.prototype.createFunction(options)

Registers a function callable from SQL: a scalar function (`xFunc`), an
aggregate (`xStep` and `xFinal`), or a window function (`xStep`,
`xFinal`, `xValue` and `xInverse`).  Registering a name again replaces
it.  Returns `this`.

```js
db.createFunction("double_it", function(pCtx, val) {
  return val * 2;
});
db.selectValue("SELECT double_it(21)"); // 42

var sums = {};
db.createFunction("sumsq", {
  xStep:  function(pCtx, v) { sums[pCtx] = (sums[pCtx] || 0) + v * v; },
  xFinal: function(pCtx) { var r = sums[pCtx] || 0; delete sums[pCtx]; return r; }
});
db.selectValues("SELECT sumsq(v) FROM t GROUP BY g");
```

Each callback is called as `(pCtx, ...args)`.  The SQL arguments arrive
as `null`, a number, a string, or a `Uint8Array` (BLOB).  For a scalar
function, `pCtx` is the `sqlite3_context*`.  For an aggregate or window
function, `pCtx` is a number that is the same for every call made for one
group, including `xFinal` -- key per-group state by it, and drop that
state in `xFinal`.  (Upstream passes the raw `sqlite3_context*`, which
SQLite changes between `xStep` and `xFinal`, and points to
`sqlite3_aggregate_context()`, which the shim does not expose.)

The return value of `xFunc`, `xValue` and `xFinal` is the SQL result,
converted as `Stmt.bind()` converts values; `xStep`'s and `xInverse`'s
are ignored.  A callback that throws makes the SQL statement fail with
the exception's message.

**Options** (the `options` object, or the single argument with `name`):

| Property | Description |
|----------|-------------|
| `name` | The SQL name (single-argument form). |
| `xFunc` | Scalar function. Cannot be combined with the others. |
| `xStep`, `xFinal` | Aggregate function: called per row, then once per group for the result. |
| `xValue`, `xInverse` | With `xStep` and `xFinal`, a window function: the current result, and the removal of a row leaving the window. Both or neither. |
| `arity` | Number of SQL arguments; `-1` for any. Default: the callback's `length` minus 1 (for `pCtx`), or `-1` for a callback declared with no parameters. |
| `deterministic`, `directOnly`, `innocuous` | Set `SQLITE_DETERMINISTIC`, `SQLITE_DIRECTONLY`, `SQLITE_INNOCUOUS`. |

---

### DB.prototype.isOpen()

Returns `true` if the database handle is open, `false` otherwise.

### DB.prototype.affirmOpen()

Throws if the database has been closed.  Returns `this`.

### DB.prototype.close()

Finalizes all of its open statements (their `pointer` becomes
`undefined`) and closes the database.  A no-op if already closed.  After
`close()`, the `pointer` property is `undefined`.

`this.onclose.before` and `this.onclose.after` are called with the DB
before and after the close.  Each may be a function or an array of
functions; one that throws is ignored, and the close goes on.  Both
start as empty arrays.

### DB.prototype.checkRc(resultCode)

Throws an `Error` with `sqlite3_errmsg()` if `resultCode` is non-zero.
Returns `this` on success.

### DB.prototype.dbFilename(dbName)

Returns `sqlite3_db_filename()` for the given database name (default
`"main"`): `""` for an in-memory or temporary database, `null` if no
database has that name.

### DB.prototype.dbName(dbNumber)

Returns the name of the given 0-based database (default `0`): `0` is
`"main"`, `1` is `"temp"`, attached databases follow.  `null` if there
is none.

### DB.prototype.dbVfsName(dbName)

Returns the name of the VFS the given database (default `"main"`) uses.

### DB.prototype.openStatementCount()

Returns the number of `Stmt` objects open on this DB (created by
`prepare()`, not yet finalized).

---

## Stmt

`Stmt` objects are created only by `DB.prepare()`; the class is not
exported.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `pointer` | number \| undefined | The `sqlite3_stmt*` WASM pointer. `undefined` after `finalize()`. |
| `db` | DB | The DB that created this statement. |
| `columnCount` | number | Number of result columns (`sqlite3_column_count()`). |
| `parameterCount` | number | Number of bindable parameters (`sqlite3_bind_parameter_count()`). |

---

### Stmt.prototype.bind(value)
### Stmt.prototype.bind(index, value)
### Stmt.prototype.bind(array)
### Stmt.prototype.bind(object)

Binds one or more values to the statement's parameters.  Returns `this`.

**Single value (1 arg):** binds to parameter index 1.

**Two arguments:** `index` is a 1-based integer or a parameter name,
with its prefix (e.g. `"$name"`, `":name"`, `"@name"`).

**Array:** each element is bound at its array-index + 1.

**Object:** each key is a parameter name, with its prefix
(e.g. `{$a: 1, ":b": 2}`).

A name the statement does not have throws.

**Bindable types:** `null` and `undefined` (bound as NULL), `number` (a
safe integer, `Number.isSafeInteger()`, as INTEGER; anything else as
REAL), `BigInt` (as INTEGER; beyond 64 bits throws a `RangeError`),
`string`, `boolean` (bound as 0/1), `Uint8Array` (bound as blob; for
other typed arrays, or a string's UTF-8 bytes, use `bindAsBlob()`).
Anything else binds as its `String()` text.

---

### Stmt.prototype.step()

Steps the statement.  Returns `true` if a row of data is available (the
`get()` family becomes legal), `false` if the statement is done.  Throws
on error, with SQLite's message.

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
auto-detected from `sqlite3_column_type()`: `null`, a number, a string,
or a `Uint8Array` for a blob.

### Stmt.prototype.get(columnIndex, asType)

Returns the value at the given column, coerced to `asType`, given as the
`SQLITE_*` type code: `1` (`SQLITE_INTEGER`), `2` (`SQLITE_FLOAT`), `3`
(`SQLITE_TEXT`), or `4` (`SQLITE_BLOB`).  A NULL column is `null` whatever
the type; another code throws.

Integers are read as 64-bit: a `number` while the value is a safe
integer (|v| ≤ 2^53 − 1), a `BigInt` beyond that, so every value comes
back exactly.  The same holds for the arguments of a `createFunction`
callback, and a callback may return a `BigInt`.

---

### Stmt.prototype.getInt(columnIndex)

Equivalent to `get(columnIndex, 1)`.

### Stmt.prototype.getFloat(columnIndex)

Equivalent to `get(columnIndex, 2)`.

### Stmt.prototype.getString(columnIndex)

Equivalent to `get(columnIndex, 3)`.

### Stmt.prototype.getBlob(columnIndex)

Equivalent to `get(columnIndex, 4)`.  Returns a `Uint8Array`.

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
also calls `clearBindings()`.  Returns `this`.  Throws if
`sqlite3_reset()` reports an error -- which it does after a failed
`step()`, repeating that error.

### Stmt.prototype.clearBindings()

Clears all bound parameter values.  Returns `this`.

### Stmt.prototype.finalize()

Finalizes the statement, releasing all resources.  Returns the result of
`sqlite3_finalize()` (0 on success), or `undefined` if already
finalized.  After this call, most methods will throw.

---

### Stmt.prototype.bindAsBlob(value)
### Stmt.prototype.bindAsBlob(index, value)

Like `bind()` but forces the BLOB binding mechanism.  `index` is as in
`bind()`.  The value must be a string (bound as its UTF-8 bytes),
`null`/`undefined` (bound as NULL), or a typed array; anything else
throws.

### Stmt.prototype.getParamIndex(name)

Returns the 1-based bind index for the given named parameter (with its
prefix), or `0` if not found.

### Stmt.prototype.getParamName(index)

Returns the name of the given 1-based bind parameter, with its prefix,
or `null` if it has none (`?`) or there is no such index.

### Stmt.prototype.isBusy()

Returns `true` if the statement has been stepped to a row but not yet
reset, finalized, or stepped past its last row.

### Stmt.prototype.isReadOnly()

Returns `true` if the statement makes no direct changes to the database.

---

## Quick start

```html
<script src="sqlcipher.js"></script>
<script src="sqlcipher-oo1.js"></script>
<script>
(async function() {
  // 1. Load the WASM module
  var Module = await initSqlcipher();

  // 2. Open an encrypted in-memory database
  var db = new DB(Module, {
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
    sql:  "INSERT INTO sensors VALUES ($id, $value, $ts)",
    bind: {$id: "humidity", $value: 0.65, $ts: 1710000002}
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

  await db.flush();   // manual flush (auto-flush happens on every exec)
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

Returns a `Handle` with `mode` set to `"opfs"` or `"indexeddb"`.  If the
worker's `init` or `open` fails, the promise rejects with its error and the
worker is terminated.

#### Handle methods

| Method | Returns | Description |
|--------|---------|-------------|
| `exec(sql, bind?)` | `Promise<{changes}>` | Execute DDL / DML. `bind` is a positional array; a `Uint8Array` binds as a BLOB, a safe-integer `number` or a `BigInt` as an INTEGER (a `BigInt` beyond 64 bits rejects). |
| `select(sql, bind?)` | `Promise<Object[]>` | Query rows as objects (keys = column names). BLOB columns are `Uint8Array`s; integers are `number`s while safe, `BigInt`s beyond 2^53 − 1. |
| `flush()` | `Promise<void>` | Manual flush. Both backends auto-flush on every `exec()`. |
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

Both backends use the same SQLite VFS (`opfs_vfs.c`), with walseal
(`walseal.c`) stacked over it.  The worker opens every database in
`locking_mode=EXCLUSIVE` and `journal_mode=WAL`, so a commit writes
its pages to the write-ahead log, not the database file.  The VFS
calls dispatch to different JS backing stores per handle:

```
VFS xRead/xWrite (walseal enciphers the -wal file's headers)
  ├── OPFS:      SyncAccessHandle   (sync I/O; database and -wal, durable per commit)
  └── IndexedDB: in-memory buffer   (sync I/O; WAL in memory, database flushed per exec)
```

**OPFS** — `FileSystemSyncAccessHandle` provides synchronous read/write
in a Web Worker.  Every `xSync` (triggered by `COMMIT`) flushes the
`-wal` file to disk; after a tab crash the next `open` replays its
committed transactions.  SQLCipher encrypts the pages in the WAL;
walseal enciphers the headers SQLite writes in the clear (page
numbers, commit sizes, salts), keyed from SQLCipher's derived key.
`PRAGMA rekey` through `exec()` keeps every commit: the worker
checkpoints before and after it, and the WAL that follows is sealed
under the new key.  A key pragma inside a transaction, or after an
`open` whose key could not read the database, is refused with
`not authorized`.

**IndexedDB page cache** — The entire file is loaded into memory on
open.  VFS reads/writes operate on the buffer; the WAL stays in
memory.  After every `exec()` the WAL is checkpointed into the buffer
and the dirty 4KB blocks are flushed to IndexedDB in one transaction,
so what is stored is always a committed state.  `flush()` is available
for manual control but is no longer required for durability.

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
| `flush` | — | `{ok}` |
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
| Durability | Every COMMIT, synced to the `-wal` | Every exec (checkpoint, then auto-flush) | On `save()` call |
| Write cost | 4KB per changed page to the WAL, again at each checkpoint | 4KB per dirty block | Entire database blob |
| Tab crash | Data safe (the next open replays the WAL) | Data safe (the stored copy is the last flushed commit) | Data since last `save()` lost |
| Thread | Worker (auto) | Worker (auto) | Main thread |
| Browser support | Chrome 108+, Safari 16.4+, Firefox 111+ (untested: CI runs current Chromium, Firefox and WebKit) | All browsers | All browsers |

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
random data, flushes to storage, then deletes the file — on OPFS, the
`-wal` file too.  The handle is unusable after shred.

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

| Feature | Official oo1 | SQLCipher WASM oo1 shim |
|---------|-------------|-------------------|
| Access | `sqlite3.oo1.DB` | global `DB`, with the module as the first constructor argument |
| Encryption | Not available (plain SQLite) | `key`, `textkey`, `hexkey` constructor options (SQLCipher / LibreSSL); a string `key` is a passphrase |
| Errors | `SQLite3Error` | `Error`, with SQLite's message |
| `pCtx` for aggregates | raw `sqlite3_context*`; state via `sqlite3_aggregate_context()` | a per-group key (see `createFunction`) |
| 64-bit values | `BigInt` binding and results, `changes(total, true)` | `BigInt` binding and results, as upstream; no 64-bit `changes()` |
| SQL as a WASM pointer | Accepted | Not accepted |
| `capi`, `wasm`, `Stmt` constructor | Exposed | Not exposed |
| `OpfsDb`, `JsStorageDb`, kvvfs (`":localStorage:"`) | Available | Not implemented; for persistence use `save()` / `DB.load()`, or the unified API |
| OPFS SAH Pool VFS, Worker1 API | Available | Not implemented; the unified API runs in a worker |
| Persistence | Through the VFS | `exportDatabase()`, `save()`, `DB.load()` (whole file, IndexedDB) |

Otherwise `DB` and `Stmt` follow upstream: exec, prepare, the select
shortcuts, transactions, binding, stepping and `get()` behave as
documented upstream, with the key applied right after
`sqlite3_open_v2()`, before anything else runs.
