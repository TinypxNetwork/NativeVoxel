#!/usr/bin/env python3
"""Download Minecraft 1.8.9 resources and prepare every native runtime cache."""
from pathlib import Path
import argparse
import subprocess
import sys

from download_assets import DEFAULT_CACHE, DownloadError, download_assets


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--output", type=Path, default=root / "assets")
    parser.add_argument("--mcp", type=Path, help="Import an existing local MCP tree instead of downloading")
    parser.add_argument("--offline", action="store_true", help="Use verified cached files without network access")
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--pack", type=Path, action="append", default=[])
    args = parser.parse_args()
    try:
        # Fail before downloading if a build dependency is missing.
        import PIL
        import numpy
        source = args.mcp.resolve() if args.mcp else download_assets(args.cache, offline=args.offline, workers=args.workers)
        output = args.output.resolve()
        common = ["--mcp", str(source)]
        packs = [argument for pack in args.pack for argument in ("--pack", str(pack.resolve()))]
        for script, arguments in (
            ("prepare_assets.py", ["--output", str(output), *packs]),
            ("prepare_render_assets.py", ["--assets", str(output)]),
            ("prepare_first_person.py", ["--assets", str(output)]),
            ("prepare_tile_entities.py", ["--assets", str(output)]),
            ("prepare_entity_visuals.py", ["--assets", str(output)]),
        ):
            print(f"Preparing assets: {script}", flush=True)
            subprocess.run([sys.executable, str(root / "tools" / script), *common, *arguments], check=True)
        print(f"Assets ready in {output}", flush=True)
    except ImportError as error:
        parser.exit(1, f"Missing build dependency: {error}. Run: python -m pip install -r requirements-dev.txt\n")
    except (DownloadError, OSError, ValueError, KeyError, StopIteration, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Asset setup failed: {error}\n")


if __name__ == "__main__":
    main()
