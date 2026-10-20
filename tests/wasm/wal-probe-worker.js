// wal-probe-worker.js — the real worker, plus a probe for the -wal's shred (test only)
//
// shred() overwrites the stored -wal and then removes it, so from outside only the
// removal shows. This wrapper loads the real worker (through backend-worker.js) and intercepts OPFS
// removeEntry for names ending in "-wal", to read the file's bytes first and post them
// on the "wal-probe" BroadcastChannel as {name, bytes} (or {name, error}).

var _walProbe = new BroadcastChannel("wal-probe");

var _removeEntry = FileSystemDirectoryHandle.prototype.removeEntry;
FileSystemDirectoryHandle.prototype.removeEntry = async function(name, opts) {
  if (/-wal$/.test(name)) {
    try {
      var file = await (await this.getFileHandle(name)).getFile();
      _walProbe.postMessage({name: name, bytes: new Uint8Array(await file.arrayBuffer())});
    } catch (e) {
      _walProbe.postMessage({name: name, error: String(e)});
    }
  }
  return _removeEntry.call(this, name, opts);
};

importScripts("backend-worker.js");   // ?backend= still selects the backend
