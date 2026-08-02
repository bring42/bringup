#!/usr/bin/env python3
"""
Shared reader for boards.json — the single source of truth for the release
board table. Imported by version.py and gen_manifest.py; also runnable so CI
can enumerate the table without a JSON parser in bash:

    python3 scripts/boards.py envs      # -> "esp32-s3-devkitc-1 esp32-c3-devkitm-1 ..."
    python3 scripts/boards.py pairs     # -> "<env>:<id>" per line
    python3 scripts/boards.py ids       # -> one asset id per line
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARDS_JSON = os.path.join(ROOT, "boards.json")


def load():
    """[(env, id, label), ...] in file order."""
    with open(BOARDS_JSON) as f:
        data = json.load(f)
    out = []
    for b in data.get("boards", []):
        out.append((b["env"], b["id"], b.get("label", b["id"])))
    if not out:
        raise SystemExit(f"{BOARDS_JSON}: no boards defined")
    return out


def id_by_env():
    return {env: bid for env, bid, _ in load()}


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "pairs"
    rows = load()
    if what == "envs":
        print(" ".join(env for env, _, _ in rows))
    elif what == "ids":
        print("\n".join(bid for _, bid, _ in rows))
    elif what == "pairs":
        print("\n".join(f"{env}:{bid}" for env, bid, _ in rows))
    else:
        raise SystemExit("usage: boards.py [envs|ids|pairs]")
