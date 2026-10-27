# Individual 3D portfolio exports

The portfolio path is separate from the Unreal project and its catalog. Prepare one web-ready `.glb` and one small `.webp`/`.jpg`/`.png` poster per object, then list them in a selection file that follows [`schemas/web-portfolio.schema.json`](../schemas/web-portfolio.schema.json). Mark each item `visibility: "public"` or `"private"`; only explicitly public entries enter a public package. Exporting never converts a UAsset or FBX and does not publish or upload files. Keep the active selection as the ignored root file `portfolio.local.json`; do not commit private object names, local source paths, or metadata into this public source repo.

The exporter copies only explicitly public allowlisted derivatives. Private entries are omitted, and a build with no public entries stops. Each public object gets its own folder and independent model URL so the viewer can fetch only the selected object. It records byte sizes and SHA-256 hashes, rejects path traversal and invalid GLB 2 containers, caps each model at 8 MiB and each poster at 512 KiB by default, and refuses an existing output path. Those limits validate a file; the exporter does not compress or reduce a mesh. Prepare and inspect an optimized GLB locally before adding it to the selection. Keep the source root limited to approved web derivatives.

Example selection: [`examples/portfolio/selection.json`](../examples/portfolio/selection.json). The paths are placeholders; the example contains no real model and is not itself publishable. After preparing approved files, copy the example to ignored `portfolio.local.json` and replace its paths with your web derivatives:

```powershell
python Tools/export_web_portfolio.py `
  --selection examples/portfolio/selection.json `
  --source-root D:\Portfolio\approved-web-assets `
  --output D:\Portfolio\site-assets
```

If a model exceeds the default budget, optimize that object before choosing a higher limit:

```powershell
python Tools/export_web_portfolio.py --selection selection.json --source-root web-assets --output site-assets --max-model-mib 12
```

The output is a static payload containing `portfolio.json` and only the selected public `assets/<id>/model.glb` and poster files. The local `website/dist/portfolio/` viewer shows poster cards first and loads one GLB only after a user chooses **3D 미리보기 불러오기**. It removes the previous model when another object is selected and loads its local viewer library only on demand. It is still an empty viewer until you add approved derivative files.

The personal portfolio repository `delight0517.github.io` already uses GitHub Pages from its `main` branch. A viewer page and empty gallery manifest are staged in a separate local checkout on `feature/ue-asset-atlas-viewer`; they have not been pushed or deployed. The gallery is empty until a real approved derivative is selected. GitHub Pages is public by default even when hosted from a private repository. Private Pages access control requires an Enterprise Cloud organization. For this personal GitHub target, private items stay local and only explicitly public entries may enter the web package. `visibility` is a publication filter, not access control: later removal cannot revoke downloads, copies, Git history, or caches.

The local selection studio is served at `http://127.0.0.1:8765/portfolio-studio/` while `python website/dev_server.py` is running. It edits only the chosen JSON selection file; it never reads model files, uploads content, or contacts GitHub. The sync adapter `Tools/sync_web_portfolio.py` verifies the existing personal-site checkout remote and package hashes. It performs a dry run by default. `--apply` copies/removes only files recorded as managed under `ue-asset-atlas/` in the local site checkout; it does not commit, push, deploy, or schedule anything. Review the resulting Git diff before any separately authorized publication.

GitHub Actions cannot read original files that remain on this PC. A 15-day refresh therefore needs an optional local scheduled task that runs while this PC is available, builds the public derivatives, and syncs only the approved site payload. Scheduling is not implemented or active. This app has no built-in AI provider or GitHub account/settings UI. An AI agent with separately approved local file access can edit `portfolio.local.json` and run the manual exporter/sync; the file remains ignored by Git. The existing `gh` login is not embedded in this app, and no credential is copied into it. Any future account adapter must use secure login/storage and remain separate from canonical files.

The current exporter enforces per-file size budgets; it does not compress or optimize a source mesh. Prepare the compact derivative in a 3D tool and inspect it before selection. Draco, Basis/KTX2, and Meshopt-compressed GLBs are rejected until their decoder files are bundled locally, so this viewer makes no third-party decoder requests. A real sample model has not been provided, so the deployed viewer's real-model rendering and size/performance remain unverified.

Do not put confidential, licensed, or client assets in a public portfolio without checking their sharing rights.

GitHub Pages are public by default even when hosted from a private repository. Private Pages access control requires an Enterprise Cloud organization. A personal GitHub Pages target must treat the gallery as public and exclude private objects. `visibility` is a publication filter, not access control: removing an object later cannot revoke downloads, copies, or caches. For genuinely private online viewing, use a destination with real authentication; until then those entries stay on this computer. See [GitHub Pages visibility](https://docs.github.com/en/pages/getting-started-with-github-pages/creating-a-github-pages-site) and [private Pages access control](https://docs.github.com/en/enterprise-cloud@latest/pages/getting-started-with-github-pages/changing-the-visibility-of-your-github-pages-site).

QA uses synthetic GLB/poster fixtures to check allowlist-only packaging, checksums, traversal rejection, malformed GLB rejection, size budgets, duplicate IDs, and no-overwrite behavior:

```powershell
python -m unittest discover -s Tools -p 'test_export_web_portfolio.py'
```
