
// ============================================================
// oo1.js — sqlite3 oo1 API over SQLCipher WASM
//
// Standalone OO1 wrapper for SQLCipher compiled to WASM.
// API reference: https://sqlite.org/wasm/doc/trunk/api-oo1.md
//
// Usage:
//   var Module = await initSqlcipher();
//   var db = new DB(Module);
//   db.exec("CREATE TABLE t (a TEXT, b REAL)");
//   db.exec({sql: "INSERT INTO t VALUES (?,?)", bind: ["x", 42]});
//   db.selectObjects("SELECT * FROM t");  // [{a:"x", b:42}]
//   db.close();
//
// SQLCipher encryption:
//   var db = new DB(Module, {filename: "/app.db", key: "secret"});
//
// Persistence (IndexedDB):
//   var bytes = db.exportDatabase();
//   await DB.saveToIndexedDB("myapp", bytes);
//   // ... page reload ...
//   var bytes = await DB.loadFromIndexedDB("myapp");
//   var db = new DB(Module, {filename: "/app.db", key: "secret", bytes: bytes});
// ============================================================

function DB(Module, opts) {
  if (typeof opts === "string") opts = {filename: opts};
  if (!opts) opts = {};
  var filename = opts.filename || ":memory:";

  var _api = {
    open:             Module.cwrap("wasm_db_open",                  "number",  ["string"]),
    close:            Module.cwrap("wasm_db_close",                 null,      ["number"]),
    exec:             Module.cwrap("wasm_db_exec",                  "number",  ["number", "string"]),
    errmsg:           Module.cwrap("wasm_db_errmsg",                "string",  ["number"]),
    changes:          Module.cwrap("wasm_db_changes",               "number",  ["number"]),
    total_changes:    Module.cwrap("wasm_db_total_changes",         "number",  ["number"]),
    prepare:          Module.cwrap("wasm_db_prepare",               "number",  ["number", "string"]),
    key:              Module.cwrap("wasm_db_key",                   "number",  ["number", "string"]),
    finalize:         Module.cwrap("wasm_stmt_finalize",            null,      ["number"]),
    reset:            Module.cwrap("wasm_stmt_reset",               null,      ["number"]),
    clear_bindings:   Module.cwrap("wasm_stmt_clear_bindings",      null,      ["number"]),
    step:             Module.cwrap("wasm_stmt_step",                "number",  ["number"]),
    bind_text:        Module.cwrap("wasm_stmt_bind_text",           null,      ["number", "number", "string"]),
    bind_int:         Module.cwrap("wasm_stmt_bind_int",            null,      ["number", "number", "number"]),
    bind_double:      Module.cwrap("wasm_stmt_bind_double",         null,      ["number", "number", "number"]),
    bind_null:        Module.cwrap("wasm_stmt_bind_null",           null,      ["number", "number"]),
    bind_param_count: Module.cwrap("wasm_stmt_bind_parameter_count","number",  ["number"]),
    columns:          Module.cwrap("wasm_stmt_columns",             "number",  ["number"]),
    colname:          Module.cwrap("wasm_stmt_colname",             "string",  ["number", "number"]),
    coltype:          Module.cwrap("wasm_stmt_coltype",             "number",  ["number", "number"]),
    col_int:          Module.cwrap("wasm_stmt_int",                 "number",  ["number", "number"]),
    col_double:       Module.cwrap("wasm_stmt_double",              "number",  ["number", "number"]),
    col_text:         Module.cwrap("wasm_stmt_text",                "string",  ["number", "number"])
  };

  var _Module = Module;

  // If loading from bytes, write to MEMFS first
  if (opts.bytes) {
    var dir = filename.substring(0, filename.lastIndexOf("/"));
    if (dir) {
      try { _Module.FS.mkdirTree(dir); } catch(e) {}
    }
    _Module.FS.writeFile(filename, opts.bytes);
  }

  var _ptr = _api.open(filename);
  var _stmts = [];  // track open statements for close()
  var self = this;

  this.filename = filename;
  this.pointer = _ptr;
  this.onclose = {before: [], after: []};

  // SQLCipher: apply key immediately after open
  if (opts.key) _api.key(_ptr, opts.key);

  function _check(msg) {
    if (!_ptr) throw new Error("DB is closed");
    if (msg) throw new Error(msg + ": " + _api.errmsg(_ptr));
  }

  // ── affirmOpen / isOpen ────────────────────────────────────

  this.affirmOpen = function() {
    if (!self.pointer) throw new Error("DB is closed");
    return self;
  };

  this.isOpen = function() {
    return !!self.pointer;
  };

  // ── close ──────────────────────────────────────────────────

  this.close = function() {
    if (!self.pointer) return;
    var i;
    for (i = 0; i < self.onclose.before.length; i++) self.onclose.before[i](self);
    for (i = _stmts.length - 1; i >= 0; i--) {
      if (_stmts[i]) _api.finalize(_stmts[i]);
    }
    _stmts = [];
    _api.close(_ptr);
    for (i = 0; i < self.onclose.after.length; i++) self.onclose.after[i](self);
    _ptr = 0;
    self.pointer = undefined;
  };

  // ── changes ────────────────────────────────────────────────

  this.changes = function(total) {
    self.affirmOpen();
    return total ? _api.total_changes(_ptr) : _api.changes(_ptr);
  };

  // ── exec(sql, opts) ────────────────────────────────────────
  //
  // Matches oo1: exec("SQL") or exec({sql, bind, callback, rowMode,
  //   resultRows, columnNames, returnValue})

  this.exec = function(sql, opts2) {
    self.affirmOpen();
    if (typeof sql === "object" && sql !== null) {
      opts2 = sql;
      sql = opts2.sql;
    }
    if (!opts2) opts2 = {};

    var bind = opts2.bind;
    var callback = opts2.callback;
    var rowMode = opts2.rowMode || "array";
    var resultRows = opts2.resultRows;
    var columnNames = opts2.columnNames;
    var returnValue = opts2.returnValue || "this";

    // Simple exec: no bind, no callback, no result collection
    if (!bind && !callback && !resultRows && !columnNames) {
      var rc = _api.exec(_ptr, sql);
      if (rc !== 0) _check("exec");
      if (returnValue === "resultRows") return [];
      return self;
    }

    // Parameterized exec via prepare/step
    var stmt = self.prepare(sql);
    try {
      if (bind) stmt.bind(bind);
      if (columnNames) {
        columnNames.length = 0;
        var names = stmt.getColumnNames();
        for (var ci = 0; ci < names.length; ci++) columnNames.push(names[ci]);
      }
      var collected = resultRows || [];
      while (stmt.step()) {
        var row;
        if (rowMode === "object") row = stmt.get({});
        else if (rowMode === "stmt") row = stmt;
        else if (rowMode === "array") row = stmt.get([]);
        else if (typeof rowMode === "number") row = stmt.get(rowMode);
        else row = stmt.get([]);

        if (callback) {
          if (callback(row, stmt) === false) break;
        }
        if (resultRows && rowMode !== "stmt") resultRows.push(row);
        else if (!callback && !resultRows) { /* discard */ }
      }
      if (returnValue === "resultRows") return collected;
      return self;
    } finally {
      stmt.finalize();
    }
  };

  // ── prepare(sql) ───────────────────────────────────────────

  this.prepare = function(sql) {
    self.affirmOpen();
    var ptr = _api.prepare(_ptr, sql);
    if (!ptr) _check("prepare(" + sql + ")");
    var stmtIdx = _stmts.length;
    _stmts.push(ptr);
    return new Stmt(ptr, stmtIdx);
  };

  // ── select shortcuts ───────────────────────────────────────

  this.selectArray = function(sql, bind) {
    var rows = [];
    self.exec({sql: sql, bind: bind, rowMode: "array", resultRows: rows});
    return rows.length ? rows[0] : undefined;
  };

  this.selectArrays = function(sql, bind) {
    var rows = [];
    self.exec({sql: sql, bind: bind, rowMode: "array", resultRows: rows});
    return rows;
  };

  this.selectObject = function(sql, bind) {
    var rows = [];
    self.exec({sql: sql, bind: bind, rowMode: "object", resultRows: rows});
    return rows.length ? rows[0] : undefined;
  };

  this.selectObjects = function(sql, bind) {
    var rows = [];
    self.exec({sql: sql, bind: bind, rowMode: "object", resultRows: rows});
    return rows;
  };

  this.selectValue = function(sql, bind) {
    var row = self.selectArray(sql, bind);
    return row ? row[0] : undefined;
  };

  this.selectValues = function(sql, bind) {
    var rows = self.selectArrays(sql, bind);
    var vals = [];
    for (var i = 0; i < rows.length; i++) vals.push(rows[i][0]);
    return vals;
  };

  // ── transaction / savepoint ────────────────────────────────

  this.transaction = function(qualifierOrFn, fn) {
    self.affirmOpen();
    var qualifier = "", callback;
    if (typeof qualifierOrFn === "function") {
      callback = qualifierOrFn;
    } else {
      qualifier = qualifierOrFn ? " " + qualifierOrFn : "";
      callback = fn;
    }
    self.exec("BEGIN" + qualifier);
    try {
      var result = callback(self);
      self.exec("COMMIT");
      return result;
    } catch (e) {
      self.exec("ROLLBACK");
      throw e;
    }
  };

  this.savepoint = function(callback) {
    self.affirmOpen();
    var name = "sp" + Math.random().toString(36).slice(2, 10);
    self.exec("SAVEPOINT " + name);
    try {
      var result = callback(self);
      self.exec("RELEASE " + name);
      return result;
    } catch (e) {
      self.exec("ROLLBACK TO " + name);
      self.exec("RELEASE " + name);
      throw e;
    }
  };

  // ── pragma (backward compat) ───────────────────────────────

  this.pragma = function(pragma) {
    var m = pragma.match(/^key\s*=\s*'(.+)'$/);
    if (m) { _api.key(_ptr, m[1]); return; }
    _api.exec(_ptr, "PRAGMA " + pragma);
  };

  // ── Stmt ───────────────────────────────────────────────────

  function Stmt(ptr, idx) {
    this.pointer = ptr;
    this.columnCount = _api.columns(ptr);
    this.parameterCount = _api.bind_param_count(ptr);
    this._idx = idx;
    this._busy = false;
  }

  Stmt.prototype.affirmNotFinalized = function() {
    if (!this.pointer) throw new Error("Statement is finalized");
    return this;
  };

  // ── bind ───────────────────────────────────────────────────
  // bind(value)         — bind to index 1
  // bind(ndx, value)    — bind to specific index (1-based)
  // bind([v1,v2,...])   — bind array positionally
  // bind({$p: v, ...})  — bind by name (not supported in C layer yet)

  Stmt.prototype.bind = function(ndxOrValue, value) {
    this.affirmNotFinalized();
    var ptr = this.pointer;

    if (arguments.length === 2) {
      _bindOne(ptr, ndxOrValue, value);
      return this;
    }

    // Single argument: array, or scalar at index 1
    var v = ndxOrValue;
    if (Array.isArray(v)) {
      for (var i = 0; i < v.length; i++) _bindOne(ptr, i + 1, v[i]);
    } else if (typeof v === "object" && v !== null && !Array.isArray(v)) {
      // Object binding — keys should be param names
      throw new Error("Named parameter binding not yet supported");
    } else {
      _bindOne(ptr, 1, v);
    }
    return this;
  };

  Stmt.prototype.clearBindings = function() {
    this.affirmNotFinalized();
    _api.clear_bindings(this.pointer);
    return this;
  };

  function _bindOne(ptr, idx, v) {
    if (v === null || v === undefined) {
      _api.bind_null(ptr, idx);
    } else if (typeof v === "number") {
      if (v === (v | 0) && v >= -2147483648 && v <= 2147483647) {
        _api.bind_int(ptr, idx, v);
      } else {
        _api.bind_double(ptr, idx, v);
      }
    } else if (typeof v === "boolean") {
      _api.bind_int(ptr, idx, v ? 1 : 0);
    } else {
      _api.bind_text(ptr, idx, String(v));
    }
  }

  // ── step / reset / finalize ────────────────────────────────

  Stmt.prototype.step = function() {
    this.affirmNotFinalized();
    this._busy = !!_api.step(this.pointer);
    return this._busy;
  };

  Stmt.prototype.stepFinalize = function() {
    var hasRow = this.step();
    this.finalize();
    return hasRow;
  };

  Stmt.prototype.stepReset = function() {
    this.step();
    this.reset();
    return this;
  };

  Stmt.prototype.reset = function(alsoClearBinds) {
    this.affirmNotFinalized();
    _api.reset(this.pointer);
    if (alsoClearBinds) _api.clear_bindings(this.pointer);
    this._busy = false;
    return this;
  };

  Stmt.prototype.finalize = function() {
    if (!this.pointer) return;
    _api.finalize(this.pointer);
    if (_stmts[this._idx] === this.pointer) _stmts[this._idx] = 0;
    this.pointer = 0;
  };

  // ── get ────────────────────────────────────────────────────
  // get([])    — array of all columns
  // get({})    — object keyed by column names
  // get(ndx)   — single column value at index

  Stmt.prototype.get = function(ndxOrTarget) {
    this.affirmNotFinalized();
    var ptr = this.pointer;
    var n = this.columnCount;

    // get({}) — object
    if (ndxOrTarget && typeof ndxOrTarget === "object" && !Array.isArray(ndxOrTarget)) {
      var obj = {};
      for (var c = 0; c < n; c++) {
        obj[_api.colname(ptr, c)] = _readCol(ptr, c);
      }
      return obj;
    }

    // get([]) — array
    if (Array.isArray(ndxOrTarget)) {
      var arr = [];
      for (var c2 = 0; c2 < n; c2++) arr.push(_readCol(ptr, c2));
      return arr;
    }

    // get(ndx) or get() — single column
    var idx = (typeof ndxOrTarget === "number") ? ndxOrTarget : 0;
    return _readCol(ptr, idx);
  };

  Stmt.prototype.getInt = function(ndx) {
    this.affirmNotFinalized();
    return _api.col_int(this.pointer, ndx);
  };

  Stmt.prototype.getFloat = function(ndx) {
    this.affirmNotFinalized();
    return _api.col_double(this.pointer, ndx);
  };

  Stmt.prototype.getString = function(ndx) {
    this.affirmNotFinalized();
    return _api.col_text(this.pointer, ndx);
  };

  Stmt.prototype.getColumnName = function(ndx) {
    this.affirmNotFinalized();
    return _api.colname(this.pointer, ndx);
  };

  Stmt.prototype.getColumnNames = function(target) {
    this.affirmNotFinalized();
    var arr = target || [];
    for (var i = 0; i < this.columnCount; i++) arr.push(_api.colname(this.pointer, i));
    return arr;
  };

  Stmt.prototype.isBusy = function() { return this._busy; };

  function _readCol(ptr, c) {
    var type = _api.coltype(ptr, c);
    if (type === 1) return _api.col_int(ptr, c);    // INTEGER
    if (type === 2) return _api.col_double(ptr, c);  // FLOAT
    if (type === 3) return _api.col_text(ptr, c);    // TEXT
    return null;                                      // NULL or BLOB
  }

  // ── Export / Import (persistence) ──────────────────────────
  //
  // The database file lives in Emscripten's MEMFS.  exportDatabase()
  // reads the encrypted bytes.  Pass {bytes} to the constructor to
  // restore.  Store the bytes in IndexedDB or localStorage.

  this.exportDatabase = function() {
    self.affirmOpen();
    // Checkpoint WAL to ensure all data is in the main file
    _api.exec(_ptr, "PRAGMA wal_checkpoint(TRUNCATE)");
    return _Module.FS.readFile(self.filename);
  };
}

// ── IndexedDB persistence helpers ────────────────────────────
//
// Usage:
//   // Save
//   var bytes = db.exportDatabase();
//   await DB.saveToIndexedDB("myapp", bytes);
//
//   // Load
//   var bytes = await DB.loadFromIndexedDB("myapp");
//   var db = new DB(Module, {filename: "/myapp.db", key: "secret", bytes: bytes});

DB.saveToIndexedDB = function(name, bytes) {
  return new Promise(function(resolve, reject) {
    var req = indexedDB.open("sqlcipher_store", 1);
    req.onupgradeneeded = function() {
      req.result.createObjectStore("databases");
    };
    req.onsuccess = function() {
      var tx = req.result.transaction("databases", "readwrite");
      tx.objectStore("databases").put(bytes, name);
      tx.oncomplete = function() { req.result.close(); resolve(); };
      tx.onerror = function() { req.result.close(); reject(tx.error); };
    };
    req.onerror = function() { reject(req.error); };
  });
};

DB.loadFromIndexedDB = function(name) {
  return new Promise(function(resolve, reject) {
    var req = indexedDB.open("sqlcipher_store", 1);
    req.onupgradeneeded = function() {
      req.result.createObjectStore("databases");
    };
    req.onsuccess = function() {
      var tx = req.result.transaction("databases", "readonly");
      var get = tx.objectStore("databases").get(name);
      get.onsuccess = function() { req.result.close(); resolve(get.result || null); };
      get.onerror = function() { req.result.close(); reject(get.error); };
    };
    req.onerror = function() { reject(req.error); };
  });
};

if (typeof module !== "undefined" && module.exports) {
  module.exports.DB = DB;
}
