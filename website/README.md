# Public product site

The product introduction and interactive WebGL sample are at https://ue-asset-atlas.rogan2534.chatgpt.site. The public site has no account, backend, analytics, AI provider, external script or generated artwork dependency. Hosted Sites deployment settings and credentials are not part of the public repository.

## Local hot-reload development

Use Python 3 and Node.js 20 or newer. From the repository root, run:

```powershell
python website/dev_server.py
```

Open `http://127.0.0.1:8765/?dev=1` in a modern browser. This development server binds only to loopback. It is intentionally separate from the plain static server used for production hosting.

The website shell is the hub. Feature modules have stable IDs and contract version 1 in `website/dist/modules.json`; each module exports `moduleId`, `contractVersion`, and `init(context, restoredState)`. `init` returns explicit `snapshot`, `canReload`, and `dispose` lifecycle hooks. The asset preview, manifest-copy workflow, shared theme, feature CSS, preferences adapter, activity guard, and reload manager are separated so ordinary changes stay inside one feature.

Saving a feature JavaScript or CSS file updates only that module or stylesheet. The preview module snapshots its selected quality, object transforms, orbit camera and selection before replacement. The hub defers changes while a control is focused, a pointer drag is active, a clipboard write is in flight, or the page is hidden. A failed feature import leaves the working module active; a failed initialization restores the old module. Changing the hub, feature registry or page structure triggers a guarded page reload and restores the current preview state, focus target and scroll position. Browser quality preferences persist in local storage; the adapter reads the previous plain-string preference format and falls back to session memory when storage is blocked. If the Python development server code itself changes, restart that server; the page will notice the updated manifest and preserve its state during its guarded reload.

To add a feature, create a module under `website/dist/modules/` and register its same-origin URL and unique ID in `modules.json`. Give every event listener an abortable lifecycle and return a complete state snapshot. Keep AI provider adapters out of this product until an actual provider integration is implemented; this repository's asset sync is local and does not connect an AI account. There is no hot update for an Unreal Editor process here: Unreal C++ changes still use UE 5.8 Live Coding where supported, and changes to module/UCLASS shape or plugin setup can require an editor restart.

## QA

Run the focused runtime/server checks from the repository root:

```powershell
node --test website/tests/*.test.mjs
node --check website/dist/app.js
node --check website/dist/modules/asset-preview.js
node --check website/dist/modules/manifest-copy.js
```

The tests cover loopback-only serving, no-cache content hashes, changed-feature-only reload, state restoration, pending-work guards, failed-update recovery, activity locks, preference migration/fallback, adaptive-DPR threshold steps, and the narrow-screen preview layout rules. A hidden-browser visual pass also checks the responsive controls at a 350px effective width. These checks do not benchmark a large real asset library or replace visual review in a consuming Unreal project.

The current browser preview is a four-part sample scene. Its Auto/Performance/Balanced/Quality controls and per-object transforms affect this website sample only; they do not configure an Unreal project's viewport or renderer.
