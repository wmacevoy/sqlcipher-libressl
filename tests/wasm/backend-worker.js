// backend-worker.js — the real worker, on the backend the test asks for (test only)
//
// The worker uses OPFS whenever navigator.storage.getDirectory() succeeds, so a browser
// with OPFS never runs the IndexedDB backend. Loaded as backend-worker.js?backend=indexeddb,
// this wrapper makes getDirectory() reject before loading sqlcipher-worker.js unchanged,
// and the worker falls back to IndexedDB. Any other query (or none) changes nothing.
//
// Other test wrappers compose with it by importScripts("backend-worker.js") in place of
// importScripts("sqlcipher-worker.js"); self.location is still theirs, query included.

if (new URLSearchParams(self.location.search).get("backend") === "indexeddb") {
  StorageManager.prototype.getDirectory = function() {
    return Promise.reject(new DOMException("OPFS hidden by backend-worker.js", "SecurityError"));
  };
}

importScripts("sqlcipher-worker.js");
