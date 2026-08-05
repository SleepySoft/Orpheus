"""Run the Orpheus HTTP server: python -m orpheus_core.server [--port 8000]."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Orpheus HTTP server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--project-root", type=Path, default=Path.cwd())
    args = parser.parse_args()

    import uvicorn

    from orpheus_core.server.app import create_app

    uvicorn.run(create_app(args.project_root), host=args.host, port=args.port)


if __name__ == "__main__":
    main()
