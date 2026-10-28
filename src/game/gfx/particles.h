#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/pager_message.h"

// The engine's particle systems of the particle component (Nx; the PC class over them is CPcParticleSystem,
// pc_particles.h): a system made from a block of parameters, its bounding box and sphere worked out from the
// paths its particles take; and their manager (0x006FA7C4), which keeps them in a list and renders, updates,
// colours and destroys them. (The flexible particle component's systems are another family: particle_system.h.)
namespace Nx {

// The parameters a particle system is made from (0x15C bytes; kept as the system's +4). Only what the particle
// component fills is named; names from the script parameters where known.
struct CParticleParams {
    uint32_t m_name;          // +0x000 (0xA1DC81F9, required; the manager finds a system by it)
    uint32_t m_unknown04;     // +0x004 (0x7321A8D6)
    uint32_t m_groupId;       // +0x008 GroupID
    float m_size[3];          // +0x00C (0x3BEE67C8, 0x8E39913D, 0x3F243A3C; scaled)
    float m_radius[3];        // +0x018 (0x05400A25, 0xFF7D9AA9, 0x5539FCEE; scaled)
    int m_unknown24;          // +0x024 (0x9E52A20F)
    float m_unknown28;        // +0x028 (0x35B65BD4; the rate)
    float m_unknown2C;        // +0x02C (0xC218CF77; the lifetime, seconds)
    float m_unknown30;        // +0x030 (0x520682C6, then 0x59463C93; when the mid is, a percentage of the lifetime)
    float m_unknown34;        // +0x034
    float m_colourScale[4];   // +0x038 (r, g, b, a: the manager's colour)
    uint32_t m_emitScript;    // +0x048 (0x092276DB: the component's +0x18)
    uint32_t m_objectId;      // +0x04C its object's id (with the above)
    uint8_t unknown50[4];
    Mth::Matrix m_rotation;   // +0x054 (0x9D2D0915's angles: X, then Y, then Z)
    Mth::Vector m_pos[3];     // +0x094 start / mid / end (scaled, oriented)
    Mth::Vector m_box[3];     // +0x0C4 (scaled, oriented)
    Mth::Vector m_localPos[3]; // +0x0F4 the positions again (0xB7B7240D: they move with the object)
    float m_unknown124;       // +0x124
    uint8_t m_colour[3][4];   // +0x128 r, g, b (three arrays), a (three integers, at most 128)
    uint32_t m_unknown134;    // +0x134 (0xDD6BB3D5)
    int m_unknown138;         // +0x138 (0x13D0BE1D)
    int m_unknown13C;         // +0x13C (0x9F677D4E)
    uint32_t m_unknown140;    // +0x140 (0x7D99F28D)
    int m_unknown144;         // +0x144 (0x4EEDFAE7)
    int m_unknown148;         // +0x148 (0xD7E4AB5D)
    int m_unknown14C;         // +0x14C (0xE4279BA2)
    uint8_t m_unknown150;     // +0x150 (0x9D5923D8's low byte)
    bool m_flag151;           // +0x151 (0xA7A5BFFB; the paths go through the mid)
    bool m_localPositions;    // +0x152 (0xB7B7240D)
    bool m_flag153;           // +0x153 (0x454962EF)
    bool m_orientWithObject;  // +0x154 (0xA2487CC3)
    uint8_t unknown155[2];
    bool m_flag157;           // +0x157 (0x5446AF14)
    bool m_flag158;           // +0x158 (0xA0E585B0: takes the manager's colour)
    uint8_t unknown159[3];

    // 0x00496240: the defaults (sizes 1, no spread, boxes 20 wide, grey colours, rate 300, lifetime 4, mid at 50%,
    // ...). Returns this.
    CParticleParams* Construct();
    // 0x00495C80: a copy of `other` (its first 0x159 bytes: the last three are left). Returns this.
    CParticleParams* Assign(const CParticleParams& other);
};
static_assert(offsetof(CParticleParams, m_colourScale) == 0x38 && offsetof(CParticleParams, m_emitScript) == 0x48 &&
                  offsetof(CParticleParams, m_rotation) == 0x54 && offsetof(CParticleParams, m_pos) == 0x94 &&
                  offsetof(CParticleParams, m_box) == 0xC4 && offsetof(CParticleParams, m_localPos) == 0xF4 &&
                  offsetof(CParticleParams, m_unknown124) == 0x124 && offsetof(CParticleParams, m_colour) == 0x128 &&
                  offsetof(CParticleParams, m_unknown150) == 0x150 && offsetof(CParticleParams, m_flag158) == 0x158 &&
                  sizeof(CParticleParams) == 0x15C,
              "layout must match the original");

// A particle system of the particle component (0x190 bytes; vtable 0x00649380: 0 the scalar deleting destructor
// (0x00495230), 1 Initialise, 2 Release, 3 visibility changed, 4 Render(advance), 5 Update, 6 colour changed,
// 7 IsEmitting - the engine's own empty or constant; the PC class's real ones).
struct CParticle {
    void** vtable;
    CParticleParams m_params; // +0x004
    Mth::BBox m_bbox;         // +0x160: of every particle's path
    Mth::Vector m_sphere;     // +0x180: around the box (centre, radius)

    // 0x00496360: the vtable, the default parameters, an empty box. Returns this.
    CParticle* Construct();
    // 0x00496380: made from `params` (copied): the bounds worked out, then Initialise (vtable slot 1).
    void Init(const CParticleParams* params);
    // 0x00495260: m_bbox and m_sphere from the paths: each of the start, mid and end positions with its box and
    // size (the mid only through the extremes of the curve through them when the paths go through the mid).
    void UpdateBounds();
    // 0x00496030 / 0x00496160: position / box `which` (0 start, 1 mid, 2 end).
    void SetPosition(int which, const Mth::Vector* pos);
    void SetBox(int which, const Mth::Vector* box);
    // 0x00496060: position `which` (relative to the start) and box `which` turned by `matrix`; the parameters'
    // rotation becomes `matrix`.
    void Rotate(int which, const Mth::Matrix* matrix);
    // 0x00496190: with the manager's colour (m_flag158), its colour scale from `rgba` (bytes, in 128ths), then
    // vtable slot 6. Null: only slot 6.
    void SetColour(const uint8_t* rgba);
    // 0x00496210: with the manager's colour (m_flag158), its colour scale = `rgba`, then vtable slot 6.
    void SetColourScale(const float* rgba);
    // 0x004963B0: the emit script: the old one's spawned scripts killed (by its name), then `script` spawned with
    // {0x40C698AF: objectId}.
    void SetEmitScript(uint32_t script, uint32_t objectId);

    // The engine's, not yet rewritten (0x00494E00, 0x00494E20, 0x00494E40): rendered (unless hidden or suspended:
    // +0x159, +0x15A), updated, released (the emit script's scripts killed, vtable slot 2).
    void Render(bool advance);
    void Update();
    void Release();
};
static_assert(offsetof(CParticle, m_params) == 0x4 && offsetof(CParticle, m_bbox) == 0x160 &&
                  offsetof(CParticle, m_sphere) == 0x180 && sizeof(CParticle) == 0x190,
              "layout must match the original");

// The particle component's particle manager (0x1C bytes, vtable 0x006493A0: 0 the scalar deleting destructor,
// 1 Create - the PC manager's, CPcParticleManager); one, at 0x006FA7C4.
struct CParticleManager {
    void** vtable;
    Obj::PointerList m_systems; // +0x04: CParticle*
    float m_colour[4];          // +0x0C: given to the systems that take it

    // 0x00496770: the vtable, an empty list, white. Returns this.
    CParticleManager* Construct();
    // 0x004967D0: the list emptied (twice, as the original) and its sentinel freed.
    void DestroyBody();
    // 0x00496860 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    CParticleManager* Destroy(unsigned int flags);
    // 0x00496510: a new system (vtable slot 1) made from `params`, given the manager's colour if it takes it, onto
    // the end of the list. Null if there's no memory.
    CParticle* CreateParticle(const CParticleParams* params);
    // 0x00496590: `particle`'s emit script's scripts killed; then the first system of its name released, deleted and
    // taken off the list.
    void DestroyParticle(CParticle* particle);
    // 0x00496640: with any, each rendered - advanced unless the game is paused (0x006FA7D4).
    void RenderParticles();
    // 0x00496680: each updated.
    void UpdateParticles();
    // 0x004966B0: each given `rgba` (SetColourScale), and the manager's colour = `rgba`.
    void SetColour(const float* rgba);
    // 0x00496700: each released and deleted, the list emptied.
    void DestroyAllParticles();
};
static_assert(offsetof(CParticleManager, m_systems) == 0x4 && offsetof(CParticleManager, m_colour) == 0xC &&
                  sizeof(CParticleManager) == 0x1C,
              "layout must match the original");

} // namespace Nx
