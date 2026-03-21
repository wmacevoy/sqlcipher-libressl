// ============================================================
// sqlcipher-worker.js — Web Worker: SQLCipher + OPFS / IndexedDB
//
// Auto-detects best persistence:
//   OPFS     — SyncAccessHandle, durable on every COMMIT
//   IndexedDB — page cache (4KB blocks), durable on save() and close()
//
// Protocol (main thread ↔ worker):
//   {type:"init"}                              → {ok, mode}
//   {type:"open",  filename, key}              → {ok}
//   {type:"exec",  sql, bind?}                 → {ok, changes}
//   {type:"select",sql, bind?}                 → {ok, rows, names}
//   {type:"save"}                              → {ok}
//   {type:"export"}                            → {ok, bytes}  (transferable)
//   {type:"import", bytes}                     → {ok}
//   {type:"shred"}                             → {ok}  (overwrite + delete)
//   {type:"shredOnClose"}                      → {ok}  (flag: close will shred)
//   {type:"close"}                             → {ok}
// ============================================================

importScripts("sqlcipher.js");

// ── IndexedDB page store ──────────────────────────────────────
//
// Database files stored as 4 KB blocks in IndexedDB.
// Keys: [filename, -1] → meta {fileSize}
//       [filename, 0…N] → Uint8Array (one block)

var _pageStore = {
  BLOCK: 4096,

  _open: function() {
    return new Promise(function(resolve, reject) {
      var req = indexedDB.open("sqlcipher_pages", 1);
      req.onupgradeneeded = function() {
        req.result.createObjectStore("blocks");
      };
      req.onsuccess = function() { resolve(req.result); };
      req.onerror = function() { reject(req.error); };
    });
  },

  /** Load entire file from IndexedDB → Uint8Array (or null). */
  load: function(filename) {
    var bs = this.BLOCK;
    return this._open().then(function(db) {
      return new Promise(function(resolve, reject) {
        var tx = db.transaction("blocks", "readonly");
        var store = tx.objectStore("blocks");
        var metaReq = store.get([filename, -1]);
        metaReq.onsuccess = function() {
          var meta = metaReq.result;
          if (!meta) { db.close(); resolve(null); return; }
          var range = IDBKeyRange.bound([filename, 0], [filename, 9999999]);
          var blocksReq = store.getAll(range);
          var keysReq = store.getAllKeys(range);
          var blocks, keys;
          blocksReq.onsuccess = function() { blocks = blocksReq.result; check(); };
          keysReq.onsuccess = function() { keys = keysReq.result; check(); };
          function check() {
            if (!blocks || !keys) return;
            var buf = new Uint8Array(meta.fileSize);
            for (var i = 0; i < keys.length; i++) {
              var off = keys[i][1] * bs;
              if (off < meta.fileSize) buf.set(new Uint8Array(blocks[i]), off);
            }
            db.close();
            resolve(buf);
          }
        };
        tx.onerror = function() { db.close(); reject(tx.error); };
      });
    });
  },

  /** Flush dirty blocks + meta to IndexedDB. */
  flush: function(filename, buf, dirty) {
    var bs = this.BLOCK;
    return this._open().then(function(db) {
      return new Promise(function(resolve, reject) {
        var tx = db.transaction("blocks", "readwrite");
        var store = tx.objectStore("blocks");
        store.put({fileSize: buf.length}, [filename, -1]);
        dirty.forEach(function(b) {
          var start = b * bs;
          if (start < buf.length) {
            store.put(buf.slice(start, Math.min(start + bs, buf.length)),
                      [filename, b]);
          }
        });
        tx.oncomplete = function() { db.close(); resolve(); };
        tx.onerror = function() { db.close(); reject(tx.error); };
      });
    });
  },

  /** Store an entire blob as pages (for import). */
  storeBlob: function(filename, bytes) {
    var bs = this.BLOCK;
    return this._open().then(function(db) {
      return new Promise(function(resolve, reject) {
        var tx = db.transaction("blocks", "readwrite");
        var store = tx.objectStore("blocks");
        var range = IDBKeyRange.bound([filename, -1], [filename, 9999999]);
        store.delete(range);
        store.put({fileSize: bytes.length}, [filename, -1]);
        var n = Math.ceil(bytes.length / bs);
        for (var b = 0; b < n; b++) {
          var start = b * bs;
          store.put(bytes.slice(start, Math.min(start + bs, bytes.length)),
                    [filename, b]);
        }
        tx.oncomplete = function() { db.close(); resolve(); };
        tx.onerror = function() { db.close(); reject(tx.error); };
      });
    });
  },

  /** Delete all blocks for a file from IndexedDB. */
  deleteFile: function(filename) {
    return this._open().then(function(db) {
      return new Promise(function(resolve, reject) {
        var tx = db.transaction("blocks", "readwrite");
        var range = IDBKeyRange.bound([filename, -1], [filename, 9999999]);
        tx.objectStore("blocks").delete(range);
        tx.oncomplete = function() { db.close(); resolve(); };
        tx.onerror = function() { db.close(); reject(tx.error); };
      });
    });
  }
};

// ── Handle table ──────────────────────────────────────────────
//
// Each entry is one of:
//   {type:"opfs", sah, name}
//   {type:"idb",  buf, name, dirty, blockSize}

var _handles = [];
var _opfsRoot = null;
var _useOpfs = false;

async function _getDir(path) {
  var parts = path.split("/").filter(function(p) { return p.length > 0; });
  var filename = parts.pop();
  var dir = _opfsRoot;
  for (var i = 0; i < parts.length; i++) {
    dir = await dir.getDirectoryHandle(parts[i], {create: true});
  }
  return {dir: dir, filename: filename};
}

// ── VFS operations (dispatch by handle type) ──────────────────

var _vfs = {
  openOpfs: async function(name, flags) {
    var loc = await _getDir(name);
    var create = !!(flags & 0x04);
    var fh = await loc.dir.getFileHandle(loc.filename, {create: create});
    var sah = await fh.createSyncAccessHandle();
    var hid = _handles.length;
    _handles.push({type: "opfs", sah: sah, name: name});
    return hid;
  },

  openIdb: function(name, buf) {
    var hid = _handles.length;
    _handles.push({
      type: "idb", buf: buf || new Uint8Array(0),
      name: name, dirty: new Set(), blockSize: _pageStore.BLOCK
    });
    return hid;
  },

  close: function(hid) {
    var h = _handles[hid];
    if (!h) return;
    if (h.type === "opfs") { h.sah.flush(); h.sah.close(); }
    _handles[hid] = null;
  },

  read: function(hid, pDest, n, offset) {
    var h = _handles[hid];
    if (h.type === "opfs") {
      var buf = Module.HEAPU8.subarray(pDest, pDest + n);
      var nRead = h.sah.read(buf, {at: offset});
      if (nRead < n) {
        Module.HEAPU8.fill(0, pDest + nRead, pDest + n);
        return 522; // SQLITE_IOERR_SHORT_READ
      }
      return 0;
    }
    var avail = Math.max(0, h.buf.length - offset);
    var toCopy = Math.min(n, avail);
    if (toCopy > 0)
      Module.HEAPU8.set(h.buf.subarray(offset, offset + toCopy), pDest);
    if (toCopy < n) {
      Module.HEAPU8.fill(0, pDest + toCopy, pDest + n);
      return 522;
    }
    return 0;
  },

  write: function(hid, pSrc, n, offset) {
    var h = _handles[hid];
    if (h.type === "opfs") {
      h.sah.write(Module.HEAPU8.subarray(pSrc, pSrc + n), {at: offset});
      return 0;
    }
    var needed = offset + n;
    if (needed > h.buf.length) {
      var grow = new Uint8Array(needed);
      grow.set(h.buf);
      h.buf = grow;
    }
    h.buf.set(Module.HEAPU8.subarray(pSrc, pSrc + n), offset);
    var bs = h.blockSize, s = Math.floor(offset / bs),
        e = Math.floor((offset + n - 1) / bs);
    for (var b = s; b <= e; b++) h.dirty.add(b);
    return 0;
  },

  sync: function(hid) {
    var h = _handles[hid];
    if (h.type === "opfs") h.sah.flush();
    return 0;
  },

  filesize: function(hid) {
    var h = _handles[hid];
    return h.type === "opfs" ? h.sah.getSize() : h.buf.length;
  },

  truncate: function(hid, size) {
    var h = _handles[hid];
    if (h.type === "opfs") {
      h.sah.truncate(size);
    } else if (size !== h.buf.length) {
      var t = new Uint8Array(size);
      t.set(h.buf.subarray(0, Math.min(size, h.buf.length)));
      h.buf = t;
    }
    return 0;
  },

  deleteFile: async function(name) {
    if (!_useOpfs) return 0;
    try {
      var loc = await _getDir(name);
      await loc.dir.removeEntry(loc.filename);
      return 0;
    } catch(e) { return 10; }
  },

  access: async function(name) {
    if (!_useOpfs) return 0;
    try {
      var loc = await _getDir(name);
      await loc.dir.getFileHandle(loc.filename, {create: false});
      return 1;
    } catch(e) { return 0; }
  }
};

// ── WASM module setup ─────────────────────────────────────────

var Module;
var _api;
var _dbPtr = 0;
var _currentFilename = null;
var _currentKey = null;
var _shredOnClose = false;

async function init() {
  try {
    _opfsRoot = await navigator.storage.getDirectory();
    _useOpfs = true;
  } catch(e) {
    _useOpfs = false;
  }

  Module = await initSqlcipher({
    _opfs_close:    function(hid)              { _vfs.close(hid); },
    _opfs_read:     function(hid, p, n, off)   { return _vfs.read(hid, p, n, off); },
    _opfs_write:    function(hid, p, n, off)   { return _vfs.write(hid, p, n, off); },
    _opfs_sync:     function(hid)              { return _vfs.sync(hid); },
    _opfs_filesize: function(hid)              { return _vfs.filesize(hid); },
    _opfs_truncate: function(hid, sz)          { return _vfs.truncate(hid, sz); },
    _opfs_delete:   function()                 { return 0; },
    _opfs_access:   function()                 { return 0; }
  });

  if (!_useOpfs) {
    Module._opfs_open_sync = function(name) {
      return _vfs.openIdb(name);
    };
  }

  _api = {
    open:       Module.cwrap("wasm_db_open",           "number",  ["string"]),
    close:      Module.cwrap("wasm_db_close",           null,      ["number"]),
    exec:       Module.cwrap("wasm_db_exec",            "number",  ["number", "string"]),
    errmsg:     Module.cwrap("wasm_db_errmsg",          "string",  ["number"]),
    changes:    Module.cwrap("wasm_db_changes",         "number",  ["number"]),
    key:        Module.cwrap("wasm_db_key",             "number",  ["number", "string"]),
    prepare:    Module.cwrap("wasm_db_prepare",         "number",  ["number", "string"]),
    finalize:   Module.cwrap("wasm_stmt_finalize",      null,      ["number"]),
    reset:      Module.cwrap("wasm_stmt_reset",         null,      ["number"]),
    step:       Module.cwrap("wasm_stmt_step",          "number",  ["number"]),
    bind_text:  Module.cwrap("wasm_stmt_bind_text",     null,      ["number", "number", "string"]),
    bind_int:   Module.cwrap("wasm_stmt_bind_int",      null,      ["number", "number", "number"]),
    bind_dbl:   Module.cwrap("wasm_stmt_bind_double",   null,      ["number", "number", "number"]),
    bind_null:  Module.cwrap("wasm_stmt_bind_null",     null,      ["number", "number"]),
    columns:    Module.cwrap("wasm_stmt_columns",       "number",  ["number"]),
    colname:    Module.cwrap("wasm_stmt_colname",       "string",  ["number", "number"]),
    coltype:    Module.cwrap("wasm_stmt_coltype",       "number",  ["number", "number"]),
    col_int:    Module.cwrap("wasm_stmt_int",           "number",  ["number", "number"]),
    col_dbl:    Module.cwrap("wasm_stmt_double",        "number",  ["number", "number"]),
    col_text:   Module.cwrap("wasm_stmt_text",          "string",  ["number", "number"]),
    opfs_init:  Module.cwrap("sqlite3_opfs_init",       "number",  [])
  };
}

// ── SQL helpers ───────────────────────────────────────────────

function _bind(stmt, args) {
  if (!args) return;
  for (var i = 0; i < args.length; i++) {
    var v = args[i], idx = i + 1;
    if (v === null || v === undefined)       _api.bind_null(stmt, idx);
    else if (typeof v === "number") {
      if (v === (v | 0) && v >= -2147483648 && v <= 2147483647)
           _api.bind_int(stmt, idx, v);
      else _api.bind_dbl(stmt, idx, v);
    }
    else if (typeof v === "boolean")         _api.bind_int(stmt, idx, v ? 1 : 0);
    else                                     _api.bind_text(stmt, idx, String(v));
  }
}

function _query(sql, bind) {
  var stmt = _api.prepare(_dbPtr, sql);
  if (!stmt) throw new Error(_api.errmsg(_dbPtr));
  _bind(stmt, bind);
  var cols = _api.columns(stmt);
  var names = [], rows = [];
  for (var i = 0; i < cols; i++) names.push(_api.colname(stmt, i));
  while (_api.step(stmt)) {
    var row = {};
    for (var j = 0; j < cols; j++) {
      var t = _api.coltype(stmt, j);
      if      (t === 1) row[names[j]] = _api.col_int(stmt, j);
      else if (t === 2) row[names[j]] = _api.col_dbl(stmt, j);
      else if (t === 3) row[names[j]] = _api.col_text(stmt, j);
      else              row[names[j]] = null;
    }
    rows.push(row);
  }
  _api.finalize(stmt);
  return {names: names, rows: rows};
}

/** Checkpoint WAL and flush dirty pages of the main DB to IndexedDB. */
async function _checkpoint_and_flush() {
  if (_useOpfs || !_dbPtr || !_currentFilename) return;
  _api.exec(_dbPtr, "PRAGMA wal_checkpoint(TRUNCATE)");
  var hid = Module._opfs_preopen && Module._opfs_preopen[_currentFilename];
  if (hid === undefined) return;
  var h = _handles[hid];
  if (!h || h.type !== "idb") return;
  await _pageStore.flush(_currentFilename, h.buf, h.dirty);
  h.dirty.clear();
}

/** Overwrite all stored data with random bytes, close DB, delete file. */
async function _shred() {
  var sfn = _currentFilename;
  if (!sfn) return;

  // Capture handle info BEFORE close (close nulls handles via VFS xClose)
  var shid = Module._opfs_preopen && Module._opfs_preopen[sfn];
  var sh = shid !== undefined ? _handles[shid] : null;
  var handleType = sh ? sh.type : null;
  var fileSize = 0;
  if (handleType === "opfs") fileSize = sh.sah.getSize();
  else if (handleType === "idb") fileSize = sh.buf.length;

  // Close the database (nulls handles)
  if (_dbPtr) { _api.close(_dbPtr); _dbPtr = 0; }

  if (handleType === "opfs") {
    // Reopen OPFS file, overwrite with random, delete
    try {
      var loc = await _getDir(sfn);
      var fh = await loc.dir.getFileHandle(loc.filename, {create: false});
      var sah = await fh.createSyncAccessHandle();
      var rnd = new Uint8Array(fileSize);
      crypto.getRandomValues(rnd);
      sah.write(rnd, {at: 0});
      sah.flush();
      sah.close();
      await loc.dir.removeEntry(loc.filename);
    } catch(e) { /* file may already be gone */ }
  } else if (handleType === "idb") {
    // Overwrite IndexedDB blocks with random, then delete
    var numBlocks = Math.ceil(fileSize / _pageStore.BLOCK);
    var allDirty = new Set();
    for (var bi = 0; bi < numBlocks; bi++) allDirty.add(bi);
    var rndBuf = new Uint8Array(fileSize);
    crypto.getRandomValues(rndBuf);
    await _pageStore.flush(sfn, rndBuf, allDirty);
    await _pageStore.deleteFile(sfn);
  }

  _currentFilename = null;
  _currentKey = null;
  _shredOnClose = false;
}

/** Pre-open a handle and register it for the C VFS. */
function _preopen(filename, hid) {
  if (!Module._opfs_preopen) Module._opfs_preopen = {};
  Module._opfs_preopen[filename] = hid;
}

// ── Message handler ───────────────────────────────────────────

async function handleMessage(msg) {
  var id = msg.id;
  try {
    switch (msg.type) {

      case "init":
        await init();
        _api.opfs_init();
        postMessage({id: id, ok: true, mode: _useOpfs ? "opfs" : "indexeddb"});
        break;

      case "open": {
        if (_dbPtr) { _api.close(_dbPtr); _dbPtr = 0; }
        _currentFilename = msg.filename;
        _currentKey = msg.key || null;

        var hid;
        if (_useOpfs) {
          hid = await _vfs.openOpfs(msg.filename, 0x06);
        } else {
          var bytes = await _pageStore.load(msg.filename);
          hid = _vfs.openIdb(msg.filename, bytes);
        }
        _preopen(msg.filename, hid);

        _dbPtr = _api.open(msg.filename);
        if (msg.key) _api.key(_dbPtr, msg.key);
        postMessage({id: id, ok: true});
        break;
      }

      case "exec": {
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
      }

      case "select": {
        var result = _query(msg.sql, msg.bind);
        postMessage({id: id, ok: true, rows: result.rows, names: result.names});
        break;
      }

      case "save":
        await _checkpoint_and_flush();
        postMessage({id: id, ok: true});
        break;

      case "export": {
        _api.exec(_dbPtr, "PRAGMA wal_checkpoint(TRUNCATE)");
        var ehid = Module._opfs_preopen[_currentFilename];
        var eh = _handles[ehid];
        var out;
        if (eh.type === "opfs") {
          var sz = eh.sah.getSize();
          out = new Uint8Array(sz);
          eh.sah.read(out, {at: 0});
        } else {
          out = new Uint8Array(eh.buf);          // copy
        }
        postMessage({id: id, ok: true, bytes: out}, [out.buffer]);
        break;
      }

      case "import": {
        if (_dbPtr) { _api.close(_dbPtr); _dbPtr = 0; }
        var fn = msg.filename || _currentFilename;
        var raw = new Uint8Array(msg.bytes);
        var ihid;

        if (_useOpfs) {
          ihid = await _vfs.openOpfs(fn, 0x06);
          _handles[ihid].sah.truncate(0);
          _handles[ihid].sah.write(raw, {at: 0});
          _handles[ihid].sah.flush();
        } else {
          await _pageStore.storeBlob(fn, raw);
          ihid = _vfs.openIdb(fn, raw);
        }
        _preopen(fn, ihid);

        _currentFilename = fn;
        _dbPtr = _api.open(fn);
        if (_currentKey) _api.key(_dbPtr, _currentKey);
        postMessage({id: id, ok: true});
        break;
      }

      case "shred":
        await _shred();
        postMessage({id: id, ok: true});
        break;

      case "shredOnClose":
        _shredOnClose = true;
        postMessage({id: id, ok: true});
        break;

      case "close":
        if (_dbPtr) {
          if (_shredOnClose) {
            await _shred();
          } else {
            await _checkpoint_and_flush();
            _api.close(_dbPtr);
            _dbPtr = 0;
          }
        }
        postMessage({id: id, ok: true});
        break;

      default:
        throw new Error("Unknown message type: " + msg.type);
    }
  } catch(e) {
    postMessage({id: id, ok: false, error: e.message});
  }
}

onmessage = function(e) { handleMessage(e.data); };
