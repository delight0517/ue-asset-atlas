"""Validate and plan deterministic, project-scoped UE scene layouts."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
from pathlib import Path

SCHEMA = "blacklion.scene-layout.v1"
LEVEL_PREFIX = "/Game/Developer/AIPlacementQA/"


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def _finite_vec(value, field, positive=False):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(f"{field} must contain exactly three numbers")
    out = []
    for part in value:
        if isinstance(part, bool) or not isinstance(part, (int, float)) or not math.isfinite(part):
            raise ValueError(f"{field} must contain finite numbers")
        if positive and part <= 0:
            raise ValueError(f"{field} values must be positive")
        out.append(float(part))
    return out


def _image_dimensions(path):
    with path.open("rb") as stream:
        header = stream.read(32)
        if header.startswith(b"\x89PNG\r\n\x1a\n") and header[12:16] == b"IHDR":
            return struct.unpack(">II", header[16:24])
        if header[:2] == b"\xff\xd8":
            stream.seek(2)
            while True:
                byte = stream.read(1)
                if not byte:
                    break
                if byte != b"\xff":
                    continue
                marker = stream.read(1)
                while marker == b"\xff":
                    marker = stream.read(1)
                if marker in (b"\xd8", b"\xd9"):
                    continue
                length_bytes = stream.read(2)
                if len(length_bytes) != 2:
                    break
                length = struct.unpack(">H", length_bytes)[0]
                if marker in {bytes([value]) for value in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF)}:
                    data = stream.read(5)
                    if len(data) == 5:
                        height, width = struct.unpack(">HH", data[1:5])
                        return width, height
                    break
                stream.seek(length - 2, 1)
    raise ValueError("reference image must be a readable PNG or JPEG")


def _validate_reference_shot(layout):
    shot_path = Path(layout["reference_shot_manifest"]).expanduser()
    if not shot_path.is_absolute() or not shot_path.is_file():
        raise ValueError("reference_shot_manifest must be an existing absolute file path")
    shot = read_json(shot_path)
    if shot.get("status") != "approved":
        raise ValueError("reference shot status must be approved before scene update")
    if (shot.get("scene") or {}).get("level_path") != layout["target_level"]:
        raise ValueError("reference shot scene level must match the layout QA target")
    image = shot.get("reference_image") or {}
    image_path = image.get("path")
    if not isinstance(image_path, str) or not Path(image_path).is_absolute() or not Path(image_path).is_file():
        raise ValueError("reference image path must be an existing absolute file")
    image_file = Path(image_path)
    if image_file.stat().st_size > 100 * 1024 * 1024:
        raise ValueError("reference image exceeds the 100 MiB input limit")
    hasher = hashlib.sha256()
    with image_file.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    digest = hasher.hexdigest()
    if digest.lower() != str(image.get("sha256") or "").lower():
        raise ValueError("reference image SHA-256 does not match the shot manifest")
    dimensions = _image_dimensions(image_file)
    if [int(dimensions[0]), int(dimensions[1])] != [image.get("width_px"), image.get("height_px")]:
        raise ValueError("reference image pixel dimensions do not match the shot manifest")
    camera = shot.get("camera") or {}
    if not isinstance(camera.get("actor_tag"), str) or not camera["actor_tag"].strip():
        raise ValueError("reference camera requires a stable actor_tag")
    _finite_vec(camera.get("position_m"), "reference camera position_m")
    _finite_vec(camera.get("rotation_deg"), "reference camera rotation_deg")
    fov = camera.get("horizontal_fov_deg")
    if isinstance(fov, bool) or not isinstance(fov, (int, float)) or not math.isfinite(fov) or not 0 < fov < 180:
        raise ValueError("reference camera FOV must be between 0 and 180 degrees")
    sensor = camera.get("sensor_width_mm", 36.0)
    if isinstance(sensor, bool) or not isinstance(sensor, (int, float)) or not math.isfinite(sensor) or sensor <= 0:
        raise ValueError("reference camera sensor_width_mm must be positive")
    output = shot.get("output") or {}
    if output.get("crop_locked") is not True or [output.get("width_px"), output.get("height_px")] != [image.get("width_px"), image.get("height_px")]:
        raise ValueError("reference output dimensions and locked crop must match the source image")


def validate_layout(layout, project_config, catalog):
    errors = []
    if layout.get("schema") != SCHEMA:
        errors.append("unsupported scene layout schema")
    project_id = layout.get("project_id")
    expected_project = project_config.get("project_id")
    if project_id != expected_project or project_id != catalog.get("project_id"):
        errors.append("project_id does not match both project.json and catalog.json")
    if not isinstance(layout.get("scene_id"), str) or not re.fullmatch(r"[A-Za-z0-9_-]+", layout["scene_id"]):
        errors.append("scene_id must use letters, digits, underscore or hyphen")
    level = layout.get("target_level", "")
    if not isinstance(level, str) or not re.fullmatch(r"/Game/Developer/AIPlacementQA/[A-Za-z0-9_/-]+", level) or ".." in level:
        errors.append(f"target_level must be inside {LEVEL_PREFIX}")
    if layout.get("units") != "cm" or layout.get("up_axis") != "Z" or layout.get("ground_plane") != "XY":
        errors.append("world contract must be centimeters, Z-up and XY ground plane")
    instances = layout.get("instances")
    if not isinstance(instances, list) or not 1 <= len(instances) <= 500:
        errors.append("instances must contain between 1 and 500 placements")
        instances = []
    entries = {item.get("id"): item for item in catalog.get("assets", []) if isinstance(item, dict)}
    seen_instances = set()
    seen_assets = set()
    for index, item in enumerate(instances):
        prefix = f"instances[{index}]"
        if not isinstance(item, dict):
            errors.append(f"{prefix} must be an object")
            continue
        iid, aid = item.get("instance_id"), item.get("asset_id")
        if not isinstance(iid, str) or not re.fullmatch(r"[A-Za-z0-9_-]+", iid):
            errors.append(f"{prefix}.instance_id is invalid")
        elif iid in seen_instances:
            errors.append(f"duplicate instance_id: {iid}")
        else:
            seen_instances.add(iid)
        entry = entries.get(aid)
        if not entry:
            errors.append(f"{prefix} asset_id is not in this project's catalog: {aid}")
        elif entry.get("project") != project_id:
            errors.append(f"{prefix} asset belongs to another project: {aid}")
        elif not entry.get("preview_mesh"):
            errors.append(f"{prefix} asset has no registered preview_mesh: {aid}")
        else:
            seen_assets.add(aid)
        for field, positive in (("location_cm", False), ("rotation_deg", False), ("scale", True)):
            try:
                item[field] = _finite_vec(item.get(field), f"{prefix}.{field}", positive)
            except ValueError as exc:
                errors.append(str(exc))
        if not isinstance(item.get("grounded"), bool):
            errors.append(f"{prefix}.grounded must be boolean")
        orientation_mode = item.get("orientation_mode")
        if orientation_mode not in ("world_axis_aligned", "free"):
            errors.append(f"{prefix}.orientation_mode must be world_axis_aligned or free")
        elif orientation_mode == "world_axis_aligned":
            rotation = item.get("rotation_deg", [0.0, 0.0, 0.0])
            if len(rotation) == 3 and (abs(rotation[0]) > 0.01 or abs(rotation[2]) > 0.01):
                errors.append(f"{prefix} violates world-axis alignment: pitch and roll must be 0 degrees")
    reference = layout.get("reference_shot_manifest")
    ref_status = layout.get("reference_status", "none")
    if reference is None and ref_status != "none":
        errors.append("reference_status must be none when no reference manifest is supplied")
    if reference is not None and (not isinstance(reference, str) or ref_status != "approved"):
        errors.append("reference image alignment requires an explicitly approved shot manifest")
    if reference is not None:
        try:
            _validate_reference_shot(layout)
        except (OSError, json.JSONDecodeError, ValueError, KeyError, TypeError) as exc:
            errors.append(str(exc))
    if errors:
        raise ValueError("; ".join(errors))
    unique_meshes = {entries[aid]["preview_mesh"] for aid in seen_assets}
    return {"instance_count": len(instances), "unique_asset_count": len(seen_assets),
            "unique_mesh_load_count": len(unique_meshes), "target_level": level,
            "project_id": project_id, "reference_enabled": reference is not None}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True, help="AssetArchive_Inbox/project.json")
    parser.add_argument("--catalog", type=Path, required=True, help="Saved/BlackLionAssetArchive/catalog.json")
    args = parser.parse_args(argv)
    try:
        summary = validate_layout(read_json(args.manifest), read_json(args.project), read_json(args.catalog))
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"SCENE_LAYOUT_REJECTED: {exc}", file=sys.stderr)
        return 2
    print(json.dumps({"status": "SCENE_LAYOUT_VALID", **summary}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
