#pragma once

#include <cstdint>
#include <string>
#include <vector>

// NOT ORIGINAL - the level editor's budget meter (ours; see level_editor.h - the UX research's #14, after the Tony Hawk
// park editors' memory meter): a bar on the toolbar showing how close the level - the edited copy as it is now - is to
// the engine's hard limits, and what the editor's edits added; its tooltip (and the Problems window's Budget tab,
// level_editor_checks.h) lists every meter. The limits are the engine's own, found in the code:
//   crowns          10      CFunc_SpawnCrown 0x00538170 collects the crowns into two 10-entry stack arrays (unchecked)
//   restarts        256     GetRestartNode 0x0046B790: a 256-name stack array for GetRestartNodes 0x0046B6C0 (every
//                           restart of every node array - unchecked)
//   level lights    150     the light manager's level light list (count at 0x006FAEF4, AddLight 0x00490570)
//   dynamic lights  256     its dynamic light list (count at 0x006FAEF0)
//   nodes           65536   the node manager's name hash (SkateScript::CNodeManager at 0x006F0D84: the original's 0x4000
//                           slots, ours 0x10000 - [memory] node_hash_slots, memory_sizes.h; open addressing - HashNode
//                           0x0045CE60) - every node of every node array
//   node arrays     20      CNodeManager's arrays (AddNodeArray 0x0045D520) - the level's, the editor's overlay...
//   script pools            Script::CreateScriptPools (qb.cpp) - every node is a CStruct of CComponents, vectors and
//                           arrays from these (pool pairs, items in use / items - the pools' own counts; ours are
//                           bigger, game/core/memory_sizes.h; the original's in brackets):
//                           CStruct 64000 (19800, 0x006F3614), CComponent 360000 (86000, 0x006F2338), CVector 48000
//                           (9500, 0x006F3654), CArray 26000 (7000, 0x006F22B4), CSymbolTableEntry 45000 (12500,
//                           0x006F362C), CScript 1200 (300, 0x006F3548); HashItem 56000 (10000)
//   script heap     0x434770 bytes + the bigger pools + 16 MB (script_heap_extra_mb)   Mem::Manager::InitStandardHeaps 0x005BBE80: the script region
//                           (the editor's compiled trigger / proximity scripts go there)
//   main memory     64 MB + the same   Mem::Manager::Construct 0x005BBC00: the main region (top-down + bottom-up heaps): the
//                           level's geometry, the editor's copies' meshes (CScene::CloneSector) and collision
//   collision faces 2048 a piece (SharedCollision::kMostFaces - CCollObjTriData::GetFacesInBox's no-tree face list)
// What the edits added: the nodes / crowns / restarts / lights the editor added (counted), and for the pools and heaps
// the difference from when the level came up (taken in the first frame the level is up, before its edited copy's
// layout goes in - so the edited copy's own additions count too; the game's own allocations since move it as well, so
// it is approximate - "about").
namespace Front::LevelEditor::Budget {

// --- the pure parts --------------------------------------------------------------------------------------------------

// How full a meter is: Ok below 80 %, Near from 80 %, Over above the limit (at the limit exactly: Near - full, not
// over: 10 crowns are allowed).
enum class Level : uint8_t { Ok, Near, Over };
Level LevelOf(double used, double limit);
// The fill 0..1 (above 1 when over; 0 for a limit of 0).
double Fill(double used, double limit);
// ARGB of a level (green / amber / red).
uint32_t LevelColour(Level level);

struct Meter {
    const char* name = "";   // "Crowns"
    double used = 0.0;       // now
    double limit = 0.0;      // the engine's (0: no limit - a count only)
    double added = 0.0;      // what the editor's edits added (counts) or the change since the level came up (memory)
    bool bytes = false;      // used / limit / added are bytes (shown as KB / MB)
    bool approximate = false; // `added` is a difference of the game's own counters (see the top)
    const char* where = "";  // the limit's source in the engine (the tooltip)
};
// The worst meter (the bar shows it): the highest fill among meters with a limit (-1: none).
int Worst(const std::vector<Meter>& meters);
// "12 / 256", "3.2 MB / 64 MB" and "+4", "+1.5 MB" (the added part; "" when nothing).
std::string UsedText(const Meter& meter);
std::string AddedText(const Meter& meter);
// A byte count as people read it: "512 B", "12.5 KB", "3.25 MB".
std::string Bytes(double bytes);

// --- the game side ------------------------------------------------------------------------------------------------

// Every frame (the editor's Update, running or not - before the layout's Tick): the baseline taken in the first frame
// a level is up.
void Observe();
// The meters now (empty outside a level).
std::vector<Meter> Meters();
// The tool strip's meter (level_editor_ui_strip.h): the worst meter's fill and colour (false: no level), its tooltip
// (every meter; ImGui), and the Problems window's Budget tab's table.
bool StripMeter(float* fill, uint32_t* argb);
void Tooltip();
void Table();

} // namespace Front::LevelEditor::Budget
