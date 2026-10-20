// ============================================================
// sqlcipher-oo1.js — sqlite3 oo1 API over SQLCipher WASM
//
// Standalone OO1 wrapper for SQLCipher compiled to WASM.
// API reference: docs/oo1-api.md (after https://sqlite.org/wasm/doc/trunk/api-oo1.md)
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
//   var db = await DB.load(Module, {filename: "/app.db", key: "secret", store: "myapp"});
//   db.exec("INSERT INTO ...");
//   await db.save();
// ============================================================

// new DB(Module [, filename [, flags [, vfs]]])  or  new DB(Module, {filename, flags, vfs, key, ...})
function DB(Module, opts, flagsArg, vfsArg) {
  if (typeof opts === "string") opts = {filename: opts, flags: flagsArg, vfs: vfsArg};
  if (!opts) opts = {};
  var filename = opts.filename === undefined ? ":memory:" : opts.filename;

  var _api = {
    open:             Module.cwrap("wasm_db_open_v2",               "number",  ["string", "number", "string"]),
    close:            Module.cwrap("wasm_db_close",                 null,      ["number"]),
    exec:             Module.cwrap("wasm_db_exec",                  "number",  ["number", "string"]),
    errmsg:           Module.cwrap("wasm_db_errmsg",                "string",  ["number"]),
    errcode:          Module.cwrap("wasm_db_errcode",               "number",  ["number"]),
    changes:          Module.cwrap("wasm_db_changes",               "number",  ["number"]),
    total_changes:    Module.cwrap("wasm_db_total_changes",         "number",  ["number"]),
    db_filename:      Module.cwrap("wasm_db_filename",              "number",  ["number", "string"]),
    db_name:          Module.cwrap("wasm_db_name",                  "number",  ["number", "number"]),
    db_vfs_name:      Module.cwrap("wasm_db_vfs_name",              "number",  ["number", "string"]),
    prepare:          Module.cwrap("wasm_db_prepare",               "number",  ["number", "string"]),
    prepare_tail:     Module.cwrap("wasm_db_prepare_tail",          "number",  ["number", "number", "number", "number"]),
    key:              Module.cwrap("wasm_db_key",                   "number",  ["number", "string"]),
    create_function:  Module.cwrap("wasm_db_create_function",       "number",  ["number", "string", "number", "number", "number", "number"]),
    finalize:         Module.cwrap("wasm_stmt_finalize",            "number",  ["number"]),
    reset:            Module.cwrap("wasm_stmt_reset",               "number",  ["number"]),
    clear_bindings:   Module.cwrap("wasm_stmt_clear_bindings",      null,      ["number"]),
    step:             Module.cwrap("wasm_stmt_step_rc",             "number",  ["number"]),
    sql:              Module.cwrap("wasm_stmt_sql",                 "string",  ["number"]),
    readonly:         Module.cwrap("wasm_stmt_readonly",            "number",  ["number"]),
    bind_text:        Module.cwrap("wasm_stmt_bind_text",           null,      ["number", "number", "string"]),
    bind_int:         Module.cwrap("wasm_stmt_bind_int",            null,      ["number", "number", "number"]),
    bind_int64:       Module.cwrap("wasm_stmt_bind_int64",          null,      ["number", "number", "number"]),
    bind_int64_text:  Module.cwrap("wasm_stmt_bind_int64_text",     null,      ["number", "number", "string"]),
    bind_double:      Module.cwrap("wasm_stmt_bind_double",         null,      ["number", "number", "number"]),
    bind_null:        Module.cwrap("wasm_stmt_bind_null",           null,      ["number", "number"]),
    bind_blob:        Module.cwrap("wasm_stmt_bind_blob",           null,      ["number", "number", "number", "number"]),
    bind_param_count: Module.cwrap("wasm_stmt_bind_parameter_count","number",  ["number"]),
    bind_param_index: Module.cwrap("wasm_stmt_bind_parameter_index","number",  ["number", "string"]),
    bind_param_name:  Module.cwrap("wasm_stmt_bind_parameter_name", "number",  ["number", "number"]),
    columns:          Module.cwrap("wasm_stmt_columns",             "number",  ["number"]),
    colname:          Module.cwrap("wasm_stmt_colname",             "string",  ["number", "number"]),
    coltype:          Module.cwrap("wasm_stmt_coltype",             "number",  ["number", "number"]),
    col_int64:        Module.cwrap("wasm_stmt_int64",               "number",  ["number", "number"]),
    col_int64_text:   Module.cwrap("wasm_stmt_int64_text",          "string",  ["number", "number"]),
    col_double:       Module.cwrap("wasm_stmt_double",              "number",  ["number", "number"]),
    col_text:         Module.cwrap("wasm_stmt_text",                "string",  ["number", "number"]),
    col_blob:         Module.cwrap("wasm_stmt_blob",                "number",  ["number", "number"]),
    col_bytes:        Module.cwrap("wasm_stmt_bytes",               "number",  ["number", "number"]),
    value_type:       Module.cwrap("wasm_value_type",               "number",  ["number", "number"]),
    value_double:     Module.cwrap("wasm_value_double",             "number",  ["number", "number"]),
    value_int64:      Module.cwrap("wasm_value_int64",              "number",  ["number", "number"]),
    value_int64_text: Module.cwrap("wasm_value_int64_text",         "string",  ["number", "number"]),
    value_text:       Module.cwrap("wasm_value_text",               "string",  ["number", "number"]),
    value_blob:       Module.cwrap("wasm_value_blob",               "number",  ["number", "number"]),
    value_bytes:      Module.cwrap("wasm_value_bytes",              "number",  ["number", "number"]),
    result_null:      Module.cwrap("wasm_result_null",              null,      ["number"]),
    result_int:       Module.cwrap("wasm_result_int",               null,      ["number", "number"]),
    result_double:    Module.cwrap("wasm_result_double",            null,      ["number", "number"]),
    result_int64:     Module.cwrap("wasm_result_int64",             null,      ["number", "number"]),
    result_int64_text:Module.cwrap("wasm_result_int64_text",        null,      ["number", "string"]),
    result_text:      Module.cwrap("wasm_result_text",              null,      ["number", "string"]),
    result_blob:      Module.cwrap("wasm_result_blob",              null,      ["number", "number", "number"]),
    result_error:     Module.cwrap("wasm_result_error",             null,      ["number", "string"])
  };

  var _Module = Module;

  // Parse everything before opening, so a bad option leaves nothing open.
  var openFlags = DB._parseFlags(opts.flags);
  var keyArg = DB._keyArg(opts);

  // If loading from bytes, write to MEMFS first
  if (opts.bytes) {
    var dir = filename.substring(0, filename.lastIndexOf("/"));
    if (dir) {
      try { _Module.FS.mkdirTree(dir); } catch(e) {}
    }
    _Module.FS.writeFile(filename, opts.bytes);
  }

  var _ptr = _api.open(filename, openFlags.sqlite, opts.vfs || null);
  if (!_ptr) throw new Error("open(" + filename + "): out of memory");
  if (_api.errcode(_ptr) !== 0) {
    var openErr = _api.errmsg(_ptr);
    _api.close(_ptr);
    throw new Error("open(" + filename + "): " + openErr);
  }
  var _stmts = {};  // pointer -> Stmt, so close() can finalize what is still open
  var _trace = openFlags.trace;
  var self = this;

  this.filename = filename;
  this.pointer = _ptr;
  this.onclose = {before: [], after: []};

  var _store = opts.store || null;  // IndexedDB key for persistence

  // SQLCipher: apply key immediately after open
  if (keyArg) _api.key(_ptr, keyArg);

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
    var hooks = self.onclose || {};
    _runHooks(hooks.before);
    for (var p in _stmts) _stmts[p].finalize();
    _api.close(_ptr);
    _runHooks(hooks.after);
    _ptr = 0;
    self.pointer = undefined;
  };

  // A hook is a function or an array of them; a hook that throws does not stop the close.
  function _runHooks(h) {
    if (!h) return;
    if (typeof h === "function") h = [h];
    for (var i = 0; i < h.length; i++) {
      try { h[i](self); } catch (e) {}
    }
  }

  // ── changes / checkRc / database info ──────────────────────

  this.changes = function(total) {
    self.affirmOpen();
    return total ? _api.total_changes(_ptr) : _api.changes(_ptr);
  };

  this.checkRc = function(rc) {
    self.affirmOpen();
    if (rc) throw new Error("SQLite result code " + rc + ": " + _api.errmsg(_ptr));
    return self;
  };

  this.dbFilename = function(dbName) {
    self.affirmOpen();
    return _cString(_api.db_filename(_ptr, dbName || "main"));
  };

  this.dbName = function(dbNumber) {
    self.affirmOpen();
    return _cString(_api.db_name(_ptr, dbNumber || 0));
  };

  this.dbVfsName = function(dbName) {
    self.affirmOpen();
    return _cString(_api.db_vfs_name(_ptr, dbName || "main"));
  };

  this.openStatementCount = function() {
    var n = 0;
    for (var p in _stmts) n++;
    return n;
  };

  // ── exec(sql, opts) ────────────────────────────────────────
  //
  // Matches oo1: exec("SQL") or exec({sql, bind, callback, rowMode,
  //   resultRows, columnNames, saveSql, returnValue})
  // Runs every statement in sql. bind goes to the first one with parameters;
  // rows of every statement with result columns go to callback / resultRows.

  this.exec = function(sql, opts2) {
    self.affirmOpen();
    if (typeof sql === "object" && sql !== null && !Array.isArray(sql) && !(sql instanceof Uint8Array)) {
      opts2 = sql;
      sql = opts2.sql;
    }
    if (!opts2) opts2 = {};

    var bind = opts2.bind;
    var callback = opts2.callback;
    var rowMode = opts2.rowMode === undefined ? "array" : opts2.rowMode;   // 0 is a column index
    var columnNames = opts2.columnNames;
    var saveSql = opts2.saveSql;
    var returnValue = opts2.returnValue || "this";
    var resultRows = opts2.resultRows;
    if (returnValue === "resultRows" && !resultRows) resultRows = [];
    if (returnValue === "saveSql" && !saveSql) saveSql = [];
    if (returnValue !== "this" && returnValue !== "resultRows" && returnValue !== "saveSql")
      throw new Error("exec: unknown returnValue " + returnValue);
    if (rowMode === "stmt" && resultRows)
      throw new Error("exec: rowMode 'stmt' cannot fill resultRows; use a callback");

    var text = DB._sqlString(sql);
    var nBytes = _Module.lengthBytesUTF8(text);
    var zSql = _Module._malloc(nBytes + 1);
    var out = _Module._malloc(8);   // sqlite3_stmt* then the tail pointer
    var gotNames = false;
    try {
      _Module.stringToUTF8(text, zSql, nBytes + 1);
      var pos = zSql, end = zSql + nBytes;
      while (pos < end) {
        var rc = _api.prepare_tail(_ptr, pos, out, out + 4);
        var words = new Uint32Array(_Module.HEAPU8.buffer, out, 2);
        var stmtPtr = words[0];
        pos = words[1];
        if (rc !== 0) _check("exec");
        if (!stmtPtr) continue;   // whitespace or a comment
        var stmt = _wrap(stmtPtr);
        try {
          if (saveSql) saveSql.push(_api.sql(stmtPtr).trim());
          if (bind !== undefined && bind !== null && stmt.parameterCount) {
            stmt.bind(bind);
            bind = null;
          }
          if (!stmt.columnCount) {
            stmt.step();
            continue;
          }
          if (columnNames && !gotNames) {
            columnNames.length = 0;
            stmt.getColumnNames(columnNames);
            gotNames = true;
          }
          var colIdx = _rowModeColumn(stmt, rowMode);
          while (stmt.step()) {
            var row;
            if (rowMode === "object") row = stmt.get({});
            else if (rowMode === "stmt") row = stmt;
            else if (rowMode === "array") row = stmt.get([]);
            else row = stmt.get(colIdx);

            if (resultRows) resultRows.push(row);
            if (callback && callback(row, stmt) === false) return _execResult();
          }
        } finally {
          stmt.finalize();
        }
      }
      return _execResult();
    } finally {
      _Module._free(out);
      _Module._free(zSql);
    }

    function _execResult() {
      if (returnValue === "resultRows") return resultRows;
      if (returnValue === "saveSql") return saveSql;
      return self;
    }
  };

  // rowMode is "array", "object", "stmt", a 0-based column index, or "$columnName".
  function _rowModeColumn(stmt, rowMode) {
    if (typeof rowMode === "number") return rowMode;
    if (rowMode === "array" || rowMode === "object" || rowMode === "stmt") return -1;
    if (typeof rowMode === "string" && rowMode.charAt(0) === "$") {
      var names = stmt.getColumnNames();
      var idx = names.indexOf(rowMode.slice(1));
      if (idx < 0) throw new Error("exec: rowMode " + rowMode + ": no such column");
      return idx;
    }
    throw new Error("exec: unknown rowMode " + rowMode);
  }

  // ── prepare(sql) ───────────────────────────────────────────

  this.prepare = function(sql) {
    self.affirmOpen();
    var text = DB._sqlString(sql);
    var ptr = _api.prepare(_ptr, text);
    if (!ptr) {
      if (_api.errcode(_ptr) === 0) throw new Error("prepare: no SQL statement in: " + text);
      _check("prepare(" + text + ")");
    }
    return _wrap(ptr);
  };

  function _wrap(ptr) {
    // sqlite3_sql() is the text as written: the leading space and the ";" too.
    if (_trace) console.log("SQL TRACE: " + _api.sql(ptr).trim());
    var stmt = new Stmt(ptr);
    _stmts[ptr] = stmt;
    return stmt;
  }

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

  this.selectValue = function(sql, bind, asType) {
    var stmt = self.prepare(sql);
    try {
      if (bind !== undefined && bind !== null) stmt.bind(bind);
      return stmt.step() ? stmt.get(0, asType) : undefined;
    } finally {
      stmt.finalize();
    }
  };

  this.selectValues = function(sql, bind, asType) {
    var stmt = self.prepare(sql);
    var vals = [];
    try {
      if (bind !== undefined && bind !== null) stmt.bind(bind);
      while (stmt.step()) vals.push(stmt.get(0, asType));
      return vals;
    } finally {
      stmt.finalize();
    }
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

  // ── createFunction ─────────────────────────────────────────
  //
  // createFunction(name, xFunc [, options]) | (name, options) | (options)
  // Each callback gets (pCtx, ...args); for an aggregate or window function, pCtx is a
  // number that is the same for every call of one group, to key its state by.
  // xFunc's, xValue's and xFinal's return value is the result; a callback that throws
  // makes the SQL statement fail with its message.

  this.createFunction = function(name, xFunc, options) {
    self.affirmOpen();
    var o = {}, k;
    if (typeof name === "object" && name !== null) {
      for (k in name) o[k] = name[k];
    } else {
      if (typeof xFunc === "object" && xFunc !== null) { options = xFunc; xFunc = undefined; }
      if (options) for (k in options) o[k] = options[k];
      o.name = name;
      if (xFunc) o.xFunc = xFunc;
    }
    if (typeof o.name !== "string" || !o.name) throw new Error("createFunction: name required");

    var kind;
    if (o.xFunc) {
      if (o.xStep || o.xFinal || o.xValue || o.xInverse)
        throw new Error("createFunction: xFunc cannot be combined with xStep/xFinal/xValue/xInverse");
      kind = 0;
    } else if (o.xStep && o.xFinal) {
      if (!o.xValue !== !o.xInverse)
        throw new Error("createFunction: a window function needs both xValue and xInverse");
      kind = o.xValue ? 2 : 1;
    } else {
      throw new Error("createFunction: needs xFunc, or xStep and xFinal");
    }

    var main = o.xFunc || o.xStep;
    var arity = o.arity !== undefined ? o.arity : (main.length ? main.length - 1 : -1);
    var flags = 1;                              // SQLITE_UTF8
    if (o.deterministic) flags |= 0x000000800;  // SQLITE_DETERMINISTIC
    if (o.directOnly)    flags |= 0x000080000;  // SQLITE_DIRECTONLY
    if (o.innocuous)     flags |= 0x000200000;  // SQLITE_INNOCUOUS

    var id = _fnRegister({
      xFunc: o.xFunc, xStep: o.xStep, xFinal: o.xFinal, xValue: o.xValue, xInverse: o.xInverse
    });
    // SQLite calls the destructor, which unregisters id, even when this fails.
    var rc = _api.create_function(_ptr, o.name, arity, flags, id, kind);
    if (rc) _check("createFunction(" + o.name + ")");
    return self;
  };

  // The C trampolines (sqlcipher_wasm.c) call Module._oo1_fn_call with an id into this
  // registry. One registry per Module, shared by every DB on it.
  function _fnRegister(fns) {
    if (!_Module._oo1_fns) {
      _Module._oo1_fns = {};
      _Module._oo1_fn_next = 1;
      _Module._oo1_fn_call = _fnCall;
      _Module._oo1_fn_destroy = function(id) { delete _Module._oo1_fns[id]; };
    }
    var id = _Module._oo1_fn_next++;
    _Module._oo1_fns[id] = fns;
    return id;
  }

  var _FN_KINDS = ["xFunc", "xStep", "xFinal", "xValue", "xInverse"];

  function _fnCall(id, kind, ctx, key, argc, argv) {
    var fns = _Module._oo1_fns[id];
    var args = [key];
    try {
      for (var i = 0; i < argc; i++) args.push(_readValue(argv, i));
      if (!key) throw new Error("out of memory");   // sqlite3_aggregate_context failed
      var result = fns[_FN_KINDS[kind]].apply(null, args);
      if (kind === 0 || kind === 2 || kind === 3) _setResult(ctx, result);
    } catch (e) {
      _api.result_error(ctx, (e && e.message) || String(e));
    }
  }

  function _readValue(argv, i) {
    var type = _api.value_type(argv, i);
    if (type === 1) {                                                   // INTEGER
      var d = _api.value_int64(argv, i);
      return Number.isSafeInteger(d) ? d : BigInt(_api.value_int64_text(argv, i));
    }
    if (type === 2) return _api.value_double(argv, i);                 // FLOAT
    if (type === 3) return _api.value_text(argv, i);                   // TEXT
    if (type === 4) {                                                   // BLOB
      var p = _api.value_blob(argv, i), n = _api.value_bytes(argv, i);
      return _Module.HEAPU8.slice(p, p + n);
    }
    return null;                                                        // NULL
  }

  // Same conversions as _bindOne.
  function _setResult(ctx, v) {
    if (v === null || v === undefined) {
      _api.result_null(ctx);
    } else if (typeof v === "number") {
      if (Number.isSafeInteger(v)) _api.result_int64(ctx, v);
      else _api.result_double(ctx, v);
    } else if (typeof v === "bigint") {
      _api.result_int64_text(ctx, DB._int64Text(v));
    } else if (typeof v === "boolean") {
      _api.result_int(ctx, v ? 1 : 0);
    } else if (ArrayBuffer.isView(v)) {
      var bytes = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
      var p = bytes.length ? _Module._malloc(bytes.length) : 0;
      if (p) _Module.HEAPU8.set(bytes, p);
      _api.result_blob(ctx, p, bytes.length);   // SQLITE_TRANSIENT: SQLite copies
      if (p) _Module._free(p);
    } else {
      _api.result_text(ctx, String(v));
    }
  }

  // ── Stmt ───────────────────────────────────────────────────

  function Stmt(ptr) {
    this.pointer = ptr;
    this.db = self;
    this.columnCount = _api.columns(ptr);
    this.parameterCount = _api.bind_param_count(ptr);
    this._busy = false;
  }

  Stmt.prototype.affirmNotFinalized = function() {
    if (!this.pointer) throw new Error("Statement is finalized");
    return this;
  };

  // ── bind ───────────────────────────────────────────────────
  // bind(value)          — bind to index 1
  // bind(ndx, value)     — bind to an index (1-based) or a name ("$a", ":a", "@a")
  // bind([v1,v2,...])    — bind array positionally
  // bind({$a: v, ...})   — bind by name; keys include the prefix

  Stmt.prototype.bind = function(ndxOrValue, value) {
    this.affirmNotFinalized();
    var ptr = this.pointer;

    if (arguments.length === 2) {
      _bindOne(ptr, this._index(ndxOrValue), value);
      return this;
    }

    // Single argument: array, object of names, or scalar at index 1
    var v = ndxOrValue;
    if (Array.isArray(v)) {
      for (var i = 0; i < v.length; i++) _bindOne(ptr, i + 1, v[i]);
    } else if (typeof v === "object" && v !== null && !ArrayBuffer.isView(v)) {
      for (var name in v) _bindOne(ptr, this._index(name), v[name]);
    } else {
      _bindOne(ptr, 1, v);
    }
    return this;
  };

  // A 1-based index, or a parameter name looked up in this statement.
  Stmt.prototype._index = function(ndxOrName) {
    if (typeof ndxOrName !== "string") return ndxOrName;
    var idx = _api.bind_param_index(this.pointer, ndxOrName);
    if (!idx) throw new Error("bind: no such parameter: " + ndxOrName);
    return idx;
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
      if (Number.isSafeInteger(v)) {
        _api.bind_int64(ptr, idx, v);
      } else {
        _api.bind_double(ptr, idx, v);
      }
    } else if (typeof v === "bigint") {
      _api.bind_int64_text(ptr, idx, DB._int64Text(v));
    } else if (typeof v === "boolean") {
      _api.bind_int(ptr, idx, v ? 1 : 0);
    } else if (v instanceof Uint8Array) {
      _bindBlob(ptr, idx, v);
    } else {
      _api.bind_text(ptr, idx, String(v));
    }
  }

  function _bindBlob(ptr, idx, bytes) {
    // The C helper binds SQLITE_TRANSIENT (SQLite copies), so the buffer is freed at once.
    var p = bytes.length ? _Module._malloc(bytes.length) : 0;
    if (p) _Module.HEAPU8.set(bytes, p);
    _api.bind_blob(ptr, idx, p, bytes.length);
    if (p) _Module._free(p);
  }

  // bindAsBlob(value) — index 1;  bindAsBlob(ndx, value)
  // A string binds as its UTF-8 bytes; a typed array as its bytes.
  Stmt.prototype.bindAsBlob = function(ndxOrValue, value) {
    this.affirmNotFinalized();
    var idx = arguments.length === 2 ? this._index(ndxOrValue) : 1;
    var v = arguments.length === 2 ? value : ndxOrValue;
    if (v === null || v === undefined) {
      _api.bind_null(this.pointer, idx);
    } else if (typeof v === "string") {
      _bindBlob(this.pointer, idx, new TextEncoder().encode(v));
    } else if (ArrayBuffer.isView(v)) {
      _bindBlob(this.pointer, idx, new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
    } else {
      throw new Error("bindAsBlob: expected a string, null, or a typed array");
    }
    return this;
  };

  Stmt.prototype.getParamIndex = function(name) {
    this.affirmNotFinalized();
    return _api.bind_param_index(this.pointer, name);
  };

  Stmt.prototype.getParamName = function(ndx) {
    this.affirmNotFinalized();
    return _cString(_api.bind_param_name(this.pointer, ndx));
  };

  // ── step / reset / finalize ────────────────────────────────

  Stmt.prototype.step = function() {
    this.affirmNotFinalized();
    var rc = _api.step(this.pointer);
    if (rc === 100) { this._busy = true; return true; }    // SQLITE_ROW
    this._busy = false;
    if (rc === 101) return false;                           // SQLITE_DONE
    throw new Error("step: " + _api.errmsg(_ptr));
  };

  Stmt.prototype.stepFinalize = function() {
    try {
      return this.step();
    } finally {
      this.finalize();
    }
  };

  Stmt.prototype.stepReset = function() {
    this.step();
    this.reset();
    return this;
  };

  Stmt.prototype.reset = function(alsoClearBinds) {
    this.affirmNotFinalized();
    var rc = _api.reset(this.pointer);
    if (alsoClearBinds) _api.clear_bindings(this.pointer);
    this._busy = false;
    if (rc) throw new Error("reset: " + _api.errmsg(_ptr));
    return this;
  };

  Stmt.prototype.finalize = function() {
    if (!this.pointer) return undefined;
    var rc = _api.finalize(this.pointer);
    delete _stmts[this.pointer];
    this.pointer = undefined;
    this._busy = false;
    return rc;
  };

  // ── get ────────────────────────────────────────────────────
  // get([])           — array of all columns
  // get({})           — object keyed by column names
  // get(ndx)          — single column value at index
  // get(ndx, asType)  — coerced: 1 INTEGER, 2 FLOAT, 3 TEXT, 4 BLOB (the SQLITE_* codes)

  Stmt.prototype.get = function(ndxOrTarget, asType) {
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
    if (asType === undefined) return _readCol(ptr, idx);
    if (asType === 1) return this.getInt(idx);
    if (asType === 2) return this.getFloat(idx);
    if (asType === 3) return this.getString(idx);
    if (asType === 4) return this.getBlob(idx);
    throw new Error("get: unknown type " + asType);
  };

  // The typed getters return null for a NULL column, as get() does.

  Stmt.prototype.getInt = function(ndx) {
    this.affirmNotFinalized();
    if (_api.coltype(this.pointer, ndx) === 5) return null;
    return _colInt64(this.pointer, ndx);
  };

  Stmt.prototype.getFloat = function(ndx) {
    this.affirmNotFinalized();
    if (_api.coltype(this.pointer, ndx) === 5) return null;
    return _api.col_double(this.pointer, ndx);
  };

  Stmt.prototype.getString = function(ndx) {
    this.affirmNotFinalized();
    if (_api.coltype(this.pointer, ndx) === 5) return null;
    return _api.col_text(this.pointer, ndx);
  };

  Stmt.prototype.getBlob = function(ndx) {
    this.affirmNotFinalized();
    if (_api.coltype(this.pointer, ndx) === 5) return null;   // NULL
    return _readBlob(this.pointer, ndx);
  };

  Stmt.prototype.getJSON = function(ndx) {
    var s = this.getString(ndx);
    return s === null ? null : JSON.parse(s);
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

  Stmt.prototype.isReadOnly = function() {
    this.affirmNotFinalized();
    return !!_api.readonly(this.pointer);
  };

  function _readCol(ptr, c) {
    var type = _api.coltype(ptr, c);
    if (type === 1) return _colInt64(ptr, c);       // INTEGER
    if (type === 2) return _api.col_double(ptr, c);  // FLOAT
    if (type === 3) return _api.col_text(ptr, c);    // TEXT
    if (type === 4) return _readBlob(ptr, c);        // BLOB
    return null;                                      // NULL
  }

  // A number while it is a safe integer, else a BigInt (REQ5/INT-1).
  function _colInt64(ptr, c) {
    var d = _api.col_int64(ptr, c);
    return Number.isSafeInteger(d) ? d : BigInt(_api.col_int64_text(ptr, c));
  }

  function _readBlob(ptr, c) {
    var p = _api.col_blob(ptr, c), n = _api.col_bytes(ptr, c);
    return _Module.HEAPU8.slice(p, p + n);
  }

  // A C string the caller does not own; NULL comes back as null, not "".
  function _cString(p) {
    return p ? _Module.UTF8ToString(p) : null;
  }

  // ── Persistence ─────────────────────────────────────────────
  //
  // The database file lives in Emscripten's MEMFS.
  //
  //   db.exportDatabase()  → Uint8Array (the encrypted file)
  //   await db.save()      → export + store to IndexedDB (needs {store} option)
  //
  // Restore:
  //   var db = await DB.load(Module, {filename, key, store});

  this.exportDatabase = function() {
    self.affirmOpen();
    _api.exec(_ptr, "PRAGMA wal_checkpoint(TRUNCATE)");
    return _Module.FS.readFile(self.filename);
  };

  this.save = function() {
    if (!_store) throw new Error("No store name — pass {store: 'name'} to constructor");
    var bytes = self.exportDatabase();
    return DB._idbPut(_store, bytes);
  };
}

// ── Constructor helpers ──────────────────────────────────────

// "c" create + read/write (default), "w" read/write, "r" read-only, "t" trace SQL to the console.
DB._parseFlags = function(flags) {
  if (flags === undefined || flags === null) flags = "c";
  var out = {sqlite: 0, trace: false};
  for (var i = 0; i < flags.length; i++) {
    var f = flags.charAt(i);
    if (f === "c") out.sqlite |= 0x02 | 0x04;     // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
    else if (f === "w") out.sqlite |= 0x02;       // SQLITE_OPEN_READWRITE
    else if (f === "r") out.sqlite |= 0x01;       // SQLITE_OPEN_READONLY
    else if (f === "t") out.trace = true;
    else throw new Error("DB: unknown flag '" + f + "' in \"" + flags + "\"");
  }
  // SQLite rejects READONLY together with READWRITE; the stronger request wins.
  if (out.sqlite & 0x02) out.sqlite &= ~0x01;
  if (!(out.sqlite & 0x03)) out.sqlite |= 0x02 | 0x04;   // only "t": as "c"
  return out;
};

// The string handed to sqlite3_key. A passphrase goes through SQLCipher's KDF; a raw key
// is written x'<hex>', which SQLCipher uses as-is. SQLCipher treats a malformed x'...'
// as a passphrase, silently, so raw keys are checked here.
DB._keyArg = function(opts) {
  var given = 0;
  if (opts.key !== undefined && opts.key !== null) given++;
  if (opts.textkey !== undefined && opts.textkey !== null) given++;
  if (opts.hexkey !== undefined && opts.hexkey !== null) given++;
  if (given > 1) throw new Error("DB: give only one of key, textkey, hexkey");
  if (!given) return null;

  if (typeof opts.key === "string") return opts.key;
  if (opts.key !== undefined && opts.key !== null) return DB._rawKey(DB._hex(DB._bytes(opts.key, "key")));
  if (opts.textkey !== undefined && opts.textkey !== null) {
    return typeof opts.textkey === "string"
      ? opts.textkey : new TextDecoder().decode(DB._bytes(opts.textkey, "textkey"));
  }
  return DB._rawKey(typeof opts.hexkey === "string" ? opts.hexkey : DB._hex(DB._bytes(opts.hexkey, "hexkey")));
};

DB._bytes = function(v, what) {
  if (v instanceof ArrayBuffer) return new Uint8Array(v);
  if (ArrayBuffer.isView(v)) return new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
  throw new Error("DB: " + what + " must be a string, ArrayBuffer, or typed array");
};

DB._hex = function(bytes) {
  var s = "";
  for (var i = 0; i < bytes.length; i++) s += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
  return s;
};

// 64 hex digits: a 256-bit key. 96: the key, then the 128-bit salt.
DB._rawKey = function(hex) {
  if (!/^[0-9a-fA-F]*$/.test(hex) || (hex.length !== 64 && hex.length !== 96))
    throw new Error("DB: a raw key is 32 bytes (64 hex digits), or 48 with its salt (96); got " +
                    hex.length + " characters");
  return "x'" + hex + "'";
};

// A BigInt's decimal text for the C helpers, which parse it as an int64.
DB._int64Text = function(v) {
  if (v < BigInt("-9223372036854775808") || v > BigInt("9223372036854775807"))
    throw new RangeError("BigInt beyond 64 bits: " + v);
  return v.toString();
};

// SQL as a string, an array of strings (joined without separators), or UTF-8 bytes.
DB._sqlString = function(sql) {
  if (typeof sql === "string") return sql;
  if (Array.isArray(sql)) return sql.join("");
  if (sql instanceof Uint8Array) return new TextDecoder().decode(sql);
  throw new Error("SQL must be a string, an array of strings, or a Uint8Array");
};

// ── Static: load from IndexedDB ──────────────────────────────
//
//   var db = await DB.load(Module, {filename: "/app.db", key: "secret", store: "myapp"});
//   // Returns a DB restored from IndexedDB, or a fresh one if none saved.

DB.load = function(Module, opts) {
  if (!opts || !opts.store) throw new Error("opts.store required");
  return DB._idbGet(opts.store).then(function(bytes) {
    if (bytes) opts.bytes = bytes;
    return new DB(Module, opts);
  });
};

// ── IndexedDB internals ──────────────────────────────────────

DB._idbPut = function(name, bytes) {
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

DB._idbGet = function(name) {
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
