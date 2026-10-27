"""Local-only safety tests for sync_web_portfolio."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("sync_web_portfolio.py")
SPEC = importlib.util.spec_from_file_location("sync_web_portfolio", SCRIPT)
assert SPEC and SPEC.loader
syncer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(syncer)


def write_package(root: Path, asset_ids: tuple[str, ...] = ("chair",)) -> None:
    assets = []
    total = 0
    for asset_id in asset_ids:
        folder = root / "assets" / asset_id
        folder.mkdir(parents=True)
        # Minimal valid GLB 2 container with the required glTF asset metadata.
        document = json.dumps({"asset": {"version": "2.0"}}, separators=(",", ":")).encode()
        document += b" " * (-len(document) % 4)
        model = (b"glTF" + struct.pack("<II", 2, 20 + len(document))
                 + struct.pack("<II", len(document), 0x4E4F534A) + document)
        poster = ("poster-" + asset_id).encode()
        (folder / "model.glb").write_bytes(model)
        (folder / "poster.webp").write_bytes(poster)
        total += len(model) + len(poster)
        assets.append({
            "id": asset_id, "name": asset_id.title(), "description": "", "alt": asset_id,
            "model": f"assets/{asset_id}/model.glb", "model_bytes": len(model),
            "model_sha256": hashlib.sha256(model).hexdigest(),
            "poster": f"assets/{asset_id}/poster.webp", "poster_bytes": len(poster),
            "poster_sha256": hashlib.sha256(poster).hexdigest(), "license": "", "attribution": "",
        })
    manifest = {"schema": syncer.SCHEMA, "title": "Test portfolio", "visibility": "public",
                "loading": "per-asset", "assets": assets, "asset_count": len(assets), "total_bytes": total}
    (root / "portfolio.json").write_text(json.dumps(manifest), encoding="utf-8")


def make_site(root: Path, remote: str = "https://github.com/delight0517/delight0517.github.io.git") -> Path:
    root.mkdir(parents=True)
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    subprocess.run(["git", "-C", str(root), "remote", "add", "origin", remote], check=True)
    viewer = root / "ue-asset-atlas"
    (viewer / "vendor").mkdir(parents=True)
    for filename in syncer.VIEWER_FILES:
        path = viewer / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("viewer-owned", encoding="utf-8")
    return viewer


class SyncPortfolioTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        self.package.mkdir()
        write_package(self.package)
        self.site = self.root / "site"
        self.viewer = make_site(self.site)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_default_is_dry_run(self) -> None:
        result = syncer.sync(self.package, self.site)
        self.assertFalse(result["applied"])
        self.assertEqual(result["writes"], ["assets/chair/model.glb", "assets/chair/poster.webp", "portfolio.json"])
        self.assertFalse((self.viewer / "portfolio.json").exists())
        self.assertFalse((self.viewer / "assets").exists())
        self.assertFalse((self.viewer / syncer.RECEIPT_NAME).exists())

    def test_apply_copies_only_gallery_payload_and_receipt(self) -> None:
        syncer.sync(self.package, self.site, apply=True)
        self.assertEqual((self.viewer / "portfolio.json").read_bytes(), (self.package / "portfolio.json").read_bytes())
        self.assertEqual((self.viewer / "assets/chair/model.glb").read_bytes(), (self.package / "assets/chair/model.glb").read_bytes())
        self.assertEqual((self.viewer / "index.html").read_text(), "viewer-owned")
        self.assertEqual(syncer._read_receipt(self.viewer), {"assets/chair/model.glb", "assets/chair/poster.webp"})

    def test_apply_removes_only_prior_receipt_files_not_selected(self) -> None:
        syncer.sync(self.package, self.site, apply=True)
        replacement = self.root / "replacement"
        replacement.mkdir()
        write_package(replacement, ("lamp",))
        syncer.sync(replacement, self.site, apply=True)
        self.assertFalse((self.viewer / "assets/chair/model.glb").exists())
        self.assertTrue((self.viewer / "assets/lamp/model.glb").is_file())
        self.assertEqual((self.viewer / "portfolio.js").read_text(), "viewer-owned")

    def test_rejects_private_manifest_and_does_not_write(self) -> None:
        manifest_path = self.package / "portfolio.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["assets"][0]["visibility"] = "private"
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaises(syncer.SyncError):
            syncer.sync(self.package, self.site, apply=True)
        self.assertFalse((self.viewer / "portfolio.json").exists())

    def test_rejects_wrong_origin(self) -> None:
        bad_site = self.root / "wrong-site"
        bad_viewer = make_site(bad_site, "https://github.com/someone/other.git")
        with self.assertRaises(syncer.SyncError):
            syncer.sync(self.package, bad_site, apply=True)
        self.assertFalse((bad_viewer / "portfolio.json").exists())

    def test_rejects_path_traversal_and_checksum_mismatch(self) -> None:
        manifest_path = self.package / "portfolio.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["assets"][0]["poster"] = "assets/chair/../../outside.png"
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaises(syncer.SyncError):
            syncer.sync(self.package, self.site)
        write_package(self.package / "replacement")
        bad_package = self.package / "replacement"
        manifest = json.loads((bad_package / "portfolio.json").read_text())
        manifest["assets"][0]["poster_sha256"] = "0" * 64
        (bad_package / "portfolio.json").write_text(json.dumps(manifest))
        with self.assertRaises(syncer.SyncError):
            syncer.sync(bad_package, self.site)

    def test_rejects_compressed_glb_without_a_local_decoder(self) -> None:
        model_path = self.package / "assets/chair/model.glb"
        document = json.dumps({"asset": {"version": "2.0"},
                               "extensionsUsed": ["KHR_draco_mesh_compression"]}, separators=(",", ":")).encode()
        document += b" " * (-len(document) % 4)
        model = (b"glTF" + struct.pack("<II", 2, 20 + len(document))
                 + struct.pack("<II", len(document), 0x4E4F534A) + document)
        model_path.write_bytes(model)
        manifest_path = self.package / "portfolio.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["assets"][0]["model_bytes"] = len(model)
        manifest["assets"][0]["model_sha256"] = hashlib.sha256(model).hexdigest()
        manifest["total_bytes"] = len(model) + manifest["assets"][0]["poster_bytes"]
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(syncer.SyncError, "without a local decoder"):
            syncer.sync(self.package, self.site)

    def test_rejects_unsafe_receipt_before_pruning(self) -> None:
        receipt = {"schema": syncer.RECEIPT_SCHEMA, "target": "ue-asset-atlas", "managed_files": ["../../README.md"]}
        (self.viewer / syncer.RECEIPT_NAME).write_text(json.dumps(receipt))
        with self.assertRaises(syncer.SyncError):
            syncer.sync(self.package, self.site, apply=True)
        self.assertFalse((self.viewer / "portfolio.json").exists())

    def test_rejects_package_symlink(self) -> None:
        outside = self.root / "outside.png"
        outside.write_bytes(b"poster")
        link = self.package / "assets/chair/poster.webp"
        try:
            link.unlink()
            link.symlink_to(outside)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation is unavailable")
        with self.assertRaises(syncer.SyncError):
            syncer.sync(self.package, self.site)

    def test_rejects_symlink_package_root(self) -> None:
        alias = self.root / "package-alias"
        try:
            alias.symlink_to(self.package, target_is_directory=True)
        except (OSError, NotImplementedError):
            self.skipTest("symlink creation is unavailable")
        with self.assertRaises(syncer.SyncError):
            syncer.sync(alias, self.site)


if __name__ == "__main__":
    unittest.main()
