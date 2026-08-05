"""Dev entry point: start the Orpheus server (API + hosted UI).

Equivalent to ``python -m orpheus_core.cli serve``, but runnable/debuggable
as a plain script (e.g. right-click -> Debug in PyCharm).

Usage:
    python serve.py [--host 127.0.0.1] [--port 8000] [--open]
"""

import sys
from pathlib import Path

# Allow running from a fresh checkout without `pip install -e orpheus_core`.
sys.path.insert(0, str(Path(__file__).resolve().parent / "orpheus_core"))

from orpheus_core.cli import cli

if __name__ == "__main__":
    sys.argv = [sys.argv[0], "serve", *sys.argv[1:]]
    cli()
