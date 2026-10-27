import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const base = path.join(here, "../dist/portfolio-studio");
const source = await readFile(path.join(base, "studio.js"), "utf8");
const app = await import(`file://${path.join(base, "studio.js").replaceAll("\\", "/")}`);

const valid = {
  schema: "blacklion.asset-archive.web-portfolio.v1",
  title: "Local selection",
  assets: [{ id: "crate-1", name: "Crate", visibility: "private", model_glb: "objects/crate.glb", poster: "posters/crate.webp" }]
};

test("accepts only explicit selection files and serializes the visibility update", () => {
  assert.equal(app.validateSelection(valid), valid);
  valid.assets[0].visibility = "public";
  const saved = JSON.parse(app.serializeSelection(valid));
  assert.equal(saved.assets[0].visibility, "public");
  assert.match(app.serializeSelection(valid), /\n$/);
});

test("rejects malformed, duplicate, unsupported, or ambiguous visibility entries", () => {
  assert.throws(() => app.validateSelection({ ...valid, schema: "other" }), /스키마/);
  assert.throws(() => app.validateSelection({ ...valid, assets: [{ ...valid.assets[0], visibility: "public-ish" }] }), /공개 범위/);
  assert.throws(() => app.validateSelection({ ...valid, assets: [valid.assets[0], valid.assets[0]] }), /중복/);
  assert.throws(() => app.validateSelection({ ...valid, assets: [{ ...valid.assets[0], source: "private.fbx" }] }), /지원하지 않는/);
});

test("permits file editing only on explicit loopback HTTP origins", () => {
  assert.equal(app.isLocalOrigin({ protocol: "http:", hostname: "127.0.0.1" }), true);
  assert.equal(app.isLocalOrigin({ protocol: "http:", hostname: "localhost" }), true);
  assert.equal(app.isLocalOrigin({ protocol: "https:", hostname: "localhost" }), false);
  assert.equal(app.isLocalOrigin({ protocol: "https:", hostname: "delight0517.github.io" }), false);
});

test("uses a user-picked local JSON file and never calls an upload API", () => {
  assert.match(source, /showOpenFilePicker/);
  assert.match(source, /fileHandle\.createWritable\(\)/);
  assert.match(source, /link\.download = fileName/);
  assert.doesNotMatch(source, /fetch\s*\(|XMLHttpRequest|navigator\.sendBeacon/);
  assert.match(source, /if \(!local\) return/);
});
