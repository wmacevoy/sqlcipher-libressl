// ============================================================
// sqlcipher-api.js — Unified SQLCipher API
//
// Auto-detects best persistence (OPFS → IndexedDB page cache).
// Single async interface regardless of backend.
//
// Usage:
//   var db = await SQLCipher.open({filename: "app.db", key: "secret"});
//   db.mode                                     // "opfs" or "indexeddb"
//   await db.exec("CREATE TABLE t (x TEXT)");
//   await db.exec("INSERT INTO t VALUES (?)", ["hi"]);
//   var rows = await db.select("SELECT * FROM t");  // [{x:"hi"}]
//   await db.save();     // indexeddb: flush dirty pages. opfs: no-op.
//   var blob = await db.export();   // Uint8Array — full encrypted DB
//   await db.import(blob);          // restore from blob
//   await db.shredOnClose();        // flag: close() will shred instead of save
//   await db.shred();               // overwrite with random + delete (immediate)
//   await db.close();               // auto-saves on indexeddb (or shreds if flagged)
//
// Requires (same directory or adjust workerUrl):
//   sqlcipher.js, sqlcipher.wasm, sqlcipher-worker.js
// ============================================================

var SQLCipher = (function() {
  "use strict";

  // ── Handle (wraps Worker) ────────────────────────────────────

  function Handle(worker, mode, filename) {
    this._worker = worker;
    this._pending = {};
    this._id = 0;
    this.mode = mode;
    this.filename = filename;

    var self = this;
    worker.onmessage = function(e) {
      var p = self._pending[e.data.id];
      if (!p) return;
      delete self._pending[e.data.id];
      if (e.data.ok) p.resolve(e.data);
      else p.reject(new Error(e.data.error));
    };
  }

  Handle.prototype._send = function(msg, transfer) {
    var self = this;
    return new Promise(function(resolve, reject) {
      msg.id = ++self._id;
      self._pending[msg.id] = {resolve: resolve, reject: reject};
      if (transfer) self._worker.postMessage(msg, transfer);
      else self._worker.postMessage(msg);
    });
  };

  /**
   * Execute SQL (DDL, INSERT, UPDATE, DELETE).
   * @param {string} sql
   * @param {Array}  [bind]  positional parameters
   * @returns {Promise<{changes:number}>}
   */
  Handle.prototype.exec = function(sql, bind) {
    return this._send({type: "exec", sql: sql, bind: bind})
      .then(function(r) { return {changes: r.changes}; });
  };

  /**
   * Query rows as objects.
   * @param {string} sql
   * @param {Array}  [bind]
   * @returns {Promise<Object[]>}
   */
  Handle.prototype.select = function(sql, bind) {
    return this._send({type: "select", sql: sql, bind: bind})
      .then(function(r) { return r.rows; });
  };

  /**
   * Persist to storage.
   * OPFS: no-op (already durable per COMMIT).
   * IndexedDB: checkpoint WAL + flush dirty 4KB pages.
   * @returns {Promise<void>}
   */
  Handle.prototype.save = function() {
    return this._send({type: "save"}).then(function() {});
  };

  /**
   * Export full encrypted database as a Uint8Array blob.
   * Suitable for download, backup, or transport.
   * @returns {Promise<Uint8Array>}
   */
  Handle.prototype.export = function() {
    return this._send({type: "export"})
      .then(function(r) { return r.bytes; });
  };

  /**
   * Import (restore) a database from an encrypted blob.
   * Replaces current database contents; keeps same filename and key.
   * @param {Uint8Array|ArrayBuffer} bytes
   * @returns {Promise<void>}
   */
  Handle.prototype.import = function(bytes) {
    var u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    var copy = u8.slice().buffer;            // safe copy for transfer
    return this._send(
      {type: "import", bytes: copy, filename: this.filename}, [copy]
    ).then(function() {});
  };

  /**
   * Set flag: close() will shred instead of save.
   * Call early to ensure destruction even if close is called from cleanup code.
   * @returns {Promise<void>}
   */
  Handle.prototype.shredOnClose = function() {
    return this._send({type: "shredOnClose"}).then(function() {});
  };

  /**
   * Securely destroy the database.  Overwrites all stored blocks with
   * random data, flushes to storage, then deletes.  Handle is unusable after.
   * @returns {Promise<void>}
   */
  Handle.prototype.shred = function() {
    var self = this;
    return this._send({type: "shred"}).then(function() {
      self._worker.terminate();
      self._worker = null;
    });
  };

  /**
   * Close the database. IndexedDB: auto-saves before closing.
   * After close the handle is unusable.
   * @returns {Promise<void>}
   */
  Handle.prototype.close = function() {
    var self = this;
    return this._send({type: "close"}).then(function() {
      self._worker.terminate();
      self._worker = null;
    });
  };

  // ── Factory ──────────────────────────────────────────────────

  /**
   * Open (or create) an encrypted database.
   *
   * @param {Object}  opts
   * @param {string}  [opts.filename="/app.db"]  database name
   * @param {string}  [opts.key]                 encryption passphrase
   * @param {string}  [opts.workerUrl="sqlcipher-worker.js"]
   * @returns {Promise<Handle>}
   */
  async function open(opts) {
    if (!opts) opts = {};
    var filename = opts.filename || "/app.db";
    if (filename.charAt(0) !== "/") filename = "/" + filename;
    var key = opts.key;
    var workerUrl = opts.workerUrl || "sqlcipher-worker.js";

    var worker = new Worker(workerUrl);
    var handle = new Handle(worker, "pending", filename);

    var initResult = await handle._send({type: "init"});
    handle.mode = initResult.mode;

    await handle._send({type: "open", filename: filename, key: key});
    return handle;
  }

  return {open: open};
})();

if (typeof module !== "undefined" && module.exports) {
  module.exports = SQLCipher;
}
