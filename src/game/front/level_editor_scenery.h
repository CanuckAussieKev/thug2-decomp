#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor.h"

// NOT ORIGINAL - the level editor's scenery (ours; see level_editor.h): picking, outlining and moving the level's
// static scene sectors.
//
// What a sector is (Nx::CSector, 0x1C bytes, vtable 0x00649A40): +0x08 flags (0x40: its collision is in the scene's
// super sectors, 0x20: moved - the super sectors to be updated), +0x10 its quarter turns, +0x14 its geometry (the PC
// CGeom, vtable 0x0064A368: +0x14 / +0x24 the bounding box's min / max as loaded, +0x3C / +0x40 its meshes and their
// count, +0x48 active, +0x4C an instance - null for level geometry), +0x18 its collision (CCollStatic, vtable
// 0x00645C18: +0x04 the sector's checksum). A scene (0x006FA7EC's list) finds its sectors by checksum in a hash
// table at +0xD0 (0x0049CB00).
//
// Moving one is what the park editor does with its pieces (0x00594730): the geometry's SetWorldPosition (vfunc +0x48,
// 0x004B55D0: every mesh's sMesh::SetPosition 0x004D6E00 moves its vertices by the change and uploads them), the
// collision moved by the same change (CCollStatic::SetWorldPosition 0x00410C20 translates by the position given
// minus a constant origin, so it is handed the change), the sector flagged 0x20, and later the scene's
// UpdateSuperSectors (0x0049D780) puts its collision where it now is. Quarter turns: CSector::SetYRotation
// (0x0049E760: collision and meshes turned about the geometry's world position).
namespace Front::LevelEditor::Scenery {

// The sector under a ray: the game's collision ray (static collision only) up to `maxT` along the (unit) direction,
// skipping collision without geometry (invisible walls); sectors without collision by their meshes' triangles (their
// boxes only to skip the rest quickly). `t`: the distance. Locked pieces (level_editor_picking.h) are passed; so are
// structure and see-through pieces for a plain click - the nearest of those in front is `passed` (Alt+click takes it).
// True when either was found.
struct Hit {
    uint32_t checksum = 0;
    float t = 0.0f;
    uint32_t passed = 0;
    float passedT = 0.0f;
};
// `layers` (click-to-cycle): every piece the ray meets (not locked), nearest first, each once.
struct Layer {
    uint32_t checksum;
    float t;
    bool passed;
};
bool Pick(V3 origin, V3 unitDir, float maxT, Hit* hit, std::vector<Layer>* layers = nullptr);
// Every mesh alpha-blended (the material's flag 0x40, as the scene sorts them): a decal, a web, a shadow, a glow.
bool SeeThrough(uint32_t checksum);
// What a plain click passes (level_editor_picking.h): structure for a camera at `camera`, or see-through.
bool Passed(uint32_t checksum, V3 camera);

// A sector's box where it is now (false: no such sector, or no geometry).
bool Box(uint32_t checksum, OrientedBox* box);
// Why it can't be moved (null: it can).
const char* WhyNotMovable(uint32_t checksum);
// For the HUD: "scenery <name> <checksum> ..." for the hovered sector.
std::string DescribeHover(uint32_t checksum);
std::string DescribeSelection();

// The selection: sectors (not objects).
bool HasSelection();
const std::vector<uint32_t>& Selection();
bool Selected(uint32_t checksum);
void ClearSelection();
// The sector, the rest of its cluster (the node's TrickObject Cluster=) and the pieces built onto them (PieceFits /
// PieceTouches: the fire truck's ladder) that can be moved, their shadows (IsShadow) - and their grind rails (rail nodes of those clusters, or
// lying within a selected sector's box), which move and turn with them.
void SelectGroup(uint32_t checksum);
// What SelectGroup would select for that sector (kept while it stays the same: the hover's highlight).
const std::vector<uint32_t>& HoverGroup(uint32_t checksum);
// Alt+click: one sector in or out (the rails chosen again). Also Ctrl+click and the Inspector's x on a selected piece.
void Toggle(uint32_t checksum);
// A group's shadows (level_editor_shadows.h) are selected with it: whether a sector has been taken as one (`caster`:
// the member whose name it carries, or the one it lies under when taken by place). They move, turn, scale and undo with the rest; the pivot
// and the bottom are the rest's.
bool IsShadow(uint32_t checksum, uint32_t* caster = nullptr);
// The selection's rail points carried along (by name); one dropped from them (Ctrl+click on its gizmo, the Inspector's
// x: not carried again until another group is selected).
const std::vector<uint32_t>& SelectedRailNames();
bool CarriesRail(uint32_t name);
void DropRail(uint32_t name);
// The Inspector's "Keep only this": that sector alone (its rails with it).
void KeepOnly(uint32_t checksum);
// The selection's rails: each segment (a rail node to the next) where it is now; how many rail nodes.
std::vector<std::pair<V3, V3>> SelectedRails();
int SelectedRailCount();

// Grind rail points one by one (the node layer and the node edits - level_editor_nodes.h, level_editor_node_edit.h).
// The rail manager's point is where skaters grind (and where a moved sector's rails were taken): the node array's pos=
// stays as the level has it, so these are what the gizmos, the Inspector and the moves use.
// A rail point's place now (false: no such point - not a rail node, or the level's rails ride an object). `climbing`:
// a ladder's point (a ClimbingNode) is one too.
bool RailPoint(uint32_t name, V3* pos, bool climbing = false);
// Where the level has it (before the editor moved it, by its scenery or by itself).
bool RailOrigin(uint32_t name, V3* pos);
// Put at `pos` (from where the level has it: a plain move - the `rail` record of a layout), its segments' boxes (the
// one from it and the one to it) made again. False: no such rail point.
bool SetRailPoint(uint32_t name, V3 pos);
// Its rail's points: `name` first, then the rest of the rail from its first point to its last (the rail manager's
// links: next / prev).
std::vector<uint32_t> RailOf(uint32_t name);
// A number that changes whenever rail points move (the node layer reads their places again).
uint32_t RailGeneration();
// The rail manager's nodes were made again (level_editor_chains.h - its RebuildRails): the rail points the editor tracks
// found again by name (those gone forgotten), and the generation moved on.
void RailsRebuilt();
// Every rail and ladder point with its place now (the node layer).
void ForEachRailPoint(void (*each)(void* context, uint32_t name, V3 pos), void* context);
// The editor starting: no selection, node lookups forgotten.
void Reset();

// The selection's pivot (the centre of its boxes when selected, moved with it) and moving it there; turning it about
// the pivot by quarter turns. Both move the sectors at once (vertices and collision); Commit updates the scenes' super
// sectors (collision lookups) - after a drag, not every frame.
V3 Pivot();
// The height of the selection's bottom (its boxes' lowest point, moved with it).
float Bottom();
// Exactly these sectors selected (those that can still be moved; their rails with them) - undo.
void SetSelection(const std::vector<uint32_t>& sectors);
void MoveTo(V3 pivot);
void Turn(int quarters);
// Scaling (level_editor_scale.h): the selection's scale (its first piece's; 1 when not scaled), and the selection scaled
// by `factor` about the middle of its bottom - limited so every piece stays within 0.25x..4x (less for compressed
// collision). Returns the factor used (1: none). Vertices and collision at once, as Turn; Commit after.
float SelectionScale();
float ScaleSelection(float factor);
void Commit();
// A sector's collision moved by something else (level_editor_walls.h: an invisible wall): flagged for the super sectors'
// update as moves are, its scene updated at the next Commit.
void NoteCollisionMoved(uint32_t checksum);
// How many sectors have been moved since the level loaded.
int MovedCount();

// The selection's state: which sectors are selected, where the editor has put each (relative to where the level has
// it) and whether it's hidden, and the same for its rails. Undo / redo save it before a change and restore it; reset
// is every placement back to none, hiding is every piece hidden (CSector::SetActive: neither drawn nor collided with -
// its rails turned off too).
struct SectorState {
    uint32_t checksum;
    Placement placement;
    bool hidden;
};
struct RailState {
    uint32_t name;
    Placement placement;
    bool hidden;
};
struct State {
    std::vector<uint32_t> selection;
    std::vector<SectorState> sectors;
    std::vector<RailState> rails;
};
State SaveState();
void RestoreState(const State& state); // (selects state.selection again; collision updated)
// Those states put in place without touching the selection (layouts); collision updated.
void ApplyStates(const std::vector<SectorState>& sectors, const std::vector<RailState>& rails);
// What the editor has changed in this level: sectors moved or hidden (copies left out), rails moved or turned off.
std::vector<SectorState> ChangedSectors();
std::vector<RailState> ChangedRails();
// The level's main scene (the first that isn't a sky) and a scene's name (+0x84).
uint8_t* MainScene();
// Whether a scene is still one of the level's (in the scene list, not a sky).
bool SceneLive(uint8_t* scene);
uint32_t SceneName(uint8_t* scene);

// Copies of sectors (Ctrl+D, the browser): a full copy (CScene::CloneSector 0x0049DF30, as the park editor makes its
// pieces) of the source as it is now, in its scene, ready to move like any sector. Its checksum (0: not made).
uint32_t Duplicate(uint32_t source);
// The copies made this level: the copy, its source, and where the source had been put when it was copied (a copy's
// place relative to the source as loaded is its placement after that one - Compose).
struct Clone {
    uint32_t checksum;
    uint32_t source;
    Placement sourceAtCopy;
};
std::vector<Clone>& Clones();
bool IsClone(uint32_t checksum);
// Where the editor has put a sector (none: false), and whether it's hidden.
bool PlacementNow(uint32_t checksum, Placement* placement);
bool Hidden(uint32_t checksum);
// Shift+click: a sector's group added to the selection.
void AddGroup(uint32_t checksum);
// The browser's pieces: the main scene's shown sectors that can be moved (copies left out).
std::vector<uint32_t> MovablePieces();
// Home: the selection back where the level has it (and shown).
void ResetSelection();
// Delete: the selection hidden, and deselected.
void HideSelection();
// Where the first selected sector has been put (false: not moved).
bool SelectionMoved(Placement* placement);

// [features] debug_editor: the scenes and their sectors.
void LogCensus();

// The geometry of a sector (null: none).
uint8_t* SectorGeometry(uint32_t checksum);
// The sector of that checksum (null: none), and every sector of the level's scenes (skies left out) - the view
// options (level_editor_view.h) read their collision and visibility.
uint8_t* SectorOf(uint32_t checksum);
void ForEachLevelSector(void (*each)(void* context, uint32_t checksum, uint8_t* sector), void* context);

// Highlighting: geometry tinted with the engine's own colour (CGeom::SetColor, vfunc +0x18 - what SetObjectColor uses
// for graffiti: each mesh's colour, or an instance's), and put back exactly as it was when no longer wanted. Each
// frame: Tint what should show, then EndTints puts back the rest (`all`: everything - the editor stopping). Who owns
// a geometry: a sector (`index` -1, `id` its checksum) or an object (its id, the geometry's index in its model);
// `resolve` finds that owner's geometry now, and a geometry is only put back when it is still that.
struct TintOwner {
    uint32_t id;
    int index;
};
bool Tintable(void* geom); // (the PC geometry class)
void Tint(TintOwner owner, void* geom, uint32_t rgba);
void EndTints(void* (*resolve)(TintOwner), bool all);

} // namespace Front::LevelEditor::Scenery
