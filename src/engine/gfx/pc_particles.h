#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/gfx/particle_sets.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

// The PC particle systems the particle component makes (Nx engine; the particle manager's virtual [1]): camera-facing
// quads emitted in bursts, each burst (up to five at once) a run of particles a period apart along the same path
// from its own random seed - positions, spreads and sizes quadratic in a particle's age, through a start, a mid and an
// end (or a straight line from start to end), colours from start through mid to end.
namespace render {

struct MaterialRef;

// A burst of particles (0x1C bytes).
struct ParticleBurst {
    int count;        // +0x00: particles in it
    float rate;       // +0x04: particles a second (the system's when it began)
    float period;     // +0x08: 1 / rate
    float age;        // +0x0C: of its first particle, seconds
    std::array<uint32_t, 3> seed; // +0x10: the random sequence its particles' spreads come from (rand(), 0x12B9B0A1, 0x0AA2FB3F)

    // 0x004BB040 (Ghidra never made it): the seed moved on past `particles` particles (four numbers each); the random
    // sequence's state (0x00705608, 0x0067E974, 0x0067E978) left there too.
    void get_skip(int particles);
};
static_assert(sizeof(ParticleBurst) == 0x1C, "layout must match the original");

// A quadratic in a particle's age t, per component: a + b t + c t^2.
struct ParticleCurve {
    maths::Vec4 a, b, c;
};

// The PC particle system (0x28C bytes, vtable 0x0064A4F0; the engine's particle system, Sparkle, for its first 0x190
// bytes).
struct PcParticleSystem final : Sparkle {
    // 0x004BCE80 (0x004BCE50 the scalar deleting destructor, vtable slot 0): the engine's.
    ~PcParticleSystem() override = default;

    // 0x004BC250 (vtable slot 1): set up from the parameters: the rate three quarters of theirs, five bursts, its
    // material (blend mode, texture, alpha cut-off; colour half grey), the curves, the identity matrix.
    void initialise() override;
    // 0x004BCE10 (slot 2): the bursts and the material freed.
    void free_resources() override;
    // 0x004BB0C0 (slot 4): a new burst begun when the rate changes (or emitting stops), with `advance` the bursts aged
    // a frame, the newest's count made up to its age, the oldest's particles past the lifetime let go (its seed moved
    // on past them; the burst ended when none are left and none are coming); then, unless culled, every particle drawn
    // as a camera-facing quad through the dynamic vertex buffer (0x1000 a draw at most).
    void draw(bool advance) override;
    // 0x004BC240 (slot 5): the path made again when it moves with the object.
    void update() override;
    // 0x004BB0B0 (slot 7): whether any burst is live.
    bool is_emitting() override;

    // 0x004BBD20 (Ghidra never made it: 0x004BC240's tail): the path from the positions and sizes (through the mid
    // when useMid), less 1.5 times the spread.
    void update_path();

    bool emitting{}; // +0x190
    std::array<uint8_t, 3> unknown191{};
    int capacity{};          // +0x194: bursts (5)
    int live{};              // +0x198
    ParticleBurst* bursts{}; // +0x19C: a ring of `capacity`
    ParticleBurst* newest{}; // +0x1A0
    ParticleBurst* oldest{}; // +0x1A4
    maths::Mat4 matrix{};    // +0x1A8 (the identity)
    std::array<uint8_t, 0x40> unknown1e8_{};
    MaterialRef* material{}; // +0x228: its own
    ParticleCurve spread{};  // +0x22C: the boxes and radii through time
    ParticleCurve path{};    // +0x25C: the positions and sizes through time, less 1.5 times the spread
};
static_assert(offsetof(PcParticleSystem, capacity) == 0x194 && offsetof(PcParticleSystem, matrix) == 0x1A8 &&
                  offsetof(PcParticleSystem, material) == 0x228 && offsetof(PcParticleSystem, spread) == 0x22C &&
                  offsetof(PcParticleSystem, path) == 0x25C && sizeof(PcParticleSystem) == 0x28C,
              "layout must match the original");

// The PC particle manager (0x1C bytes, vtable 0x0064A304; the engine's particle manager with nothing of its own but
// the systems it makes. Its destructor 0x004B2740 - 0x004B2710 the scalar deleting one - is the engine's.)
struct PcParticleManager final : EffectsDirector {
    // 0x004BCE90 (vtable slot 1): a new PC particle system.
    Sparkle* create_system() override;
};

} // namespace render
