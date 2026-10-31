#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace render {
struct EffectsDirector;
struct Sparkle;
} // namespace render

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// The particle component (type 0xA9DB601E "particle", vtable 0x00646340, 0x30 bytes; made by 0x0042A000): an object's
// particle system (the Nx particle manager's, 0x006FA7C4) - made from the object's structure, placed and steered by
// script member functions (the table at 0x00683D30, registered by 0x00429940; the member function pointers filled
// in by the dynamic initialiser 0x006432D0).
//
// Vtable: 0 Destroy, 1 Update (0x0042AD90), 2 configure_from_data (0x0042A090), 3 Finalize, 4 / 9 the empty
// 0x005480A0, 5 / 10 the empty 0x005BAB90, 6 0x0048DCD0, 7 the base's Suspend, 8 Hide, 11 0x0042BB20 (the table
// entry's member function, else slot 12), 12 invoke_method_by_name (0x0042AEE0).
class EffectsEmitterModule : public ObjectModule {
public:
    static constexpr uint32_t kType = 0xA9DB601E;

    // 0x0042A000 (registered with the component types): a new one (memory::alloc_zeroed, the constructor).
    static ObjectModule* __cdecl fabricate();
    // 0x00429940: its member function table registered.
    static void __cdecl publish_method_table();
    // 0x00429B20: the constructor (returns this): the base's, its vtable and type, no particle system, no
    // lifetime, started now (the game clock).
    EffectsEmitterModule* make_new();
    // 0x00429B90: the destructor body: the particle system destroyed by the particle manager, then the base's.
    void destroy_body();
    // 0x0042A060 (vtable slot 0): the scalar deleting destructor.
    EffectsEmitterModule* teardown(unsigned int flags);
    // 0x00429BF0 (slot 3): the object's suspend component kept.
    void seal();
    // 0x0042A090 (slot 2): its particle system made by the particle manager from the parameters (sizes, radii,
    // positions and boxes scaled by 0x13B9DA7B and turned to face 0x0C4C809E with 0x7045843A; colours; a
    // rotation...); the emit target's script spawned on the object; the model to follow (0x5E2A8D9F's object).
    void configure_from_data(script::Struct* params);
    // 0x0042AD90 (slot 1): nothing while suspended (unless a network game runs and it has a lifetime); past its
    // lifetime the object dies; the model's +0x22C to the particle system; with its parameters' +0x152, its three
    // positions (+0xF4, offsets from the object) moved with the object, and with +0x154 turned by its orientation.
    void tick_frame();
    // 0x0042AEE0 (slot 12): 0x2299A9F2 (whether the particle system's vtable slot 7 says so: 1 / 0), 0x96896315 (a
    // feeler from 1 above its position (+0x94 of its parameters) to 4800 below: the vertex value hit doubled, else
    // 1, to the particle system's +0x38) - 1; anything else 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script);
    // 0x00429960 (slot 8): the particle system hidden or shown (its 0x00494E70; none: a crash).
    void conceal(bool hide);

    // --- script member functions (all true; nothing without a particle system) ---
    // 0x00429970 SetEmitRate / 0x004299A0 set_lifespan / 0x004299E0 (0x19AF74D1) / 0x00429A20 change_midpoint_fraction: the
    // unnamed float (required) to the particle system (each but the first then refreshed: 0x00494E20).
    bool set_emit_rate(script::Struct* params, script::Script* script);
    bool set_lifespan(script::Struct* params, script::Script* script);
    bool set_float_19a_f_74d_1(script::Struct* params, script::Script* script);
    bool change_midpoint_fraction(script::Struct* params, script::Script* script);
    // 0x00429A60 / 0x00429AA0 / 0x00429AE0: set_start_radius / set_mid_radius / set_end_radius (the unnamed float).
    bool set_start_radius(script::Struct* params, script::Script* script);
    bool set_mid_radius(script::Struct* params, script::Script* script);
    bool set_end_radius(script::Struct* params, script::Script* script);
    // 0x00429C10 / 0x00429C50 / 0x00429C90: SetStartPos / set_mid_pos / set_end_pos (the unnamed vector, required).
    bool set_start_pos(script::Struct* params, script::Script* script);
    bool set_mid_pos(script::Struct* params, script::Script* script);
    bool set_end_pos(script::Struct* params, script::Script* script);
    // 0x00429CD0 / 0x00429D10 / 0x00429D50 (0xF71D696E, 0x0AAB006D, 0xE2B99038): the unnamed vector as the
    // particle system's start / mid / end box (0x00496160; not refreshed).
    bool set_start_box(script::Struct* params, script::Script* script);
    bool set_mid_box(script::Struct* params, script::Script* script);
    bool set_end_box(script::Struct* params, script::Script* script);
    // 0x00429D90 (0xC036F4D0): the unnamed checksum (required) kept, and given with the object's id (0x004963B0).
    bool set_emit_target(script::Struct* params, script::Script* script);
    // 0x00429DD0 setStartColor / 0x00429E50 setMidColor / 0x00429ED0 setEndColor: the unnamed array's first four
    // integers (their low bytes: r, g, b, a) as that colour.
    bool set_start_color(script::Struct* params, script::Script* script);
    bool set_mid_color(script::Struct* params, script::Script* script);
    bool set_end_color(script::Struct* params, script::Script* script);
    // 0x00429F50 (0x5446AF14): the unnamed integer (default 0) == 1 into the particle system's +0x15B.
    bool set_flag_5446a_f14(script::Struct* params, script::Script* script);
    // 0x00429F90 (0x55835CF2): the unnamed array's colour (as setStartColor) through 0x00496190.
    bool set_color_55835c_f2(script::Struct* params, script::Script* script);

    // 0x0042B040 (GetParticleParams): the script's parameters get 0xB91EE381, a structure of the particle system's
    // name, two floats, its three colours (arrays of four integers), positions, sizes and boxes.
    bool get_particle_params(script::Struct* params, script::Script* script);

    uint32_t emit_target_;           // +0x18 (0xC036F4D0's checksum)
    render::Sparkle* effect_handle_; // +0x1C its particle system
    ObjectModule* suspend_;          // +0x20 the object's suspend component
    ObjectModule* mesh_instance_;    // +0x24 the model it follows (configure_from_data)
    int start_time_;                 // +0x28 the game clock when made
    uint32_t lifespan_;              // +0x2C ms (0: for ever)
};
static_assert(offsetof(EffectsEmitterModule, emit_target_) == 0x18 &&
                  offsetof(EffectsEmitterModule, lifespan_) == 0x2C && sizeof(EffectsEmitterModule) == 0x30,
              "layout must match the original (0x0042A000 allocates 0x30 bytes)");

extern render::EffectsDirector* g_particle_hub;

} // namespace objects
