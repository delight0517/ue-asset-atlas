"""Apply a validated JSON layout to its isolated UE 5.8 AI-placement QA level.

Run only through apply_scene_layout.ps1. The manifest is untrusted data: this
script never evaluates code, shell text, or asset-provided instructions.
"""
import hashlib
import json
import math
import os
import sys
import time
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scene_layout import validate_layout


def load_json(path):
    with open(path, "r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def require_reference(layout, manifest_path):
    ref_path = layout.get("reference_shot_manifest")
    if ref_path is None:
        return None
    ref_path = os.path.abspath(ref_path)
    shot = load_json(ref_path)
    if shot.get("status") != "approved":
        raise ValueError("Reference shot is not approved")
    scene_level = (shot.get("scene") or {}).get("level_path")
    if scene_level != layout["target_level"]:
        raise ValueError("Reference shot level does not match layout target_level")
    image = shot.get("reference_image") or {}
    image_path = image.get("path")
    if not image_path or not os.path.isfile(image_path):
        raise ValueError("Approved reference image is missing")
    digest = hashlib.sha256()
    with open(image_path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest().lower() != str(image.get("sha256") or "").lower():
        raise ValueError("Reference image SHA-256 does not match the shot manifest")
    camera = shot.get("camera") or {}
    output = shot.get("output") or {}
    if output.get("crop_locked") is not True:
        raise ValueError("Reference shot crop must be locked")
    if output.get("width_px") != image.get("width_px") or output.get("height_px") != image.get("height_px"):
        raise ValueError("Reference shot output dimensions do not match the image dimensions")
    position = camera.get("position_m")
    rotation = camera.get("rotation_deg")
    fov = camera.get("horizontal_fov_deg")
    if not isinstance(position, list) or len(position) != 3 or not all(isinstance(v, (int, float)) for v in position):
        raise ValueError("Reference camera position_m must contain three values")
    if not isinstance(rotation, list) or len(rotation) != 3 or not all(isinstance(v, (int, float)) for v in rotation):
        raise ValueError("Reference camera rotation_deg must contain three values")
    if not isinstance(fov, (int, float)) or not 0 < fov < 180:
        raise ValueError("Reference camera FOV is invalid")
    return {"manifest": ref_path, "shot": shot, "camera": camera}


def bounds(actor):
    origin, extent = actor.get_actor_bounds(False)
    return (origin.x - extent.x, origin.x + extent.x,
            origin.y - extent.y, origin.y + extent.y,
            origin.z - extent.z, origin.z + extent.z)


def overlap_pairs(actors):
    # X sweep: near O(n log n + k), rather than comparing every pair.
    ordered = sorted(((bounds(actor), actor.get_actor_label()) for actor in actors), key=lambda item: item[0][0])
    active = []
    found = []
    for box, label in ordered:
        active = [(other_box, other_label) for other_box, other_label in active if other_box[1] > box[0] + 0.01]
        for other_box, other_label in active:
            intersects = (box[0] < other_box[1] - 0.01 and box[1] > other_box[0] + 0.01 and
                          box[2] < other_box[3] - 0.01 and box[3] > other_box[2] + 0.01 and
                          box[4] < other_box[5] - 0.01 and box[5] > other_box[4] + 0.01)
            if intersects:
                found.append((other_label, label))
        active.append((box, label))
    return found


def set_reference_camera(reference, actors):
    if not reference:
        return None
    unreal.EditorLevelLibrary
    shot = reference["shot"]
    camera_data = reference["camera"]
    tag = camera_data["actor_tag"]
    matches = [actor for actor in actors if tag in [str(value) for value in actor.tags]]
    if len(matches) > 1:
        raise ValueError("Reference camera tag is not unique")
    camera = matches[0] if matches else unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.CineCameraActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if not camera:
        raise RuntimeError("Could not create reference CineCameraActor")
    camera.set_actor_label(tag)
    camera.tags = list(camera.tags) + ([] if tag in [str(value) for value in camera.tags] else [unreal.Name(tag)])
    camera_data = reference["camera"]
    camera.set_actor_location(unreal.Vector(*(float(value) * 100.0 for value in camera_data["position_m"])), False, True)
    camera.set_actor_rotation(unreal.Rotator(*(float(value) for value in camera_data["rotation_deg"])), False)
    component = camera.get_component_by_class(unreal.CineCameraComponent)
    if not component:
        raise RuntimeError("Reference CineCameraActor has no CineCameraComponent")
    filmback = component.get_editor_property("filmback")
    filmback.set_editor_property("sensor_width", float(camera_data.get("sensor_width_mm", 36.0)))
    if camera_data.get("sensor_height_mm") is not None:
        filmback.set_editor_property("sensor_height", float(camera_data["sensor_height_mm"]))
    component.set_editor_property("filmback", filmback)
    sensor = float(camera_data.get("sensor_width_mm", 36.0))
    fov = float(camera_data["horizontal_fov_deg"])
    focal_mm = sensor / (2.0 * math.tan(math.radians(fov) / 2.0))
    component.set_editor_property("current_focal_length", focal_mm)
    shot["camera"]["actor_path"] = camera.get_path_name()
    return camera


def run():
    started = time.perf_counter()
    layout_path = os.environ.get("BL_SCENE_LAYOUT_MANIFEST")
    project_file = os.environ.get("BL_SCENE_LAYOUT_PROJECT")
    catalog_path = os.environ.get("BL_SCENE_LAYOUT_CATALOG")
    if not layout_path or not project_file or not catalog_path:
        raise RuntimeError("Set manifest/project/catalog through the guarded PowerShell runner")
    layout = load_json(layout_path)
    project_config = load_json(os.path.join(os.path.dirname(project_file), "AssetArchive_Inbox", "project.json"))
    catalog = load_json(catalog_path)
    validation_started = time.perf_counter()
    summary = validate_layout(layout, project_config, catalog)
    reference = require_reference(layout, layout_path)
    validation_ms = round((time.perf_counter() - validation_started) * 1000.0, 2)
    map_path = layout["target_level"]
    if not unreal.EditorAssetLibrary.does_asset_exist(map_path):
        if not unreal.EditorLevelLibrary.new_level(map_path):
            raise RuntimeError("Could not create isolated QA level: " + map_path)
    elif not unreal.EditorLevelLibrary.load_level(map_path):
        raise RuntimeError("Could not load isolated QA level: " + map_path)

    scene_tag = "BL_AI_LAYOUT::" + layout["scene_id"]
    actors_before = unreal.EditorLevelLibrary.get_all_level_actors()
    old_owned = [actor for actor in actors_before if scene_tag in [str(tag) for tag in actor.tags]]
    old_camera_tag = None
    if reference:
        old_camera_tag = (reference["camera"].get("actor_tag") or "").strip()
    for actor in old_owned:
        unreal.EditorLevelLibrary.destroy_actor(actor)
    if old_camera_tag:
        for actor in actors_before:
            if old_camera_tag in [str(tag) for tag in actor.tags]:
                unreal.EditorLevelLibrary.destroy_actor(actor)

    entries = {entry["id"]: entry for entry in catalog["assets"]}
    mesh_cache = {}
    spawned = []
    item_labels = {}
    label_instances = {}
    requested_floor_z = {}
    mesh_load_ms = 0.0
    placement_started = time.perf_counter()
    for item in layout["instances"]:
        entry = entries[item["asset_id"]]
        mesh_path = entry["preview_mesh"]
        mesh = mesh_cache.get(mesh_path)
        if mesh is None:
            load_started = time.perf_counter()
            mesh = unreal.load_asset(mesh_path)
            mesh_load_ms += (time.perf_counter() - load_started) * 1000.0
            if not mesh or not isinstance(mesh, unreal.StaticMesh):
                raise RuntimeError("Could not load registered StaticMesh: " + mesh_path)
            mesh_cache[mesh_path] = mesh
        location = [float(value) for value in item["location_cm"]]
        rotation = unreal.Rotator(*(float(value) for value in item["rotation_deg"]))
        scale_values = [float(value) for value in item["scale"]]
        if item["grounded"]:
            b = mesh.get_bounds()
            center = b.origin
            extent = b.box_extent
            transform = unreal.Transform(unreal.Vector(0, 0, 0), rotation, unreal.Vector(*scale_values))
            local_corners = [unreal.Vector(center.x + sx * extent.x, center.y + sy * extent.y, center.z + sz * extent.z)
                             for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
            min_z = min(transform.transform_location(corner).z for corner in local_corners)
            location[2] -= min_z
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(*location), rotation)
        if not actor:
            raise RuntimeError("UE failed to spawn instance " + item["instance_id"])
        label = "BLAI_" + layout["scene_id"] + "_" + item["instance_id"]
        actor.set_actor_label(label)
        actor.set_actor_scale3d(unreal.Vector(*scale_values))
        actor.static_mesh_component.set_static_mesh(mesh)
        actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        actor.tags = [unreal.Name(scene_tag), unreal.Name("BL_AI_INSTANCE::" + item["instance_id"])]
        spawned.append(actor)
        item_labels[item["instance_id"]] = label
        label_instances[label] = item["instance_id"]
        if item["grounded"]:
            requested_floor_z[item["instance_id"]] = float(item["location_cm"][2])
    placement_ms = round((time.perf_counter() - placement_started) * 1000.0, 2)

    verification_started = time.perf_counter()
    overlaps = overlap_pairs(spawned)
    if overlaps:
        for actor in spawned:
            unreal.EditorLevelLibrary.destroy_actor(actor)
        raise RuntimeError("AABB overlap check rejected placements: " + json.dumps(overlaps))
    floor_errors = []
    for actor in spawned:
        box = bounds(actor)
        instance_id = label_instances.get(actor.get_actor_label())
        if instance_id is not None and abs(box[4] - requested_floor_z[instance_id]) > 0.1:
            floor_errors.append({"instance": instance_id, "actual_bottom_cm": box[4],
                                 "requested_ground_cm": requested_floor_z[instance_id]})
    if floor_errors:
        for actor in spawned:
            unreal.EditorLevelLibrary.destroy_actor(actor)
        raise RuntimeError("Ground contact verification failed: " + json.dumps(floor_errors))
    verification_ms = round((time.perf_counter() - verification_started) * 1000.0, 2)
    camera = set_reference_camera(reference, unreal.EditorLevelLibrary.get_all_level_actors())
    save_started = time.perf_counter()
    unreal.EditorLevelLibrary.save_current_level()
    save_ms = round((time.perf_counter() - save_started) * 1000.0, 2)
    elapsed_ms = round((time.perf_counter() - started) * 1000.0, 2)
    report = {"status": "SCENE_LAYOUT_APPLIED", **summary, "spawned": len(spawned),
              "unique_mesh_loads": len(mesh_cache), "bounds_overlaps": 0,
              "ground_contact_max_error_cm": max((abs(bounds(actor)[4] - requested_floor_z[label_instances[actor.get_actor_label()]])
                                                   for actor in spawned if label_instances[actor.get_actor_label()] in requested_floor_z), default=0.0),
              "reference_camera": camera.get_path_name() if camera else None,
              "timings_ms": {"manifest_validation": validation_ms, "unique_mesh_loads": round(mesh_load_ms, 2),
                             "placement_pass": placement_ms, "bounds_and_floor_checks": verification_ms,
                             "save_level": save_ms, "executor_total": elapsed_ms},
              "saved_level": map_path}
    unreal.log("SCENE_LAYOUT_APPLIED " + json.dumps(report, sort_keys=True))
    output_path = os.path.join(os.path.dirname(layout_path), "scene-layout-report.json")
    with open(output_path, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)


run()
