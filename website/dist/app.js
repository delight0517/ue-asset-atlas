import { ActivityTracker } from "/runtime/activity.js";
import { HotReloadManager } from "/runtime/hot-reload.js";
import { Preferences } from "/runtime/preferences.js";

const recoveryKey = "asset-atlas:dev-recovery:v1";
const isDevRequested = new URLSearchParams(location.search).get("dev") === "1" && ["127.0.0.1", "localhost", "::1"].includes(location.hostname);
const statusNode = document.getElementById("module-update-status");
const activity = new ActivityTracker();
const preferences = new Preferences();
const context = Object.freeze({ activity, preferences });
let recovery = readRecovery();
let lastManifest = null;
let hmrServer = false;
let reloadScheduled = false;
let pollBusy = false;
const failedStyleHashes = new Set();

function readRecovery() {
  try { return JSON.parse(sessionStorage.getItem(recoveryKey) || "null"); }
  catch { return null; }
}

function storeRecovery(value) {
  try { sessionStorage.setItem(recoveryKey, JSON.stringify(value)); }
  catch { report("Session restore storage is unavailable; a full page reload may lose preview settings."); }
}

function clearRecovery() {
  try { sessionStorage.removeItem(recoveryKey); } catch { /* session storage is optional */ }
  recovery = null;
}

function report(message) {
  if (statusNode) {
    statusNode.hidden = false;
    statusNode.textContent = message;
    statusNode.title = message;
  }
}

async function fetchJson(path) {
  const response = await fetch(path, { cache: "no-store", credentials: "same-origin" });
  if (!response.ok) throw new Error(`${path} returned HTTP ${response.status}`);
  return response.json();
}

async function importFeature(definition, hash) {
  const source = new URL(definition.src, location.href);
  if (source.origin !== location.origin || !source.pathname.startsWith("/modules/")) throw new Error(`Feature source must be a same-origin module path: ${definition.src}`);
  source.searchParams.set("v", hash);
  return import(source.href);
}

const manager = new HotReloadManager({
  context,
  importFeature,
  canApply: () => !document.hidden && activity.isIdle,
  report,
  persistState: (id, state) => {
    const features = { ...(recovery?.features || {}), [id]: state };
    storeRecovery({ ...(recovery || {}), features });
  }
});

function shellState() {
  const active = document.activeElement;
  return {
    features: manager.snapshotAll(),
    scrollX: window.scrollX,
    scrollY: window.scrollY,
    focusId: active?.id || null,
    timestamp: Date.now()
  };
}

async function updateStyles(styles) {
  for (const style of styles || []) {
    const styleUrl = new URL(style.src, location.href);
    if (styleUrl.origin !== location.origin || !(styleUrl.pathname === "/styles.css" || styleUrl.pathname.startsWith("/modules/"))) {
      report(`Ignored style outside the local feature allowlist: ${style.src}`);
      continue;
    }
    const current = document.querySelector(`link[data-module-style="${CSS.escape(style.id)}"]`);
    if (!current || current.dataset.hash === style.hash) continue;
    if (failedStyleHashes.has(`${style.id}:${style.hash}`)) continue;
    const response = await fetch(style.src, { cache: "no-store", credentials: "same-origin" });
    if (!response.ok) { failedStyleHashes.add(`${style.id}:${style.hash}`); report(`Style update failed for ${style.id}; previous style remains active.`); continue; }
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.dataset.moduleStyle = style.id;
    link.dataset.hash = style.hash;
    styleUrl.searchParams.set("v", style.hash);
    link.href = styleUrl.href;
    const loaded = new Promise(resolve => {
      link.onload = () => resolve(true);
      link.onerror = () => resolve(false);
    });
    document.head.append(link);
    if (!await loaded) { link.remove(); failedStyleHashes.add(`${style.id}:${style.hash}`); report(`Style update failed for ${style.id}; previous style remains active.`); continue; }
    current.remove();
    report(`Live style update applied: ${style.id}.`);
  }
}

async function refreshModules(manifest) {
  if (!manager.canApplyFeatures()) return false;
  await updateStyles(manifest.styles);
  const result = await manager.apply(manifest);
  if (result.status === "updated") {
    clearRecovery();
    report(`Live update applied: ${result.changed.join(", ")}.`);
    return true;
  }
  return false;
}

function scheduleShellReload(manifest) {
  if (reloadScheduled || !manager.canApplyFeatures()) return false;
  reloadScheduled = true;
  storeRecovery(shellState());
  report("Core or page structure changed; saving preview state and reloading safely…");
  lastManifest = manifest;
  setTimeout(() => location.reload(), 120);
  return true;
}

async function pollForChanges() {
  if (!hmrServer || pollBusy || document.hidden) return;
  pollBusy = true;
  try {
    const manifest = await fetchJson("/__dev__/manifest");
    if (statusNode?.textContent.startsWith("Live update check failed;")) {
      statusNode.textContent = "LOCAL HOT RELOAD ON";
      statusNode.title = statusNode.textContent;
    }
    if (!lastManifest) { lastManifest = manifest; return; }
    if (manifest.shellHash !== lastManifest.shellHash) {
      if (scheduleShellReload(manifest)) return;
      report(`Core update waiting for a safe moment${activity.activeLabels.length ? ` (${activity.activeLabels.join(", ")})` : " (finish the current control interaction)"}.`);
      return;
    }
    const changed = JSON.stringify(manifest.modules.map(({ id, hash }) => [id, hash])) !== JSON.stringify(lastManifest.modules.map(({ id, hash }) => [id, hash])) || JSON.stringify(manifest.styles.map(({ id, hash }) => [id, hash])) !== JSON.stringify(lastManifest.styles.map(({ id, hash }) => [id, hash]));
    if (changed) {
      if (manager.canApplyFeatures()) {
        const applied = await refreshModules(manifest);
        if (applied) lastManifest = manifest;
        else if (manifest.styles.every(style => !style.hash || document.querySelector(`link[data-module-style="${CSS.escape(style.id)}"]`)?.dataset.hash === style.hash)) lastManifest = manifest;
      } else {
        report(`Feature update waiting for a safe moment${activity.activeLabels.length ? ` (${activity.activeLabels.join(", ")})` : " (finish the current control interaction)"}.`);
      }
    }
  } catch (error) {
    report(`Live update check failed; the current app remains active. ${error.message}`);
  } finally { pollBusy = false; }
}

async function boot() {
  let manifest = await fetchJson("/modules.json");
  if (isDevRequested) {
    try {
      const liveManifest = await fetchJson("/__dev__/manifest");
      if (liveManifest.dev) { manifest = liveManifest; lastManifest = liveManifest; hmrServer = true; }
    } catch { hmrServer = false; }
  }
  if (manifest.contractVersion !== 1 || !Array.isArray(manifest.modules)) throw new Error("Feature manifest contract mismatch.");
  const states = recovery?.features || {};
  for (const definition of manifest.modules) {
    try { await manager.register(definition, definition.hash, states[definition.id]); }
    catch (error) { report(`Feature ${definition.id} could not start; other features remain available. ${error.message}`); }
  }
  if (recovery) {
    window.scrollTo(recovery.scrollX || 0, recovery.scrollY || 0);
    if (recovery.focusId) document.getElementById(recovery.focusId)?.focus({ preventScroll: true });
    clearRecovery();
  }
  await updateStyles(manifest.styles);
  if (statusNode) {
    statusNode.hidden = false;
    statusNode.textContent = hmrServer ? "LOCAL HOT RELOAD ON" : "MODULES ACTIVE";
    statusNode.title = statusNode.textContent;
  }
  if (hmrServer) {
    setInterval(pollForChanges, 700);
    document.addEventListener("visibilitychange", () => { if (!document.hidden) pollForChanges(); });
  }
}

boot().catch(error => {
  report(`App startup failed safely. ${error.message}`);
  console.error("UE Asset Atlas module boot failed", error);
});
