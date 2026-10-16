# Checks that no PartyMod patch lands inside the first bytes of a function we replace.
#
# MinHook copies a replaced function's first instructions into its trampoline when our hooks are installed;
# PartyMod patches the exe after that. A PartyMod patch inside those first bytes would be lost from the
# trampoline (and broken by our jump), so the two must never overlap. Run from the repo root:
#   python tools/check_partymod_overlap.py
import glob
import re
import sys

WINDOW = 16  # more than any trampoline copies

src = "".join(open(f, encoding="utf-8", errors="replace").read()
              for f in glob.glob("src/partymod/**/*.[ch]", recursive=True))
src = re.sub(r"//[^\n]*", "", src)  # (commented-out patches don't count)
src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
names = {m.group(1): int(m.group(2), 16)
         for m in re.finditer(r"(addr_\w+)\s*=\s*\(?(?:void\s*\*\))?\s*\(?(0x[0-9a-fA-F]+)", src)}
patches, unresolved = set(), set()
for m in re.finditer(r"patch\w*\(\s*\(?(?:void\s*\*\)\s*)?\(?(0x[0-9a-fA-F]+|addr_\w+)\s*(?:([+-])\s*(\d+))?", src):
    target, offset = m.group(1), int(m.group(3) or 0) * (-1 if m.group(2) == "-" else 1)
    value = int(target, 16) if target.startswith("0x") else names.get(target)
    if value is None:
        unresolved.add(target)
    else:
        patches.add(value + offset)

replaced = set()
for path in glob.glob("src/**/*.cpp", recursive=True):
    for m in re.finditer(r"REPLACE(?:_SHADOW|_UNCHECKED)?\(0x([0-9A-Fa-f]{8})", open(path, encoding="utf-8").read()):
        replaced.add(int(m.group(1), 16))

# Patches inside the 16-byte window that are known to be past the instructions MinHook copies (checked by hand).
PAST_TRAMPOLINE = {
    0x0051F4C8: "0x0051F4C0's hook copies 6 bytes (PUSH ECX; MOV EAX,[0x007CEFA8])",
}
overlaps = sorted((p, r) for p in patches for r in replaced if r <= p < r + WINDOW and p not in PAST_TRAMPOLINE)

# A patch anywhere inside a rewritten function is bypassed by our version (the game runs ours, not the patched
# original), so our source must do what the patch does. Each such patch is listed here with how it's done.
HANDLED = {
    0x0054DD14: "Net::RetrieveTopStats uses PartyMod's gamestats host (partymod_gamestats_host)",
    0x004B91E7: "Nx::DrawFlash rotates the first three vertices as PartyMod's reorder_flash_vertices does",
    0x0044FB8C: "Nx::DrawMovieFrame calls PartyMod's movie_setTexture",
    0x0044FC88: "Nx::DrawMovieFrame draws through PartyMod's movie_drawPrim",
    0x0044FC8D: "(the NOP after that call)",
    0x0051F4C8: "Skater::Component::PlatformStructure compares with 5 instead of 7 under PS2 controls",
    0x00527546: "Skater::Component::TryWalkAcidDrop skips the +0xC0 button under PS2 controls",
    0x00527636: "Skater::Component::TryAcidDrop skips the +0xC0 button under PS2 controls",
    0x00504239: "Skater::CorePhysicsComponent::Spin skips the L1 spin delay under PS2 controls",
    0x0050430B: "Skater::CorePhysicsComponent::Spin skips the R1 spin delay under PS2 controls",
    0x00507184: "Skater::CorePhysicsComponent::BreakVert takes either trigger under PS2 controls",
    0x00507185: "(the same JNZ)",
    0x005071AE: "Skater::CorePhysicsComponent::BreakVert takes either trigger under PS2 controls (second check)",
    0x005071AF: "(the same JNZ)",
    0x00533652: "GameNet::KeyboardTask takes F1 as PartyMod's key 0x1F",
    0x0046EE80: "Script::LoadQB loads through PartyMod's pipLoadWrapper (its script patches)",
    0x0048C170: "Nx::CFunc_SetScreenBlur (source only): PartyMod's restoreBlur rewrites the bytes already there",
    0x0048C171: "(the same five bytes)",
    0x0048C172: "(the same five bytes)",
    0x0048C173: "(the same five bytes)",
    0x0048C174: "(the same five bytes)",
    0x004A0780: "Nx::SetScreenAspectRatio is PartyMod's setAspectRatio (source only)",
    0x004A07C0: "Nx::GetScreenAngleFactor is PartyMod's getScreenAngleFactor (source only)",
    0x004B2330: "Nx::SetScreenBlur is PartyMod's setBlurWrapper when DisableBlur is on (source only)",
    0x004B2440: "Nx::SetLetterbox is PartyMod's setLetterbox (source only)",
    0x005BDA31: "Sio::Device::ActivateActuator leaves out the ReadData call PartyMod NOPs",
    0x005BDCC0: "Sio::Device::Process is PartyMod's processController (source only)",
    0x005A6E7E: "Mc::GetMemCardSpaceAvailable calls PartyMod's calcAvailableSpaceFudged",
    0x005F3A50: "Platform::SetActuators is PartyMod's set_actuators (source only)",
    0x005F4591: "Platform::InitInput calls PartyMod's writeConfigValues",
    0x005F459D: "Platform::InitInput leaves out the DirectInput set-up PartyMod NOPs",
    0x005F45A2: "Platform::InitInput calls PartyMod's initManager",
    0x005F56E0: "Platform::ProcessWindowEvents is PartyMod's handleEvents (source only)",
    0x0048D8F8: "Nx::LoadFont loads the meta button map PartyMod's GlyphStyle names (MetaButtonMapName)",
    0x0048D983: "Nx::CFunc_LoadFont uses the button font PartyMod's GlyphStyle names (ButtonFontName)",
    0x004B6C64: "Nx::UpdateLoadingBarColors writes its table into PartyMod's resbuffer",
    0x004B2E65: "Nx::RenderWorld calls PartyMod's setDisplayRegion for a letterboxed viewport",
    0x004B33CA: "Nx::RenderWorld calls PartyMod's setDisplayRegion for the letterboxed full-screen viewport",
    0x004B341D: "Nx::RenderWorld draws the 2D through PartyMod's draw2DWrapper (black bars)",
    0x004DB35D: "Nx::SetCamera uses PartyMod's far plane (96000)",
    0x004DF63A: "Nx::DrawScreenBlur leaves out the letterbox adjustment PartyMod jumps over",
}
import json
bodies = {f["addr"]: f["end"] for f in json.load(open("data/derived/functions.json", encoding="utf-8"))}
all_registered = set()
for path in glob.glob("src/game/**/*.cpp", recursive=True):
    for m in re.finditer(r"REPLACE\w*\(0x([0-9A-Fa-f]{8})", open(path, encoding="utf-8").read()):
        all_registered.add(int(m.group(1), 16))
inside = sorted((p, r) for p in patches for r in all_registered if r in bodies and r <= p < bodies[r])
unhandled = [(p, r) for p, r in inside if p not in HANDLED]
for p, r in inside:
    print(f"  0x{p:08X} inside rewritten 0x{r:08X}: " + (HANDLED.get(p) or "NOT HANDLED - our version bypasses this patch"))

print(f"{len(patches)} PartyMod patch addresses, {len(replaced)} replaced functions")
for name in sorted(unresolved):
    print(f"  could not resolve {name}")
for p, r in overlaps:
    print(f"  OVERLAP: PartyMod patches 0x{p:08X}, inside the start of replaced 0x{r:08X}")
print("No overlaps." if not overlaps else f"{len(overlaps)} overlap(s).")
if unhandled:
    print(f"{len(unhandled)} PartyMod patch(es) inside rewritten functions not handled.")
sys.exit(1 if overlaps or unresolved or unhandled else 0)
