"""Sync generic asset archive manifests for the current Unreal project.

No built-in user asset lists are shipped. Every entry comes from the local,
project-scoped AssetArchive_Inbox and uses stable IDs and explicit links.
"""

import json
import hashlib
import os
import re
import unreal

PROJECT_ROOT = os.path.realpath(os.environ.get("BLACKLION_ARCHIVE_PROJECT_ROOT", "")).strip()
PROJECT_FILES = [name for name in os.listdir(PROJECT_ROOT)] if PROJECT_ROOT and os.path.isdir(PROJECT_ROOT) else []
if not PROJECT_ROOT or sum(name.lower().endswith(".uproject") for name in PROJECT_FILES) != 1:
    raise RuntimeError("BLACKLION_ARCHIVE_PROJECT_ROOT must point to a folder containing exactly one target Unreal project")
OUTPUT = os.path.join(PROJECT_ROOT, "Saved", "BlackLionAssetArchive", "catalog.json")
RECEIPTS = os.path.join(PROJECT_ROOT, "Saved", "BlackLionAssetArchive", "intake_receipts.json")
INBOX_ROOT = os.path.join(PROJECT_ROOT, "AssetArchive_Inbox")
ITEM_MANIFEST = "archive_item.json"
PROJECT_META = os.path.join(INBOX_ROOT, "project.json")
SHARED_PLUGIN_ROOT = os.path.realpath(os.path.join(PROJECT_ROOT, "Plugins", "BlackLionAssetArchive"))
SHARED_ASSET_ROOT = "/BlackLionAssetArchive/SharedAssets"

if not os.path.isfile(PROJECT_META):
    raise RuntimeError("Project identity file is required at AssetArchive_Inbox/project.json; refusing implicit cross-project intake")
with open(PROJECT_META, "r", encoding="utf-8-sig") as stream:
    PROJECT = json.load(stream)
PROJECT_ID = str(PROJECT.get("project_id", "")).strip()
if not re.fullmatch(r"[A-Za-z0-9_-]+", PROJECT_ID):
    raise RuntimeError("project.json requires a stable project_id using letters, numbers, _ or -")
DEST_ROOT = SHARED_ASSET_ROOT + "/" + PROJECT_ID
APPROVED_EXTERNAL_ROOTS = []
for raw_root in PROJECT.get("approved_source_roots", []):
    if not isinstance(raw_root, str) or not raw_root.strip():
        raise RuntimeError("approved_source_roots must contain non-empty local directory paths")
    canonical_root = os.path.normcase(os.path.realpath(raw_root))
    if not os.path.isdir(canonical_root):
        raise RuntimeError("An explicitly approved source root does not exist or is not a directory")
    APPROVED_EXTERNAL_ROOTS.append(canonical_root)
def safe_name(value):
    return re.sub(r"[^A-Za-z0-9_-]+", "_", value).strip("_") or "Mesh"


def approved_path(raw_path, manifest_dir, require_inbox=False):
    candidate = os.path.normcase(os.path.realpath(raw_path if os.path.isabs(raw_path) else os.path.join(manifest_dir, raw_path)))
    allowed_roots = [os.path.normcase(os.path.realpath(INBOX_ROOT)), os.path.normcase(os.path.realpath(PROJECT_ROOT)), os.path.normcase(os.path.realpath(SHARED_PLUGIN_ROOT))]
    if not require_inbox:
        allowed_roots.extend(APPROVED_EXTERNAL_ROOTS)
    else:
        allowed_roots = [os.path.normcase(os.path.realpath(INBOX_ROOT))]
    for root in allowed_roots:
        try:
            if os.path.commonpath([candidate, root]) == root:
                return candidate
        except ValueError:
            continue
    raise RuntimeError("Manifest path is outside the project, inbox, shared plugin, or explicitly approved source roots")


def is_openable_file(path):
    return os.path.splitext(path)[1].lower() in (".blend", ".spp", ".fbx", ".png", ".jpg", ".jpeg", ".uproject", ".umap")


def import_intake_item(manifest_path, receipts):
    manifest_dir = os.path.dirname(manifest_path)
    with open(manifest_path, "r", encoding="utf-8-sig") as stream:
        item = json.load(stream)
    if item.get("schema") != "blacklion.asset-archive.item.v1":
        raise RuntimeError("Unsupported archive item schema")
    raw_id = str(item.get("id", "")).strip()
    item_id = safe_name(raw_id)
    name = str(item.get("name", "")).strip()
    kind = str(item.get("kind", "object")).strip().lower()
    folder = str(item.get("folder", os.path.basename(manifest_dir))).strip()
    item_project_id = str(item.get("project_id", PROJECT_ID)).strip()
    if not raw_id or not re.fullmatch(r"[A-Za-z0-9_-]+", raw_id) or not name or kind not in ("object", "space"):
        raise RuntimeError("Manifest requires id, name, and kind=object|space")
    if item_project_id != PROJECT_ID:
        raise RuntimeError("Manifest project_id does not match this Unreal project's project.json")
    source_fbx = ""
    if item.get("preview_fbx"):
        source_fbx = approved_path(item["preview_fbx"], manifest_dir)
        if not os.path.isfile(source_fbx) or os.path.splitext(source_fbx)[1].lower() != ".fbx":
            raise RuntimeError("preview_fbx must be an existing FBX inside an approved project/library root")
    preview_asset_paths = item.get("preview_assets", [])
    if not isinstance(preview_asset_paths, list):
        raise RuntimeError("preview_assets must be a list of existing Unreal object paths")
    if source_fbx and preview_asset_paths:
        raise RuntimeError("Choose preview_fbx import or existing preview_assets references; combining them can duplicate the same asset")
    source_file = item.get("source_file", "")
    links = []
    if source_file:
        scene_path = approved_path(source_file, manifest_dir)
        if not os.path.isfile(scene_path) or not is_openable_file(scene_path):
            raise RuntimeError("source_file must be an existing supported scene/project file: " + scene_path)
        links.append({"label": "Open source scene", "path": scene_path})
    elif kind == "space":
        raise RuntimeError("A space must link its authoritative source_file (for example .blend or .uproject)")
    if source_fbx:
        links.append({"label": "Open preview FBX", "path": source_fbx})
    for link in item.get("files", []):
        if not isinstance(link, dict) or not link.get("path"):
            continue
        linked_path = approved_path(link["path"], manifest_dir)
        if not os.path.isfile(linked_path) or not is_openable_file(linked_path):
            raise RuntimeError("Linked file does not exist or has unsupported extension: " + linked_path)
        links.append({"label": str(link.get("label", "Open linked file")), "path": linked_path})
    thumbnail = ""
    if item.get("thumbnail"):
        thumbnail = approved_path(item["thumbnail"], manifest_dir)
        if not os.path.isfile(thumbnail) or os.path.splitext(thumbnail)[1].lower() not in (".png", ".jpg", ".jpeg"):
            raise RuntimeError("thumbnail must be an existing PNG/JPEG within approved roots")
        links.append({"label": "Open scene thumbnail", "path": thumbnail})
    preview_records = []
    receipt = None
    if source_fbx:
        destination = DEST_ROOT + "/" + safe_name(item_id)
        digest = hashlib.sha256()
        with open(source_fbx, "rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        source_hash = digest.hexdigest()
        old_receipt = receipts.get(item_id)
        if old_receipt and old_receipt.get("preview_fbx_sha256") != source_hash:
            raise RuntimeError("Preview FBX changed for an existing stable ID; use a new ID/version instead of replacing imported content")

        def find_meshes():
            paths = unreal.EditorAssetLibrary.list_assets(destination, recursive=True, include_folder=False)
            return [path for path in paths if isinstance(unreal.EditorAssetLibrary.load_asset(path), unreal.StaticMesh)]

        meshes = find_meshes()
        if meshes and not old_receipt:
            raise RuntimeError("Imported content exists without an intake receipt; refusing to guess whether it matches this file")
        if old_receipt and old_receipt.get("preview_meshes"):
            recorded_meshes = old_receipt["preview_meshes"]
            if all(unreal.EditorAssetLibrary.load_asset(path) for path in recorded_meshes):
                meshes = recorded_meshes
        if not meshes:
            options = unreal.FbxImportUI()
            options.import_as_skeletal = False
            options.import_mesh = True
            options.import_materials = True
            options.import_textures = True
            if options.static_mesh_import_data:
                options.static_mesh_import_data.combine_meshes = kind == "space"
                options.static_mesh_import_data.convert_scene_unit = True
                options.static_mesh_import_data.import_uniform_scale = 1.0
            task = unreal.AssetImportTask()
            task.filename = source_fbx
            task.destination_path = destination
            task.automated = True
            task.replace_existing = False
            task.replace_existing_settings = False
            task.save = True
            task.options = options
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            meshes = find_meshes()
        if not meshes:
            raise RuntimeError("No preview StaticMesh found/imported for " + item_id)
        receipt = {"id": item_id, "preview_fbx_sha256": source_hash, "preview_meshes": meshes}
        preview_records = [{"path": mesh_path, "mesh": mesh_path, "type": "StaticMesh"} for mesh_path in meshes]
    elif preview_asset_paths:
        canonical_asset_paths = [str(path).strip() for path in preview_asset_paths]
        old_receipt = receipts.get(item_id)
        if old_receipt and old_receipt.get("preview_asset_paths") != canonical_asset_paths:
            raise RuntimeError("Existing Unreal asset paths changed for this stable ID; use a new versioned ID instead of silently retargeting it")
        if old_receipt and old_receipt.get("preview_fbx_sha256"):
            raise RuntimeError("This stable ID already records an FBX import; create a new ID for direct-UAsset registration")
        for object_path in preview_asset_paths:
            object_path = str(object_path).strip()
            if not (object_path.startswith("/Game/") or object_path.startswith("/BlackLionAssetArchive/")) or ".." in object_path:
                raise RuntimeError("preview_assets accepts only explicit /Game/ or shared-plugin object paths")
            asset = unreal.EditorAssetLibrary.load_asset(object_path)
            if not asset:
                raise RuntimeError("Existing Unreal asset path could not be loaded: " + object_path)
            asset_class = asset.get_class().get_name()
            if isinstance(asset, unreal.StaticMesh):
                preview_records.append({"path": object_path, "mesh": object_path, "type": "StaticMesh"})
            elif isinstance(asset, unreal.Texture2D):
                preview_records.append({"path": object_path, "mesh": "", "type": "Texture2D"})
            elif asset_class in ("Material", "MaterialInstance", "MaterialInstanceConstant"):
                preview_records.append({"path": object_path, "mesh": "", "type": asset_class})
            else:
                raise RuntimeError("Unsupported existing Unreal asset type for visual archive: " + asset_class)
        receipt = {"id": item_id, "preview_asset_paths": [row["path"] for row in preview_records]}
    elif kind != "space" or not thumbnail:
        raise RuntimeError("Without preview_fbx, only a space with a scene thumbnail can be registered")

    output = []
    if not preview_records:
        preview_records = [{"path": "", "mesh": "", "type": "ScenePreview"}]
    for record in preview_records:
        asset_path = record["path"]
        mesh_path = record["mesh"]
        asset_name = asset_path.rsplit("/", 1)[-1].split(".", 1)[0] if asset_path else "ScenePreview"
        output.append({
            "id": "INBOX-" + item_id + "::" + asset_name,
            "name": name if kind == "space" or len(preview_records) == 1 else asset_name,
            "project": item_project_id,
            "group": "INBOX-" + item_id,
            "folder": folder,
            "kind": kind,
            "preview_mesh": mesh_path,
            "preview_asset": asset_path,
            "asset_type": record["type"],
            "preview_thumbnail": thumbnail,
            "status": str(item.get("status", "New local team intake; preview/source link registered from manifest.")),
            "todos": [str(todo).strip() for todo in item.get("todos", []) if str(todo).strip()][:24],
            "pbr_channels": item.get("pbr_channels", []),
            "files": links,
        })
    return output, receipt


def main():
    unreal.log("BlackLion Asset Archive import started; original .blend/FBX/SPP files are read-only.")
    # Preserve the existing project catalog. Intake only owns rows whose IDs
    # begin with its manifest-specific INBOX-<id>:: prefix.
    existing_source_blend = ""
    try:
        with open(OUTPUT, "r", encoding="utf-8-sig") as stream:
            existing_catalog = json.load(stream)
        if existing_catalog.get("schema") != "blacklion.asset-archive.catalog.v1":
            raise RuntimeError("Unsupported existing catalog schema; refusing to replace it")
        if existing_catalog.get("project_id") != PROJECT_ID:
            raise RuntimeError("Existing catalog belongs to a different project; refusing to replace it")
        existing_source_blend = str(existing_catalog.get("source_blend", ""))
        entries = existing_catalog.get("assets", [])
        if not isinstance(entries, list) or any(not isinstance(entry, dict) or not entry.get("id") for entry in entries):
            raise RuntimeError("Existing catalog assets are malformed; refusing to replace it")
    except FileNotFoundError:
        entries = []
    failures = []
    os.makedirs(INBOX_ROOT, exist_ok=True)
    seen_ids = {entry["id"] for entry in entries}
    if len(seen_ids) != len(entries):
        raise RuntimeError("Existing catalog has duplicate IDs; refusing intake to avoid data loss")
    try:
        with open(RECEIPTS, "r", encoding="utf-8") as stream:
            receipts = json.load(stream)
    except FileNotFoundError:
        receipts = {}
    except Exception as exc:
        raise RuntimeError("Could not read intake receipts; no intake imports were run: " + str(exc))
    for current_root, _, filenames in os.walk(INBOX_ROOT):
        if ITEM_MANIFEST not in filenames:
            continue
        manifest_path = os.path.join(current_root, ITEM_MANIFEST)
        try:
            manifest_path = approved_path(manifest_path, INBOX_ROOT, require_inbox=True)
            with open(manifest_path, "r", encoding="utf-8-sig") as stream:
                manifest_id = safe_name(str(json.load(stream).get("id", "")))
            prefix = "INBOX-" + manifest_id + "::"
            imported, receipt = import_intake_item(manifest_path, receipts)
            owned_ids = {entry["id"] for entry in entries if entry["id"].startswith(prefix)}
            duplicate = next((entry["id"] for entry in imported if entry["id"] in seen_ids and entry["id"] not in owned_ids), None)
            if duplicate:
                raise RuntimeError("Duplicate archive ID owned by another source; no overwrite performed: " + duplicate)
            entries = [entry for entry in entries if not entry["id"].startswith(prefix)] + imported
            seen_ids = {entry["id"] for entry in entries}
            unreal.log("Archive intake %s: %d preview meshes (%s)" % (manifest_path, len(imported), imported[0]["kind"]))
            if receipt:
                receipts[receipt["id"]] = receipt
        except Exception as exc:
            failures.append({"manifest": manifest_path, "error": str(exc)})
            unreal.log_error("Archive intake failed %s: %s" % (manifest_path, exc))
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    payload = {
        "schema": "blacklion.asset-archive.catalog.v1",
        "project_id": PROJECT_ID,
        "source_blend": str(PROJECT.get("source_blend", existing_source_blend)),
        "assets": entries,
        "failures": failures,
    }
    temporary = OUTPUT + ".tmp"
    with open(temporary, "w", encoding="utf-8") as stream:
        json.dump(payload, stream, indent=2, ensure_ascii=False)
    os.replace(temporary, OUTPUT)
    os.makedirs(os.path.dirname(RECEIPTS), exist_ok=True)
    receipt_tmp = RECEIPTS + ".tmp"
    with open(receipt_tmp, "w", encoding="utf-8") as stream:
        json.dump(receipts, stream, indent=2, ensure_ascii=False)
    os.replace(receipt_tmp, RECEIPTS)
    unreal.log("BlackLion Asset Archive catalog written: %s (%d items, %d failed manifests)" % (OUTPUT, len(entries), len(failures)))


main()
