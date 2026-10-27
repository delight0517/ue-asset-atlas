import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const source = await readFile(path.join(here, "../dist/portfolio/portfolio.js"), "utf8");
const base = path.join(here, "../dist/portfolio");
const app = await import(`file://${path.join(base, "portfolio.js").replaceAll("\\", "/")}`);

const sample = {
  schema: "blacklion.asset-archive.web-portfolio.v1", title: "Selected work", loading: "per-asset",
  assets: [{ id: "sample-crate", name: "Sample Crate", model: "assets/sample-crate/model.glb", poster: "assets/sample-crate/poster.webp", model_bytes: 1200 }]
};

test("validates the per-object manifest and formats optimized payload sizes", () => {
  assert.equal(app.validatePortfolioManifest(sample), sample);
  assert.equal(app.formatBytes(1200), "1 KiB");
  assert.equal(app.formatBytes(2 * 1024 * 1024), "2.0 MiB");
});

test("rejects external, mismatched, and duplicate object paths", () => {
  assert.throws(() => app.validatePortfolioManifest({ ...sample, assets: [{ ...sample.assets[0], model: "https://example.com/model.glb" }] }), /로컬 미디어 경로/);
  assert.throws(() => app.validatePortfolioManifest({ ...sample, assets: [{ ...sample.assets[0], model: "assets/other/model.glb" }] }), /오브젝트 폴더를 벗어났습니다/);
  assert.throws(() => app.validatePortfolioManifest({ ...sample, assets: [sample.assets[0], sample.assets[0]] }), /중복된 오브젝트 ID/);
});

test("does not request model URLs until the user clicks the explicit viewer action", () => {
  const validation = source.indexOf("const response = await fetch(\"./portfolio.json\"");
  const modelAssignment = source.indexOf("viewer.src = new URL(asset.model, manifestUrl).href");
  const handlerStart = source.indexOf("async function loadSelectedModel()");
  const handlerEnd = source.indexOf('loadButton.addEventListener("click", loadSelectedModel)', handlerStart);
  assert.ok(validation >= 0 && handlerStart >= 0 && modelAssignment > handlerStart && modelAssignment < handlerEnd);
  assert.equal((source.match(/viewer\.src = new URL\(asset\.model/g) || []).length, 1);
  assert.match(source, /poster\.loading = "lazy"/);
  assert.match(source, /currentViewer\.removeAttribute\("src"\)/);
  assert.match(source, /vendor\/model-viewer\.min\.js/);
  assert.doesNotMatch(source, /https?:\/\//);
});

test("ships a Korean accessible page and Korean status/error copy", async () => {
  const html = await readFile(path.join(base, "index.html"), "utf8");
  assert.match(html, /<html lang="ko">/);
  assert.match(html, /role="status" aria-live="polite"/);
  assert.match(html, /3D 미리보기 불러오기/);
  assert.match(source, /로컬 3D 뷰어를 불러오지 못했습니다/);
  assert.match(source, /포트폴리오 정보를 불러오지 못했습니다/);
});
