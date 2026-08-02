#!/usr/bin/env python3
"""
Generate manifest.json for a release.

Usage: gen_manifest.py <version> [release_dir]

Reads the already-built per-board images in <release_dir> and writes
<release_dir>/manifest.json with a SHA-256 + size for each. The device fetches
this file from .../releases/latest/download/manifest.json and verifies every
image against it before booting — the checksum is the only thing standing
between a truncated or corrupted download and a bricked device, so it is
computed here from the exact bytes that get uploaded.

Board ids come from boards.json (shared with version.py and the release
workflow). File names MUST match what the release workflow copies into
<release_dir>: <PROJECT>-<id>.bin and littlefs-<id>.bin.
"""
import hashlib
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from boards import load as load_boards  # noqa: E402

# Firmware asset filename prefix. Renamed along with everything else by
# scripts/rename_project.py; must match the release workflow's `cp`.
PROJECT = "bringup"


def entry(path):
    if not os.path.isfile(path):
        raise SystemExit(f"❌ missing release asset: {path}")
    data = open(path, "rb").read()
    return {
        "file": os.path.basename(path),
        "sha256": hashlib.sha256(data).hexdigest(),
        "size": len(data),
    }


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: gen_manifest.py <version> [release_dir]")
    version = sys.argv[1].lstrip("vV")
    release_dir = sys.argv[2] if len(sys.argv) > 2 else "release"

    try:
        build_hash = (
            subprocess.check_output(["git", "rev-parse", "--short", "HEAD"])
            .decode()
            .strip()
        )
    except Exception:
        build_hash = "dev"

    manifest = {
        "version": version,
        "buildHash": build_hash,
        "notes": "See the release notes for details.",
        "boards": {},
    }
    for _env, bid, _label in load_boards():
        manifest["boards"][bid] = {
            "app": entry(os.path.join(release_dir, f"{PROJECT}-{bid}.bin")),
            "fs": entry(os.path.join(release_dir, f"littlefs-{bid}.bin")),
        }

    out = os.path.join(release_dir, "manifest.json")
    with open(out, "w") as f:
        json.dump(manifest, f, indent=2)
    print(json.dumps(manifest, indent=2))
    print(f"\nwrote {out}", file=sys.stderr)


if __name__ == "__main__":
    main()
