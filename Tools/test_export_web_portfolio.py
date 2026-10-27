import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("export_web_portfolio.py")
spec = importlib.util.spec_from_file_location("export_web_portfolio", SCRIPT)
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


def minimal_glb():
    payload = b'{"asset":{"version":"2.0"}} '
    return b"glTF" + (2).to_bytes(4, "little") + (20 + len(payload)).to_bytes(4, "little") + len(payload).to_bytes(4, "little") + (0x4E4F534A).to_bytes(4, "little") + payload


class WebPortfolioExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.source = self.base / "source"
        (self.source / "objects").mkdir(parents=True)
        (self.source / "posters").mkdir()
        (self.source / "objects/sample.glb").write_bytes(minimal_glb())
        (self.source / "posters/sample.webp").write_bytes(b"RIFF\x00\x00\x00\x00WEBP")
        self.selection = self.base / "selection.json"
        self.data = {"schema": exporter.SCHEMA, "title": "Portfolio", "assets": [{
            "id": "sample", "name": "Sample", "visibility": "public",
            "model_glb": "objects/sample.glb", "poster": "posters/sample.webp"
        }]}
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        self.output = self.base / "out"

    def tearDown(self):
        self.temp.cleanup()

    def test_exports_only_selected_web_ready_payload_with_checksums(self):
        (self.source / "objects/unselected.glb").write_bytes(b"should not be included")
        result = exporter.package(self.selection, self.source, self.output)
        self.assertEqual(result["asset_count"], 1)
        self.assertEqual(result["visibility"], "public")
        self.assertEqual(result["loading"], "per-asset")
        self.assertEqual(len(result["assets"][0]["model_sha256"]), 64)
        self.assertEqual(sorted(p.relative_to(self.output).as_posix() for p in self.output.rglob("*")), [
            "assets", "assets/sample", "assets/sample/model.glb", "assets/sample/poster.webp", "portfolio.json"
        ])
        self.assertNotIn("unselected", (self.output / "portfolio.json").read_text(encoding="utf-8"))

    def test_preserves_explicit_project_group_metadata_in_public_derivative(self):
        self.data["assets"][0].update(project_id="corridor-set", project_name="Corridor Set")
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        result = exporter.package(self.selection, self.source, self.output)
        self.assertEqual(result["assets"][0]["project_id"], "corridor-set")
        self.assertEqual(result["assets"][0]["project_name"], "Corridor Set")

    def test_rejects_invalid_project_group_id(self):
        self.data["assets"][0]["project_id"] = "../private"
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        with self.assertRaisesRegex(exporter.ExportError, "project_id"):
            exporter.package(self.selection, self.source, self.output)

    def test_private_assets_are_excluded_from_public_build(self):
        private_model = self.source / "objects/private.glb"
        private_poster = self.source / "posters/private.webp"
        private_model.write_bytes(minimal_glb())
        private_poster.write_bytes(b"private poster")
        self.data["assets"].append({"id": "private", "name": "Private", "visibility": "private",
                                    "model_glb": "objects/private.glb", "poster": "posters/private.webp"})
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        result = exporter.package(self.selection, self.source, self.output)
        self.assertEqual([item["id"] for item in result["assets"]], ["sample"])
        self.assertFalse((self.output / "assets/private").exists())

    def test_fails_closed_without_an_explicit_public_item(self):
        self.data["assets"][0]["visibility"] = "private"
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        with self.assertRaisesRegex(exporter.ExportError, "no explicitly public"):
            exporter.package(self.selection, self.source, self.output)

    def test_rejects_path_traversal_without_creating_output(self):
        self.data["assets"][0]["model_glb"] = "../secret.glb"
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        with self.assertRaises(exporter.ExportError):
            exporter.package(self.selection, self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_invalid_container_and_oversize_model(self):
        model = self.source / "objects/sample.glb"
        model.write_bytes(b"not glb")
        with self.assertRaisesRegex(exporter.ExportError, "valid GLB"):
            exporter.package(self.selection, self.source, self.output)
        model.write_bytes(minimal_glb())
        with self.assertRaisesRegex(exporter.ExportError, "limit"):
            exporter.package(self.selection, self.source, self.output, model_limit=8)
        self.assertFalse(self.output.exists())

    def test_rejects_compressed_extensions_without_local_decoders(self):
        payload = b'{"asset":{"version":"2.0"},"extensionsUsed":["KHR_draco_mesh_compression"]}'
        payload += b" " * ((-len(payload)) % 4)
        model = self.source / "objects/sample.glb"
        model.write_bytes(b"glTF" + (2).to_bytes(4, "little") + (20 + len(payload)).to_bytes(4, "little")
                          + len(payload).to_bytes(4, "little") + (0x4E4F534A).to_bytes(4, "little") + payload)
        with self.assertRaisesRegex(exporter.ExportError, "without a local decoder"):
            exporter.package(self.selection, self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_rejects_duplicate_ids_and_existing_output(self):
        self.data["assets"].append(dict(self.data["assets"][0]))
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        with self.assertRaisesRegex(exporter.ExportError, "duplicate"):
            exporter.package(self.selection, self.source, self.output)
        self.data["assets"].pop()
        self.selection.write_text(json.dumps(self.data), encoding="utf-8")
        self.output.mkdir()
        with self.assertRaisesRegex(exporter.ExportError, "already exists"):
            exporter.package(self.selection, self.source, self.output)


if __name__ == "__main__":
    unittest.main()
