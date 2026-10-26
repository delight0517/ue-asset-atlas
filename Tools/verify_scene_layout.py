"""Reopen a saved isolated scene-layout QA level and verify persisted transforms."""
import json
import math
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from scene_layout import validate_layout


def read_json(path):
    with open(path, "r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def close(a, b, tolerance=0.1):
    return abs(float(a) - float(b)) <= tolerance


def run():
    manifest_path = os.environ.get("BL_SCENE_LAYOUT_MANIFEST")
    project_file = os.environ.get("BL_SCENE_LAYOUT_PROJECT")
    catalog_path = os.environ.get("BL_SCENE_LAYOUT_CATALOG")
    if not manifest_path or not project_file or not catalog_path:
        raise RuntimeError("Scene verification must run through the guarded project wrapper")
    layout = read_json(manifest_path)
    project = read_json(os.path.join(os.path.dirname(project_file), "AssetArchive_Inbox", "project.json"))
    catalog = read_json(catalog_path)
    summary = validate_layout(layout, project, catalog)
    if not unreal.EditorAssetLibrary.does_asset_exist(layout["target_level"]):
        raise RuntimeError("Saved QA level package is missing")
    if not unreal.EditorLevelLibrary.load_level(layout["target_level"]):
        raise RuntimeError("Could not reopen saved QA level")
    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    tag = "BL_AI_LAYOUT::" + layout["scene_id"]
    placed = [actor for actor in actors if tag in [str(value) for value in actor.tags]]
    expected_ids = {item["instance_id"] for item in layout["instances"]}
    actual_ids = [str(value).split("::", 1)[1] for actor in placed for value in actor.tags
                  if str(value).startswith("BL_AI_INSTANCE::")]
    if len(placed) != len(layout["instances"]) or set(actual_ids) != expected_ids or len(actual_ids) != len(set(actual_ids)):
        raise RuntimeError("Persisted actor count or instance IDs do not match the manifest")
    entries = {entry["id"]: entry for entry in catalog["assets"]}
    instance_by_id = {item["instance_id"]: item for item in layout["instances"]}
    by_id = {}
    for actor in placed:
        ids = [str(value).split("::", 1)[1] for value in actor.tags if str(value).startswith("BL_AI_INSTANCE::")]
        if len(ids) != 1:
            raise RuntimeError("Actor has missing or duplicate stable instance tags")
        by_id[ids[0]] = actor
    for instance_id, actor in by_id.items():
        item = instance_by_id[instance_id]
        expected_mesh = entries[item["asset_id"]]["preview_mesh"]
        mesh = actor.static_mesh_component.get_editor_property("static_mesh")
        if not mesh or mesh.get_path_name() != expected_mesh:
            raise RuntimeError("Persisted mesh reference differs for " + instance_id)
        pos, rot, scale = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
        if not close(pos.x, item["location_cm"][0]) or not close(pos.y, item["location_cm"][1]):
            raise RuntimeError("Persisted X/Y changed for " + instance_id)
        if item["grounded"] and not close(actor.get_actor_bounds(False)[0].z - actor.get_actor_bounds(False)[1].z,
                                           item["location_cm"][2], 0.1):
            raise RuntimeError("Persisted floor contact failed for " + instance_id)
        for actual, expected in zip((rot.pitch, rot.yaw, rot.roll), item["rotation_deg"]):
            if not close(actual, expected, 0.02):
                raise RuntimeError("Persisted rotation changed for " + instance_id)
        for actual, expected in zip((scale.x, scale.y, scale.z), item["scale"]):
            if not close(actual, expected, 0.001):
                raise RuntimeError("Persisted scale changed for " + instance_id)
    reference_camera_tested = False
    if layout.get("reference_shot_manifest"):
        shot = read_json(layout["reference_shot_manifest"])
        camera_data = shot["camera"]
        camera_tag = camera_data["actor_tag"]
        camera_actors = [actor for actor in actors if camera_tag in [str(value) for value in actor.tags]]
        if len(camera_actors) != 1 or not isinstance(camera_actors[0], unreal.CineCameraActor):
            raise RuntimeError("Persisted reference CineCameraActor is missing or duplicated")
        camera_actor = camera_actors[0]
        cam_loc, cam_rot = camera_actor.get_actor_location(), camera_actor.get_actor_rotation()
        for actual, expected in zip((cam_loc.x, cam_loc.y, cam_loc.z),
                                    [float(value) * 100.0 for value in camera_data["position_m"]]):
            if not close(actual, expected, 0.1):
                raise RuntimeError("Persisted reference camera translation differs")
        for actual, expected in zip((cam_rot.pitch, cam_rot.yaw, cam_rot.roll), camera_data["rotation_deg"]):
            if not close(actual, expected, 0.02):
                raise RuntimeError("Persisted reference camera rotation differs")
        component = camera_actor.get_component_by_class(unreal.CineCameraComponent)
        focal = component.get_editor_property("current_focal_length")
        sensor = float(camera_data.get("sensor_width_mm", 36.0))
        actual_fov = math.degrees(2.0 * math.atan(sensor / (2.0 * float(focal))))
        if not close(actual_fov, camera_data["horizontal_fov_deg"], 0.02):
            raise RuntimeError("Persisted reference camera lens differs")
        reference_camera_tested = True
    report = {"status": "SCENE_LAYOUT_PERSISTED_OK", **summary, "actors_reopened": len(placed),
              "unique_meshes_referenced": len({entries[item["asset_id"]]["preview_mesh"] for item in layout["instances"]}),
              "reference_camera_tested": reference_camera_tested}
    unreal.log("SCENE_LAYOUT_PERSISTED_OK " + json.dumps(report, sort_keys=True))
    output = os.path.join(os.path.dirname(manifest_path), "scene-layout-reopen-report.json")
    with open(output, "w", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)


run()
