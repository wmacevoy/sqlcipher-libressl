// ============================================================
// sqlcipher-worker.js — Web Worker: SQLCipher + OPFS VFS
//
// Loads the WASM module, provides OPFS file handles to the C VFS,
// and processes database commands from the main thread.
//
// Usage (main thread):
//   var worker = new Worker("sqlcipher-worker.js");
//   worker.postMessage({type: "open", filename: "/app.db", key: "secret"});
//   worker.postMessage({type: "exec", sql: "CREATE TABLE t (x TEXT)"});
//   worker.postMessage({type: "exec", sql: "INSERT INTO t VALUES (?)", bind: ["hello"]});
//   worker.postMessage({type: "select", sql: "SELECT * FROM t"});
//   // worker.onmessage receives results
// ============================================================

importScripts("sqlcipher.js");

// ── OPFS handle table (used by C VFS via Module._opfs_*) ────

var _handles = [];   // [{sah, name}, ...]
var _opfsRoot = null;

async function _getDir(path) {
  var parts = path.split("/").filter(function(p) { return p.length > 0; });
  var filename = parts.pop();
  var dir = _opfsRoot;
  for (var i = 0; i < parts.length; i++) {
    dir = await dir.getDirectoryHandle(parts[i], {create: true});
  }
  return {dir: dir, filename: filename};
}

// Called by C VFS via EM_JS
var _opfs = {
  open: async function(name, flags) {
    var loc = await _getDir(name);
    var create = !!(flags & 0x04); // SQLITE_OPEN_CREATE
    var fh = await loc.dir.getFileHandle(loc.filename, {create: create});
    var sah = await fh.createSyncAccessHandle();
    var hid = _handles.length;
    _handles.push({sah: sah, name: name});
    return hid;
  },

  close: function(hid) {
    if (_handles[hid]) {
      _handles[hid].sah.flush();
      _handles[hid].sah.close();
      _handles[hid] = null;
    }
  },

  read: function(hid, pDest, n, offset) {
    var h = _handles[hid];
    var buf = Module.HEAPU8.subarray(pDest, pDest + n);
    var nRead = h.sah.read(buf, {at: offset});
    if (nRead < n) {
      // Zero-fill the rest (SQLite expects this for short reads)
      Module.HEAPU8.fill(0, pDest + nRead, pDest + n);
      return 522; // SQLITE_IOERR_SHORT_READ
    }
    return 0; // SQLITE_OK
  },

  write: function(hid, pSrc, n, offset) {
    var h = _handles[hid];
    var buf = Module.HEAPU8.subarray(pSrc, pSrc + n);
    h.sah.write(buf, {at: offset});
    return 0;
  },

  sync: function(hid) {
    _handles[hid].sah.flush();
    return 0;
  },

  filesize: function(hid) {
    return _handles[hid].sah.getSize();
  },

  truncate: function(hid, size) {
    _handles[hid].sah.truncate(size);
    return 0;
  },

  delete: async function(name) {
    try {
      var loc = await _getDir(name);
      await loc.dir.removeEntry(loc.filename);
      return 0;
    } catch(e) {
      return 10; // SQLITE_IOERR
    }
  },

  access: async function(name) {
    try {
      var loc = await _getDir(name);
      await loc.dir.getFileHandle(loc.filename, {create: false});
      return 1;
    } catch(e) {
      return 0;
    }
  }
};

// ── WASM module setup ────────────────────────────────────────

var Module;
var _api;
var _dbPtr = 0;

async function init() {
  _opfsRoot = await navigator.storage.getDirectory();

  Module = await initSqlcipher({
    // Wire the OPFS callbacks into Module before WASM runs
    _opfs_open: function(name, flags) {
      // Synchronous wrapper — open must be sync for VFS.
      // Pre-open handles or use a sync workaround.
      // For the SAH pool pattern, we pre-open at DB.open time.
      throw new Error("Use _opfs_open_async instead");
    },
    _opfs_close: function(hid) { _opfs.close(hid); },
    _opfs_read: function(hid, pDest, n, offset) { return _opfs.read(hid, pDest, n, offset); },
    _opfs_write: function(hid, pSrc, n, offset) { return _opfs.write(hid, pSrc, n, offset); },
    _opfs_sync: function(hid) { return _opfs.sync(hid); },
    _opfs_filesize: function(hid) { return _opfs.filesize(hid); },
    _opfs_truncate: function(hid, size) { return _opfs.truncate(hid, size); },
    _opfs_delete: function(name) { return 0; }, // async — handled separately
    _opfs_access: function(name) { return 0; }  // async — handled separately
  });

  _api = {
    open:       Module.cwrap("wasm_db_open",       "number",  ["string"]),
    close:      Module.cwrap("wasm_db_close",       null,      ["number"]),
    exec:       Module.cwrap("wasm_db_exec",        "number",  ["number", "string"]),
    errmsg:     Module.cwrap("wasm_db_errmsg",      "string",  ["number"]),
    changes:    Module.cwrap("wasm_db_changes",     "number",  ["number"]),
    key:        Module.cwrap("wasm_db_key",         "number",  ["number", "string"]),
    prepare:    Module.cwrap("wasm_db_prepare",     "number",  ["number", "string"]),
    finalize:   Module.cwrap("wasm_stmt_finalize",  null,      ["number"]),
    reset:      Module.cwrap("wasm_stmt_reset",     null,      ["number"]),
    step:       Module.cwrap("wasm_stmt_step",      "number",  ["number"]),
    bind_text:  Module.cwrap("wasm_stmt_bind_text", null,      ["number", "number", "string"]),
    bind_int:   Module.cwrap("wasm_stmt_bind_int",  null,      ["number", "number", "number"]),
    bind_dbl:   Module.cwrap("wasm_stmt_bind_double", null,    ["number", "number", "number"]),
    bind_null:  Module.cwrap("wasm_stmt_bind_null", null,      ["number", "number"]),
    columns:    Module.cwrap("wasm_stmt_columns",   "number",  ["number"]),
    colname:    Module.cwrap("wasm_stmt_colname",   "string",  ["number", "number"]),
    coltype:    Module.cwrap("wasm_stmt_coltype",   "number",  ["number", "number"]),
    col_int:    Module.cwrap("wasm_stmt_int",       "number",  ["number", "number"]),
    col_dbl:    Module.cwrap("wasm_stmt_double",    "number",  ["number", "number"]),
    col_text:   Module.cwrap("wasm_stmt_text",      "string",  ["number", "number"]),
    opfs_init:  Module.cwrap("sqlite3_opfs_init",   "number",  [])
  };
}

// ── Command handler ──────────────────────────────────────────

function _bind(stmt, args) {
  if (!args) return;
  for (var i = 0; i < args.length; i++) {
    var v = args[i];
    var idx = i + 1;
    if (v === null || v === undefined) {
      _api.bind_null(stmt, idx);
    } else if (typeof v === "number") {
      if (v === (v | 0) && v >= -2147483648 && v <= 2147483647) {
        _api.bind_int(stmt, idx, v);
      } else {
        _api.bind_dbl(stmt, idx, v);
      }
    } else {
      _api.bind_text(stmt, idx, String(v));
    }
  }
}

function _query(sql, bind) {
  var stmt = _api.prepare(_dbPtr, sql);
  if (!stmt) throw new Error(_api.errmsg(_dbPtr));
  _bind(stmt, bind);
  var cols = _api.columns(stmt);
  var names = [];
  for (var i = 0; i < cols; i++) names.push(_api.colname(stmt, i));
  var rows = [];
  while (_api.step(stmt)) {
    var row = {};
    for (var i = 0; i < cols; i++) {
      var t = _api.coltype(stmt, i);
      if (t === 1) row[names[i]] = _api.col_int(stmt, i);
      else if (t === 2) row[names[i]] = _api.col_dbl(stmt, i);
      else if (t === 3) row[names[i]] = _api.col_text(stmt, i);
      else row[names[i]] = null;
    }
    rows.push(row);
  }
  _api.finalize(stmt);
  return {names: names, rows: rows};
}

async function handleMessage(msg) {
  var id = msg.id;
  try {
    switch (msg.type) {
      case "init":
        await init();
        // Register OPFS VFS before opening any database
        _api.opfs_init();
        postMessage({id: id, ok: true});
        break;

      case "open":
        if (_dbPtr) _api.close(_dbPtr);
        // Pre-open OPFS handle for the main database file
        var hid = await _opfs.open(msg.filename, 0x06); // CREATE|READWRITE
        // Store in Module for the C VFS to find
        if (!Module._opfs_preopen) Module._opfs_preopen = {};
        Module._opfs_preopen[msg.filename] = hid;
        _dbPtr = _api.open(msg.filename);
        if (msg.key) _api.key(_dbPtr, msg.key);
        postMessage({id: id, ok: true});
        break;

      case "exec":
        if (msg.bind) {
          var stmt = _api.prepare(_dbPtr, msg.sql);
          if (!stmt) throw new Error(_api.errmsg(_dbPtr));
          _bind(stmt, msg.bind);
          while (_api.step(stmt)) {}
          _api.finalize(stmt);
        } else {
          var rc = _api.exec(_dbPtr, msg.sql);
          if (rc !== 0) throw new Error(_api.errmsg(_dbPtr));
        }
        postMessage({id: id, ok: true, changes: _api.changes(_dbPtr)});
        break;

      case "select":
        var result = _query(msg.sql, msg.bind);
        postMessage({id: id, ok: true, rows: result.rows, names: result.names});
        break;

      case "close":
        if (_dbPtr) { _api.close(_dbPtr); _dbPtr = 0; }
        postMessage({id: id, ok: true});
        break;

      default:
        throw new Error("Unknown message type: " + msg.type);
    }
  } catch(e) {
    postMessage({id: id, ok: false, error: e.message});
  }
}

onmessage = function(e) {
  handleMessage(e.data);
};
