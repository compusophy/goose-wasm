#!/usr/bin/env python3
"""Serves web/dist locally: python serve.py [port]. A module worker refuses
JavaScript served with a non-JS type, which some systems' registries map .js
to, so the types are set here rather than guessed."""
import functools
import http.server
import sys
from pathlib import Path

DIST = Path(__file__).resolve().parent / "web" / "dist"


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        ".html": "text/html; charset=utf-8",
        ".js": "text/javascript; charset=utf-8",
        ".mjs": "text/javascript; charset=utf-8",
        ".wasm": "application/wasm",
        ".json": "application/json",
        "": "application/octet-stream",
    }


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8642
    handler = functools.partial(Handler, directory=str(DIST))
    print(f"serving {DIST} at http://localhost:{port}")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()
