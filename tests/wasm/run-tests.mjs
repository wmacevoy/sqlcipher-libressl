// run-tests.mjs — Run WASM tests in headless Chromium via Playwright
//
// Usage:  node tests/wasm/run-tests.mjs
//
// Expects sqlcipher.js, sqlcipher.wasm, sqlcipher-api.js, sqlcipher-worker.js,
// and test.html to be co-located in the same directory as this script.

import { chromium } from "playwright";
import { createServer } from "http";
import { readFileSync, existsSync } from "fs";
import { join, extname, dirname } from "path";
import { fileURLToPath } from "url";

var __dirname = dirname(fileURLToPath(import.meta.url));

var MIME = {
  ".html": "text/html",
  ".js":   "application/javascript",
  ".wasm": "application/wasm"
};

function serve(dir) {
  return new Promise(function(resolve) {
    var srv = createServer(function(req, res) {
      var file = join(dir, req.url === "/" ? "/test.html" : req.url);
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

async function main() {
  var srv = await serve(__dirname);
  var port = srv.address().port;
  console.log("Serving tests on port " + port);

  var browser = await chromium.launch();
  var page = await browser.newPage();

  page.on("console", function(msg) { console.log("[browser]", msg.text()); });
  page.on("pageerror", function(err) { console.error("[browser error]", err.message); });

  await page.goto("http://localhost:" + port + "/test.html");

  try {
    await page.waitForFunction(function() {
      var el = document.getElementById("result");
      return el && (el.dataset.status === "pass" || el.dataset.status === "fail");
    }, {timeout: 60000});
  } catch(e) {
    console.error("Timed out waiting for tests");
    console.log(await page.textContent("#log"));
    await browser.close();
    srv.close();
    process.exit(2);
  }

  var status = await page.getAttribute("#result", "data-status");
  var passed = await page.getAttribute("#result", "data-passed");
  var total  = await page.getAttribute("#result", "data-total");
  var failed = await page.getAttribute("#result", "data-failed");
  var log    = await page.textContent("#log");

  console.log(log);
  console.log("\nResult: " + passed + "/" + total + " passed, " + failed + " failed -- " + status.toUpperCase());

  await browser.close();
  srv.close();
  process.exit(status === "pass" ? 0 : 1);
}

main().catch(function(e) { console.error(e); process.exit(2); });
