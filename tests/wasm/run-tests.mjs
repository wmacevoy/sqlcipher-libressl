// run-tests.mjs — Run WASM tests via Playwright in Chromium, Firefox and WebKit, once per backend
//
// Usage:  node tests/wasm/run-tests.mjs
//
// Expects sqlcipher.js, sqlcipher.wasm, sqlcipher-api.js, sqlcipher-worker.js, sqlcipher-oo1.js,
// sqlcipher-4.0-testkey.db (from sqlcipher-resources/), oo1-api.md (from docs/), and test.html to be
// co-located in the same directory as this script.
//
// In each browser, test.html runs once with ?backend=opfs and once with ?backend=indexeddb, each
// in a fresh profile directory so no run sees another's storage. A run passes only if every test
// passes and the page reports the mode it was asked for. Every browser and backend must pass;
// a browser that fails to launch fails its runs.

import { chromium, firefox, webkit } from "playwright";
import { createServer } from "http";
import { readFileSync, existsSync, mkdtempSync, rmSync } from "fs";
import { tmpdir } from "os";
import { join, extname, dirname } from "path";
import { fileURLToPath } from "url";

var __dirname = dirname(fileURLToPath(import.meta.url));

var BROWSERS = {chromium: chromium, firefox: firefox, webkit: webkit};
var BACKENDS = ["opfs", "indexeddb"];

var MIME = {
  ".html": "text/html",
  ".js":   "application/javascript",
  ".wasm": "application/wasm"
};

function serve(dir) {
  return new Promise(function(resolve) {
    var srv = createServer(function(req, res) {
      var path = req.url.split("?")[0];
      var file = join(dir, path === "/" ? "/test.html" : path);
      if (!existsSync(file)) { res.writeHead(404); res.end(); return; }
      res.writeHead(200, {
        "Content-Type": MIME[extname(file)] || "application/octet-stream",
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp"
      });
      res.end(readFileSync(file));
    });
    srv.listen(0, function() { resolve(srv); });
  });
}

// Runs test.html in one browser on one backend; returns true if it passed.
async function runBackend(browserType, browserName, port, backend) {
  var tag = "[" + browserName + "/" + backend + "]";
  // A persistent context, not browser.newContext(): WebKit has no OPFS in an ephemeral context
  // (getDirectory() rejects with UnknownError, as in Safari private browsing), so the OPFS run
  // would silently become a second IndexedDB run.
  var profile = mkdtempSync(join(tmpdir(), "sqlcipher-wasm-" + browserName + "-"));
  var context = await browserType.launchPersistentContext(profile);
  try {
    var page = context.pages()[0] || await context.newPage();

    page.on("console", function(msg) { console.log(tag, "[browser]", msg.text()); });
    page.on("pageerror", function(err) { console.error(tag, "[browser error]", err.message); });

    await page.goto("http://localhost:" + port + "/test.html?backend=" + backend);
    console.log(tag, await page.evaluate(function() { return navigator.userAgent; }));

    try {
      await page.waitForFunction(function() {
        var el = document.getElementById("result");
        return el && (el.dataset.status === "pass" || el.dataset.status === "fail");
      }, {timeout: 60000});
    } catch(e) {
      console.error(tag, "Timed out waiting for tests");
      console.log(await page.textContent("#log"));
      console.log(tag + " Result: timed out -- FAIL");
      return false;
    }

    var status = await page.getAttribute("#result", "data-status");
    var passed = await page.getAttribute("#result", "data-passed");
    var total  = await page.getAttribute("#result", "data-total");
    var failed = await page.getAttribute("#result", "data-failed");
    var mode   = await page.getAttribute("#result", "data-mode");
    var log    = await page.textContent("#log");
  } finally {
    await context.close();
    rmSync(profile, {recursive: true, force: true});
  }

  var ok = status === "pass" && mode === backend;
  console.log("\n" + tag + "\n" + log);
  console.log(tag + " mode: " + mode + ", expected " + backend +
              (mode === backend ? "" : " -- WRONG BACKEND"));
  console.log(tag + " Result: " + passed + "/" + total + " passed, " + failed + " failed -- " +
              (ok ? "PASS" : "FAIL"));
  return ok;
}

async function main() {
  var srv = await serve(__dirname);
  var port = srv.address().port;
  console.log("Serving tests on port " + port);

  var runs = [];
  var failedRuns = [];
  for (var browserName of Object.keys(BROWSERS)) {
    for (var backend of BACKENDS) {
      var run = browserName + "/" + backend;
      runs.push(run);
      var ok = false;
      try {
        ok = await runBackend(BROWSERS[browserName], browserName, port, backend);
      } catch(e) {
        // A browser that is not installed or will not launch lands here.
        console.error("[" + run + "] " + e.message);
        console.log("[" + run + "] Result: error -- FAIL");
      }
      if (!ok) failedRuns.push(run);
    }
  }
  srv.close();

  console.log("\nResult: " + (runs.length - failedRuns.length) + "/" + runs.length +
              " browser/backend runs passed" +
              (failedRuns.length ? ", failed: " + failedRuns.join(", ") + " -- FAIL" : " -- PASS"));
  process.exit(failedRuns.length ? 1 : 0);
}

main().catch(function(e) { console.error(e); process.exit(2); });
