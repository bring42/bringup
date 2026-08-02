#!/usr/bin/env python3
"""
Inject firmware version metadata into the build as -D macros.

Runs as a PlatformIO *pre* extra_script. It defines, per build:
  FIRMWARE_VERSION    semver string, from the git tag (or FW_VERSION env), else
                      the fallback in constants.h stays in effect.
  FIRMWARE_BUILD_HASH short git hash (or "dev" when git is unavailable).
  BRINGUP_BOARD_ID    release-asset id for THIS env, so the running firmware
                      knows which manifest.json entry / .bin to pull.

Version resolution order:
  1. $FW_VERSION            (explicit override, e.g. CI passes the tag)
  2. `git describe --tags`  (e.g. "v1.2.0" or "v1.2.0-3-gabc1234")
  3. (nothing)             -> constants.h fallback "0.1.0" is used

The macro values are added ONLY when we resolved them, so a plain local
`pio run` in a repo with no tags still compiles.

The env -> board-id map lives in boards.json (see scripts/boards.py), shared
with gen_manifest.py and the release workflow.
"""
Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
import os
import re
import subprocess
import sys

# PlatformIO exec()s extra_scripts without setting __file__, so derive the
# scripts directory from the project instead.
sys.path.insert(0, os.path.join(env.subst("$PROJECT_DIR"), "scripts"))
from boards import id_by_env  # noqa: E402


def _git(args):
    try:
        out = subprocess.check_output(
            ["git"] + args, stderr=subprocess.DEVNULL, cwd=env.subst("$PROJECT_DIR")
        )
        return out.decode().strip()
    except Exception:
        return ""


def _clean_semver(raw):
    """Strip a leading 'v' and any -N-gHASH suffix -> bare semver, or ''."""
    if not raw:
        return ""
    raw = raw.lstrip("vV")
    m = re.match(r"^(\d+\.\d+\.\d+)", raw)
    return m.group(1) if m else ""


pioenv = env.get("PIOENV", "")
board_id = id_by_env().get(pioenv, "")

version = _clean_semver(os.environ.get("FW_VERSION", "")) or _clean_semver(
    _git(["describe", "--tags", "--always"])
)
build_hash = _git(["rev-parse", "--short", "HEAD"]) or "dev"

defines = [("FIRMWARE_BUILD_HASH", env.StringifyMacro(build_hash))]
if version:
    defines.append(("FIRMWARE_VERSION", env.StringifyMacro(version)))
if board_id:
    defines.append(("BRINGUP_BOARD_ID", env.StringifyMacro(board_id)))

env.Append(CPPDEFINES=defines)

# Stamp the filesystem with its version so the running device can tell whether
# its UI (LittleFS image) is up to date INDEPENDENTLY of the firmware — the
# updater reads /fsver and compares it to the release manifest. This is what
# makes the classic footgun (new firmware, stale UI) self-healing rather than a
# silent mystery. Written for device builds only (native/host builds have no
# board_id). The "0.1.0" fallback matches constants.h so an untagged local build
# stays self-consistent. data/fsver is a build artifact (gitignored); buildfs
# packs it into littlefs.bin (it is not gzipped).
if board_id:
    project_dir = env.subst("$PROJECT_DIR")
    data_dir = os.path.join(project_dir, "data")
    if os.path.isdir(data_dir):
        fs_version = version or "0.1.0"
        with open(os.path.join(data_dir, "fsver"), "w") as fsver_file:
            fsver_file.write(fs_version + "\n")

print(
    "🔖 version.py: env=%s version=%s hash=%s board_id=%s"
    % (pioenv, version or "(fallback)", build_hash, board_id or "(fallback)")
)
