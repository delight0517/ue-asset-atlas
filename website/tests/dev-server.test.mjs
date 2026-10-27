import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

test("dev server serves the actual static build and a non-cached content hash manifest on loopback", async () => {
  const child = spawn("python", [path.join(root, "dev_server.py"), "--port", "0"], { stdio: ["ignore", "pipe", "pipe"] });
  let output = "";
  child.stdout.setEncoding("utf8").on("data", chunk => { output += chunk; });
  child.stderr.setEncoding("utf8").on("data", chunk => { output += chunk; });
  try {
    let port;
    for (let attempt = 0; attempt < 40 && !port; attempt++) {
      const match = output.match(/http:\/\/127\.0\.0\.1:(\d+)\/\?dev=1/);
      if (match) port = Number(match[1]);
      else await delay(50);
    }
    assert.ok(port, `server should report its assigned loopback port; output=${output}`);
    const manifestResponse = await fetch(`http://127.0.0.1:${port}/__dev__/manifest`);
    assert.equal(manifestResponse.status, 200);
    assert.equal(manifestResponse.headers.get("cache-control"), "no-store");
    const manifest = await manifestResponse.json();
    assert.equal(manifest.dev, true);
    assert.deepEqual(manifest.modules.map(module => module.id), ["asset-preview", "manifest-copy"]);
    assert.deepEqual(manifest.styles.map(style => style.id), ["site-theme", "asset-preview"]);
    const sourceResponse = await fetch(`http://127.0.0.1:${port}/modules/manifest-copy.js`);
    const source = await sourceResponse.text();
    const hash = createHash("sha256").update(source).digest("hex").slice(0, 20);
    assert.equal(manifest.modules.find(module => module.id === "manifest-copy").hash, hash);
    const page = await fetch(`http://127.0.0.1:${port}/?dev=1`);
    assert.equal(page.status, 200);
    assert.match(await page.text(), /type="module"/);
    const rejected = spawn("python", [path.join(root, "dev_server.py"), "--host", "0.0.0.0", "--port", "0"], { stdio: "ignore" });
    const exitCode = await new Promise(resolve => rejected.once("close", resolve));
    assert.notEqual(exitCode, 0);
  } finally {
    child.kill();
    await new Promise(resolve => child.once("close", resolve));
  }
});
