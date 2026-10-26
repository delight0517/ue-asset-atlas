# Manifest contract

The schema files are the canonical machine-readable format. Keep project IDs unique and stable, and asset IDs stable for the lifetime of the same source object.

- `project.json` scopes one Unreal project's catalog and declares its axis/unit conventions.
- `archive_item.json` records one object or scene, its status, TODOs, PBR channels, source links and preview route.
- `preview_fbx` imports a new mesh preview once under the shared plugin mount.
- `preview_assets` names existing `/Game/...` or `/BlackLionAssetArchive/...` UObject paths and does not import/copy them.
- `source_file`, `files[].path`, and `thumbnail` resolve only under the inbox, project root, shared plugin or explicitly listed `approved_source_roots`.

There is no cloud service or account sync in this release. Catalog JSON is generated per-project by the Unreal commandlet and shared through that project's normal files/backups.
