#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/front/level_editor.h"

namespace Front::LevelEditor::Overlays {
struct Painter;
}

// NOT ORIGINAL - the level editor's raised / lowered ground with ramps (ours; see level_editor.h - the UX research's
// skate idea #7, after the THPS3 / THPS4 / THUG park editors, where a block of ground goes up or down and its edges
// become quarter pipes or banks).
//
// THUG2's levels are fixed shipped meshes: nothing can be cut or reshaped. So a raise / lower is a GENERATED PIECE - a
// new scene sector made at run time (and baked into the level's files by tools/game/apply_layout.ts):
//   - Raise: a platform over a rectangle on the ground (snapped to a grid), `height` high, each of its four sides a
//     plain wall (wall-ridable) or a ramp up to its deck, outside the rectangle: a kicker (a straight bank) or a quarter
//     pipe (a circular transition, vert at its top) - `length` long (0: a kicker twice the height, a quarter pipe its
//     height as its radius).
//   - Lower: a pit - impossible without cutting the floor, so only where the floor under the rectangle is pieces wholly
//     inside it (tiles): those are hidden (CSector::SetActive - neither drawn nor collided with) and the pit is made:
//     walls facing in, a floor `height` below, ramps inside the walls (a bowl with quarter pipes). Where a floor piece
//     (or a collision-only object holding the ground's faces) reaches outside the rectangle, it is refused with why.
//
// How a piece is made at run time (level_editor_ground.cpp): a copy of a level sector (CScene::CloneSector 0x0049DF30,
// as the editor's copies - its own meshes, buffers and collision, in the scene's render lists and collision) whose
// material the piece takes (the "material" - by default the floor piece clicked); then the copy's first mesh gets new
// buffers holding the generated vertices (the donor's vertex format: position, normal, colour, texture coordinates;
// NewVertexBuffer 0x004D5760 / NewIndexBuffer 0x004D4270, as sMesh::Clone makes them) and a triangle strip with every
// triangle both ways (drawn whatever the material's culling), its other meshes' strips made degenerate; its collision
// geometry gets new arrays (Mem::New, as CCollObjTriData::Clone - the game frees them with the level) holding the
// generated faces (floats, 10-byte faces) and a one-leaf BSP tree, the face-rejection scale 0; the super sectors
// updated. Face flags: decks and ramps skatable (0x01), quarter pipes' faces steeper than 45 degrees vert too (0x08),
// walls and cheeks wall-ridable (0x04); terrain the donor's commonest. A piece isn't movable as scenery
// (Scenery::WhyNotMovable): it is edited through its own form (the Ground tool, Alt+U) and its record.
//
// Layout record (version 2 still; older readers keep it as an unknown record):
//   ground <name> raise|lower rect=x0,z0,x1,z1 base=y height=h ramps=<-x>,<+x>,<-z>,<+z> length=l material=<sector>
//          uv=u color=AARRGGBB terrain=t [hides=<sector>,<sector>...]
// ramps: `-` none, `k` kicker, `q` quarter pipe. uv: world units per texture repeat; color: the vertices' colour (both
// measured on the donor when the piece was made, so the bake gives the same piece). hides: the floor tiles a pit hides.
namespace Front::LevelEditor::Ground {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux9.cpp; tools/game/ground_pieces.ts is their port) -------

enum class Ramp : uint8_t { None, Kicker, Quarter };
enum Side { kMinusX, kPlusX, kMinusZ, kPlusZ };

struct Spec {
    std::string name;  // ed_ground01 (the sector's name)
    bool lower = false;
    float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f; // the rectangle (x0 < x1, z0 < z1)
    float base = 0.0f;   // the ground's height there
    float height = 200.0f;
    std::array<Ramp, 4> ramps = {Ramp::None, Ramp::None, Ramp::None, Ramp::None}; // by Side
    float length = 0.0f; // the ramps' (0: automatic)
    uint32_t material = 0; // the donor sector (its material and vertex format)
    float uv = 256.0f;   // world units per texture repeat
    uint32_t colour = 0xFF808080u; // ARGB
    uint16_t terrain = 0;
    std::vector<uint32_t> hides; // (lower) the floor tiles hidden
};
bool SameSpec(const Spec& a, const Spec& b);

constexpr float kMinHeight = 10.0f, kMaxHeight = 2000.0f;
constexpr float kMinSide = 50.0f, kMaxSide = 8000.0f;
constexpr float kMaxLength = 4000.0f;
constexpr float kSink = 4.0f;        // walls go this far below the ground (no seam on uneven ground)
constexpr int kQuarterSegments = 8;  // a quarter pipe's faces along its curve
constexpr uint16_t kFlagDeck = 0x0001, kFlagRamp = 0x0001, kFlagVert = 0x0009, kFlagWall = 0x0004;
constexpr int kMaxFaces = 2048;      // (a geometry with no tree lists every face through a table of 2048 - coll_tri.h)

// The rectangle put in order, sizes and lengths kept within the limits, the uv scale sensible (false: not a number).
bool Normalise(Spec* spec);
// A ramp's length as built: the automatic one (a kicker twice the height, a quarter pipe its height), and in a pit
// opposite ramps kept from meeting (each at most half the width across, less 10).
float RampLength(const Spec& spec, int side);

struct Vertex {
    V3 pos, normal;
    float u, v;
};
struct Face {
    uint16_t a, b, c;
    uint16_t flags;
};
struct Mesh {
    std::vector<Vertex> vertices; // (each face its own corners: flat shaded)
    std::vector<Face> faces;      // (winding: (b - a) x (c - a) points out of the solid - where the skater is)
    V3 min = {}, max = {};
};
Mesh Build(const Spec& spec);

// The profile of a quarter pipe of radius `radius` rising `height` (distance out from its top edge, height above its
// foot), foot first: a circular arc (truncated when lower than the radius) and, higher than the radius, a vertical
// extension.
std::vector<std::pair<float, float>> QuarterProfile(float radius, float height, int segments);

// A triangle strip drawing every face both ways (`twoSided`) or as wound: degenerate joins, each face starting at an
// even position (Direct3D's strips flip odd triangles). And the strip read back into its triangles (degenerate ones
// left out; odd positions flipped).
std::vector<uint16_t> Strip(const std::vector<Face>& faces, bool twoSided);
std::vector<std::array<uint16_t, 3>> StripTriangles(const std::vector<uint16_t>& strip);

// A Direct3D FVF's vertex layout (the donor's): byte offsets (-1: none) - false when it isn't a plain XYZ format whose
// size is `stride` (a built-in shader handle 0..6, a larger handle, other position types).
struct Layout {
    int normal = -1, size = -1, diffuse = -1, specular = -1, tex = -1;
    int texCount = 0;
};
bool LayoutFor(uint32_t fvf, uint32_t stride, Layout* layout);
// The vertices written in that layout (`out`: mesh.vertices.size() * stride bytes).
void WriteVertices(const Mesh& mesh, const Layout& layout, uint32_t stride, uint32_t colour, uint8_t* out);

// The record (after "ground ") and back (false: a bad or repeated key, a missing rect / base / height / material, an
// unknown kind; `known`: the material's name, if any).
std::string Record(const Spec& spec, const char* materialName);
bool ReadRecord(const std::string& text, Spec* spec);
// "ed_ground" and the lowest two-digit number not taken.
std::string FreshName(const std::vector<Spec>& specs);
char RampLetter(Ramp ramp);

// A pit's floor: a piece with ground faces under the rectangle is hidden when its whole box (x / z) lies within the
// rectangle grown by `margin`; one reaching out refuses the pit.
bool TileInside(V3 min, V3 max, const Spec& spec, float margin);
// Whether a triangle is ground under the rectangle: facing up (normal y >= 0.7), within `tolerance` of `base` in height,
// and its box overlapping the rectangle shrunk by 1.
bool GroundFaceUnder(const V3 corners[3], const Spec& spec, float tolerance);

// A value on a grid (0: as it is).
float SnapTo(float value, float grid);

// --- the game side ----------------------------------------------------------------------------------------------

// The pieces of this level (as their records say) and whether one is made now.
const std::vector<Spec>& All();
bool IsPiece(uint32_t sector);
const Spec* Find(const std::string& name);
// The editor starting / stopping.
void Start();
void Stop();
// A piece made / changed (`spec.name` new or one of the pieces) - or refused (`why`): one undo step. Removed likewise.
bool Put(const Spec& spec, std::string* why);
void Remove(const std::string& name);
// Everything as `specs` says (undo, a layout put in place): made, changed, removed. How many are in place.
int SetAll(const std::vector<Spec>& specs, std::string* message);
// A pit's tiles under the rectangle (`spec.hides` set) - false and why when the floor there reaches out of it.
bool FindPitTiles(Spec* spec, std::string* why);
// The donor chosen for a material (`sector`: the floor clicked, else the selection; 0: any) - its colour, uv scale and
// terrain read into `spec`. False (`why`) when no level piece can lend its material.
bool ChooseMaterial(uint32_t sector, Spec* spec, std::string* why);
// Why a sector can't lend its material (null: it can).
const char* WhyNotDonor(uint32_t sector);

// The layout's records (Layout::AddExtension).
std::vector<std::string> CaptureRecords();
void ApplyRecords(const std::vector<std::string>& lines);
void ResetAll();

// Drawing a spec's piece as lines (the form's preview, a selected piece's outline).
void AddPreview(const Overlays::Painter& painter, const Spec& spec, uint32_t argb);

} // namespace Front::LevelEditor::Ground
