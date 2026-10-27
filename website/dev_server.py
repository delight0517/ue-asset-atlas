"""Loopback-only static server with content-hash manifests for source hot reload."""

from __future__ import annotations

import argparse
import hashlib
import json
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parent / "dist"
SHELL_FILES = ("app.js", "index.html", "modules.json", "runtime/activity.js", "runtime/frame-rate.js", "runtime/hot-reload.js", "runtime/preferences.js")


def digest(relative_path: str) -> str:
    data = (ROOT / relative_path).read_bytes()
    return hashlib.sha256(data).hexdigest()[:20]


def development_manifest() -> dict:
    source_manifest = json.loads((ROOT / "modules.json").read_text(encoding="utf-8"))
    modules = [{**definition, "hash": digest(relative_url(definition["src"]))} for definition in source_manifest.get("modules", [])]
    styles = [{**definition, "hash": digest(relative_url(definition["src"]))} for definition in source_manifest.get("styles", [])]
    return {
        "contractVersion": 1,
        "dev": True,
        "shellHash": hashlib.sha256("".join(digest(path) for path in SHELL_FILES).encode()).hexdigest()[:20],
        "modules": modules,
        "styles": styles,
    }


def relative_url(url: str) -> str:
    parsed = urlsplit(url)
    path = parsed.path
    if parsed.netloc or not (path.startswith("/modules/") or path == "/styles.css"):
        raise ValueError(f"Feature manifest URL is outside the local module and stylesheet paths: {url}")
    relative = Path(path.lstrip("/"))
    candidate = (ROOT / relative).resolve()
    if not candidate.is_relative_to(ROOT.resolve()) or not candidate.is_file():
        raise ValueError(f"Feature manifest path is missing or outside the static site: {url}")
    return relative.as_posix()


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def do_GET(self):
        if urlsplit(self.path).path == "/__dev__/manifest":
            try:
                body = json.dumps(development_manifest(), separators=(",", ":")).encode()
            except OSError as error:
                body = json.dumps({"error": str(error)}).encode()
                self.send_response(500)
            else:
                self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def end_headers(self):
        if urlsplit(self.path).path != "/__dev__/manifest":
            self.send_header("Cache-Control", "no-cache")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="Loopback only; do not expose the development server to a network.")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if args.host not in {"127.0.0.1", "localhost", "::1"}:
        parser.error("The hot-reload server may bind only to loopback.")
    if not ROOT.is_dir():
        parser.error(f"Static site directory not found: {ROOT}")
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"UE Asset Atlas dev server: http://{args.host}:{server.server_address[1]}/?dev=1", flush=True)
    print("Hot reload is enabled for this local browser session. Press Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        print("\nStopping local development server.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
