#!/usr/bin/env python3
"""Package only explicitly selected web-ready GLB assets for a static portfolio."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tempfile
from pathlib import Path

SCHEMA = "blacklion.asset-archive.web-portfolio.v1"
MAX_ASSETS = 100
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")
POSTER_SUFFIXES = {".webp", ".jpg", ".jpeg", ".png"}


class ExportError(ValueError):
    pass


def checked_source(root: Path, relative: object, label: str) -> Path:
    if not isinstance(relative, str) or not relative.strip():
        raise ExportError(f"{label} must be a non-empty relative path")
    candidate = Path(relative)
    if candidate.is_absolute() or any(part in {"..", ""} for part in candidate.parts):
        raise ExportError(f"{label} must stay under the source directory")
    resolved_root = root.resolve(strict=True)
    resolved = (resolved_root / candidate).resolve(strict=True)
    if not resolved.is_relative_to(resolved_root) or not resolved.is_file():
        raise ExportError(f"{label} must resolve to a file under the source directory")
    return resolved


def validate_selection(data: object) -> list[dict]:
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise ExportError(f"selection schema must be {SCHEMA}")
    title, assets = data.get("title"), data.get("assets")
    if not isinstance(title, str) or not title.strip() or len(title) > 120:
        raise ExportError("title must contain 1 to 120 characters")
    if not isinstance(assets, list) or not assets or len(assets) > MAX_ASSETS:
        raise ExportError(f"assets must contain 1 to {MAX_ASSETS} explicitly selected entries")
    seen: set[str] = set()
    allowed = {"id", "name", "visibility", "project_id", "project_name", "description", "model_glb", "poster", "alt", "license", "attribution"}
    for item in assets:
        if not isinstance(item, dict) or set(item) - allowed:
            raise ExportError("each asset must be an object containing only supported fields")
        asset_id, name = item.get("id"), item.get("name")
        if not isinstance(asset_id, str) or not ID_PATTERN.fullmatch(asset_id):
            raise ExportError("asset id may contain only letters, numbers, underscore, and hyphen")
        if asset_id in seen:
            raise ExportError(f"duplicate asset id: {asset_id}")
        seen.add(asset_id)
        project_id = item.get("project_id")
        if project_id is not None and (not isinstance(project_id, str) or not ID_PATTERN.fullmatch(project_id)):
            raise ExportError(f"{asset_id}: project_id may contain only letters, numbers, underscore, and hyphen")
        project_name = item.get("project_name", "")
        if not isinstance(project_name, str) or len(project_name) > 120:
            raise ExportError(f"{asset_id}: project_name must be text up to 120 characters")
        if not isinstance(name, str) or not name.strip() or len(name) > 120:
            raise ExportError(f"{asset_id}: name must contain 1 to 120 characters")
        for field, limit in (("description", 1200), ("alt", 240), ("license", 160), ("attribution", 240)):
            value = item.get(field, "")
            if not isinstance(value, str) or len(value) > limit:
                raise ExportError(f"{asset_id}: {field} must be text up to {limit} characters")
        if item.get("visibility") not in {"public", "private"}:
            raise ExportError(f"{asset_id}: visibility must be public or private")
        if not isinstance(item.get("model_glb"), str) or not isinstance(item.get("poster"), str):
            raise ExportError(f"{asset_id}: model_glb and poster are required")
    return assets


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_glb(path: Path, asset_id: str) -> None:
    size = path.stat().st_size
    with path.open("rb") as source:
        header = source.read(12)
        if len(header) != 12 or header[:4] != b"glTF":
            raise ExportError(f"{asset_id}: file is not a valid GLB 2 container")
        magic, version, declared_size = struct.unpack("<4sII", header)
        if magic != b"glTF" or version != 2 or declared_size != size:
            raise ExportError(f"{asset_id}: GLB header version/length is invalid")
        first_chunk_header = source.read(8)
        if len(first_chunk_header) != 8:
            raise ExportError(f"{asset_id}: GLB has no JSON chunk")
        chunk_size, chunk_type = struct.unpack("<II", first_chunk_header)
        if chunk_type != 0x4E4F534A or chunk_size % 4 or 20 + chunk_size > size:
            raise ExportError(f"{asset_id}: GLB JSON chunk header is invalid")
        try:
            document = json.loads(source.read(chunk_size).decode("utf-8").rstrip(" \t\r\n\x00"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ExportError(f"{asset_id}: GLB JSON chunk is invalid") from error
        if not isinstance(document, dict) or "asset" not in document:
            raise ExportError(f"{asset_id}: GLB JSON must contain asset metadata")
        # The vendored lightweight viewer must work without contacting third-party
        # decoder CDNs. These extensions need decoder assets not included locally.
        unsupported = {"KHR_draco_mesh_compression", "KHR_texture_basisu", "EXT_meshopt_compression"}
        used = set(document.get("extensionsUsed", []))
        required = set(document.get("extensionsRequired", []))
        if unsupported.intersection(used | required):
            raise ExportError(f"{asset_id}: GLB uses a compressed extension without a local decoder")
        while source.tell() < size:
            chunk_header = source.read(8)
            if len(chunk_header) != 8:
                raise ExportError(f"{asset_id}: GLB has a truncated chunk header")
            extra_size, _extra_type = struct.unpack("<II", chunk_header)
            if extra_size % 4 or source.tell() + extra_size > size:
                raise ExportError(f"{asset_id}: GLB has an invalid chunk length")
            source.seek(extra_size, os.SEEK_CUR)
        if source.tell() != size:
            raise ExportError(f"{asset_id}: GLB chunks do not match the declared file length")


def package(selection_path: Path, source_root: Path, output: Path,
            model_limit: int = 8 * 1024 * 1024, poster_limit: int = 512 * 1024,
            visibility: str = "public") -> dict:
    data = json.loads(selection_path.read_text(encoding="utf-8"))
    if visibility != "public":
        raise ExportError("only public portfolio exports are supported; private entries stay local")
    assets = [item for item in validate_selection(data) if item["visibility"] == "public"]
    if not assets:
        raise ExportError("there are no explicitly public assets to export")
    if model_limit < 1 or poster_limit < 1:
        raise ExportError("file size limits must be positive")
    source_root = source_root.resolve(strict=True)
    output = output.resolve()
    if output.exists():
        raise ExportError(f"output already exists; choose a new directory: {output}")
    if output == source_root or source_root in output.parents:
        raise ExportError("output must not be inside the source directory")

    prepared = []
    for item in assets:
        model = checked_source(source_root, item["model_glb"], f"{item['id']}.model_glb")
        poster = checked_source(source_root, item["poster"], f"{item['id']}.poster")
        if model.suffix.lower() != ".glb":
            raise ExportError(f"{item['id']}: model_glb must use the .glb format")
        if poster.suffix.lower() not in POSTER_SUFFIXES:
            raise ExportError(f"{item['id']}: poster must be WebP, JPEG, or PNG")
        if model.stat().st_size > model_limit:
            raise ExportError(f"{item['id']}: GLB is {model.stat().st_size} bytes; limit is {model_limit}")
        if poster.stat().st_size > poster_limit:
            raise ExportError(f"{item['id']}: poster is {poster.stat().st_size} bytes; limit is {poster_limit}")
        validate_glb(model, item["id"])
        prepared.append((item, model, poster))

    output.parent.mkdir(parents=True, exist_ok=True)
    temp_path = Path(tempfile.mkdtemp(prefix=f".{output.name}-", dir=output.parent))
    try:
        exported, total_bytes = [], 0
        for item, model, poster in prepared:
            folder = temp_path / "assets" / item["id"]
            folder.mkdir(parents=True)
            model_name, poster_name = f"model{model.suffix.lower()}", f"poster{poster.suffix.lower()}"
            model_dest, poster_dest = folder / model_name, folder / poster_name
            shutil.copyfile(model, model_dest)
            shutil.copyfile(poster, poster_dest)
            model_size, poster_size = model_dest.stat().st_size, poster_dest.stat().st_size
            total_bytes += model_size + poster_size
            exported.append({
                "id": item["id"], "name": item["name"], "description": item.get("description", ""),
                "alt": item.get("alt", item["name"]), "model": f"assets/{item['id']}/{model_name}",
                "model_bytes": model_size, "model_sha256": sha256(model_dest),
                "poster": f"assets/{item['id']}/{poster_name}", "poster_bytes": poster_size,
                "poster_sha256": sha256(poster_dest), "license": item.get("license", ""),
                "attribution": item.get("attribution", "")
            })
            if item.get("project_id"):
                exported[-1]["project_id"] = item["project_id"]
                if item.get("project_name"):
                    exported[-1]["project_name"] = item["project_name"]
        manifest = {"schema": SCHEMA, "title": data["title"], "visibility": "public", "loading": "per-asset",
                    "assets": exported, "asset_count": len(exported), "total_bytes": total_bytes}
        (temp_path / "portfolio.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if output.exists():
            raise ExportError(f"output was created during export: {output}")
        os.replace(temp_path, output)
        return manifest
    finally:
        if temp_path.exists():
            shutil.rmtree(temp_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", type=Path, required=True, help="Explicit allowlist JSON")
    parser.add_argument("--source-root", type=Path, required=True, help="Root containing approved web derivatives")
    parser.add_argument("--output", type=Path, required=True, help="New directory for the selected static payload")
    parser.add_argument("--max-model-mib", type=float, default=8, help="Per-GLB budget (default: 8 MiB)")
    parser.add_argument("--max-poster-kib", type=float, default=512, help="Per-poster budget (default: 512 KiB)")
    args = parser.parse_args()
    try:
        manifest = package(args.selection, args.source_root, args.output,
                           int(args.max_model_mib * 1024 * 1024), int(args.max_poster_kib * 1024))
        print(f"Packaged {manifest['asset_count']} selected asset(s), {manifest['total_bytes']} bytes, to {args.output.resolve()}")
        print("No Unreal project files or unselected catalog entries were copied.")
        return 0
    except (OSError, json.JSONDecodeError, ExportError) as error:
        print(f"Portfolio export stopped: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
