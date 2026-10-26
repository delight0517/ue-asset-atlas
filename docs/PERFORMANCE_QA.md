# Large catalog preview and QA

The showroom keeps the interactive list bounded as an archive grows:

- 24 asset rows and 12 folder rows maximum are constructed per page.
- Only the selected StaticMesh is requested asynchronously for the 3D preview. A newer selection invalidates the prior callback.
- External thumbnail files are read and decoded off the game thread, limited to 12 MiB and 4096 × 4096 pixels, then downsampled before creating a transient texture.
- Preview thumbnails use up to 768 px for small catalogs, 512 px above 64 entries, and 384 px above 256 entries.
- The selected mesh uses an available lower LOD above 128 entries (LOD1) or 512 entries (LOD2); the preview component disables its shadow above 256 entries. A missing LOD falls back to the highest available valid LOD.
- The showroom's preview rotation is disabled when no mesh is selected and capped at 30 updates per second. These policies affect the archive preview only; they do not change project-wide viewport resolution or scalability settings.

## Run the headless checks

Close the target UE editor normally, then run from PowerShell:

```powershell
.\Tools\run_archive_qa.ps1 -ProjectFile 'D:\Projects\MyGame\MyGame.uproject'
```

The runner verifies UE 5.8, refuses to overlap a live editor process, builds the editor target, runs the `BlackLion.AssetArchive` automation group with `-NullRHI`, and checks both the process exit code and the UE automation log. `-SkipBuild` is available when the exact source has already been built.

Included tests cover 5,000 synthetic IDs paged exactly once across 209 pages, page row bounds and edge handling, thumbnail resolution tiers, available-LOD fallback and shadow policy thresholds. Logs are written under `Saved/BlackLionAssetArchive/qa/`.

## What this does not prove

These are policy and commandlet checks, not a framerate benchmark or visual review of the Unreal map. They do not establish a specific FPS target, test every mesh's actual LOD quality, or verify how a full-screen rendered game performs on a particular GPU. The user-facing editor/game window is deliberately not opened by the runner.
