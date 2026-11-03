#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <array>

#include "editor/level_editor.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace render {
class PcGeom;
class WorldCell;
struct WorldScene;
} // namespace render

// NOT ORIGINAL - the level editor's shared collision (ours, added at the user's request; see level_editor.h): a
// moved / turned / scaled piece of scenery takes its collision along when that collision isn't its own.
//
// Why: a THUG2 level's collision (<level>.col) isn't always one object per render sector. A sector's collision is the
// collision object of the same name (the scene's sector +0x18, moved by the engine with it - level_editor_scenery.h);
// but level designers also put collision into objects of their own that have no render sector - collision-only sectors
// (geometry null) - and those often hold the collision of render pieces nearby. TR's SkWare_Rail01 (a LevelGeometry
// rail box, TrickObject Cluster=SkWare_Rail01) has no collision object of its own: its 16 faces are in SkWare_Main_1
// (a collision-only sector of 20 faces - the other 4 are SkWare_Rail02's, 1400 units away). Moving the rail moved its
// meshes and its rail nodes, and the engine had no collision of the sector to move: the skater met an invisible box
// where the rail had been. (TR: 276 of its 790 render sectors have no collision of their own; 47 collision-only
// sectors. The game itself never moves such pieces - the park editor's pieces carry their own collision.)
//
// What we do: the first time the editor moves (or hides) a piece, the faces of the level's collision-only sectors that
// belong to it are copied into a collision object of our own for the piece (a Companion: a plain StaticCollider with the
// sector collision vtable and a float geometry without a BSP tree), and the originals are turned off (face flag 0x10,
// "not collidable": every feeler's default ignore mask; the camera's too).
// From then on the companion's vertices are written from their copy by the piece's placement (the same Apply the
// meshes follow: moved, turned, scaled exactly with it), and Commit puts the companion in the scene's collision grid
// (CollGrid::tick_frame 0x00418F60 - added once, then moved - as UpdateSuperSectors 0x0049D780 does for sectors).
// A face belongs to the piece when:
//   - it is collidable and not a trigger (flags 0x10 / 0x40 clear: gap and kill volumes stay where they are), its
//     collision-only sector is on (no flag 2 / 4), and all three vertices are within the piece's box (grown kNear);
//   - and either its object is named after the piece (<name>_col / _ic / _col01 / _ic01 - BlackBoard_Cut_Col), or
//     every vertex is within kNear units of the piece's triangles (its meshes as drawn) and the piece is strictly the
//     nearest of the level's other solid pieces (not see-through, not the editor's copies) whose boxes hold the face.
//     Faces of an object named after another piece stay with that one. See-through pieces (decals, shadows, glows) take
//     nothing.
// At most kMostFaces faces (the no-tree face list the queries fall back on - TriMeshSource::get_faces_in_box - has 2048).
// A copy (Ctrl+D) gets a copy of its source's companion (or of the faces that would be its source's), as the engine's
// duplicate_scene_sector copies a sector's own collision; the originals stay on for the source.
namespace frontend::level_editor::shared_collision {

constexpr float kNear = 6.0f;
constexpr int kMostFaces = 2048;

// --- the pieces (pure) ------------------------------------------------------------------------------------------

// The distance from a point to a triangle (a degenerate one: to its longest edge).
float point_triangle_distance(V3 p, V3 a, V3 b, V3 c);
// A strip's index count and the best distance so far.
struct StripLimit {
    uint32_t index_count_{};
    float best{};
};

// The least of `best` and the distance from `p` to the triangles of a strip (each three consecutive indices; those
// repeating an index or past `vertexCount` skipped), positions every `stride` bytes.
float strip_distance(V3 p, const uint8_t* positions, uint32_t stride, uint32_t vertex_count, const uint16_t* indices, StripLimit strip_limit);
// Whether a face whose farthest vertex is `own` from the piece (and `nearest_other` from the nearest other piece) is
// the piece's: within kNear and strictly nearer.
bool belongs(float own, float nearest_other);
// Whether a face of these flags may go with a piece: collidable, not a trigger.
inline bool takeable(uint32_t flags) { return !(flags & 0x50); }

// A face as copied: its corners (world space), flags, terrain and its vertices' bytes (the ground's light).
struct FaceCopy {
    std::array<V3, 3> v;
    uint16_t flags, terrain;
    std::array<uint8_t, 3> bytes;
};
// Face `face` of a geometry (compressed or not, small faces or not).
FaceCopy read_face(const render::TriMeshSource* geometry, int face);

// A collision object of our own (see above): the faces given (corners shared where equal), float vertices, no tree, no
// face rejection (scale 0). `Base` is where the vertices were given; Place writes them where `placement` takes those.
class Companion {
public:
    Companion(uint32_t checksum, const std::vector<FaceCopy>& faces);
    Companion(const Companion&) = delete;
    Companion& operator=(const Companion&) = delete;
    ~Companion() = default;

    void get_place(const Placement& placement);
    // The vertices where they are now become the base (a placement of none leaves them).
    void rebase();
    // The faces as they are now (a copy's).
    [[nodiscard]] std::vector<FaceCopy> get_faces() const;
    // On / off (the object's flag 4, as WorldCell::change_live turns a sector's collision off).
    void change_live(bool active);
    // A face's flags set (level_editor_faces.h: the face flags edited).
    void change_tri_flags(int face, uint16_t flags);
    [[nodiscard]] bool live_now() const;

    render::StaticCollider* get_object() { return &entity_; }
    [[nodiscard]] const render::TriMeshSource& get_geometry() const { return geometry_; }
    [[nodiscard]] int face_count() const { return geometry_.tri_count(); }
    [[nodiscard]] int get_vertex_count() const { return geometry_.num_vertices(); }
    [[nodiscard]] V3 base_vertex(int i) const { return core::at(base_, i); }
    [[nodiscard]] V3 get_vertex(int i) const;

private:
    void update_box();

    std::vector<V3> base_;
    std::vector<float> vertices_;
    std::vector<uint8_t> faces_;     // 10 bytes each: flags, terrain, three u16 indices
    std::vector<uint8_t> bytes_;     // one per vertex
    std::vector<uint8_t> rejection_; // one per face (all 0: the rejection scale is 0)
    render::TriMeshSource geometry_{};
    render::StaticCollider entity_;
};

// --- the game side ----------------------------------------------------------------------------------------------

// The piece's companion made if it has none yet (its faces gathered; the originals turned off) - while the piece is
// still where the editor first found it (before its first move: apply_track calls it first). Nothing when the setting
// is off ([editor] shared_collision=0) or no face belongs to it.
void ensure(uint32_t checksum, render::WorldScene* scene, render::WorldCell* sector, render::PcGeom* geom);
// The companion (if any) placed with the piece.
void get_place(uint32_t checksum, const Placement& placement);
// The piece's track started again where it is (level_editor_scenery.cpp's track_of): its companion's base is now.
void rebase(uint32_t checksum);
// Shown / hidden with the piece.
void change_live(uint32_t checksum, bool active);
// A copy `copy` (in `scene`) of `source` made: the copy's companion, from the source's (or from the faces that belong
// to the source, left on). Call before the copy exists in the level (it would compete for them) - `Prepare` - and once
// it does - `Finish`.
std::shared_ptr<std::vector<FaceCopy>> prepare_copy(uint32_t source);
void finish_copy(const std::shared_ptr<std::vector<FaceCopy>>& faces, uint32_t copy, render::WorldScene* scene,
                 render::WorldCell* sector, render::PcGeom* geom);
// The scene's companions put in its collision grid / moved there (after UpdateSuperSectors).
void finalise(render::WorldScene* scene);
// How many pieces have a companion, and their faces (the HUD's debug line, the log).
int total_of(int* faces = nullptr);
// The collision view (level_editor_view.h): every live companion that is on (its piece's checksum and it); the faces of
// a level geometry turned off for companions (their indices); a number that changes whenever faces are taken or given
// back (its cache is made again then).
void for_each_companion(void (*each)(void* context, uint32_t checksum, const Companion& companion), void* context);
std::vector<int> taken_faces(const render::TriMeshSource* geometry);
uint32_t get_generation();
// (level_editor_faces.h - face flags edited) A level geometry's face taken along by a piece's companion: the piece, the
// face's index in the companion and the flags it had (given back when the companion goes; the original carries 0x10
// meanwhile). False: not taken.
bool taken_face(const render::TriMeshSource* geometry, int face, uint32_t* piece, int* index, uint16_t* flags);
// A companion's face's origin: the level geometry and face it was taken from (false: none - a copy's companion, whose
// faces are copies).
bool origin_of(checksums::Name piece_name, int index, render::TriMeshSource** geometry, int* face);
// A face and the flags taken for it.
struct FaceFlags {
    int face{};
    uint16_t flags{};
};

// A taken face's flags set: the companion's copy and the flags given back when the companion goes (false: not taken).
bool set_taken_flags(const render::TriMeshSource* geometry, FaceFlags face_flags);
// The piece's companion (null: none, or its level gone).
const Companion* companion_of(uint32_t piece);

} // namespace frontend::level_editor::shared_collision
