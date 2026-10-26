# Scene layout and reference-shot pipeline

## What the fast path does

The AI writes a small transform list referencing existing stable archive IDs. The validator resolves those IDs against both the project's identity file and local catalog before Unreal starts. The UE executor caches each unique `preview_mesh`, spawns the requested instances in one commandlet pass, computes floor contact from the rotated/scaled mesh bounds, rejects unintended AABB overlaps, and saves once. It creates no mesh copies and calls no paid AI or asset-generation service.

The output level is restricted to `/Game/Developer/AIPlacementQA/`. It does not modify the startup map, production levels, source Blender/Painter assets, or project-wide scalability. Unreal must be closed normally for the target project before the background command starts.

## Reference-guided scene edits

An approved shot manifest binds an exact image hash/crop/output size, the QA level, and camera pose/lens. This preserves one camera across iterations and allows the existing comparison tool to make same-frame overlays and diagnostic metrics. The reference is not an automatic 3D solver: a model or artist still proposes instance transforms. NullRHI QA verifies assets and actor transforms, not final rendered pixels. For visual similarity, render from the locked camera with a supported render path, compare the exact crop, and review the difference image and landmarks.

Until a target image and camera are approved, use `reference_shot_manifest: null` and leave `reference_status: "none"`; do not make a production scene look like an unverified reference.

## Bounded local validation

```powershell
python .\Tools\test_scene_layout.py
python .\Tools\scene_layout.py --manifest .\examples\scene-layout\layout.json --project .\examples\scene-layout\project.json --catalog .\examples\scene-layout\catalog.json
```

Apply only to a real project with a project-specific manifest and registered catalog assets:

```powershell
.\Tools\apply_scene_layout.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject' -ManifestPath 'D:\QA\layout.json'
```

The output report contains spawned instance count, distinct mesh load count, rejected bounds overlaps, the target level and elapsed milliseconds. This is placement evidence, not visual acceptance.
