import sys
import hashlib
import json
import struct
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from scene_layout import validate_layout


class SceneLayoutTests(unittest.TestCase):
    def setUp(self):
        self.project = {"project_id": "Synthetic_Project"}
        self.catalog = {"project_id": "Synthetic_Project", "assets": [
            {"id": "A", "project": "Synthetic_Project", "preview_mesh": "/Game/A.A"},
            {"id": "B", "project": "Synthetic_Project", "preview_mesh": "/Game/B.B"},
        ]}
        self.layout = {"schema": "blacklion.scene-layout.v1", "project_id": "Synthetic_Project",
                       "scene_id": "Test", "target_level": "/Game/Developer/AIPlacementQA/Test",
                       "units": "cm", "up_axis": "Z", "ground_plane": "XY", "reference_status": "none",
                       "reference_shot_manifest": None, "instances": [
                           {"instance_id": "one", "asset_id": "A", "location_cm": [0, 0, 0],
                            "rotation_deg": [0, 0, 0], "scale": [1, 1, 1], "grounded": True, "orientation_mode": "world_axis_aligned"},
                           {"instance_id": "two", "asset_id": "A", "location_cm": [2000, 0, 0],
                            "rotation_deg": [0, 90, 0], "scale": [1, 1, 1], "grounded": True, "orientation_mode": "world_axis_aligned"},
                           {"instance_id": "three", "asset_id": "B", "location_cm": [0, 2000, 0],
                            "rotation_deg": [0, 0, 0], "scale": [1, 1, 1], "grounded": True, "orientation_mode": "world_axis_aligned"},
                       ]}

    def test_caches_unique_meshes_for_multiple_instances(self):
        result = validate_layout(self.layout, self.project, self.catalog)
        self.assertEqual(result["instance_count"], 3)
        self.assertEqual(result["unique_asset_count"], 2)
        self.assertEqual(result["unique_mesh_load_count"], 2)

    def test_rejects_cross_project_layout(self):
        self.layout["project_id"] = "Other_Project"
        with self.assertRaisesRegex(ValueError, "project_id"):
            validate_layout(self.layout, self.project, self.catalog)

    def test_rejects_unknown_asset_before_apply(self):
        self.layout["instances"][0]["asset_id"] = "missing"
        with self.assertRaisesRegex(ValueError, "not in this project's catalog"):
            validate_layout(self.layout, self.project, self.catalog)

    def test_rejects_tilted_world_contract_and_bad_scale(self):
        self.layout["ground_plane"] = "XZ"
        with self.assertRaisesRegex(ValueError, "world contract"):
            validate_layout(self.layout, self.project, self.catalog)
        self.layout["ground_plane"] = "XY"
        self.layout["instances"][0]["scale"] = [1, 0, 1]
        with self.assertRaisesRegex(ValueError, "positive"):
            validate_layout(self.layout, self.project, self.catalog)

    def test_rejects_unapproved_reference(self):
        self.layout["reference_shot_manifest"] = "reference.json"
        self.layout["reference_status"] = "provisional"
        with self.assertRaisesRegex(ValueError, "approved shot manifest"):
            validate_layout(self.layout, self.project, self.catalog)

    def test_rejects_tilted_axis_aligned_prop(self):
        self.layout["instances"][0]["rotation_deg"] = [0, 0, 8]
        with self.assertRaisesRegex(ValueError, "world-axis alignment"):
            validate_layout(self.layout, self.project, self.catalog)

    def test_accepts_hashed_locked_reference_camera_for_exact_qa_level(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image = root / "fixture.png"
            image.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\x0dIHDR" + struct.pack(">II", 640, 360) + b"\x08\x02\x00\x00\x00")
            shot_path = root / "shot.json"
            shot = {"status": "approved", "scene": {"level_path": self.layout["target_level"]},
                    "reference_image": {"path": str(image), "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                                        "width_px": 640, "height_px": 360},
                    "camera": {"actor_tag": "BL_REF_TEST", "position_m": [0, -4, 2],
                               "rotation_deg": [0, 0, 0], "horizontal_fov_deg": 60, "sensor_width_mm": 36},
                    "output": {"width_px": 640, "height_px": 360, "crop_locked": True}}
            shot_path.write_text(json.dumps(shot), encoding="utf-8")
            self.layout["reference_shot_manifest"] = str(shot_path)
            self.layout["reference_status"] = "approved"
            result = validate_layout(self.layout, self.project, self.catalog)
            self.assertTrue(result["reference_enabled"])

    def test_rejects_reference_image_hash_mismatch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image = root / "fixture.png"
            image.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\x0dIHDR" + struct.pack(">II", 640, 360) + b"\x08\x02\x00\x00\x00")
            shot_path = root / "shot.json"
            shot = {"status": "approved", "scene": {"level_path": self.layout["target_level"]},
                    "reference_image": {"path": str(image), "sha256": "0" * 64, "width_px": 640, "height_px": 360},
                    "camera": {"actor_tag": "BL_REF_TEST", "position_m": [0, -4, 2],
                               "rotation_deg": [0, 0, 0], "horizontal_fov_deg": 60},
                    "output": {"width_px": 640, "height_px": 360, "crop_locked": True}}
            shot_path.write_text(json.dumps(shot), encoding="utf-8")
            self.layout["reference_shot_manifest"] = str(shot_path)
            self.layout["reference_status"] = "approved"
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                validate_layout(self.layout, self.project, self.catalog)


if __name__ == "__main__":
    unittest.main(verbosity=2)
