"""A manifest of a THUG2 install's files - path, size and SHA-256 of each - and a check of another install against
it, to see how a different version of the game (another release, a patch, a different PartyMod build) differs from
the one this project is built against.

    python -I tools/game/install_manifest.py make  [--game C:\\Games\\THUG2] [--out data/install_manifest.json]
    python -I tools/game/install_manifest.py check --game D:\\Other\\THUG2 [--manifest data/install_manifest.json]

Left out: what this project or the player made, not the game - our dinput8.dll, thug2-decomp* (ini, logs, overlays,
layouts, converted levels), the game lock, Backup, Save and the Faces folder. A file's "kind" says where it comes from:
exe (THUG2PM.exe - the patched build every replacement address is for), partymod (PartyMod's SDL2 files), game.
"""
import argparse
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_MANIFEST = os.path.join(ROOT, "data", "install_manifest.json")
# Ours or the player's (top-level names, lower case; "thug2-decomp" also covers every thug2-decomp-* name).
SKIP = {"dinput8.dll", "backup", "save", "faces", ".game-lock", ".anteara-backup", "thug2pm_ghidra_mcp.lock",
        "thug2pm_ghidra_mcp.lock~"}
PARTYMOD = {"sdl2.dll", "gamecontrollerdb.txt"}


def skipped(top):
    top = top.lower()
    return top in SKIP or top.startswith("thug2-decomp")


def kind(rel):
    low = rel.lower()
    if low == "thug2pm.exe":
        return "exe"
    return "partymod" if low in PARTYMOD else "game"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def scan(game):
    files = {}
    for dirpath, dirnames, filenames in os.walk(game):
        rel_dir = os.path.relpath(dirpath, game)
        if rel_dir == ".":
            dirnames[:] = [d for d in dirnames if not skipped(d)]
            filenames = [f for f in filenames if not skipped(f)]
        for name in filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, game).replace(os.sep, "/")
            files[rel] = {"size": os.path.getsize(path), "sha256": sha256(path), "kind": kind(rel)}
        if len(files) and len(files) % 2000 < len(filenames):
            print("  %d files..." % len(files), file=sys.stderr)
    return files


def make(args):
    files = scan(args.game)
    manifest = {
        "about": "THUG2 install manifest (tools/game/install_manifest.py): every game file's size and SHA-256; "
                 "this project's files, Backup, Save and Faces left out",
        "source": os.path.abspath(args.game),
        "files": len(files),
        "bytes": sum(f["size"] for f in files.values()),
        "exe_sha256": files.get("THUG2PM.exe", {}).get("sha256"),
        "manifest": dict(sorted(files.items(), key=lambda kv: kv[0].lower())),
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=0, sort_keys=False)
        f.write("\n")
    print("%s: %d files, %.1f MB; THUG2PM.exe %s" % (args.out, manifest["files"], manifest["bytes"] / 1e6,
                                                     manifest["exe_sha256"]))


def check(args):
    with open(args.manifest, encoding="utf-8") as f:
        reference = json.load(f)["manifest"]
    files = scan(args.game)
    ref_lower = {k.lower(): k for k in reference}
    have_lower = {k.lower(): k for k in files}
    missing = sorted(ref_lower[k] for k in ref_lower.keys() - have_lower.keys())
    extra = sorted(have_lower[k] for k in have_lower.keys() - ref_lower.keys())
    changed = sorted(ref_lower[k] for k in ref_lower.keys() & have_lower.keys()
                     if reference[ref_lower[k]]["sha256"] != files[have_lower[k]]["sha256"])
    same = len(ref_lower.keys() & have_lower.keys()) - len(changed)

    def by_kind(paths):
        counts = {}
        for p in paths:
            k = reference[p]["kind"] if p in reference else kind(p)
            counts[k] = counts.get(k, 0) + 1
        return ", ".join("%s %d" % kv for kv in sorted(counts.items())) or "none"

    print("%s against %s:" % (args.game, args.manifest))
    print("  identical: %d of %d" % (same, len(reference)))
    print("  changed:   %d (%s)" % (len(changed), by_kind(changed)))
    print("  missing:   %d (%s)" % (len(missing), by_kind(missing)))
    print("  extra:     %d (%s)" % (len(extra), by_kind(extra)))
    exe = next((k for k in changed + missing if k.lower() == "thug2pm.exe"), None)
    if exe:
        print("  THUG2PM.exe differs or is missing: the replacements' addresses are for the reference exe - "
              "expect crashes until it's identical")
    else:
        print("  THUG2PM.exe identical: the DLL's addresses fit")
    for title, paths in (("changed", changed), ("missing", missing), ("extra", extra)):
        for p in paths[:args.list]:
            print("    %s %s" % (title, p))
        if len(paths) > args.list:
            print("    ... %d more %s (--list N shows more)" % (len(paths) - args.list, title))
    return 1 if changed or missing else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="action", required=True)
    m = sub.add_parser("make", help="write a manifest of an install")
    m.add_argument("--game", default=r"C:\Games\THUG2")
    m.add_argument("--out", default=DEFAULT_MANIFEST)
    c = sub.add_parser("check", help="compare an install with a manifest")
    c.add_argument("--game", required=True)
    c.add_argument("--manifest", default=DEFAULT_MANIFEST)
    c.add_argument("--list", type=int, default=40, help="how many paths to list per group")
    args = p.parse_args()
    if not os.path.isdir(args.game):
        sys.exit("not a folder: %s" % args.game)
    sys.exit(make(args) if args.action == "make" else check(args))


if __name__ == "__main__":
    main()
