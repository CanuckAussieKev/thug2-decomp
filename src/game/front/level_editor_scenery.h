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
// skipping collision without geometry (invisible walls); sectors without collision by their boxes. `t`: the
// distance.
struct Hit {
    uint32_t checksum = 0;
    float t = 0.0f;
};
bool Pick(V3 origin, V3 unitDir, float maxT, Hit* hit);

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
// PieceTouches: the fire truck's ladder) that can be moved - and their grind rails (rail nodes of those clusters, or
// lying within a selected sector's box), which move and turn with them.
void SelectGroup(uint32_t checksum);
// What SelectGroup would select for that sector (kept while it stays the same: the hover's highlight).
const std::vector<uint32_t>& HoverGroup(uint32_t checksum);
// Alt+click: one sector in or out (the rails chosen again).
void Toggle(uint32_t checksum);
// The selection's rails: each segment (a rail node to the next) where it is now; how many rail nodes.
std::vector<std::pair<V3, V3>> SelectedRails();
int SelectedRailCount();
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
void Commit();
// How many sectors have been moved since the level loaded.
int MovedCount();

// [features] debug_editor: the scenes and their sectors.
void LogCensus();

// The geometry of a sector (null: none).
uint8_t* SectorGeometry(uint32_t checksum);

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
