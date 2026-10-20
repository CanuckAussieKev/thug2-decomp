#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "game/front/level_editor.h"
#include "game/mth/vector.h"

namespace Nx {
struct CCollObjTriData;
struct CCollBSPNode;
struct sMesh;
struct sVertexBuffer;
} // namespace Nx

// NOT ORIGINAL - the level editor's scaling of scenery (ours, added at the user's request; see level_editor.h): a
// uniform scale of a selected scene sector or group, 0.25x to 4x, about the middle of the selection's bottom (it keeps
// standing where it stands). Non-uniform scale isn't offered (a later step: normals and the collision's face-rejection
// would have to follow).
//
// The engine has no scale for a level sector: it moves and turns one in place (level_editor_scenery.h) - the meshes'
// vertices (each vertex buffer's copy in memory, uploaded on unlock), their bounding spheres and the collision's
// vertices, box and BSP tree. So a scale is written the same way: the first time a sector is scaled, its vertices and
// collision as they are then are kept (an Original), and from then on, after the engine has turned / moved it, they are
// written again from that copy - turned, scaled about the geometry's world position and moved there. A scale is always
// applied from the copy (no drift, however often it changes; back to 1 gives the copy's own numbers back). Normals are
// left as they are (a uniform scale doesn't change them).
//
// The collision (Nx::CCollObjTriData, coll_tri.h): vertices are floats, or - "compressed" - 16-bit offsets from the
// box's minimum in sixteenths of a unit (at most 4095.9 units across); its BSP tree's splits are in sixteenths too. It
// is put back from the copy (vertices, box, tree, face-rejection scale) and scaled / turned / moved with the game's own
// CCollObjTriData::Scale / RotateY / Translate (as the park editor's pieces are), so a compressed geometry keeps its
// format: its scale is limited to what fits (CompressedLimit). Scale and RotateY clear the face-rejection scale (+0x3C):
// line tests then reject nothing by distance - correct, a little slower. The sector is flagged for the super sectors'
// update (as moves are) and Commit puts its collision where it now reaches.
namespace Front::LevelEditor::Scale {

constexpr float kMin = 0.25f;
constexpr float kMax = 4.0f;
// [ / ]: a step down / up (Shift: a fine step).
constexpr float kStep = 1.1f;
constexpr float kFineStep = 1.01f;
// A compressed collision geometry's largest side (16-bit sixteenths: 4095.9; a margin for the rounding of the box).
constexpr float kCompressedExtent = 4000.0f;

// --- the pieces (pure, self-tested: src/hook/selftests_editor_scale.cpp) ----------------------------------------

// Scaled by `factor` about `pivot` (a world point, where it is now): x' = (x - pivot) * factor + pivot.
void ScaleAbout(Placement* placement, V3 pivot, float factor);
// The factor a selection may be scaled by: `factor` limited so that each member's scale (`scales`) times it stays
// within kMin and that member's limit (`limits`: kMax, or less for compressed collision). 1 when it can't go that way
// at all; a factor that isn't a positive number is 1.
float AllowedFactor(float factor, const std::vector<float>& scales, const std::vector<float>& limits);
// The largest scale a compressed collision geometry of `extent` (its largest side unscaled) allows (at most kMax).
float CompressedLimit(float extent);
// The value the Inspector's field asks for, limited to kMin..kMax (not a number: 1).
float Clamp(float scale);
// A point of the copy (taken with the engine's position at `from`) where it goes for the engine's position `to`, turned
// `quarters` quarter turns more and scaled by `scale`: to + QuarterTurn((p - from) * scale, quarters).
V3 Rebuilt(V3 p, V3 from, V3 to, int quarters, float scale);
// A vertex buffer's positions (the first 12 bytes of each of `count` vertices `stride` apart) written from `original`
// (x, y, z each) as Rebuilt.
void RewritePositions(uint8_t* data, uint32_t stride, uint32_t count, const float* original, V3 from, V3 to,
                      int quarters, float scale);
// A layout record's scale: " scale=<s>" after a record (nothing when 1), and reading a field "scale=<s>" (false: not
// that field, or not a positive number).
std::string Token(float scale);
bool ReadToken(const std::string& field, float* scale);

// --- the collision's copy (pure too: our CCollObjTriData code on any geometry) -----------------------------------

struct CollisionCopy {
    Nx::CCollObjTriData* data = nullptr;
    uint16_t numVertices = 0;
    bool compressed = false;
    void* vertexArray = nullptr;
    std::vector<uint8_t> vertices;
    Mth::Vector min = {}, max = {};
    uint32_t rejection = 0;
    struct Node {
        Nx::CCollBSPNode* node;
        int32_t split;
        uint32_t link;
    };
    std::vector<Node> nodes; // (the tree's split nodes; leaves don't change)
};
CollisionCopy CopyCollision(Nx::CCollObjTriData* data);
// Whether the geometry is still the one copied (its arrays, count and format).
bool SameCollision(const CollisionCopy& copy);
// The geometry put back as copied, then scaled by `scale` about `from`, turned `quarters` about it and moved to `to`.
void RebuildCollision(const CollisionCopy& copy, int quarters, V3 from, V3 to, float scale);
// The largest scale a geometry allows (kMax unless compressed), its scale now being `scale`.
float CollisionLimit(const Nx::CCollObjTriData* data, float scale);

// --- the game side -----------------------------------------------------------------------------------------------

// A sector's meshes and collision as they were when it was first scaled, with the engine's state then.
struct Original;
// Kept for a sector (CSector) and its geometry (the PC CGeom) with the engine at `quarters` (CSector +0x10) and
// `position` (the geometry's world position); null if its meshes can't be copied.
std::shared_ptr<Original> Capture(uint8_t* sector, uint8_t* geom, int quarters, V3 position);
// The sector's meshes (uploaded) and collision written for the engine now at `quarters` / `position`, scaled by `scale`
// about that position. False (nothing written) when the sector no longer matches its copy (another level).
bool Rebuild(const Original& original, uint8_t* sector, uint8_t* geom, int quarters, V3 position, float scale);
// The largest scale a sector's collision allows (kMax with none, or not compressed), its scale now being `scale`.
float Limit(uint8_t* sector, float scale);

} // namespace Front::LevelEditor::Scale
