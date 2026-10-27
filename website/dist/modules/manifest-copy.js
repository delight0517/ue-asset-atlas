export const contractVersion = 1;
export const moduleId = "manifest-copy";

const manifest = {
  schema: "blacklion.asset-archive.item.v1",
  id: "relay_housing_v1",
  project_id: "studio_game_alpha",
  name: "Relay Housing",
  kind: "object",
  preview_assets: ["/Game/Factory/SM_RelayHousing.SM_RelayHousing"],
  todos: ["Check floor contact in archive"],
  pbr_channels: ["base_color", "normal"]
};

export function init(ctx, restored = {}) {
  const button = document.getElementById("copy-manifest");
  if (!button) return { snapshot: () => restored, dispose() {}, canReload: () => true };
  const listeners = new AbortController();
  const timers = new Set();
  let result = restored.result || "Copy example";
  let pending = false;
  button.textContent = result;
  if (result !== "Copy example") resetLater(result);
  const resetLater = value => {
    result = value;
    button.textContent = value;
    const timer = setTimeout(() => { timers.delete(timer); if (!pending) { result = "Copy example"; button.textContent = result; } }, 1600);
    timers.add(timer);
  };
  button.addEventListener("click", async () => {
    if (pending) return;
    pending = true;
    const finishActivity = ctx.activity.begin("manifest clipboard write");
    try {
      await navigator.clipboard.writeText(JSON.stringify(manifest, null, 2));
      resetLater("Copied");
    } catch {
      resetLater("Select code to copy");
    } finally {
      pending = false;
      finishActivity();
    }
  }, { signal: listeners.signal });
  return {
    snapshot: () => ({ result }),
    canReload: () => !pending,
    dispose: () => { listeners.abort(); for (const timer of timers) clearTimeout(timer); timers.clear(); }
  };
}
