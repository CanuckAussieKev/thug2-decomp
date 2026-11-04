#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"
#include "engine/maths/vec4.h"

namespace render {
class HitMesh;
class PcGeom;
class WorldCell;
struct TriMeshSource;
struct HitTreeNode;
struct MeshRef;
struct VertexBuffer;
} // namespace render
namespace objects {
class GameEntity;
}

// NOT ORIGINAL - the level editor's scaling (ours, added at the user's request; see level_editor.h): scenery and
// objects scaled - uniformly or stretched along one axis at a time -, 0.25x to 4x on each axis.
//
// Scenery (a selected scene sector or group): about the middle of the selection's bottom (it keeps standing where it
// stands). The engine has no scale for a level sector: it moves and turns one in place (level_editor_scenery.h) - the
// meshes' vertices (each vertex buffer's copy in memory, uploaded on unlock), their bounding spheres and the collision's
// vertices, box and BSP tree. So a scale is written the same way: the first time a sector is scaled, its vertices,
// normals and collision as they are then are kept (an Original), and from then on, after the engine has turned / moved
// it, they are written again from that copy - scaled along the piece's own axes, turned and moved there. A scale is
// always applied from the copy (no drift, however often it changes; back to 1 gives the copy's own numbers back).
// Normals (meshes whose vertex format has them - the FVF's NORMAL bit, three floats after the position): a stretch
// changes them - they're the copy's through the inverse transpose (n / scale per axis), normalised, and turned with the
// piece (the engine's own quarter turns leave normals as they are; a scaled piece gets them right). Textures stretch
// with the faces (a known look: a stretched brick wall has stretched bricks).
//
// The collision (render::TriMeshSource, coll_tri.h): vertices are floats, or - "compressed" - 16-bit offsets from the
// box's minimum in sixteenths of a unit (at most 4095.9 units across on each axis); its BSP tree's splits are in
// sixteenths too. It is put back from the copy (vertices, box, tree, face-rejection scale) and scaled (the game's own
// TriMeshSource::scale_tool 0x00416730 takes a vector: per axis, the tree's splits on each axis too) / turned / moved with
// the game's own code (as the park editor's pieces are), so a compressed geometry keeps its format: its scale is limited
// to what fits (compressed_limit, per axis). Scale and RotateY clear the face-rejection scale (+0x3C): line tests then
// reject nothing by distance - correct, a little slower. The sector is flagged for the super sectors' update (as moves
// are) and Commit puts its collision where it now reaches. Shared collision (level_editor_shared_coll.h) follows the
// same placement.
//
// Objects (level objects, game objects, cars - anything with a model): about the object's own position, along its own
// axes. The model's scale (RenderModel::change_scale 0x00492EE0: drawn scaled) and the object's collision: the collision
// component (GameObjects::ContactShapeModule, engine/objects/collision_component.h) keeps a movable collision object - a triangle
// object (render::HitMovingTriangles; several under a HitMeshGroup) or a box (HitMovingBox) - whose geometry is the model's, shared
// by every object of that model and kept in the object's own space. So the first time an object is scaled each of its
// triangle objects gets a geometry of its own (TriMeshSource::Replicate, owned by the collision object: deleted with it,
// kOwnsGeometry), kept as copied, and every scale is written from that copy (Scale about the object's origin); a box is
// scaled from its first box. Scale 1 puts the copies' numbers back exactly.
namespace frontend::level_editor::scale_tool {

constexpr float kMin = 0.25F;
constexpr float kMax = 4.0F;
// [ / ]: a step down / up (Shift: a fine step); Alt+[ / ]: the same along one axis (a stretch).
constexpr float kStep = 1.1F;
constexpr float kFineStep = 1.01F;
// A compressed collision geometry's largest side (16-bit sixteenths: 4095.9; a margin for the rounding of the box).
constexpr float kCompressedExtent = 4000.0F;

// --- the pieces (pure) ------------------------------------------------------------------------------------------

inline V3 get_uniform(float scale) { return {.x = scale, .y = scale, .z = scale}; }
// Scaled by `factor` (along the world's axes) about `pivot` (a world point, where it is now): x' = (x - pivot) * factor
// + pivot, per axis.
void scale_about(Placement* placement, V3 pivot, V3 factor);
inline void scale_about(Placement* placement, V3 pivot, float factor)
{
    scale_about(placement, pivot, get_uniform(factor));
}
// The factor a selection may be scaled by (per axis): `factor` limited so that each member's scale (`scales`, along
// the same axes) times it stays within kMin and that member's limit (`limits`: kMax, or less for compressed
// collision). 1 on an axis that can't go that way at all; a factor that isn't a positive number is 1. `uniform`: the
// same on every axis - the most every axis allows.
V3 allowed_factor(V3 factor, const std::vector<V3>& scales, const std::vector<V3>& limits, bool uniform);
// The largest scale a compressed collision geometry of `extent` (its side along that axis, unscaled) allows (at most
// kMax).
float compressed_limit(float extent);
// The value the Inspector's field asks for, limited to kMin..kMax (not a number: 1).
float confine(float scale);
inline V3 confine(V3 scale) { return {.x = confine(scale.x), .y = confine(scale.y), .z = confine(scale.z)}; }
// The world axis a direction is most along (0 x, 1 y, 2 z): Alt+[ / ] stretch along the axis the camera faces most.
int main_axis(V3 direction);
// The factor `f` along axis `axis` only.
V3 along_axis(int axis, float factor);
// Of three axes (an object's rows), the one most along the world's axis `world_axis`.
int nearest_axis(std::span<const V3, 3> axes, int world_axis);
// "1.50x" (uniform) or "1.50 x 1.00 x 2.00".
std::string describe(V3 scale);
// A point of the copy (taken with the engine's position at `from`) where it goes for the engine's position `to`, turned
// `quarters` quarter turns more and scaled by `scale` (along the copy's axes): to + get_quarter_turn(per_axis(p - from,
// scale), quarters).
V3 get_rebuilt(V3 point, V3 from, V3 to, int quarters, V3 scale);
// How a copy taken with the engine at `copied_quarters` / `copied_position` (the sector's quarters when first touched:
// `quarters0`) is written for the engine now at `quarters` / `position` and the placement's `scale` (along the piece's
// axes as first touched): positions get_rebuilt(p, from, to, turns, copy_scale_); normals scaled_normal(n, scale,
// normal_turns_) - the engine never turns normals, so the copy's are the piece's as first touched.
struct RebuildFrame {
    V3 from, to;
    int turns;
    V3 copy_scale_;
    int normal_turns_;
};
RebuildFrame frame_for(int copied_quarters, V3 copied_position, int quarters0, int quarters, V3 position, V3 scale);
// A vertex buffer's positions (the first 12 bytes of each of `count` vertices `stride` apart) written from `original`
// (x, y, z each) as Rebuilt.
void rewrite_positions(uint8_t* data, uint32_t stride, uint32_t count, const float* original, V3 from, V3 to,
                       int quarters, V3 scale);
// A normal of the copy for a piece scaled by `scale` and turned `quarters`: get_quarter_turn(normalise(n / scale)) - the
// inverse transpose of a per-axis scale (unscaled: just turned, not normalised again; zero stays zero).
V3 scaled_normal(V3 normal, V3 scale, int quarters);
// Where the normals are: the vertices' stride, their count and the normal's offset.
struct NormalRun {
    uint32_t stride{};
    uint32_t count{};
    uint32_t offset{};
};

// The normals (at `offset` in each vertex) written from `original` as scaled_normal.
void rewrite_normals(uint8_t* data, NormalRun normal_run, const float* original, V3 scale,
                     int quarters);
// Where a mesh's vertices keep their normal (-1: none): its vertex format (MeshRef +0x38) a Direct3D FVF with XYZ
// positions and NORMAL (0x10) - the normal's three floats after the position - in a vertex of `stride` bytes. (Meshes
// with one of the built-in vertex shaders have a handle there, not an FVF: none.)
int surface_nudge(uint32_t fvf, uint32_t stride);
// A layout record's scale: " scale=<s>" (uniform) or " scale=<x>,<y>,<z>" after a record (nothing when 1, 1, 1), and
// reading a field "scale=<s>" / "scale=<x>,<y>,<z>" (false: not that field, or not positive numbers).
std::string get_token(V3 scale);
bool read_token(const std::string& field, V3* scale);

// --- the collision's copy (pure too: our TriMeshSource code on any geometry) -----------------------------------

struct CollisionCopy {
    render::TriMeshSource* data = nullptr;
    uint16_t num_vertices_ = 0;
    bool compressed = false;
    void* vertex_array_ = nullptr;
    std::vector<uint8_t> vertices;
    maths::Vec4 min = {}, max = {};
    uint32_t rejection = 0;
    struct Elem {
        render::HitTreeNode* node;
        int32_t split;
        uint32_t link;
    };
    std::vector<Elem> nodes; // (the tree's split nodes; leaves don't change)
};
CollisionCopy copy_collision(render::TriMeshSource* data);
// Whether the geometry is still the one copied (its arrays, count and format).
bool same_collision(const CollisionCopy& copy);
// The geometry put back as copied, then scaled by `scale` (along its axes as copied) about `from`, turned `quarters`
// about it and moved to `to`.
void rebuild_collision(const CollisionCopy& copy, int quarters, V3 from, V3 to, V3 scale);
// The largest scale on each of its axes a geometry allows (kMax unless compressed), its scale along them now being
// `scale`.
V3 collision_limit(const render::TriMeshSource* data, V3 scale);

// --- the game side: scenery --------------------------------------------------------------------------------------

// A sector's meshes and collision as they were when it was first scaled, with the engine's state then.
struct Original;
// Kept for a sector and its geometry with the engine at `quarters` (the sector's quarter turns) and
// `position` (the geometry's world position); null if its meshes can't be copied.
std::shared_ptr<Original> get_capture(render::WorldCell* sector, render::PcGeom* geom, int quarters, V3 position);
// The sector's meshes (uploaded) and collision written for the engine now at `quarters` / `position`, scaled by `scale`
// (the placement's: along the piece's axes when first touched, the engine then at `quarters0`) about that position.
// False (nothing written) when the sector no longer matches its copy (another level).
bool reassemble(const Original& original, render::WorldCell* sector, render::PcGeom* geom, int quarters, V3 position,
                V3 scale, int quarters0);
// The largest scale along each world axis a sector's collision allows (kMax with none, or not compressed), its scale
// along them now being `world_scale`.
V3 get_limit(render::WorldCell* sector, V3 world_scale);

// --- the game side: objects --------------------------------------------------------------------------------------

// An object's scale as the editor set it (along its own axes; 1, 1, 1 when it hasn't).
V3 get_object_scale(objects::GameEntity* object);
// Whether the object can be scaled: it has a model.
bool object_scalable(objects::GameEntity* object);
// The object's model and collision scaled to `scale` (its own axes, about its position; each axis within kMin and
// what its compressed collision allows). False when it can't be (no model, or its collision couldn't be copied).
bool set_object_scale(objects::GameEntity* object, V3 scale);
// The largest scale on each of the object's axes its collision allows (kMax unless compressed).
V3 object_limit(objects::GameEntity* object);
// A new level: the objects' records forgotten (their collision copies belong to the objects).
void forget_objects();

} // namespace frontend::level_editor::scale_tool
