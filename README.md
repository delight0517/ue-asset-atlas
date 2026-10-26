# BlackLion Asset Archive

Product site: [UE Asset Atlas](https://ue-asset-atlas.rogan2534.chatgpt.site) · Source: [GitHub](https://github.com/delight0517/ue-asset-atlas)

**A small Unreal Engine asset catalog for 3D teams and AI production tools.** Keep a project's asset index, TODOs, Blender/Substance Painter sources, and Unreal previews connected without making another copy of every asset.

> Current public build: source-only plugin for Unreal Engine 5.8 on Windows. The in-game archive map and complete cross-project UX are still evolving. Read the compatibility and limitations before installing.

## What it does

- Browse archive entries in a small Unreal showroom with paged lists; only the selected mesh is loaded and rotated.
- Adapt thumbnail pixel caps, preview LOD and preview shadows to catalog size to reduce archive browsing cost.
- Keep catalogs, collections, project IDs and TODO lists separate per Unreal project.
- Link authoritative `.blend`, `.spp`, `.fbx`, `.uproject` and image sources.
- Register existing `/Game/...` StaticMesh, Texture2D and Material object paths in place; registration does not copy those assets.
- Import a new preview FBX once into the shared plugin Content mount so multiple linked Unreal projects can refer to the same package.
- Refresh from local JSON manifests and an Unreal Python commandlet. No AI provider call, Tripo call, or API token is needed for the sync step.
- Apply AI-authored placement manifests in one deterministic pass to a dedicated QA level; cache each distinct mesh, ground instances and reject unintended bounds overlaps.
- Connect an approved fixed-camera reference-shot manifest to a QA placement and preserve the locked camera/crop for later render comparison.

## Install on Windows / UE 5.8

1. Close the target Unreal project normally.
2. Clone this repository to a stable shared path, for example `C:\UE-Shared\BlackLionAssetArchive`.
3. In PowerShell, link and enable the plugin for a project:

   ```powershell
   .\Tools\link_shared_plugin.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject' -Apply
   ```

   Omit `-Apply` first to preview. The helper refuses to replace a non-junction plugin folder and refuses a running target project. It creates a project-local `Plugins\BlackLionAssetArchive` NTFS junction that points to the single shared repository folder.
4. Build the project once in Unreal's normal toolchain. Open the project and enable/open the dedicated archive map or the actor in your own map.
5. Create `AssetArchive_Inbox\project.json` and manifests following `schemas/` and `examples/`.
6. Close the project, then sync:

   ```powershell
   .\Tools\sync_archive_project.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject'
   ```

   The commandlet refuses to run if that project is open. It refreshes catalog metadata, imports only new stable FBX IDs, and writes a local receipt to prevent silent replacements.

For a repeatable headless build and large-catalog QA run (with the target editor closed):

```powershell
.\Tools\run_archive_qa.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject'
```

This verifies paging of 5,000 synthetic assets and adaptive preview limits. It does not replace visual in-editor review. See [performance QA](docs/PERFORMANCE_QA.md).

For batched placement, validate a scene layout JSON and apply it to an isolated QA level:

```powershell
python .\Tools\scene_layout.py --manifest .\examples\scene-layout\layout.json --project .\examples\scene-layout\project.json --catalog .\examples\scene-layout\catalog.json
.\Tools\apply_scene_layout.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject' -ManifestPath 'D:\QA\layout.json'
```

The second command runs only with the target Unreal project closed and only writes under `/Game/Developer/AIPlacementQA/`. See the [scene-layout contract](docs/SCENE_LAYOUT.md). The layout tool applies transform proposals; it does not perform image-to-3D inference. Reference matching needs an approved fixed camera and a rendered candidate for comparison.

See [Windows setup](docs/SETUP_WINDOWS.md) and [AI team workflow](docs/AI_TEAM_WORKFLOW.md) for the full steps.

## Sharing assets between projects

The plugin source and its Content mount are shared through one physical plugin directory. For a new FBX preview, sync imports once under `/BlackLionAssetArchive/SharedAssets/<project-id>/`; projects using the same plugin junction can reference that UAsset package. Each project's `Saved/BlackLionAssetArchive/catalog.json` and collections stay local and project-scoped.

Existing Unreal assets remain where they already are. `preview_assets` registers an existing asset in the current project's catalog without copying it. A `/Game/...` object path is project-local and is not magically visible to another project; to share that asset, deliberately move/import it into the shared plugin Content mount and then reference its `/BlackLionAssetArchive/...` object path. This release does not automatically migrate existing UAssets.

The provided junction helper currently targets Windows and UE 5.8. macOS/Linux setup and other engine versions have not been verified. Do not assume the current setup script is portable across those platforms.

## For AI tools

The AI should write a small manifest, not duplicate or rewrite the source scene. The deterministic sync tool reads those manifests locally. Provide the AI with the project ID, units/axes, approved source roots, authoritative source files, preview route, stable asset ID, visible PBR channels, TODOs and the command to sync. The sync process does not decide art quality or claim an asset has been approved; the team reviews it in Unreal.

Copy the brief in [AI team workflow](docs/AI_TEAM_WORKFLOW.md) into your agent instructions or project-specific skill. An AI agent needs local file access and permission to invoke the project sync command; installing the plugin alone does not connect an AI model or grant it filesystem access.

## Layout

- `Source/BlackLionAssetArchiveCore`: project identity, catalog validation and collection data.
- `Source/BlackLionAssetArchive`: showroom and UI.
- `Tools/link_shared_plugin.ps1`: preview/apply a shared junction for a closed UE 5.8 project.
- `Tools/sync_archive_project.ps1`: safe background commandlet guard and sync entry point.
- `Tools/run_archive_qa.ps1`: optional UE 5.8 build plus headless automation checks for paging and adaptive preview policy.
- `Tools/scene_layout.py` / `Tools/apply_scene_layout.ps1`: validate and batch-place catalog assets in an isolated QA level; `apply_scene_layout.py` is the UE-side executor.
- `schemas/scene-layout.schema.json`: units, axes, stable IDs, transforms and optional approved reference shot contract.
- `Tools/import_archive_assets.py`: generic per-project inbox importer (no bundled user's asset list).
- `schemas/`, `examples/`, `docs/`: machine-readable contracts, synthetic sample and setup guidance.
- `Content/SharedAssets/`: local shared assets live here when teams import them; project-specific assets are not bundled in this repository.
- `website/`: source for the public static product site. It uses plain HTML/CSS/WebGL and has no build or runtime dependencies.

## Current boundaries

- The source code has built successfully against UE 5.8 in the originating project. The public source package is a sanitized source distribution; it does not include engine binaries or the user's project content.
- A ready-made archive `.umap` is not included in this source package because maps belong to the consuming project. Add the showroom actor to a project map or create a dedicated archive map using the project's own layout.
- The source-only build was not validated against UE 5.7/5.9, macOS, or Linux.
- UE Live Coding can patch eligible code. Module/UCLASS shape changes or first-time plugin setup may require restarting the editor. JSON catalog refresh is a data reload and is not a C++ hot reload.
- Game UI and external Blender/Painter launch behavior have not been visually verified in the user's active desktop session.

## License

MIT. See [LICENSE](LICENSE).
