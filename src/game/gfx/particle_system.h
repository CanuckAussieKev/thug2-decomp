#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/smtptr.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The engine's flexible particle systems (Nx; the particle manager 0x006FA7C0's - the flexible particle component's
// systems): particles emitted from a box about the emitter in a cone about a direction, each with a speed, a force, a
// size from start to end and a life; their positions kept for a number of history steps (the ribbons' trails); the
// colours from start through mid to end. Each type is a class of its own over the base (newflat, a flat ribbon, glow,
// glowribbontrail, line, ...), with a PC class over that which draws it (pc_flexible_particles.h).
namespace Nx {

// A particle (0x30 bytes).
struct sFlexibleParticle {
    float velocity[3];  // +0x00
    float force[3];     // +0x0C: added to the velocity each second (the system's when it was emitted)
    float startSize[2]; // +0x18: across, up
    float endSize[2];   // +0x20
    float age;          // +0x28
    float lifetime;     // +0x2C
};
static_assert(sizeof(sFlexibleParticle) == 0x30, "layout must match the original");

// The base of every type (0x134 bytes; vtable 0x00649428: 0 the scalar deleting destructor, 1 Render (the engine's
// prints "STUB"), 2 GetNumVertexLists - history steps -, 3 GetNumColours; a list node of the particle manager's at
// +0x0C, its vtable 0x00649424 - slot 0 the destructor through the node).
struct CParticleSystem : Obj::CRefCounted {
    // 0x0041D9C0 (thiscall; flexible_particle.cpp): flag bit `bit` of m_flags set or cleared.
    void SetFlag(int bit, bool on);

    uint32_t unknown08;
    Mem::ListNode m_node;         // +0x0C: in the particle manager's list (data: the system)
    Mth::Vector m_pos;            // +0x20: the emitter
    Mth::Vector m_lastPos;        // +0x30: where it was last update (FillPath emits along the way)
    Mth::Vector m_alignTo;        // +0x40 align_to
    Mth::Vector m_bounds;         // +0x50: the bounding sphere (centre, radius)
    float m_colourScale[4];       // +0x60: the level's colour (with 0x10000 set)
    float m_emitRate;             // +0x70 emit_rate (particles a second)
    float m_emitOwed;             // +0x74: particles owed, a fraction
    uint32_t m_unknown78;         // +0x78 (the component's 0x184197D5: an object's id - the system dies with it)
    int m_maxParticles;           // +0x7C
    int m_lodMinParticles;        // +0x80 setLODParams min_particles
    float m_lodNear;              // +0x84 lod_dist1, squared
    float m_lodFar;               // +0x88 lod_dist2, squared
    int m_unknown8C;              // +0x8C: live particles (EmptySystem: 0)
    float m_speed[2];             // +0x90 setSpeedRange min, max
    float m_emitRange[3];         // +0x98 setEmitRange width, height, depth
    float m_angleSpread;          // +0xA4 setAngleSpread spread
    float m_force[3];             // +0xA8 setForce
    float m_size[4];              // +0xB4 setParticleSize sw, sh, ew, eh
    float m_sizeScale;            // +0xC4 SetParticleSizeScale
    float m_life[2];              // +0xC8 setLife min, max
    float m_emitTarget[3];        // +0xD0 setEmitTarget (a direction)
    Mth::Matrix* mp_matrix;       // +0xDC the matrix it follows (its object's display matrix)
    float m_rotation[3];          // +0xE0 (x, y, z angles: the matrix of GetRotationMatrix; setEmitAngle)
    uint32_t m_unknownEC;         // +0xEC (a float: a model's +0x22C, update_Brightness; the ground's brightness)
    uint32_t m_name;              // +0xF0
    uint32_t m_groupId;           // +0xF4 groupID
    sFlexibleParticle* mp_particles; // +0xF8 (m_maxParticles of them, after their count)
    Script::CScript* mp_emitScript;   // +0xFC emitscript (run each emit... update)
    Script::CScript* mp_updateScript; // +0x100 UpdateScript
    Script::CStruct* mp_scriptParams; // +0x104 their params
    float m_colourMid;            // +0x108 setColor midTime: when the mid colour is, a fraction of a life (below 0: none)
    uint8_t* mp_startColour;      // +0x10C (RGBA; the types with segments: one a segment and three more)
    uint8_t* mp_midColour;        // +0x110
    uint8_t* mp_endColour;        // +0x114
    int m_historyLength;          // +0x118 (the glow ribbon trail's history steps)
    float* mp_positions;          // +0x11C: three floats a particle a history step (step-major)
    uint32_t m_flags;             // +0x120 (bit 0 while the component updates it; 4 the system's set; 0x10 random
                                  // spread (0xDB8621F7), 0x100 setCircularEmit; 0x4000 UseRandomUVQuadrants, 0x8000
                                  // 0x5446AF14, 0x10000 takes the level's colour)
    uint8_t m_priority;           // +0x124 priority
    bool m_dying;                 // +0x125: emits no more, dies once empty
    bool m_emitterIndependent;    // +0x126 setEmitterIndependent: positions in the world, not about the emitter
    bool m_aligned;               // +0x127 align_to given
    bool m_randomDirection;       // +0x128 (0xDB8621F7 random)
    bool m_circularEmit;          // +0x129 setCircularEmit circular
    bool m_wantsTime;             // +0x12A (Update is given the component's time)
    bool m_emitting;              // +0x12B
    bool m_moving;                // +0x12C (setLife: particles get a direction and speed, and move)
    bool m_endColourSet;          // +0x12D (setColor's end colour given: the start no longer copies over it)
    uint8_t unknown12E;
    bool m_unknown12F;            // +0x12F (set while the component updates it, cleared while its object is hidden;
                                  // Hide sets it to its argument)
    bool m_unknown130;            // +0x130 (cleared while its object is hidden; set: updated this frame)
    bool m_fillPath;              // +0x131 FillPath
    bool m_updateBrightness;      // +0x132
    bool m_unknown133;            // +0x133 (cleared by 0xDE45A90D: not culled)

    // 0x00496F10: CRefCounted's constructor, the node (not linked), the vtables, no flags, then Reset. Returns this.
    CParticleSystem* Construct();
    // 0x00496D70: the defaults (no scripts, no matrix, rate 0, the emitter at (2048, 144, -928), speed 1 - 2, emit
    // range (1, 1, 0), spread 0.5, force (0, -0.02, 0), sizes 2, life 2, target (0, 1, 0), white, ...).
    void Reset();
    // 0x00498690: the destructor's body: the scripts, the parameters, the colours, the positions and the particles
    // freed; the particle manager told (0x0049AD60); the node unlinked; CRefCounted's.
    void DestroyBody();
    // 0x00498F80 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    CParticleSystem* Destroy(unsigned int flags);
    // 0x00496F50 (node vtable slot 0, `this` the node): the same for the system.
    CParticleSystem* DestroyFromNode(unsigned int flags);

    // 0x004968B0 / 0x004968E0 / 0x00496910 / 0x00496940: m_emitRange / m_force / m_emitTarget / m_rotation.
    void SetEmitRange(float width, float height, float depth);
    void SetForce(float x, float y, float z);
    void SetEmitTarget(float x, float y, float z);
    void SetRotation(float x, float y, float z);
    // 0x00496970 / 0x004969B0: particle `particle`'s position at history step `step` read / written.
    void GetPosition(int particle, int step, float* x, float* y, float* z);
    void SetPosition(int particle, int step, float x, float y, float z);
    // 0x004969F0: the matrix particles are emitted with: the one it follows, else rows from the emit target (the
    // third row; the first across it and up - or along x when the target is nearly up -, the second their cross).
    void GetEmitMatrix(Mth::Matrix* out);
    // 0x00496B40: `count`, or fewer with LOD (near < far, at most one viewport, a camera): between the distances
    // (squared) the number falls from the maximum to m_lodMinParticles; beyond them m_lodMinParticles.
    int LodParticleCount(int count);
    // 0x00496C30: the bounding sphere: about the emitter, as far as the largest emit range plus the farthest a
    // particle goes in its longest life (the largest force and the top speed).
    void UpdateBounds();
    // 0x00496CE0: with 0x10000 (it takes the level's colour), the colour scale = `rgba`.
    void SetColourScale(const float* rgba);
    // 0x00496F60: the emit script run on; once it ends, deleted.
    void RunEmitScript();
    // 0x00496FA0: when shown and updated this frame (m_unknown12F, m_unknown130), or when `force`: Render (vtable slot
    // 1), and flag 1 and m_unknown12F cleared.
    void RenderIfVisible(bool force);
    // 0x00497790 / 0x00497900: the emit / update script: `script` (on a CScript of its own) with the shared
    // parameters plus `params`, on `object`; 0: the script deleted.
    void SetEmitScript(uint32_t script, Script::CStruct* params, void* object);
    void SetUpdateScript(uint32_t script, Script::CStruct* params, void* object);
    // 0x00497A70: flag 0x10000 and m_wantsTime = `on` (the byte as given).
    void SetWantsTime(uint8_t on);
    // 0x00497AB0: the matrix of m_rotation (InitFromAngles) into `out`. Returns out.
    Mth::Matrix* GetRotationMatrix(Mth::Matrix* out);

    // 0x00496890 / 0x004968A0 (vtable slots 3 and 2; the latter not a Ghidra function): "STUB:
    // plat_get_num_particle_colors" / "STUB: plat_get_num_vertex_lists" printed, 0.
    static int __cdecl GetNumColoursStub();
    static int __cdecl GetNumVertexListsStub();

    // (particle_system_update.cpp)
    // 0x00496FE0: `count` particles emitted (no more than there's room for; none once dying). Moving ones: a
    // direction in a cone about the emit matrix's third row (the spread a fraction of pi; random or circular
    // spreads of the emit box as set), a speed and a life between their limits, the force and sizes (scaled), a
    // position in the emit box (FillPath: somewhere along the way from the last position); else only sizes and a life,
    // at the emitter. Every history step starts at the position (in the world when emitter independent).
    void Emit(int count);
    // 0x00497B00: a member function the flexible particle component passes on: setEmitAngle, setpos,
    // setParticleSize, SetParticleSizeScale, EmitRate, setColor (corner; sr ... ea, mr ... ma, er ... ea; midtime),
    // 0x482482C6 (no particles), setSpeedRange, emit, setAngleSpread, ApplyEnvBrightness (with 0x652F5D69 every
    // update, else now: the ground's brightness under the emitter), setEmitRange, setForce, setEmitTarget (only
    // when not (0, 1, 0)), setLife (the particles move), setEmitterIndependent, 0xDB8621F7 (random), setCircularEmit,
    // setLODParams. True.
    bool CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
    // 0x004987F0: a frame of `time` seconds: dying and empty, or following an object that's gone: deleted. Unless
    // `skip` or not shown: the emit script run, the bounds, emission at the rate (none while out of view with a
    // rate of 0: then the particles only age and expire), the update script; with moving particles and time, the
    // brightness, each particle aged, sped up and moved (its history shifted), the expired and those past the LOD
    // count taken out (the last moved into the gap), the last position kept.
    void Update(float time, bool skip);
    // The brightness of the ground under the emitter (a line from 1 above to 4800 below; `movables` too) into
    // m_unknownEC, doubled; left as it was without a hit.
    void UpdateGroundBrightness(bool movables);
    // Particle `index` taken out: the last moved into its place (each history step's position too), the last's
    // positions set far away (1024, 1024, 1024).
    void RemoveParticle(int index, bool throughAccessors);
};
static_assert(offsetof(CParticleSystem, m_node) == 0x0C && offsetof(CParticleSystem, m_pos) == 0x20 &&
                  offsetof(CParticleSystem, m_alignTo) == 0x40 && offsetof(CParticleSystem, m_bounds) == 0x50 &&
                  offsetof(CParticleSystem, m_emitRate) == 0x70 && offsetof(CParticleSystem, m_unknown78) == 0x78 &&
                  offsetof(CParticleSystem, m_unknown8C) == 0x8C && offsetof(CParticleSystem, m_emitRange) == 0x98 &&
                  offsetof(CParticleSystem, m_force) == 0xA8 && offsetof(CParticleSystem, m_size) == 0xB4 &&
                  offsetof(CParticleSystem, m_life) == 0xC8 && offsetof(CParticleSystem, m_emitTarget) == 0xD0 &&
                  offsetof(CParticleSystem, mp_matrix) == 0xDC && offsetof(CParticleSystem, m_unknownEC) == 0xEC &&
                  offsetof(CParticleSystem, m_groupId) == 0xF4 && offsetof(CParticleSystem, mp_particles) == 0xF8 &&
                  offsetof(CParticleSystem, mp_scriptParams) == 0x104 &&
                  offsetof(CParticleSystem, m_colourMid) == 0x108 &&
                  offsetof(CParticleSystem, mp_positions) == 0x11C && offsetof(CParticleSystem, m_flags) == 0x120 &&
                  offsetof(CParticleSystem, m_wantsTime) == 0x12A && offsetof(CParticleSystem, m_unknown12F) == 0x12F &&
                  offsetof(CParticleSystem, m_unknown133) == 0x133 && sizeof(CParticleSystem) == 0x134,
              "layout must match the original");

// The types' engine classes: constructed with (name, max particles, texture, blend mode, fixed alpha, segments, a
// float, history) - each reads what it uses -, the particles, positions and colours allocated (each step stops at
// the first that fails), the colours grey, no mid colour, flag 4.

// "newflat" (vtables 0x0064943C and 0x00649438): a position and one colour each.
struct CNewFlatParticles : CParticleSystem {
    // 0x00499010. Returns this.
    CNewFlatParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                                 int segments, float unknown, int history);
    // 0x00498FB0: the vtables, the base's body.
    void DestroyBody();
    // 0x00498FE0 (vtable slot 0) / 0x00498FD0 (through the node)
    CNewFlatParticles* Destroy(unsigned int flags);
    CNewFlatParticles* DestroyFromNode(unsigned int flags);
};

// The flat ribbon (0x3D56A7A6; vtables 0x00649450 and 0x0064944C): history + 1 positions each, a colour a segment
// and three more.
struct CFlatRibbonParticles : CParticleSystem {
    int m_segments; // +0x134: history + 1

    // 0x004992C0. Returns this.
    CFlatRibbonParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                    int fixedAlpha, int segments, float unknown, int history);
    // 0x00499200: the vtables, the particles freed (and forgotten), the base's body.
    void DestroyBody();
    // 0x00499290 (vtable slot 0) / 0x004991F0 (through the node)
    CFlatRibbonParticles* Destroy(unsigned int flags);
    CFlatRibbonParticles* DestroyFromNode(unsigned int flags);
    // 0x004991E0 (vtable slot 3): m_segments + 3.
    int GetNumColours();
};

// "glow" (vtables 0x00649464 and 0x00649460): three colours.
struct CGlowParticles : CParticleSystem {
    int m_segments;   // +0x134
    float m_glowWidth; // +0x138

    // 0x004995B0. Returns this.
    CGlowParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                              int segments, float glowWidth, int history);
    // 0x00499550
    void DestroyBody();
    // 0x00499580 (vtable slot 0) / 0x00499570 (through the node)
    CGlowParticles* Destroy(unsigned int flags);
    CGlowParticles* DestroyFromNode(unsigned int flags);
};

// "glowribbontrail" (vtables 0x00649478 and 0x00649474): history + 1 positions each (m_historyLength), colours as the
// flat ribbon.
struct CGlowRibbonTrailParticles : CParticleSystem {
    int m_segments; // +0x134: the glow's
    float m_width;  // +0x138

    // 0x00499880. Returns this.
    CGlowRibbonTrailParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode,
                                         int fixedAlpha, int segments, float width, int history);
    // 0x00499800
    void DestroyBody();
    // 0x00499850 (vtable slot 0) / 0x00499840 (through the node)
    CGlowRibbonTrailParticles* Destroy(unsigned int flags);
    CGlowRibbonTrailParticles* DestroyFromNode(unsigned int flags);
    // 0x00499820 (vtable slot 3): m_historyLength + 3. 0x00499830 (slot 2): m_historyLength.
    int GetNumColours();
    int GetNumVertexLists();
};

// "line" (vtables 0x0064948C and 0x00649488): history + 1 positions each, a colour a step.
struct CLineParticles : CParticleSystem {
    int m_steps; // +0x134: history + 1

    // 0x00499B80. Returns this.
    CLineParticles* Construct(uint32_t name, int maxParticles, uint32_t texture, uint32_t blendMode, int fixedAlpha,
                              int segments, float unknown, int history);
    // 0x00499B20
    void DestroyBody();
    // 0x00499B50 (vtable slot 0) / 0x00499B40 (through the node)
    CLineParticles* Destroy(unsigned int flags);
    CLineParticles* DestroyFromNode(unsigned int flags);
};

// The model type (0x286A8D26; vtables 0x006494A0 and 0x0064949C; constructed by 0x0049A0C0): models as particles.
struct CModelParticles : CParticleSystem {
    unsigned int m_modelCount; // +0x134
    unsigned int m_drawnModels; // +0x138: the models Render goes through (the same count)
    void* mp_unknown13C;       // +0x13C (freed): a rotation (Mth::Quat) a model, turned by its particle's velocity
    uint8_t unknown140[4];
    void** mp_models;          // +0x144: m_modelCount of them (deleted with 0x0048C3C0)

    // 0x0049A0C0 (model_particles.cpp): constructed with (name, max particles, model file name, ...): the base's,
    // `maxParticles` particles, positions and models - each from "<model>.mdl" (lower case) at a random scale 1 - 2.98 -
    // unless the memory left is under 0x1194 bytes a model; a random rotation each (only the first ever normalised, as
    // the original); one colour of each kind (not set); no mid colour, flag 4. Each step stops at the first that fails.
    // Returns this.
    CModelParticles* Construct(uint32_t name, int maxParticles, const char* model, uint32_t blendMode, int fixedAlpha,
                               int segments, float unknown, int history);
    // 0x0049A4C0 (vtable slot 1): with no particles every model hidden; else from the last particle's model on hidden,
    // and each particle's model shown, coloured (its colour through its life), turned (its rotation moved by its
    // velocity and normalised), scaled (its start size less a quarter for each of its index's lowest two bits) and
    // drawn at its position.
    void Render();

    // 0x00499E10: the vtables, +0x13C freed, the models deleted and their array freed (each forgotten), the base's.
    void DestroyBody();
    // 0x00499EE0: the colour `t` of the way from `from` to `to` (RGBA bytes, each channel wrapping as a byte), its red,
    // green and blue then scaled by the colour scale times the brightness (clamped to 0-255; the blue's scale rounded
    // to a float first, as the original) - into *out, RGBA. Returns out.
    uint32_t* LerpColour(uint32_t* out, const uint8_t* from, const uint8_t* to, float t);
    // 0x0049A090 (vtable slot 0) / 0x00499E00 (through the node)
    CModelParticles* Destroy(unsigned int flags);
    CModelParticles* DestroyFromNode(unsigned int flags);
};
static_assert(offsetof(CModelParticles, m_modelCount) == 0x134 && offsetof(CModelParticles, mp_unknown13C) == 0x13C &&
                  offsetof(CModelParticles, mp_models) == 0x144,
              "layout must match the original");

} // namespace Nx
