// run-tests.mjs — Run WASM tests in headless Chromium via Playwright, once per backend
//
// Usage:  node tests/wasm/run-tests.mjs
//
// Expects sqlcipher.js, sqlcipher.wasm, sqlcipher-api.js, sqlcipher-worker.js, sqlcipher-oo1.js,
// sqlcipher-4.0-testkey.db (from sqlcipher-resources/), oo1-api.md (from docs/), and test.html to be
// co-located in the same directory as this script.
//
// test.html runs once with ?backend=opfs and once with ?backend=indexeddb, each in a fresh
// browser context so neither run sees the other's storage. A run passes only if every test
// passes and the page reports the mode it was asked for.

import { chromium } from "playwright";
import { createServer } from "http";
import { readFileSync, existsSync } from "fs";
import { join, extname, dirname } from "path";
import { fileURLToPath } from "url";

var __dirname = dirname(fileURLToPath(import.meta.url));

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

// Runs test.html on one backend; returns true if it passed.
async function runBackend(browser, port, backend) {
  var tag = "[" + backend + "]";
  var context = await browser.newContext();
  var page = await context.newPage();

  page.on("console", function(msg) { console.log(tag, "[browser]", msg.text()); });
  page.on("pageerror", function(err) { console.error(tag, "[browser error]", err.message); });

  await page.goto("http://localhost:" + port + "/test.html?backend=" + backend);

  try {
    await page.waitForFunction(function() {
      var el = document.getElementById("result");
      return el && (el.dataset.status === "pass" || el.dataset.status === "fail");
    }, {timeout: 60000});
  } catch(e) {
    console.error(tag, "Timed out waiting for tests");
    console.log(await page.textContent("#log"));
    await context.close();
    return false;
  }

  var status = await page.getAttribute("#result", "data-status");
  var passed = await page.getAttribute("#result", "data-passed");
  var total  = await page.getAttribute("#result", "data-total");
  var failed = await page.getAttribute("#result", "data-failed");
  var mode   = await page.getAttribute("#result", "data-mode");
  var log    = await page.textContent("#log");
  await context.close();

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

  var browser = await chromium.launch();
  var failedBackends = [];
  for (var backend of BACKENDS) {
    if (!(await runBackend(browser, port, backend))) failedBackends.push(backend);
  }
  await browser.close();
  srv.close();

  var passedCount = BACKENDS.length - failedBackends.length;
  console.log("\nResult: " + passedCount + "/" + BACKENDS.length + " backends passed" +
              (failedBackends.length ? ", failed: " + failedBackends.join(", ") + " -- FAIL" : " -- PASS"));
  process.exit(failedBackends.length ? 1 : 0);
}

main().catch(function(e) { console.error(e); process.exit(2); });
