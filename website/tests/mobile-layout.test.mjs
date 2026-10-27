import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const cssPath = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../dist/modules/asset-preview.css");
const modulePath = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../dist/modules/asset-preview.js");

test("narrow preview controls use a grid and floating cards stay in the scene", async () => {
  const css = await readFile(cssPath, "utf8");
  const mobile = css.slice(css.lastIndexOf("@media(max-width:430px)"));
  assert.match(mobile, /\.preview-object-row\{display:grid;grid-template-columns:repeat\(3,minmax\(0,1fr\)\)/);
  assert.match(mobile, /\.preview-object-row #reset-object\{grid-column:3/);
  assert.match(mobile, /\.float-card-left\{left:8px;top:47px;bottom:auto/);
  assert.match(mobile, /\.float-card-right\{right:8px;top:47px/);
});

test("middle-button drag pans the view and hot reload restores its offset", async () => {
  const source = await readFile(modulePath, "utf8");
  const css = await readFile(cssPath, "utf8");
  assert.match(source, /e\.button===1\?"pan":"orbit"/);
  assert.match(source, /panX-=dx\/Math\.max\(1,canvas\.clientWidth\)/);
  assert.match(source, /panY\+=dy\/Math\.max\(1,canvas\.clientHeight\)/);
  assert.match(source, /snapshot: \(\) => \(\{ qualityMode, yaw, pitch, panX, panY,/);
  const page = await readFile(path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../dist/index.html"), "utf8");
  assert.match(page, /가운데 버튼 드래그/);
});
