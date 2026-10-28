#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"

// The engine's sector (Nx::CSector; the PC sector CPcSector - pc_scene.h - extends it): a piece of a scene with a
// name, its geometry (a CGeom - drawn) and its collision (a CCollObj - collided with), moved, turned, scaled, shown,
// hidden and coloured through both.
namespace Nx {

class CCollObj;

// CSector::m_flags (4, 8, 0x10 and 0x20 are acted on by the scene's next UpdateSuperSectors)
enum : uint32_t {
    kSectorCollidable = 0x01,      // (set when made)
    kSectorClone = 0x02,           // a clone: its collision object is its own (deleted with it)
    kSectorAddToGrid = 0x04,       // its collision to be put in the super sectors (a clone made with Clone's flag)
    kSectorDelete = 0x08,          // to be taken out of them and deleted
    kSectorRemoveFromGrid = 0x10,  // to be taken out of them
    kSectorMoved = 0x20,           // moved, turned or scaled while kSectorInGrid: to be moved in them
    kSectorInGrid = 0x40,          // its collision is in the scene's super sectors
};

// The sector (0x1C bytes of the engine's; vtable 0x00649A40 - PC 0x0064A718). Its virtual slots: [0] destroy, [1]
// PlatSetShatter, [2] PlatGetShatter, [3] PlatClone (the platform's new sector).
class CSector {
public:
    // 0x0049E900: construct - collidable, no geometry or collision, unturned, the default terrain 0xE3714DC1.
    // Returns this.
    CSector* Construct();
    // 0x0049E630: the destructor's body - its collision object deleted if it's a clone's, its geometry deleted.
    void DestroyBody();
    // 0x0049E930 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CSector* Destroy(unsigned int freeFlags);
    // 0x0049E8C0 / 0x0049E8D0 / 0x0049E8E0 (virtual [1] - [3], the engine's stubs: "STUB: Plat...").
    void PlatSetShatter(bool on);
    bool PlatGetShatter();
    CSector* PlatClone(int instance, void* scene);

    // 0x0049EAD0: a clone - the platform's new sector with a new name (0x0048CA80), these flags and kSectorClone,
    // the turn, a clone of the geometry (for `scene`) and of the collision (named as the clone, out of the grid,
    // kSectorAddToGrid with `flag`; the geometry given its faces). Null if the platform made none.
    CSector* Clone(int instance, bool flag, void* scene);

    // 0x0049E660: the geometry.
    void SetGeom(void* geom);
    // 0x0049EAA0: the collision object, if it has none yet (the geometry given its faces). Whether it was set.
    bool SetCollObj(CCollObj* coll);
    // 0x0049E810 (4 bytes; folded with other classes' getters of the word at +8): the flags.
    uint32_t GetFlags();
    // 0x0049E890 / 0x0049E8A0: flags set / cleared.
    void SetFlags(uint32_t flags);
    void ClearFlags(uint32_t flags);

    // 0x0049E670: the geometry's colour taken off.
    void ClearColor();
    // 0x0049E980: the geometry's colour (RGBA) into `out` - 0 without geometry. Returns out.
    uint8_t* GetColor(uint8_t* out);
    // 0x0049E680 / 0x0049E690: the viewports it shows in (a mask; 0 without geometry).
    void SetVisibility(uint32_t mask);
    uint32_t GetVisibility();
    // 0x0049E9B0 / 0x0049E9E0: drawn and collided with (the collision's flag 4 cleared), or neither; whether it is
    // (the geometry's, else the collision's flag).
    void SetActive(bool active);
    bool IsActive();
    // 0x0049EA30: collided with or not (kSectorCollidable; the collision's flag 2 cleared / set).
    void SetCollidable(bool collidable);
    // 0x0049EA60: an occluder - the collision's flag 0x80 set (and its occlusion polygons added, 0x0040AEB0) the
    // first time, cleared with false. Nothing without collision.
    void SetOccluder(bool occluder);

    // 0x0049E6A0: its box (the geometry's, else the collision's - slot 21).
    const Mth::BBox* GetBoundingBox();
    // 0x0049E740: its orientation (the collision's - slot 8 - either way).
    const Mth::Matrix* GetOrientation();
    // 0x0049E6C0 / 0x0049E700: where it is / its orientation (collision and geometry; kSectorMoved when in the grid).
    void SetWorldPosition(const Mth::Vector* position);
    void SetOrientation(const Mth::Matrix* orientation);
    // 0x0049E760: turned to `turns` quarter turns about y (the collision about the geometry's position, else its
    // own) - by the difference from the last.
    void SetYRotation(int turns);
    // 0x0049E7C0: its scale (the collision about the geometry's position, else its own; kSectorMoved when in the
    // grid).
    void SetScale(const Mth::Vector* scale);
    // 0x0049EA00: the collision geometry's vertices set (kSectorMoved when in the grid). Nothing without collision
    // or its geometry.
    void SetRawVertices(const Mth::Vector* vertices);

    // The geometry's vertex colour and UV wibbles and texture animation (nothing without geometry): 0x0049E820 the
    // vertex colour wibble played from a keyframe (time in ms), 0x0049E830 the UV wibble's parameters, 0x0049E840
    // the explicit UV wibble on / off, 0x0049E850 its offsets, 0x0049E860 / 0x0049E870 / 0x0049E880 the texture
    // animation reset, started, stopped.
    void PlayVCWibbleFromKeyframe(int sequence, int keyframe, int timeMs);
    void SetUVWibbleParams(float uVel, float uAmp, float uFreq, float uPhase, float vVel, float vAmp, float vFreq,
                           float vPhase);
    void UseExplicitUVWibble(bool on);
    void SetUVWibbleOffsets(float u, float v);
    void ResetTextureAnim();
    void StartTextureAnim(bool reset);
    void StopTextureAnim();

    void** vtable;
    uint32_t m_checksum;   // +0x04: its name
    uint32_t m_flags;      // +0x08
    uint32_t m_terrain;    // +0x0C: its terrain type (the node's TerrainType)
    int m_rotY;            // +0x10: quarter turns about y
    void* mp_geom;         // +0x14: CGeom (on PC a CPcGeom), or null
    CCollObj* mp_collObj;  // +0x18: or null
};
static_assert(offsetof(CSector, m_rotY) == 0x10 && offsetof(CSector, mp_collObj) == 0x18 && sizeof(CSector) == 0x1C,
              "layout must match the original");

} // namespace Nx
