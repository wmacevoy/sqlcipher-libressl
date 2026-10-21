// crash-worker.js — the real worker, stopped at a chosen VFS write (test only, REQ8/DUR-2)
//
// Loads the real worker through backend-worker.js (so ?backend= still selects the backend)
// and wraps the _opfs_write callback the VFS calls for every write, main file or journal.
// Two extra messages, which carry no id and get no reply through the unified API:
//   {type: "crash-arm", killAt: N}  start counting writes from here; at write N, before it
//                                   happens, post {crash: "dying", write, name, offset, bytes}
//                                   and spin until the page terminates this worker. N = 0
//                                   only counts.
//   {type: "crash-count"}           post {crash: "count", writes, log}: the writes since arming.
// Spinning, not self.close(): close() lets the current task run on, so SQLite would carry on
// writing; a spinning worker stops exactly at write N, and terminate() still ends it.
//
// It reaches into the worker's globals (initSqlcipher, _handles, onmessage); a rename there
// breaks this wrapper, and the crash test fails.

var _crash = {armed: false, killAt: 0, writes: 0, log: []};

importScripts("backend-worker.js");

var _initSqlcipher = initSqlcipher;
initSqlcipher = function(config) {
  var write = config._opfs_write;
  config._opfs_write = function(hid, p, n, offset) {
    if (_crash.armed) {
      _crash.writes++;
      var h = _handles[hid];
      var w = {write: _crash.writes, name: h ? h.name : "?", offset: offset, bytes: n};
      _crash.log.push(w);
      if (_crash.writes === _crash.killAt) {
        postMessage(Object.assign({crash: "dying"}, w));
        for (;;) {}
      }
    }
    return write(hid, p, n, offset);
  };
  return _initSqlcipher(config);
};

var _onmessage = onmessage;
onmessage = function(e) {
  var msg = e.data;
  if (msg && msg.type === "crash-arm") {
    _crash = {armed: true, killAt: msg.killAt, writes: 0, log: []};
  } else if (msg && msg.type === "crash-count") {
    postMessage({crash: "count", writes: _crash.writes, log: _crash.log});
  } else {
    _onmessage(e);
  }
};
