// fail-worker.js — the real worker, made to fail one message (test only, REQ1/API-1)
//
// Loaded as fail-worker.js?fail=init or ?fail=open (with ?backend= as backend-worker.js reads
// it), it answers that message with an error instead of passing it on; ?fail=none passes all.
// While it runs it answers pings on the BroadcastChannel "worker-alive-<fail>": {ping: t} gets
// {pong: t}. So the page can tell whether a worker SQLCipher.open made still runs after the
// open failed.

var _fail = new URLSearchParams(self.location.search).get("fail") || "none";

var _alive = new BroadcastChannel("worker-alive-" + _fail);
_alive.onmessage = function(e) { if (e.data && e.data.ping) _alive.postMessage({pong: e.data.ping}); };

importScripts("backend-worker.js");

var _passOn = onmessage;
onmessage = function(e) {
  var msg = e.data;
  if (msg && msg.type === _fail) {
    postMessage({id: msg.id, ok: false, error: msg.type + " refused by fail-worker.js"});
  } else {
    _passOn(e);
  }
};
