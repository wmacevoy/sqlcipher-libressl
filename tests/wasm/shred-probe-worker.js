// shred-probe-worker.js — the real worker, plus a probe for REQ1/SHR-1 (test only)
//
// shred() overwrites the stored file and then deletes it, so from outside only the
// deletion shows. This wrapper loads sqlcipher-worker.js unchanged and intercepts the
// two ways it deletes a file — OPFS removeEntry and the IndexedDB page store's
// deleteFile — to read the file's bytes first and post them on the "shred-probe"
// BroadcastChannel as {backend, name, bytes}.
//
// It reaches into the worker's _pageStore global; a rename there breaks this probe,
// and the test fails waiting for it.

var _probe = new BroadcastChannel("shred-probe");

var _removeEntry = FileSystemDirectoryHandle.prototype.removeEntry;
FileSystemDirectoryHandle.prototype.removeEntry = async function(name, opts) {
  try {
    var file = await (await this.getFileHandle(name)).getFile();
    _probe.postMessage({backend: "opfs", name: name, bytes: new Uint8Array(await file.arrayBuffer())});
  } catch (e) {
    _probe.postMessage({backend: "opfs", name: name, error: String(e)});
  }
  return _removeEntry.call(this, name, opts);
};

importScripts("sqlcipher-worker.js");

var _deleteFile = _pageStore.deleteFile;
_pageStore.deleteFile = async function(filename) {
  var bytes = await _pageStore.load(filename);
  _probe.postMessage({backend: "indexeddb", name: filename, bytes: bytes});
  return _deleteFile.call(_pageStore, filename);
};
