# NOT ORIGINAL - the Anteara Mod (the user's own THUG2 script mod, 2010 + 2011 merged; README.md) as an optional part
# of the data overlay. Only the mod's own changes are stored here (mod\): its new files, and for each game script it
# changes just the scripts / globals it changes or adds (and a manifest of what it removes, its qdir.txt changes and
# a few line edits). The game's scripts come from the user's install each time.
#
# install() (called by tools/menu_level/make_menu_level.py on every run) writes <overlay>\anteara\: the complete
# script set the mod plays with - every file its qdir.txt lists, from the game's loose scripts (Data\scripts, which
# the mod was made from) with the mod's changes applied, plus the data overlay's own edits re-applied to the files the
# overlay also changes (Levels.qb, the pause menu, the level select, the front end's roundbar menu...). The DLL reads
# it first while thug2-decomp.ini [features] anteara_mod=1 (src/game/front/data_overlay.h).
#
#   python -I tools/anteara/anteara.py extract --unified <scripts_unified folder> [--game C:\Games\THUG2]
#       (re)makes mod\ from a merged mod folder (its src\*.ns and qdir.txt) and the game's loose scripts
#   python -I tools/anteara/anteara.py check --unified <scripts_unified folder> --overlay <game>\thug2-decomp-data
#       compares an installed anteara\ set with the merged mod's .ns, names ignored (the overlay's edits listed)
#
# .ns text is Latin-1 here: some strings hold bytes >= 0x80 (the fonts' button glyphs) that NeverScript reads and
# writes byte for byte.

import argparse
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.join(HERE, "mod")
MANIFEST = os.path.join(MOD, "manifest.json")
REPO = os.path.dirname(os.path.dirname(HERE))
QDIR_PREFIX = "c:\\ports\\THUG2\\THUG2XB\\Skate6\\data\\scripts\\"
ENC = "latin-1"


# --- .ns text as units (scripts and globals) ----------------------------------------------------------------------

UNIT_START = re.compile(r"^(?:script (\S+?) ?\{|(`[^`\n]+`|[A-Za-z_#][A-Za-z0-9_]*) ?= ?)")


def crc(name):
    return (zlib.crc32(name.lower().encode(ENC)) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def key_of(name):
    """A unit name as NeverScript shows its checksum (#xxxxxxxx: the checksum's bytes in file order)."""
    name = name.strip("`")
    if re.fullmatch(r"#[0-9a-fA-F]{8}", name):
        return name.lower()
    return "#%08x" % int.from_bytes(crc(name).to_bytes(4, "little"), "big")


def split(text):
    """(head, units, register): units = [(kind, name, text)], each unit's text with the comment lines right before
    it; register = the __register_checksums__ line (or "")."""
    register = ""
    m = re.search(r"(?m)^__register_checksums__.*$", text)
    if m:
        register = m.group(0)
        text = text[:m.start()] + text[m.end():]
    lines = text.split("\n")
    head, units, pending = [], [], []
    current = None
    for line in lines:
        start = UNIT_START.match(line)
        if line.startswith("//"):
            pending.append(line)
            continue
        if start:
            kind, name = ("script", start.group(1)) if start.group(1) else ("global", start.group(2))
            current = [kind, name, pending + [line]]
            units.append(current)
            pending = []
            continue
        target = current[2] if current else head
        target.extend(pending)
        pending = []
        target.append(line)
    if pending:
        (current[2] if current else head).extend(pending)
    return "\n".join(head), [(k, n, "\n".join(t)) for k, n, t in units], register


def join(head, units, register):
    body = "\n".join([head] + [t for _, _, t in units]) if head else "\n".join(t for _, _, t in units)
    body = body.rstrip("\n") + "\n"
    return body + register if register else body


IDENT = re.compile(r'"(?:[^"\\\n]|\\.)*"|#[0-9a-fA-F]{8}|`[^`\n]+`|[A-Za-z_][A-Za-z0-9_]*')


def norm(unit_text):
    """A unit's code for comparing: comments out, every name as its checksum, spacing collapsed."""
    lines = [l for l in unit_text.split("\n") if not l.lstrip().startswith("//")]
    text = re.sub(r"/\*.*?\*/", "", "\n".join(lines), flags=re.S)

    def one(m):
        t = m.group(0)
        if t.startswith('"'):
            return t
        if t.startswith("#"):
            return t.lower()
        return key_of(t)
    return re.sub(r"\s+", " ", IDENT.sub(one, text)).strip()


# --- NeverScript ---------------------------------------------------------------------------------------------------

def ns(tool, *args):
    result = subprocess.run([tool, *args], capture_output=True, text=True, errors="replace")
    if result.returncode != 0:
        sys.exit("ns %s failed:\n%s%s" % (" ".join(args), result.stdout, result.stderr))


def decompile(tool, path, work):
    out = os.path.join(work, "d_%08x.ns" % crc(path))
    result = subprocess.run([tool, "-d", path, "-o", out], capture_output=True)
    if result.returncode != 0:  # (a few retail files end in 0x00 padding)
        ns(tool, "-lenient", "-d", path, "-o", out)
    return open(out, encoding=ENC).read()


def compile_to(tool, text, target, work):
    src = os.path.join(work, "c_%08x.ns" % crc(target))
    open(src, "w", encoding=ENC, newline="").write(text)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    ns(tool, "-c", src, "-o", target, "-targetGame", "thug2")


# --- qdir.txt ------------------------------------------------------------------------------------------------------

def qdir_files(text):
    out = []
    for line in text.split("\n"):
        m = re.search(r"\\scripts\\(.+\.qb)", line.strip(), re.I)
        if m:
            out.append(m.group(1))
    return out


def mod_qdir(game_qdir, manifest):
    files = [f for f in game_qdir if f.lower() not in {r.lower() for r in manifest["qdir"]["remove"]}]
    for rel, after in manifest["qdir"]["insert_after"]:
        at = [f.lower() for f in files].index(after.lower()) + 1 if after else 0
        files.insert(at, rel)
    for rel, after in manifest["qdir"].get("move_after", []):  # (files the mod loads in another order)
        files = [f for f in files if f.lower() != rel.lower()]
        files.insert([f.lower() for f in files].index(after.lower()) + 1 if after else 0, rel)
    return files


# --- applying the mod to the game's text -----------------------------------------------------------------------------

def apply_patch(base, patch_text, entry, rel):
    """The game's file text with the mod's units in place: changed ones replaced where they are, new ones after the
    unit the manifest names, removed ones dropped, then the manifest's line edits."""
    head, units, register = split(base)
    index = {key_of(n): i for i, (_, n, _) in enumerate(units)}
    remove = {key_of(n) for n in entry.get("remove", [])}
    after = {key_of(k): v for k, v in entry.get("insert_after", {}).items()}
    if patch_text:
        _, patch_units, _ = split(patch_text)
        for kind, name, text in patch_units:
            k = key_of(name)
            if k in index:
                units[index[k]] = (kind, name, text)
                continue
            prev = after.get(k)
            if prev is None:
                units.append((kind, name, text))
            else:
                at = 0 if prev == "" else next(i for i, u in enumerate(units) if key_of(u[1]) == key_of(prev)) + 1
                units.insert(at, (kind, name, text))
            index = {key_of(n): i for i, (_, n, _) in enumerate(units)}
    units = [u for u in units if key_of(u[1]) not in remove]
    text = join(head, units, register)
    for edit in entry.get("edits", []):
        if text.count(edit["old"]) != 1:
            sys.exit("anteara: %s: the line edit's text %r found %d times" % (rel, edit["old"][:60], text.count(edit["old"])))
        text = text.replace(edit["old"], edit["new"])
    # names the mod adds, for decompilers (the checksum table)
    if patch_text and register:
        have = {w.lower() for w in register.split()}
        extra = sorted({w for w in re.findall(r"(?m)^script ([A-Za-z_][A-Za-z0-9_]*)", patch_text)} |
                       {w for w in re.findall(r"(?m)^([A-Za-z_][A-Za-z0-9_]*) =", patch_text)})
        extra = [w for w in extra if w.lower() not in have]
        if extra:
            text = text.replace(register, register + " " + " ".join(extra))
    return text


def install(game, out, tool, edits=None, front_end_edits=None):
    """Writes <out>\\anteara\\: every file of the mod's qdir.txt (scripts\\...), its qdir.txt and localtext.qb.
    edits / front_end_edits: {r"scripts\\game\\levels.qb" (lower case): function(text) -> text}, the data overlay's
    own changes to re-apply - edits for anteara\\, front_end_edits for anteara\\front_end\\ (the skate-shop front
    end's versions). Returns a description, or None when the game has no loose scripts to build on."""
    edits = {k.lower(): v for k, v in (edits or {}).items()}
    front_end_edits = {k.lower(): v for k, v in (front_end_edits or {}).items()}
    loose = os.path.join(game, "Data", "scripts")
    target = os.path.join(out, "anteara")
    if os.path.isdir(target):
        shutil.rmtree(target)
    if not os.path.exists(os.path.join(loose, "qdir.txt")):
        return None
    manifest = json.load(open(MANIFEST, encoding="utf-8"))
    files = {k.lower(): v for k, v in manifest["files"].items()}
    game_qdir = qdir_files(open(os.path.join(loose, "qdir.txt"), encoding=ENC).read())
    qdir = mod_qdir(game_qdir, manifest)
    made = {"patched": 0, "new": 0, "copied": 0, "overlay": 0}
    with tempfile.TemporaryDirectory() as work:
        for rel in qdir + ["localtext.qb"]:
            key = "scripts\\" + rel.lower()
            entry = files.get(rel.lower())
            dst = os.path.join(target, "scripts", rel)
            new_path = os.path.join(MOD, "new", rel + ".ns")
            if entry and entry.get("new"):
                text = open(os.path.join(MOD, "new", entry["path"]), encoding=ENC).read()
                made["new"] += 1
            else:
                src = os.path.join(loose, rel)
                if not os.path.exists(src):
                    sys.exit("anteara: %s missing from %s" % (rel, loose))
                if not entry and key not in edits and key not in front_end_edits:
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copyfile(src, dst)
                    made["copied"] += 1
                    continue
                text = decompile(tool, src, work)
                if entry:
                    patch = os.path.join(MOD, "patch", entry["path"]) if entry.get("path") else None
                    text = apply_patch(text, open(patch, encoding=ENC).read() if patch else "", entry, rel)
                    made["patched"] += 1
            if key in front_end_edits:
                compile_to(tool, front_end_edits[key](text), os.path.join(target, "front_end", "scripts", rel), work)
            if key in edits:
                text = edits[key](text)
                made["overlay"] += 1
            compile_to(tool, text, dst, work)
        open(os.path.join(target, "scripts", "qdir.txt"), "w", encoding=ENC, newline="").write(
            "".join(QDIR_PREFIX + rel + "\r\n" for rel in qdir))
    return ("Anteara Mod: %d files in its qdir.txt + localtext.qb -> %s (%d with the mod's changes, %d its own, %d "
            "copied from Data\\scripts, %d with the overlay's edits too)" %
            (len(qdir), target, made["patched"], made["new"], made["copied"], made["overlay"]))


# --- extract: the mod's changes from a merged mod folder --------------------------------------------------------------

def line_edits(base_unit, mod_unit):
    """For a huge global the mod changes in a line or two (localtext's Translations): line edits instead of the
    unit. None if it isn't that."""
    a, b = base_unit.split("\n"), mod_unit.split("\n")
    if len(a) < 500 or len(a) != len(b):
        return None
    diff = [(x, y) for x, y in zip(a, b) if x != y]
    if not diff or len(diff) > 8:
        return None
    return diff


def extract(unified, game, tool):
    src_root = os.path.join(unified, "src")
    loose = os.path.join(game, "Data", "scripts")
    unified_qdir = qdir_files(open(os.path.join(unified, "qdir.txt"), encoding=ENC).read())
    game_qdir = qdir_files(open(os.path.join(loose, "qdir.txt"), encoding=ENC).read())
    # one spelling per folder (the merged qdir has game\menu\Anteara_Stuff and game\menu\anteara_stuff)
    spelling = {}
    for f in unified_qdir:
        folder = os.path.dirname(f)
        spelling.setdefault(folder.lower(), folder)
    unified_qdir = [os.path.join(spelling[os.path.dirname(f).lower()], os.path.basename(f)) if os.path.dirname(f)
                    else f for f in unified_qdir]
    lower_game = [f.lower() for f in game_qdir]
    lower_mod = [f.lower() for f in unified_qdir]
    manifest = {"qdir": {"remove": [f for f in game_qdir if f.lower() not in lower_mod], "insert_after": []},
                "files": {}}
    for i, f in enumerate(unified_qdir):
        if f.lower() not in lower_game:
            manifest["qdir"]["insert_after"].append([f, unified_qdir[i - 1] if i else ""])
    current = mod_qdir(game_qdir, manifest)
    manifest["qdir"]["move_after"] = []
    for i, f in enumerate(unified_qdir):
        if current[i].lower() != f.lower():
            after = unified_qdir[i - 1] if i else ""
            manifest["qdir"]["move_after"].append([f, after])
            current = mod_qdir(game_qdir, manifest)
    assert [f.lower() for f in current] == [f.lower() for f in unified_qdir]
    for sub in ("new", "patch"):
        if os.path.isdir(os.path.join(MOD, sub)):
            shutil.rmtree(os.path.join(MOD, sub))
    report = []
    with tempfile.TemporaryDirectory() as work:
        for rel in unified_qdir + ["localtext.qb"]:
            mod_text = open(os.path.join(src_root, rel + ".ns"), encoding=ENC).read()
            base_path = os.path.join(loose, rel)
            if not os.path.exists(base_path):
                path = rel.replace("\\", "/") + ".ns"
                dst = os.path.join(MOD, "new", path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "w", encoding=ENC, newline="").write(mod_text)
                manifest["files"][rel] = {"new": True, "path": path}
                report.append("new      %s" % rel)
                continue
            _, base_units, _ = split(decompile(tool, base_path, work))
            _, mod_units, _ = split(mod_text)
            base_by = {key_of(n): (k, n, t) for k, n, t in base_units}
            mod_keys = {key_of(n) for _, n, _ in mod_units}
            entry = {}
            out_units = []
            insert_after = {}
            prev = ""
            for kind, name, text in mod_units:
                k = key_of(name)
                if k in base_by:
                    if norm(base_by[k][2]) != norm(text):
                        edits = line_edits(base_by[k][2], text)
                        if edits:
                            entry.setdefault("edits", []).extend({"old": x + "\n", "new": y + "\n"} for x, y in edits)
                        else:
                            out_units.append(text)
                else:
                    out_units.append(text)
                    insert_after[name] = prev
                prev = name
            removed = [n for _, n, _ in base_units if key_of(n) not in mod_keys]
            if removed:
                entry["remove"] = removed
            if insert_after:
                entry["insert_after"] = insert_after
            if out_units:
                path = rel.replace("\\", "/") + ".ns"
                dst = os.path.join(MOD, "patch", path)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, "w", encoding=ENC, newline="").write("\n".join(out_units).rstrip("\n") + "\n")
                entry["path"] = path
            if entry:
                manifest["files"][rel] = entry
                report.append("patched  %-48s %d units, %d removed, %d line edits" % (
                    rel, len(out_units), len(removed), len(entry.get("edits", []))))
    json.dump(manifest, open(MANIFEST, "w", encoding="utf-8"), indent=1)
    print("\n".join(report))
    print("%d files: %d new, %d patched" % (len(manifest["files"]),
                                             sum(1 for e in manifest["files"].values() if e.get("new")),
                                             sum(1 for e in manifest["files"].values() if not e.get("new"))))


# --- check: an installed set against the merged mod ------------------------------------------------------------------

def code_lines(tool, path, work, from_ns=False):
    """A file's code with every name as its checksum (compiled with -removeChecksums, decompiled again)."""
    src = path
    if not from_ns:
        src = os.path.join(work, "k_%08x.ns" % crc(path))
        text = decompile(tool, path, work)
        open(src, "w", encoding=ENC, newline="").write(text)
    q = os.path.join(work, "k_%08x.qb" % crc(path + "q"))
    ns(tool, "-c", src, "-o", q, "-removeChecksums")
    d = os.path.join(work, "k_%08x.d.ns" % crc(path + "d"))
    ns(tool, "-d", q, "-o", d)
    return open(d, encoding=ENC).read()


def check(unified, overlay, tool):
    target = os.path.join(overlay, "anteara", "scripts")
    unified_qdir = qdir_files(open(os.path.join(unified, "qdir.txt"), encoding=ENC).read())
    installed_qdir = qdir_files(open(os.path.join(target, "qdir.txt"), encoding=ENC).read())
    print("qdir.txt: %s" % ("same files, same order" if [f.lower() for f in unified_qdir] ==
                            [f.lower() for f in installed_qdir] else "DIFFERS"))
    same, unit_same, differ = 0, 0, []
    with tempfile.TemporaryDirectory() as work:
        for rel in unified_qdir + ["localtext.qb"]:
            a = code_lines(tool, os.path.join(unified, "src", rel + ".ns"), work, from_ns=True)
            b = code_lines(tool, os.path.join(target, rel), work)
            if a == b:
                same += 1
                continue
            # same units in another order?
            ua = sorted(norm(t) for _, _, t in split(a)[1])
            ub = sorted(norm(t) for _, _, t in split(b)[1])
            if ua == ub:
                unit_same += 1
                continue
            d = list(difflib.unified_diff(a.split("\n"), b.split("\n"), "merged", "installed", n=0, lineterm=""))
            differ.append((rel, d))
    print("%d files the same code, %d the same units in another order, %d differ:" % (same, unit_same, len(differ)))
    for rel, d in differ:
        print("--- %s (%d diff lines)" % (rel, len(d)))
        sys.stdout.buffer.write(("\n".join(d[:40]) + "\n").encode(ENC, "replace"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["extract", "check", "install"])
    parser.add_argument("--unified")
    parser.add_argument("--game", default=r"C:\Games\THUG2\Game")
    parser.add_argument("--overlay")
    parser.add_argument("--ns", default=os.path.join(REPO, "out", "neverscript", "ns.exe"))
    args = parser.parse_args()
    # (a stock install keeps the game in a Game subfolder: C:\Games\THUG2 -> C:\Games\THUG2\Game)
    if not os.path.isdir(os.path.join(args.game, "Data")) and os.path.isdir(os.path.join(args.game, "Game", "Data")):
        args.game = os.path.join(args.game, "Game")
    if args.action == "extract":
        extract(args.unified, args.game, args.ns)
    elif args.action == "check":
        check(args.unified, args.overlay or os.path.join(args.game, "thug2-decomp-data"), args.ns)
    else:  # (without the overlay's edits - make_menu_level.py installs it with them)
        print(install(args.game, args.overlay or os.path.join(args.game, "thug2-decomp-data"), args.ns))


if __name__ == "__main__":
    main()
