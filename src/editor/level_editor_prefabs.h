#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_layout.h"

// NOT ORIGINAL - the level editor's prefabs (ours, at the user's request: Unity's prefabs, Unreal's Blueprints /
// packed level actors, lite - a set of parts saved once and placed again anywhere, as one thing).
//
// A prefab is any mix of the level's parts kept together in a file of its own: scenery pieces (the level's own or
// prop packs' - level_editor_props.h), objects, and nodes - rails (the level's or drawn), ladders, paths, triggers (the
// presets: boosters, teleports and their exits, gaps), spawns, crowns, lights, sounds, proximity nodes.
//   - Made from the selection (Edit > Create Prefab, Ctrl+Alt+G; the Outliner's right-click; the Content Browser's
//     Prefabs folder) or from one of the Outliner's groups (its right-click, or the group dragged onto the Content
//     Browser): the parts chosen and what comes with them - the rail and ladder points over the chosen pieces, the
//     editor's own nodes and triggers within the parts' box, a teleport's exit, a gap's other end, a rail's whole chain.
//   - Kept relative to a pivot: the middle of the parts' box, on its bottom. The file is a layout fragment (the
//     layout's own records - level_editor_layout.h - so it reads as one): `clone` records (a piece placed relative to
//     its source as loaded), `copy` (objects), `node`, `trigger`, `point` / `links` (chains), with `prefab <name>`,
//     `bounds`, `source <scene> <level>` and `prop <pack>` (the packs its pieces come from) - in
//     <output>\thug2-decomp-prefabs\<id>.txt (import::output_folder, beside the prop packs), so every level can use it.
//   - Shown in the Content Browser's Prefabs folder, each with a picture (drawn by the content pipeline's
//     prop_packs.ts prefab-thumb from the pieces' meshes, images\prefabs\<id>); dragged into the Scene (a box of its
//     size follows the ground) or Place at Cursor: its packs loaded, every part put in place at once - the nodes and
//     triggers renamed where their names are taken, the links between them (a rail's, a teleport's target, a gap's
//     ends - a fresh GapID) following the new names - one undo step, the parts made a group of the Outliner ("<name>
//     1", "<name> 2"... - Select it there: the prefab's instance). Placed, they are plain parts: saved, loaded and
//     exported to retail as any. No live link to the file (changing the prefab later changes no instance).
//   - A piece of this level placed in another level isn't there: left out and said (prop packs' pieces go anywhere).
//   - Right-click a prefab: Place at Cursor, Update from Selection (the file made again from the selection, its name
//     kept), Delete, Copy Name.
namespace frontend::level_editor::prefabs {

inline constexpr const char* kFolder = "thug2-decomp-prefabs";
inline constexpr const char* kHeader = "# thug2-decomp prefab";
// The ImGui drag payload of an Outliner group (its index) - dropped on the Content Browser it makes a prefab.
inline constexpr const char* kGroupPayload = "THUG2_ED_GROUP";

// --- the pure parts -------------------------------------------------------------------------------------------------

struct Prefab {
    std::string id;   // its file's name ("ramp_set")
    std::string name; // as shown ("Ramp Set")
    uint32_t scene = 0;
    std::string level; // the level it was made in ("BO"; "" not known)
    V3 min{}, max{};   // the parts' box, relative to the pivot
    std::vector<std::string> packs;
    layout::FileLoad parts; // relative to the pivot
};
// How many parts it has.
int part_count(const Prefab& prefab);
// The file's text, and one read back (false: not a prefab).
std::string prefab_text(const Prefab& prefab);
bool read_prefab(const std::string& text, Prefab* prefab);
// A file name for a prefab's name: letters and digits, lower case, '_' between words (at most 40); "prefab" when
// nothing is left.
std::string id_of(const std::string& name);
// Every place in a fragment moved by `delta`.
void translate(layout::FileLoad* parts, V3 delta);
// What placing needs to know of the level: a node name taken, an object id taken, whether a sector / an object to
// copy is there.
struct PlaceHooks {
    std::function<bool(const std::string& name)> name_taken_;
    std::function<bool(uint32_t id)> id_taken_;
    std::function<bool(uint32_t sector)> sector_exists_;
    std::function<bool(uint32_t object)> object_copyable_;
};
// What a placement added: the new copies' ids, the nodes' names (triggers' and chain points' too), how many parts,
// and the parts left out (not in this level).
struct PlaceResult {
    std::vector<uint32_t> copies;
    std::vector<std::string> nodes;
    int clones = 0;
    int added = 0;
    std::vector<std::string> missing;
};
// The prefab's parts put at `at` into `into` (a layout of the level as it is now): names not taken there (each name's
// own, else name_2, name_3...; a node of the editor's kinds its stem's next), the links between the parts following
// them, fresh GapIDs, the copies named as Ctrl+V names them.
PlaceResult instantiate(const Prefab& prefab, V3 at, const PlaceHooks& hooks, layout::FileLoad* into);
// A name not taken: `name`, else name_2, name_3...
std::string unique_name(const std::string& name, const std::function<bool(const std::string&)>& taken);

// --- the game side ------------------------------------------------------------------------------------------------

// The action (Create Prefab); once, as the editor starts.
void prepare_all();
// Once a frame from the editor's Update (the Content Browser's tick): what the UI asked for done.
void tick_frame();
// Edit > Create Prefab and the Outliner's menus: the name asked, then the prefab made of the selection / of group
// `group` (-1: the selection).
void ask_create(int group);
// The Content Browser's Prefabs folder: its items (`scale` the UI's), the drop target for a group, and the drag's
// feedback (a box of the prefab's size over the Scene) - drawn every frame, the panel open or not.
void folder_body(float scale);
void accept_group_drop();
void overlay(float scale);
// How many prefabs there are (the folder's count).
int count();
// The automation channel's `fe editor prefab ...`: create <name> [sectors=<hex>,...] [objects=<hex>,...]
// [nodes=<name>,...] (the selection when none is given), place <name> [x,y,z] (the cursor when none), list. "ok ..." or
// "error: ...".
std::string automation(const std::vector<std::string>& words);

} // namespace frontend::level_editor::prefabs
