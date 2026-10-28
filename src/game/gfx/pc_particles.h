#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"

// The PC particle systems the particle component makes (Nx engine; the particle manager's virtual [1]): camera-facing
// quads emitted in bursts, each burst (up to five at once) a run of particles a period apart along the same path
// from its own random seed - positions, spreads and sizes quadratic in a particle's age, through a start, a mid and an
// end (or a straight line from start to end), colours from start through mid to end.
namespace Nx {

// A burst of particles (0x1C bytes).
struct sParticleBurst {
    int count;      // +0x00: particles in it
    float rate;     // +0x04: particles a second (the system's when it began)
    float period;   // +0x08: 1 / rate
    float age;      // +0x0C: of its first particle, seconds
    uint32_t seed[3]; // +0x10: the random sequence its particles' spreads come from (rand(), 0x12B9B0A1, 0x0AA2FB3F)

    // 0x004BB040 (Ghidra never made it): the seed moved on past `particles` particles (four numbers each); the random
    // sequence's state (0x00705608, 0x0067E974, 0x0067E978) left there too.
    void Skip(int particles);
};
static_assert(sizeof(sParticleBurst) == 0x1C, "layout must match the original");

// A quadratic in a particle's age t, per component: a + b t + c t^2.
struct sParticleCurve {
    Mth::Vector a, b, c;
};

// The PC particle system (0x28C bytes, vtable 0x0064A4F0; the engine's particle system - constructor 0x00496360,
// vtable 0x00649380, destructor body 0x00494DF0 - for its first 0x190 bytes: +0x04 the parameters it was made from,
// particle_component.cpp's ParticleParams).
struct CPcParticleSystem {
    void** vtable;
    uint8_t params04[0xC];
    float sizes[3];          // +0x10: start, mid, end
    float radii[3];          // +0x1C: start, mid, end (the spread's fourth component)
    uint8_t params28[4];
    float rate;              // +0x2C: particles a second (0: none)
    float lifetime;          // +0x30: seconds
    float midPercent;        // +0x34: when the mid is, a percentage of the lifetime
    uint8_t params38[0x60];
    Mth::Vector positions[3]; // +0x098: start, mid, end
    Mth::Vector boxes[3];     // +0x0C8: the spread, start, mid, end
    uint8_t params0F8[0x30];
    float colourMidPercent;  // +0x128: when the mid colour is, a percentage of the lifetime
    uint8_t colours[3][4];   // +0x12C: start, mid, end (RGBA)
    uint32_t blendMode;      // +0x138: a checksum
    int alphaCutOff;         // +0x13C: of 128
    uint8_t params140[4];
    uint32_t texture;        // +0x144: a checksum (the skater's texture dictionary)
    uint8_t params148[0xD];
    bool useMid;             // +0x155: the paths go through the mid
    bool localPositions;     // +0x156: they move with the object (the path made again each update; never culled)
    bool colourUseMid;       // +0x157: the colours go through the mid colour
    uint8_t params158[3];
    bool cullOutside;        // +0x15B: culled by 0x004DB570 rather than 0x004DB790
    uint8_t engine15C[0x24];
    Mth::Vector bounds;      // +0x180: the bounding sphere (centre, radius)
    bool emitting;           // +0x190
    uint8_t unknown191[3];
    int capacity;            // +0x194: bursts (5)
    int live;                // +0x198
    sParticleBurst* bursts;  // +0x19C: a ring of `capacity`
    sParticleBurst* newest;  // +0x1A0
    sParticleBurst* oldest;  // +0x1A4
    Mth::Matrix matrix;      // +0x1A8 (the identity)
    uint8_t unknown1E8[0x40];
    uint8_t* material;       // +0x228: its own (0x110 bytes)
    sParticleCurve spread;   // +0x22C: the boxes and radii through time
    sParticleCurve path;     // +0x25C: the positions and sizes through time, less 1.5 times the spread

    // 0x004BCE90 (the PC particle manager's virtual [1]; Ghidra's thiscall on the manager, which it doesn't use):
    // a new one (0x28C bytes).
    static CPcParticleSystem* __fastcall Create(void* manager);
    // 0x004BCE80: the destructor's body (a jump to the engine's 0x00494DF0).
    void DestroyBody();
    // 0x004BCE50 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcParticleSystem* Destroy(unsigned int flags);
    // 0x004BC250 (virtual [1]): set up from the parameters: the rate three quarters of theirs, five bursts, its
    // material (blend mode, texture, alpha cut-off; colour half grey), the curves, the identity matrix.
    void Initialise();
    // 0x004BCE10 (virtual [2]): the bursts and the material freed.
    void Release();
    // 0x004BB0C0 (virtual [4]): a new burst begun when the rate changes (or emitting stops), with `advance` the
    // bursts aged a frame, the newest's count made up to its age, the oldest's particles past the lifetime let go
    // (its seed moved on past them; the burst ended when none are left and none are coming); then, unless culled,
    // every particle drawn as a camera-facing quad through the dynamic vertex buffer (0x1000 a draw at most).
    void Render(bool advance);
    // 0x004BC240 (virtual [5]): the path made again when it moves with the object.
    void Update();
    // 0x004BBD20 (Ghidra never made it: 0x004BC240's tail): the path from the positions and sizes (through the mid
    // when useMid), less 1.5 times the spread.
    void UpdatePath();
    // 0x004BB0B0 (virtual [7]): whether any burst is live.
    bool IsEmitting();

    // The engine's particle system's own members (engine_particles.cpp; its first 0x190 bytes - +0x38 a value the
    // particle component sets, +0x4C the id of scripts spawned for it, +0x159 hidden, +0x15A held back):
    // 0x00494DF0: the destructor's body (its vtable). 0x00495230 (the engine's virtual [0]): destroyed, and freed with
    // flags & 1. Returns this.
    void EngineDestroyBody();
    CPcParticleSystem* EngineDestroy(unsigned int flags);
    // 0x00494E00 / 0x00494E20: drawn (virtual [4]) / updated (virtual [5]) unless hidden or held back.
    void EngineRender(bool advance);
    void EngineUpdate();
    // 0x00494E40: its spawned scripts killed (0x00474040), then released (virtual [2]).
    void EngineRelease();
    // 0x00494E70 (not a Ghidra function): hidden or shown - the platform told (virtual [3]) when it changes.
    void Hide(bool hide);
    // 0x00494E90 / 0x00494EA0: the rate and the lifetime set.
    void SetRate(float rate);
    void SetLifetime(float lifetime);
    // 0x00494EB0: size `which` (start, mid, end) set.
    void SetSize(int which, float size);
    // 0x00494EC0 / 0x00494EE0 / 0x00494EF0: colour `which`, +0x38 and the colour mid percentage set - then the
    // platform told (virtual [6]).
    void SetColour(int which, const uint32_t* colour);
    void SetValue38(float value);
    void SetColourMidPercent(float percent);
};
static_assert(offsetof(CPcParticleSystem, sizes) == 0x10 && offsetof(CPcParticleSystem, rate) == 0x2C &&
                  offsetof(CPcParticleSystem, positions) == 0x98 && offsetof(CPcParticleSystem, boxes) == 0xC8 &&
                  offsetof(CPcParticleSystem, colourMidPercent) == 0x128 &&
                  offsetof(CPcParticleSystem, blendMode) == 0x138 && offsetof(CPcParticleSystem, texture) == 0x144 &&
                  offsetof(CPcParticleSystem, useMid) == 0x155 && offsetof(CPcParticleSystem, cullOutside) == 0x15B &&
                  offsetof(CPcParticleSystem, bounds) == 0x180 && offsetof(CPcParticleSystem, capacity) == 0x194 &&
                  offsetof(CPcParticleSystem, matrix) == 0x1A8 && offsetof(CPcParticleSystem, material) == 0x228 &&
                  offsetof(CPcParticleSystem, spread) == 0x22C && offsetof(CPcParticleSystem, path) == 0x25C &&
                  sizeof(CPcParticleSystem) == 0x28C,
              "layout must match the original");

} // namespace Nx
