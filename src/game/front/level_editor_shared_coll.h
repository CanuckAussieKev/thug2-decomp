#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "game/front/level_editor.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"

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
// belong to it are copied into a collision object of our own for the piece (a Companion: a plain CCollStatic with the
// sector collision vtable and a float geometry without a BSP tree), and the originals are turned off (face flag 0x10,
// "not collidable": every feeler's default ignore mask - docs/pending/editor-view.md's face flags; the camera's too).
// From then on the companion's vertices are written from their copy by the piece's placement (the same Apply the
// meshes follow: moved, turned, scaled exactly with it), and Commit puts the companion in the scene's collision grid
// (CCollGrid::Update 0x00418F60 - added once, then moved - as UpdateSuperSectors 0x0049D780 does for sectors).
// A face belongs to the piece when:
//   - it is collidable and not a trigger (flags 0x10 / 0x40 clear: gap and kill volumes stay where they are), its
//     collision-only sector is on (no flag 2 / 4), and all three vertices are within the piece's box (grown kNear);
//   - and either its object is named after the piece (<name>_col / _ic / _col01 / _ic01 - BlackBoard_Cut_Col), or
//     every vertex is within kNear units of the piece's triangles (its meshes as drawn) and the piece is strictly the
//     nearest of the level's other solid pieces (not see-through, not the editor's copies) whose boxes hold the face.
//     Faces of an object named after another piece stay with that one. See-through pieces (decals, shadows, glows) take
//     nothing.
// At most kMostFaces faces (the no-tree face list the queries fall back on - CCollObjTriData::GetFacesInBox - has 2048).
// A copy (Ctrl+D) gets a copy of its source's companion (or of the faces that would be its source's), as the engine's
// CloneSector copies a sector's own collision; the originals stay on for the source.
namespace Front::LevelEditor::SharedCollision {

constexpr float kNear = 6.0f;
constexpr int kMostFaces = 2048;

// --- the pieces (pure, self-tested) -----------------------------------------------------------------------------

// The distance from a point to a triangle (a degenerate one: to its longest edge).
float PointTriangleDistance(V3 p, V3 a, V3 b, V3 c);
// The least of `best` and the distance from `p` to the triangles of a strip (each three consecutive indices; those
// repeating an index or past `vertexCount` skipped), positions every `stride` bytes.
float StripDistance(V3 p, const uint8_t* positions, uint32_t stride, uint32_t vertexCount, const uint16_t* indices,
                    uint32_t indexCount, float best);
// Whether a face whose farthest vertex is `own` from the piece (and `nearestOther` from the nearest other piece) is
// the piece's: within kNear and strictly nearer.
bool Belongs(float own, float nearestOther);
// Whether a face of these flags may go with a piece: collidable, not a trigger.
inline bool Takeable(uint32_t flags) { return !(flags & 0x50); }

// A face as copied: its corners (world space), flags, terrain and its vertices' bytes (the ground's light).
struct FaceCopy {
    V3 v[3];
    uint16_t flags, terrain;
    uint8_t bytes[3];
};
// Face `face` of a geometry (compressed or not, small faces or not).
FaceCopy ReadFace(const Nx::CCollObjTriData* geometry, int face);

// A collision object of our own (see above): the faces given (corners shared where equal), float vertices, no tree, no
// face rejection (scale 0). `Base` is where the vertices were given; Place writes them where `placement` takes those.
class Companion {
public:
    Companion(uint32_t checksum, const std::vector<FaceCopy>& faces);
    Companion(const Companion&) = delete;
    Companion& operator=(const Companion&) = delete;

    void Place(const Placement& placement);
    // The vertices where they are now become the base (a placement of none leaves them).
    void Rebase();
    // The faces as they are now (a copy's).
    std::vector<FaceCopy> Faces() const;
    // On / off (the object's flag 4, as CSector::SetActive turns a sector's collision off).
    void SetActive(bool active);
    bool Active() const;

    Nx::CCollStatic* Object() { return &m_object; }
    const Nx::CCollObjTriData& Geometry() const { return m_geometry; }
    int FaceCount() const { return m_geometry.m_numFaces; }
    int VertexCount() const { return m_geometry.m_numVertices; }
    V3 BaseVertex(int i) const { return m_base[i]; }
    V3 Vertex(int i) const;

private:
    void UpdateBox();

    std::vector<V3> m_base;
    std::vector<float> m_vertices;
    std::vector<uint8_t> m_faces;      // 10 bytes each: flags, terrain, three u16 indices
    std::vector<uint8_t> m_bytes;      // one per vertex
    std::vector<uint8_t> m_rejection;  // one per face (all 0: the rejection scale is 0)
    Nx::CCollObjTriData m_geometry{};
    Nx::CCollStatic m_object{};
};

// --- the game side ----------------------------------------------------------------------------------------------

// The piece's companion made if it has none yet (its faces gathered; the originals turned off) - while the piece is
// still where the editor first found it (before its first move: ApplyTrack calls it first). Nothing when the setting
// is off ([editor] shared_collision=0) or no face belongs to it.
void Ensure(uint32_t checksum, uint8_t* scene, uint8_t* sector, uint8_t* geom);
// The companion (if any) placed with the piece.
void Place(uint32_t checksum, const Placement& placement);
// The piece's track started again where it is (level_editor_scenery.cpp's TrackOf): its companion's base is now.
void Rebase(uint32_t checksum);
// Shown / hidden with the piece.
void SetActive(uint32_t checksum, bool active);
// A copy `copy` (in `scene`) of `source` made: the copy's companion, from the source's (or from the faces that belong
// to the source, left on). Call before the copy exists in the level (it would compete for them) - `Prepare` - and once
// it does - `Finish`.
std::shared_ptr<std::vector<FaceCopy>> PrepareCopy(uint32_t source);
void FinishCopy(const std::shared_ptr<std::vector<FaceCopy>>& faces, uint32_t copy, uint8_t* scene, uint8_t* sector,
                uint8_t* geom);
// The scene's companions put in its collision grid / moved there (after UpdateSuperSectors).
void Commit(uint8_t* scene);
// How many pieces have a companion, and their faces (the HUD's debug line, the log).
int Count(int* faces = nullptr);

} // namespace Front::LevelEditor::SharedCollision
