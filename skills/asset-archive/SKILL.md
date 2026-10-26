---
name: ue-asset-archive
description: Register and synchronize 3D team assets in the project-scoped Unreal Asset Archive with stable IDs, source links, PBR facts, and TODOs.
---

# Unreal Asset Archive skill

Use this workflow when an AI 3D team member needs to register a finished or WIP asset in the Unreal archive.

1. Resolve the exact target `.uproject` and read its `AssetArchive_Inbox/project.json`. Confirm `project_id`, units, up axis, ground plane and `approved_source_roots`.
2. Read current manifests to avoid duplicate IDs. Create a new item folder and `archive_item.json` from `examples/` or validate against `schemas/item.schema.json`.
3. Record only observed facts: asset name, type, preview path, available PBR channels, authoritative `.blend`/`.spp` links, status and specific TODOs. Keep uncertain facts explicitly WIP.
4. Choose one route: `preview_fbx` for a new one-time import, or `preview_assets` for existing Unreal object paths. Never set both. A `/Game/...` path is local to this project; use the shared plugin mount only for intentionally shared content.
5. Confirm that this exact target project is saved and closed. Run `Tools/sync_archive_project.ps1 -ProjectFile <absolute-project.uproject>` once. Do not auto-retry on failure.
6. Read the project log and generated catalog. Report added/failed IDs and remaining TODOs. A successful sync is not model approval or in-game validation.

No AI API or Tripo request is needed for the sync operation. Follow the project owner's access policy for local files; do not upload project art to external services without separate authorization.
