#!/usr/bin/env python3
"""
Rename this template from Bringup to your project. Run ONCE, right after you
copy the template — it rewrites the placeholder in every tracked text file:

    python3 scripts/rename_project.py "Fermentor" --gh-owner you --gh-repo fermentor

Replacements (all three casings, so the C++ namespace, the macros and the
release asset names stay consistent with each other):

    bringup   -> fermentor    namespace, mDNS hostname, .bin asset prefix
    BRINGUP   -> FERMENTOR    preprocessor macros (BRINGUP_BOARD_ID, ...)
    Bringup   -> Fermentor    display name, setup-AP SSID, UI title

--gh-owner / --gh-repo additionally rewrite the two update-source defines in
src/constants.h. Those are targeted edits rather than a blanket find-and-replace,
because the current owner's name also appears in prose (README links) where it
should NOT be rewritten.

Nothing here is magic — it is a find-and-replace you could do by hand. It exists
so you cannot half-rename and end up with a firmware whose BRINGUP_BOARD_ID no
longer matches the id its own CI writes into manifest.json — a device that can
never find its update.

⚠️ Do this BEFORE you flash devices you can't easily reach again. Board ids are
compiled into the firmware, so renaming later orphans deployed devices from
their update path.

Pass --dry-run to see the file list without writing.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The placeholder this template currently uses. If you fork the template itself
# and rename it, update these three to match.
PLACEHOLDER_UPPER = "BRINGUP"
PLACEHOLDER_TITLE = "Bringup"
PLACEHOLDER_LOWER = "bringup"

SKIP_DIRS = {".git", ".pio", "node_modules", ".vscode", "__pycache__"}
SKIP_SUFFIXES = {".gz", ".bin", ".png", ".jpg", ".ico", ".woff", ".woff2"}
# This script documents the placeholder; rewriting it would garble the
# instructions and leave no record of what the original placeholder was.
SKIP_FILES = {os.path.join("scripts", "rename_project.py")}


def slug_ok(s):
    return bool(re.fullmatch(r"[a-z][a-z0-9-]{1,30}", s))


def iter_files():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, ROOT)
            if rel in SKIP_FILES:
                continue
            if os.path.splitext(name)[1] in SKIP_SUFFIXES:
                continue
            yield path, rel


def set_gh_defaults(upper, owner, repo, dry_run):
    """Point the updater at your repo. Targeted, so prose is left alone."""
    path = os.path.join(ROOT, "src", "constants.h")
    text = open(path, encoding="utf-8").read()
    original = text
    if owner:
        text = re.sub(rf'(#define\s+{upper}_GH_OWNER\s+)"[^"]*"', rf'\1"{owner}"', text)
    if repo:
        text = re.sub(rf'(#define\s+{upper}_GH_REPO\s+)"[^"]*"', rf'\1"{repo}"', text)
    if text != original:
        print(f"  src/constants.h  (update source -> {owner}/{repo})")
        if not dry_run:
            open(path, "w", encoding="utf-8").write(text)
        return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name", help='Display name, e.g. "Fermentor"')
    ap.add_argument("--slug", help="lowercase id (default: name.lower(), spaces -> -)")
    ap.add_argument("--gh-owner", help="GitHub owner that publishes your releases")
    ap.add_argument("--gh-repo", help="GitHub repo that publishes your releases")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    display = args.name
    slug = args.slug or re.sub(r"[^a-z0-9]+", "-", display.lower()).strip("-")
    if not slug_ok(slug):
        sys.exit(
            f"❌ slug {slug!r} must be lowercase letters/digits/hyphens, "
            "start with a letter, 2-31 chars (it becomes a C++ namespace and an "
            "mDNS hostname). Pass --slug explicitly."
        )
    upper = slug.upper().replace("-", "_")

    if slug == PLACEHOLDER_LOWER:
        sys.exit(f"❌ {slug!r} is the current placeholder — nothing to rename.")

    pairs = [
        (PLACEHOLDER_UPPER, upper),
        (PLACEHOLDER_TITLE, display),
        (PLACEHOLDER_LOWER, slug),
    ]

    changed = 0
    for path, rel in iter_files():
        try:
            text = open(path, encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue
        new = text
        for old, repl in pairs:
            new = new.replace(old, repl)
        if new != text:
            changed += 1
            print(f"  {rel}")
            if not args.dry_run:
                open(path, "w", encoding="utf-8").write(new)

    if args.gh_owner or args.gh_repo:
        # Runs after the bulk pass, so the macro prefix is already the new one.
        set_gh_defaults(upper, args.gh_owner, args.gh_repo, args.dry_run)

    verb = "would rewrite" if args.dry_run else "rewrote"
    print(f"\n{verb} {changed} file(s): {PLACEHOLDER_TITLE} -> {display} ({slug} / {upper})")
    if not args.gh_owner or not args.gh_repo:
        print(
            f"\n⚠️  GitHub owner/repo not set — OTA cannot work until they are.\n"
            f"   Edit {upper}_GH_OWNER / {upper}_GH_REPO in src/constants.h, or\n"
            f"   re-run with --gh-owner/--gh-repo."
        )
    if not args.dry_run:
        print(
            "\nNext:\n"
            "  1. Update the three PLACEHOLDER_* constants at the top of this\n"
            "     script, so a future rename of YOUR project works too.\n"
            "  2. python3 scripts/gzip_web_files.py   # re-stamp data/*.html:\n"
            "     renaming changed the asset CONTENT, but the ?v= cache-busting\n"
            "     hashes are only recomputed at image-build time, so the\n"
            "     committed HTML points at stale hashes until you do this.\n"
            "  3. pio test -e native && pio run\n"
        )


if __name__ == "__main__":
    main()
