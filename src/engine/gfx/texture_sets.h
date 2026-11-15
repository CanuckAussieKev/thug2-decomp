#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// NOT ORIGINAL - texture sets (ours, at the user's request 2026-11-13: "Can I toggle between textures from each model
// whilst in game?"): the base game's textures as the game has them (original) or upscaled by one of the AI models
// (tools/pipeline/hd_textures/base.ts: pbrify, seedvr2), installed side by side in the game folder -
// thug2-decomp-textures\<set>\ holds a texture dictionary's HD version under the name the game asks for
// (levels\bo\bo.tex.xbx), with the set's record set.json.
//
//   [Graphics] texture_set=original|pbrify|seedvr2   (default original; a set that isn't installed: original, logged)
//
// The data overlay (engine/file/data_overlay.h) serves a .tex.xbx from the chosen set's folder before anything else.
// The set changes while the game runs - the Options screen's Textures row, the console's `textures <set>` (automation:
// `console textures <set>`): the switch is made at the start of the next frame (the front end's frame, before anything
// is drawn) - the loaded level's texture dictionaries (every dictionary loaded from a file under levels\: the level, its
// sky, the scenes added to it) read again from the new set's files and each texture's image swapped in place (its
// TextureRef keeps its address: the materials, splats and projections that point at it draw the new one; the old
// device texture released through the TextureRef's own destructor - unset from the stages, its ray-tracing slot freed
// - render_pc/texture_set_reload.cpp). Other dictionaries (skaters, peds, vehicles - upscaled only with base.ts
// --scope all) take the set the next time they're loaded; a texture something is editing (a skater's colours) or
// replaced at run time keeps what it has until then too.
namespace render {
class TextureSet;
}

namespace render::texture_sets {

inline constexpr const char* kOriginal = "original";
// The sets base.ts makes, in the Options screen's order (after Original).
inline constexpr std::array<const char*, 2> kKnownSets = {"pbrify", "seedvr2"};

// The sets' folder (<game folder>\thug2-decomp-textures).
const std::string& sets_folder();
// Whether `set` is installed (its folder holds its record; original always is).
bool installed(std::string_view set);
// The known sets installed (kKnownSets' order).
std::vector<std::string> installed_sets();
// The de-lighting version a set's textures were made with (its set.json's "delight": base.ts --delight took the light
// painted into some of them out before upscaling - tools/pipeline/hd_textures/delight.ts); 0 for a set made without
// it, one not installed, and the original.
int delight_version(std::string_view set);
// The textures of the dictionary `overlay_name` ("levels\bo\bo.tex.xbx") that `set` made de-lit (its set.json's
// "delit" for that file - formats/texture_set_record.h), sorted checksums; empty for the original, a set made without
// de-lighting and a file it lists none for. The surface maps (render_pc/surface_map_load.h) mark them as they attach,
// and the renderer leaves those textures out of [Graphics] rt_delight - the same light flattened at draw time - while
// the set's other textures (most of them: delight.ts takes only what it is sure of) keep it.
std::vector<uint32_t> delit_textures(std::string_view set, std::string_view overlay_name);

// The set in use ("original" until [Graphics] texture_set names an installed one - read at the first file the game asks
// for, logged).
const std::string& active();

// The chosen set's file for a game file name as the data overlay keeps it ("levels\bo\bo.tex.xbx": lower case,
// backslashes), if it is a texture dictionary the set has: its full path in `path`. None while an OriginalFiles lives
// on this thread.
bool file_for(const char* overlay_name, std::string* path);
// While one lives (on the thread that made it), the files the game reads are its own - no set's: what the surface maps
// (render_pc/surface_map_load.h) read a set's dictionary's original from, to know its textures by the original texels
// the maps were made from.
class OriginalFiles {
public:
    OriginalFiles();
    ~OriginalFiles();
    OriginalFiles(const OriginalFiles&) = delete;
    OriginalFiles& operator=(const OriginalFiles&) = delete;
    OriginalFiles(OriginalFiles&&) = delete;
    OriginalFiles& operator=(OriginalFiles&&) = delete;
};

// The set to switch to (by the front end's next frame). Returns what happens, one line ("pbrify: switching" / "pbrify:
// not installed - tools/pipeline/hd_textures/base.ts" / "pbrify already in use").
std::string request(std::string_view set);
// [Graphics] texture_set read again (the Options screen wrote it) and requested. True: a change is coming (or none was
// needed).
bool request_from_ini();
// The switch requested, made now (the front end's frame - ext::menu_frame). Whether one was made.
bool tick();
// What the console's `textures` prints: the set in use, the sets installed and the last switch.
std::string status_text();

// --- the loaded dictionaries' files (texture_dict.cpp's load_tex_dict / unload_tex_dict) ---

void dictionary_loaded(TextureSet* dictionary, const char* file);
void dictionary_unloaded(const TextureSet* dictionary);

// What reads a dictionary's textures again from its file and swaps them in (the renderer's: render_pc, installed at
// start-up by app/renderer_choice.cpp). Returns the textures swapped (-1: the file couldn't be read).
using Reloader = int (*)(TextureSet* dictionary, const char* file);
void set_reloader(Reloader reloader);

// The console's command: `textures` (the status), `textures <set>` (a switch).
std::string console_command(const std::string& arguments);

} // namespace render::texture_sets

namespace ext {
class Extension;
}

namespace render::texture_sets {
// The extension that makes the switch at the front end's frame (app/extensions.cpp adds it).
ext::Extension& extension();
} // namespace render::texture_sets
