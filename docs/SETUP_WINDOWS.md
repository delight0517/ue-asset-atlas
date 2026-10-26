# Windows setup — Unreal Engine 5.8

## Requirements

- Windows 10/11 with PowerShell 7 or Windows PowerShell 5.1.
- Unreal Engine 5.8 installed at the standard Epic Games path, or update `Tools/sync_archive_project.ps1` to point to your installed `UnrealEditor-Cmd.exe`.
- Unreal Python Script Plugin enabled in the target project for commandlet sync.
- A closed target `.uproject` when linking the plugin or running sync.

## One shared plugin directory

Clone this repository once to a stable location used by your team. The helper creates a project plugin junction; it does not copy the plugin for every project.

```powershell
git clone https://github.com/delight0517/ue-asset-atlas.git C:\UE-Shared\BlackLionAssetArchive
Set-Location C:\UE-Shared\BlackLionAssetArchive
.\Tools\link_shared_plugin.ps1 -ProjectFile 'D:\Projects\GameA\GameA.uproject'
.\Tools\link_shared_plugin.ps1 -ProjectFile 'D:\Projects\GameA\GameA.uproject' -Apply
```

Repeat the helper for other closed UE 5.8 projects to link them to the same checkout. Each project still gets a separate `AssetArchive_Inbox/project.json` and local catalog. Do not point different projects at the same `project_id`.

If a project already has `Plugins/BlackLionAssetArchive` as a real directory, the helper stops instead of replacing it. Back up and resolve that conflict manually; do not delete an existing plugin folder automatically.

## Create the identity record

Create `AssetArchive_Inbox/project.json`. Start from `examples/demo-project/AssetArchive_Inbox/project.json` and replace the sample `project_id` with a stable ID unique to your project. Paths in `approved_source_roots` are local directories that the importer is explicitly allowed to read for source files; omit the field when all sources live under the project or inbox.

```json
{
  "schema": "blacklion.asset-archive.project.v1",
  "project_id": "studio_game_alpha",
  "display_name": "Studio Game Alpha",
  "units": "centimeters",
  "up_axis": "Z",
  "ground_plane": "XY",
  "approved_source_roots": ["D:/Art/StudioGameAlpha"]
}
```

## Add an asset

Copy `examples/demo-project/AssetArchive_Inbox/demo-crate/archive_item.json` into a new inbox subfolder and replace the sample ID and references. The manifest can register either:

- `preview_fbx`: import one new mesh preview once into the shared plugin mount; or
- `preview_assets`: reference already-imported Unreal asset object paths without copying.

Never put both on one entry. Existing `/Game/...` object paths are local to this Unreal project. For sharing across linked projects, reference a shared-plugin package at `/BlackLionAssetArchive/SharedAssets/<project-id>/...`.

## Sync safely

Save manifests and source files, close the target Unreal project normally, then run:

```powershell
.\Tools\sync_archive_project.ps1 -ProjectFile 'D:\Projects\GameA\GameA.uproject'
```

Read the command output and `GameA/Saved/BlackLionAssetArchive/sync.log`. The project stays isolated by `project_id`. A duplicate ID, missing linked file, root escape, changed FBX under a stable ID, or open target project stops that entry or run without overwriting the existing asset. Use a new versioned ID when replacing geometry intentionally.

## Team workflow

Store the shared plugin checkout in a version-controlled team location. Keep each project's identity/catalog/TODOs with that project; agree on one canonical `project_id` per team project. Do not put FBX, `.blend`, `.spp`, credentials or private project folders into a public fork.
