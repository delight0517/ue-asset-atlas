import test from "node:test";
import assert from "node:assert/strict";
import { HotReloadManager } from "../dist/runtime/hot-reload.js";
import { ActivityTracker } from "../dist/runtime/activity.js";
import { Preferences } from "../dist/runtime/preferences.js";
import { frameRateFromSamples, nextAdaptiveDpr } from "../dist/runtime/frame-rate.js";

function feature(id, events, { failInit = false, canReload = true } = {}) {
  return {
    contractVersion: 1,
    moduleId: id,
    init(_context, restored) {
      events.push(["init", id, restored]);
      if (failInit) throw new Error("synthetic init failure");
      return {
        snapshot: () => ({ value: restored.value ?? `${id}-state` }),
        canReload: () => canReload,
        dispose: () => events.push(["dispose", id])
      };
    }
  };
}

function harness(modules) {
  const events = [];
  const reports = [];
  const stored = [];
  let safe = true;
  const manager = new HotReloadManager({
    context: {}, canApply: () => safe, report: message => reports.push(message),
    persistState: (id, state) => stored.push([id, state]),
    importFeature: async (definition, hash) => {
      const module = modules.get(`${definition.id}:${hash}`);
      if (module instanceof Error) throw module;
      if (!module) throw new Error("missing fixture module");
      return module;
    }
  });
  return { manager, events, reports, stored, setSafe(value) { safe = value; } };
}

test("reloads only the changed feature and restores its snapshot", async () => {
  const events = [];
  const modules = new Map([
    ["preview:v1", feature("preview", events)], ["copy:v1", feature("copy", events)],
    ["preview:v2", feature("preview", events)]
  ]);
  const h = harness(modules);
  await h.manager.register({ id: "preview" }, "v1");
  await h.manager.register({ id: "copy" }, "v1");
  const result = await h.manager.apply({ modules: [{ id: "preview", src: "preview.js", hash: "v2" }, { id: "copy", src: "copy.js", hash: "v1" }] });
  assert.deepEqual(result, { status: "updated", changed: ["preview"] });
  assert.deepEqual(events.at(-1), ["init", "preview", { value: "preview-state" }]);
  assert.equal(events.filter(event => event[0] === "dispose" && event[1] === "copy").length, 0);
  assert.deepEqual(h.stored, [["preview", { value: "preview-state" }]]);
});

test("defers reload while user work or a module guard is active", async () => {
  const events = [];
  const modules = new Map([["preview:v1", feature("preview", events)], ["preview:v2", feature("preview", events)]]);
  const h = harness(modules);
  await h.manager.register({ id: "preview" }, "v1");
  h.setSafe(false);
  assert.equal((await h.manager.apply({ modules: [{ id: "preview", src: "preview.js", hash: "v2" }] })).status, "deferred");
  assert.equal(events.some(event => event[0] === "dispose"), false);
  h.setSafe(true);
});

test("keeps the live feature if loading a replacement fails", async () => {
  const events = [];
  const modules = new Map([["preview:v1", feature("preview", events)], ["preview:v2", new Error("bad syntax")]]);
  const h = harness(modules);
  await h.manager.register({ id: "preview" }, "v1");
  const result = await h.manager.apply({ modules: [{ id: "preview", src: "preview.js", hash: "v2" }] });
  assert.deepEqual(result, { status: "unchanged", changed: [] });
  assert.equal(events.some(event => event[0] === "dispose"), false);
  assert.match(h.reports[0], /current version remains active/);
});

test("restores the old feature if replacement initialization fails", async () => {
  const events = [];
  const modules = new Map([["preview:v1", feature("preview", events)], ["preview:v2", feature("preview", events, { failInit: true })]]);
  const h = harness(modules);
  await h.manager.register({ id: "preview" }, "v1");
  await h.manager.apply({ modules: [{ id: "preview", src: "preview.js", hash: "v2" }] });
  assert.deepEqual(events.slice(-3), [["dispose", "preview"], ["init", "preview", { value: "preview-state" }], ["init", "preview", { value: "preview-state" }]]);
  assert.match(h.reports.at(-1), /previous version restored/);
  assert.deepEqual(h.manager.snapshotAll(), { preview: { value: "preview-state" } });
});

test("activity locks release exactly once and preferences persist with a safe memory fallback", () => {
  const activity = new ActivityTracker();
  const finish = activity.begin("clipboard");
  assert.equal(activity.isIdle, false);
  finish(); finish();
  assert.equal(activity.isIdle, true);
  const values = new Map();
  const storage = { getItem: key => values.get(key) ?? null, setItem: (key, value) => values.set(key, value) };
  const preferences = new Preferences(storage);
  preferences.set("quality", "performance");
  assert.equal(preferences.get("quality"), "performance");
  const legacy = new Preferences({ getItem: () => "performance", setItem() {} });
  assert.equal(legacy.get("quality"), "performance");
  const blocked = new Preferences({ getItem: () => null, setItem: () => { throw new Error("blocked"); } });
  assert.equal(blocked.set("quality", "balanced"), false);
  assert.equal(blocked.get("quality"), "balanced");
});

test("FPS estimates require a real animation sample and reject suspended or one-shot draws", () => {
  assert.equal(frameRateFromSamples(37, 1200), 30.833333333333332);
  assert.equal(frameRateFromSamples(1, 1200), null);
  assert.equal(frameRateFromSamples(0, 0), null);
  assert.equal(frameRateFromSamples(Number.NaN, 1200), null);
  assert.equal(nextAdaptiveDpr(1, 20, 2), .82);
  assert.equal(nextAdaptiveDpr(.62, 20, 2), .6);
  assert.equal(nextAdaptiveDpr(.8, 30, 2), .9);
  assert.equal(nextAdaptiveDpr(1.95, 30, 2), 2);
  assert.equal(nextAdaptiveDpr(1, 26, 2), 1);
  assert.equal(nextAdaptiveDpr(1, 20, Number.NaN), null);
});
