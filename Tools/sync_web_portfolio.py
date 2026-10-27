#!/usr/bin/env python3
"""Stage a validated public GLB package into the existing personal Pages checkout.

This command is dry-run by default. ``--apply`` only changes the local
``ue-asset-atlas/`` folder in an existing ``delight0517.github.io`` checkout;
it never commits, pushes, deploys, or starts a schedule. The destination is a
public GitHub Pages site, so its contents are public. Removing an object from
the current gallery cannot erase prior Git commits, browser caches, or copies
already downloaded by visitors. Private assets must never be put in the public
package.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

SCHEMA = "blacklion.asset-archive.web-portfolio.v1"
RECEIPT_SCHEMA = "blacklion.asset-archive.web-portfolio-sync.v1"
EXPECTED_REMOTE = "github.com/delight0517/delight0517.github.io"
TARGET_RELATIVE = PurePosixPath("ue-asset-atlas")
ID_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")
HASH_PATTERN = re.compile(r"^[a-f0-9]{64}$")
POSTER_PATTERN = re.compile(r"^poster\.(webp|jpe?g|png)$", re.IGNORECASE)
MAX_ASSETS = 100
MAX_MODEL_BYTES = 8 * 1024 * 1024
MAX_POSTER_BYTES = 512 * 1024
VIEWER_FILES = (
    "index.html",
    "portfolio.js",
    "portfolio.css",
    "vendor/model-viewer.min.js",
    "vendor/LICENSE",
)
RECEIPT_NAME = ".portfolio-sync-receipt.json"


class SyncError(ValueError):
    """A closed safety or input validation failure."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _ensure_no_symlink(path: Path, boundary: Path) -> None:
    """Reject a symlink at path or on any existing parent down to boundary."""
    boundary = boundary.resolve(strict=True)
    try:
        relative = path.relative_to(boundary)
    except ValueError as error:
        raise SyncError(f"path escapes its allowed directory: {path}") from error
    current = boundary
    if current.is_symlink():
        raise SyncError(f"symlink directory is not allowed: {current}")
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise SyncError(f"symlink path is not allowed: {current}")


def _reject_tree_symlinks(root: Path) -> None:
    if root.is_symlink():
        raise SyncError("package root must not be a symlink")
    for item in root.rglob("*"):
        if item.is_symlink():
            raise SyncError(f"package contains a symlink: {item.relative_to(root)}")


def _source_file(package_root: Path, relative: str) -> Path:
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts or "\\" in relative or not path.parts:
        raise SyncError(f"unsafe package file path: {relative!r}")
    candidate = package_root.joinpath(*path.parts)
    _ensure_no_symlink(candidate, package_root)
    resolved = candidate.resolve(strict=True)
    if not resolved.is_relative_to(package_root.resolve(strict=True)) or not resolved.is_file():
        raise SyncError(f"package path is not a regular file: {relative!r}")
    return resolved


def _validate_glb(path: Path, asset_id: str) -> None:
    size = path.stat().st_size
    if size < 20:
        raise SyncError(f"{asset_id}: GLB is too short")
    with path.open("rb") as source:
        header = source.read(12)
        if len(header) != 12:
            raise SyncError(f"{asset_id}: GLB header is truncated")
        magic, version, declared_size = struct.unpack("<4sII", header)
        if magic != b"glTF" or version != 2 or declared_size != size:
            raise SyncError(f"{asset_id}: GLB 2 header or declared size is invalid")
        chunk_header = source.read(8)
        if len(chunk_header) != 8:
            raise SyncError(f"{asset_id}: GLB has no JSON chunk")
        chunk_size, chunk_type = struct.unpack("<II", chunk_header)
        if chunk_type != 0x4E4F534A or chunk_size % 4 or 20 + chunk_size > size:
            raise SyncError(f"{asset_id}: GLB JSON chunk is invalid")
        try:
            document = json.loads(source.read(chunk_size).decode("utf-8").rstrip(" \t\r\n\x00"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise SyncError(f"{asset_id}: GLB JSON chunk is invalid") from error
        if not isinstance(document, dict) or "asset" not in document:
            raise SyncError(f"{asset_id}: GLB JSON must contain asset metadata")
        unsupported = {"KHR_draco_mesh_compression", "KHR_texture_basisu", "EXT_meshopt_compression"}
        used = set(document.get("extensionsUsed", []))
        required = set(document.get("extensionsRequired", []))
        if unsupported.intersection(used | required):
            raise SyncError(f"{asset_id}: GLB uses a compressed extension without a local decoder")


def _validate_manifest(package_root: Path) -> tuple[dict[str, Any], dict[str, Path], set[str]]:
    if package_root.is_symlink():
        raise SyncError("package root must not be a symlink")
    package_root = package_root.resolve(strict=True)
    if not package_root.is_dir():
        raise SyncError("package path must be an existing directory")
    _reject_tree_symlinks(package_root)
    manifest_path = _source_file(package_root, "portfolio.json")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SyncError(f"portfolio.json is not valid UTF-8 JSON: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise SyncError(f"portfolio.json schema must be {SCHEMA}")
    if manifest.get("visibility") != "public" or manifest.get("loading") != "per-asset":
        raise SyncError("only public per-asset packages are supported")
    title = manifest.get("title")
    assets = manifest.get("assets")
    if not isinstance(title, str) or not title.strip() or len(title) > 120:
        raise SyncError("portfolio title must contain 1 to 120 characters")
    if not isinstance(assets, list) or not 1 <= len(assets) <= MAX_ASSETS:
        raise SyncError(f"public package must contain 1 to {MAX_ASSETS} assets")
    if manifest.get("asset_count") != len(assets):
        raise SyncError("portfolio asset_count does not match the asset list")

    source_files: dict[str, Path] = {"portfolio.json": manifest_path}
    managed: set[str] = set()
    seen: set[str] = set()
    total_bytes = 0
    for asset in assets:
        if not isinstance(asset, dict):
            raise SyncError("each portfolio asset must be an object")
        if "visibility" in asset and asset["visibility"] != "public":
            raise SyncError("portfolio contains a non-public asset entry")
        asset_id, name = asset.get("id"), asset.get("name")
        if not isinstance(asset_id, str) or not ID_PATTERN.fullmatch(asset_id) or asset_id in seen:
            raise SyncError("portfolio contains an invalid or duplicate asset ID")
        seen.add(asset_id)
        if not isinstance(name, str) or not name.strip() or len(name) > 120:
            raise SyncError(f"{asset_id}: name must contain 1 to 120 characters")
        for field, limit in (("description", 1200), ("alt", 240), ("license", 160), ("attribution", 240)):
            value = asset.get(field, "")
            if not isinstance(value, str) or len(value) > limit:
                raise SyncError(f"{asset_id}: {field} must be text up to {limit} characters")

        model_rel = f"assets/{asset_id}/model.glb"
        poster_rel = asset.get("poster")
        poster_path = PurePosixPath(poster_rel) if isinstance(poster_rel, str) else PurePosixPath()
        if asset.get("model") != model_rel:
            raise SyncError(f"{asset_id}: model must be {model_rel!r}")
        if (poster_path.is_absolute() or ".." in poster_path.parts or "\\" in str(poster_rel)
                or len(poster_path.parts) != 3 or poster_path.parts[:2] != ("assets", asset_id)
                or not POSTER_PATTERN.fullmatch(poster_path.name)):
            raise SyncError(f"{asset_id}: poster must be inside its own assets/{asset_id}/ folder")

        for field, relative, max_bytes in (
            ("model", model_rel, MAX_MODEL_BYTES),
            ("poster", poster_rel, MAX_POSTER_BYTES),
        ):
            size_field = f"{field}_bytes"
            hash_field = f"{field}_sha256"
            expected_size, expected_hash = asset.get(size_field), asset.get(hash_field)
            if isinstance(expected_size, bool) or not isinstance(expected_size, int) or not 1 <= expected_size <= max_bytes:
                raise SyncError(f"{asset_id}: {size_field} must be between 1 and {max_bytes}")
            if not isinstance(expected_hash, str) or not HASH_PATTERN.fullmatch(expected_hash):
                raise SyncError(f"{asset_id}: {hash_field} must be a SHA-256 hex digest")
            source = _source_file(package_root, relative)
            if source.stat().st_size != expected_size:
                raise SyncError(f"{asset_id}: {relative} byte count does not match the manifest")
            if sha256(source) != expected_hash:
                raise SyncError(f"{asset_id}: {relative} SHA-256 does not match the manifest")
            if field == "model":
                _validate_glb(source, asset_id)
            source_files[relative] = source
            managed.add(relative)
            total_bytes += expected_size

    if isinstance(manifest.get("total_bytes"), bool) or manifest.get("total_bytes") != total_bytes:
        raise SyncError("portfolio total_bytes does not match the selected files")
    return manifest, source_files, managed


def _normalize_remote(value: str) -> str | None:
    value = value.strip()
    if value.startswith("https://") or value.startswith("http://"):
        from urllib.parse import urlparse
        parsed = urlparse(value)
        if parsed.hostname is None or parsed.path.count("/") < 2:
            return None
        owner, repository = parsed.path.strip("/").split("/", 1)
        if repository.endswith(".git"):
            repository = repository[:-4]
        return f"{parsed.hostname.lower()}/{owner.lower()}/{repository.lower()}"
    match = re.fullmatch(r"git@([^:]+):([^/]+)/([^/]+?)(?:\.git)?", value)
    if match:
        return f"{match.group(1).lower()}/{match.group(2).lower()}/{match.group(3).lower()}"
    match = re.fullmatch(r"ssh://git@([^/]+)/([^/]+)/([^/]+?)(?:\.git)?", value)
    if match:
        return f"{match.group(1).lower()}/{match.group(2).lower()}/{match.group(3).lower()}"
    return None


def _verify_site(site_root: Path) -> tuple[Path, str]:
    if site_root.is_symlink():
        raise SyncError("site-root must not be a symlink")
    site_root = site_root.resolve(strict=True)
    if not site_root.is_dir():
        raise SyncError("site-root must be an existing Git checkout directory")
    try:
        result = subprocess.run(
            ["git", "-C", str(site_root), "remote", "get-url", "origin"],
            check=True, capture_output=True, text=True, encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise SyncError("site-root must have a readable origin remote") from error
    remote = result.stdout.strip()
    if _normalize_remote(remote) != EXPECTED_REMOTE:
        raise SyncError("site-root origin must be github.com/delight0517/delight0517.github.io")

    target = site_root.joinpath(*TARGET_RELATIVE.parts)
    if target.is_symlink() or not target.is_dir():
        raise SyncError("site-root must contain the existing ue-asset-atlas viewer folder")
    target_resolved = target.resolve(strict=True)
    if not target_resolved.is_relative_to(site_root):
        raise SyncError("ue-asset-atlas target resolves outside the site repository")
    _ensure_no_symlink(target, site_root)
    for relative in VIEWER_FILES:
        viewer_file = target / relative
        _ensure_no_symlink(viewer_file, target)
        if not viewer_file.is_file():
            raise SyncError(f"viewer file is missing: {relative}")
    return target_resolved, remote


def _read_receipt(target: Path) -> set[str]:
    receipt_path = target / RECEIPT_NAME
    _ensure_no_symlink(receipt_path, target)
    if not receipt_path.exists():
        return set()
    if not receipt_path.is_file():
        raise SyncError("sync receipt is not a regular file")
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SyncError(f"sync receipt is not valid JSON: {error}") from error
    if (not isinstance(receipt, dict) or receipt.get("schema") != RECEIPT_SCHEMA
            or receipt.get("target") != str(TARGET_RELATIVE)):
        raise SyncError("sync receipt schema or target is invalid; no files were changed")
    files = receipt.get("managed_files")
    if not isinstance(files, list) or len(files) > MAX_ASSETS * 2:
        raise SyncError("sync receipt file list is invalid; no files were changed")
    result: set[str] = set()
    for relative in files:
        if not isinstance(relative, str):
            raise SyncError("sync receipt contains an invalid path; no files were changed")
        path = PurePosixPath(relative)
        if (path.is_absolute() or ".." in path.parts or "\\" in relative or len(path.parts) != 3
                or path.parts[0] != "assets" or not ID_PATTERN.fullmatch(path.parts[1])
                or not (path.name == "model.glb" or POSTER_PATTERN.fullmatch(path.name))):
            raise SyncError("sync receipt contains an unsafe path; no files were changed")
        if relative in result:
            raise SyncError("sync receipt repeats a path; no files were changed")
        result.add(relative)
    return result


def _safe_target_file(target: Path, relative: str) -> Path:
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts or "\\" in relative:
        raise SyncError(f"unsafe destination path: {relative!r}")
    destination = target.joinpath(*path.parts)
    _ensure_no_symlink(destination, target)
    return destination


def _atomic_write(path: Path, data: bytes, boundary: Path) -> None:
    _ensure_no_symlink(path, boundary)
    path.parent.mkdir(parents=True, exist_ok=True)
    _ensure_no_symlink(path, boundary)
    handle, temporary = tempfile.mkstemp(prefix=f".{path.name}-", dir=path.parent)
    temp_path = Path(temporary)
    try:
        with os.fdopen(handle, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temp_path, path)
    finally:
        if temp_path.exists():
            temp_path.unlink()


def _apply_changes(target: Path, package_root: Path, manifest: dict[str, Any],
                   source_files: dict[str, Path], managed: set[str], stale: set[str]) -> None:
    old_receipt_path = target / RECEIPT_NAME
    _ensure_no_symlink(old_receipt_path, target)
    old_receipt_bytes = old_receipt_path.read_bytes() if old_receipt_path.is_file() else None
    manifest_path = target / "portfolio.json"
    _ensure_no_symlink(manifest_path, target)
    old_manifest_bytes = manifest_path.read_bytes() if manifest_path.is_file() else None
    touched = set(managed) | stale
    backup: dict[str, bytes | None] = {}
    for relative in touched:
        destination = _safe_target_file(target, relative)
        if destination.exists() and not destination.is_file():
            raise SyncError(f"destination is not a regular file: {relative}")
        backup[relative] = destination.read_bytes() if destination.is_file() else None

    stage = Path(tempfile.mkdtemp(prefix=".portfolio-sync-", dir=target))
    try:
        staged_files: dict[str, Path] = {}
        for relative in managed:
            staged = stage.joinpath(*PurePosixPath(relative).parts)
            staged.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source_files[relative], staged)
            staged_files[relative] = staged
        staged_manifest = stage / "portfolio.json"
        shutil.copyfile(source_files["portfolio.json"], staged_manifest)
        receipt_data = {
            "schema": RECEIPT_SCHEMA,
            "target": str(TARGET_RELATIVE),
            "managed_files": sorted(managed),
        }
        staged_receipt = stage / RECEIPT_NAME
        staged_receipt.write_text(json.dumps(receipt_data, indent=2) + "\n", encoding="utf-8")

        try:
            for relative, staged in staged_files.items():
                destination = _safe_target_file(target, relative)
                destination.parent.mkdir(parents=True, exist_ok=True)
                _ensure_no_symlink(destination, target)
                os.replace(staged, destination)
            for relative in stale:
                destination = _safe_target_file(target, relative)
                if destination.exists():
                    destination.unlink()
            _atomic_write(manifest_path, staged_manifest.read_bytes(), target)
            _atomic_write(old_receipt_path, staged_receipt.read_bytes(), target)
        except Exception:
            # Roll back every managed output file, the public manifest and the receipt.
            for relative, original in backup.items():
                destination = _safe_target_file(target, relative)
                if original is None:
                    if destination.exists():
                        destination.unlink()
                else:
                    _atomic_write(destination, original, target)
            if old_manifest_bytes is None:
                if manifest_path.exists():
                    manifest_path.unlink()
            else:
                _atomic_write(manifest_path, old_manifest_bytes, target)
            if old_receipt_bytes is None:
                if old_receipt_path.exists():
                    old_receipt_path.unlink()
            else:
                _atomic_write(old_receipt_path, old_receipt_bytes, target)
            raise
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def sync(package_path: Path, site_root: Path, apply: bool = False) -> dict[str, Any]:
    if package_path.is_symlink():
        raise SyncError("package root must not be a symlink")
    package_root = package_path.resolve(strict=True)
    if not package_root.is_dir():
        raise SyncError("package path must be an existing directory")
    manifest, source_files, managed = _validate_manifest(package_root)
    target, remote = _verify_site(site_root)
    previous_managed = _read_receipt(target)
    stale = previous_managed - managed
    writes = sorted(managed | {"portfolio.json"})
    removals = sorted(stale)
    if apply:
        _apply_changes(target, package_root, manifest, source_files, managed, stale)
    return {
        "applied": apply,
        "target": str(target),
        "remote": remote,
        "asset_count": len(manifest["assets"]),
        "total_bytes": manifest["total_bytes"],
        "writes": writes,
        "removals": removals,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True, help="Validated exporter output folder")
    parser.add_argument("--site-root", type=Path, required=True, help="Existing local delight0517.github.io checkout")
    parser.add_argument("--apply", action="store_true", help="Copy selected public files locally; never commit/push/deploy")
    args = parser.parse_args()
    try:
        result = sync(args.package, args.site_root, apply=args.apply)
        mode = "Applied locally" if result["applied"] else "Dry run; no files changed"
        print(f"{mode}: {result['asset_count']} public asset(s), {result['total_bytes']} bytes -> {result['target']}")
        for relative in result["writes"]:
            print(f"  write  {relative}")
        for relative in result["removals"]:
            print(f"  remove {relative}")
        if not result["applied"]:
            print("Pass --apply to update the local gallery. This command never commits, pushes, or deploys.")
        return 0
    except (OSError, SyncError) as error:
        print(f"Portfolio sync stopped: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
