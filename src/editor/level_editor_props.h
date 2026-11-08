#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/level_editor.h"

namespace frontend::level_editor::ghost {
struct Part;
}

// NOT ORIGINAL - prop packs in the level editor (ours, at the user's request: pieces of any THUG2 level placed in any
// other - LA's fire truck in Boston).
//
// A pack is a piece of a level cut out by the content pipeline (prop_packs/prop_packs.ts extract: the sectors of a
// scenery piece or trick-object cluster, their textures and collision, the grind rails on them) into a small scene of
// its own, levels\<pack>\<pack>.scn / .tex / .col.xbx in the data overlay, moved so the middle of its bottom is at the
// origin, with <pack>.txt beside it (its sectors' names, its rails). Packs are named pp_<level>_<piece>.
//   - Loading one into the level being edited is the game's own LoadScene / LoadCollision scene=<pack> (a scene beside
//     the level's, as skies are; every scene's collision is walked by the collision tests): its sectors are scenery
//     pieces like the level's - picked, moved, turned, copied, saved in the layout by their checksums. They load
//     hidden (at the origin).
//   - Placing it (the Content Browser's drop, Place at Cursor): the first time its own sectors shown and put at the
//     point (the middle of their bottom on it); after that, copies of them (as Ctrl+D) - with the pack's rails as rail
//     points the editor added (level_editor_chains.h), at their places on the piece. One undo step.
//   - The layout records the packs the level uses: `prop <pack>` (an older reader skips it); they are loaded before
//     the layout's sector / clone records are put in place. The level as shipped never loads them.
//   - Retail: the pipeline's retail export bakes the pieces placed in THUG2's own levels into one <level>_Props scene
//     the level loads (prop_packs/retail_props.ts) - the rails not yet.
namespace frontend::level_editor::props {

inline constexpr const char* kPackPrefix = "pp_";
// The way packs are cut now (the manifest's `prop-pack <version>` - prop_packs/pack.ts MANIFEST_VERSION): a pack cut by
// an older pipeline is cut again before it is placed (when the content tools are there). 2: the collision-only objects
// sunk below a piece's bottom taken along (LA's Fire_Tender: its collision shell - without it the skater rolls through
// the truck's sides, which start above the skater's feelers).
inline constexpr int kManifestVersion = 2;

// --- the pure parts -------------------------------------------------------------------------------------------------

struct RailInfo {
    std::string name;
    V3 pos{};
    std::string terrain; // TERRAIN_... ("" none)
    std::vector<std::string> links;
    bool trick = false;
};
struct Manifest {
    int version = 0; // (the header's)
    std::string id;
    std::string source_level;
    std::string source_piece;
    std::vector<uint32_t> sectors;         // the pack's sectors (its own first)
    std::vector<std::string> sector_names; // their names in the source level
    std::vector<RailInfo> rails;
};
// The manifest's text (prop_packs/pack.ts makePack): false when it isn't one.
bool read_manifest(const std::string& text, Manifest* manifest);
// Whether a scene's name is a pack's.
bool is_pack_scene(const char* name);
// A rail point's name not among `taken`: `name`, else name_2, name_3...
std::string free_name(const std::string& name, const std::vector<std::string>& taken);

// --- the game side ------------------------------------------------------------------------------------------------

// The pack's manifest from the overlay (false: not there).
bool find_manifest(const std::string& id, Manifest* manifest);
// Whether it is loaded in this level.
bool is_loaded(const std::string& id);
// Loaded into the level (LoadScene / LoadCollision), its sectors named and hidden. False (`message`): no such pack, or
// it didn't load.
bool load(const std::string& id, std::string* message);
// Placed at `at` (the middle of its bottom on it): loaded first if need be; selected; one undo step. False: refused
// (`message`).
bool place(const std::string& id, V3 at, std::string* message);
// The packs loaded in this level.
const std::vector<std::string>& loaded_packs();
// The loaded pack a sector is one of (its own sectors - a copy's source, not the copy): its id and the piece it was
// cut from ("Fire_Tender (LA)"). False: none.
struct PackOf {
    std::string id;
    std::string piece;
};
bool pack_of(uint32_t sector, PackOf* pack);
// The drag's preview (level_editor_ghost.h): the pack's sectors drawn where place(id, at) would put them - its own
// sectors or copies of them, as place decides. False: not loaded.
bool ghost_parts(const std::string& id, V3 at, std::vector<ghost::Part>* parts);
// Whether a loaded pack is used in the level: one of its sectors shown or put somewhere, or a shown copy of one. A pack
// loaded only for a drag's preview (or whose placement was undone) isn't: no `prop` record is written for it.
bool in_use(const std::string& id);
// Whether a sector is a loaded pack's, resting as it loaded (hidden where the pack put it): not the layout's.
bool is_resting(uint32_t sector);
// The layout's own records (layout::FileLoad::extra): its packs loaded, before its sector records are put in place.
void load_from_layout(const std::vector<std::string>& extra);
// The layout extension (`prop` records); once, as the editor starts.
void prepare_all();
// A new level: nothing loaded.
void forget_level();

} // namespace frontend::level_editor::props
