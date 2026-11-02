#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/sound/sfx.h"
#include <gsl/pointers>
#include <array>
#include "engine/sound/surface_kind.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's looping sound component (SkaterLoopingSound, type 0xB5F1E9D2, vtable 0x0064BF38, 0x110 bytes): the
// wheels' roll on the ground (the terrain's TABLE_WHEELROLL sound), the air sound, and a grind or slide on a rail
// (TABLE_GRIND / TABLE_SLIDE) - one looping sound, its volume and pitch following the skater's speed (the finalize
// physics component gives the fraction of the top speed each frame, and the state, terrain and rail flags). From
// the disassembly of 0x00513860-0x00513FBE (0x00513860 isn't a Ghidra function).
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize, 7 Suspend, 11 the shared 0x0042BB20, 12
// invoke_method_by_name.
namespace objects {

class RiderBoardPhysics;

class RiderRollingAudio final : public ObjectModule {
public:
    // The skater states cur_phase_ holds.
    static constexpr int kStateGround = 0, kStateAir = 1, kStateRail = 4;

    RiderRollingAudio(const RiderRollingAudio&) = delete;
    RiderRollingAudio& operator=(const RiderRollingAudio&) = delete;
    // 0x00513AD0 (registered by register_component_types): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x00513860 (registered with Create): its script member functions (the table at 0x00684A68) registered.
    static void publish_method_table();
    // 0x005138E0: its type; on, the sound to be chosen (changed), in the air, volume_mult 1.
    //
    RiderRollingAudio();
    // 0x00513B30: the sound stopped, then the base's.
    ~RiderRollingAudio() override;
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x00513880 (vtable +0x08): the air sound's pitch fall (45 and 18.33 a second) and volume_mult.
    void configure_from_data(script::Struct* params) override;
    // 0x00513970 (vtable +0x0C): the physics control component found.
    void seal() override;
    // 0x00513F60 (vtable +0x1C): the base's; suspended, the sound stops; resumed, the sound is chosen again.
    void hold_off(bool suspend) override;
    // 0x005139E0 (vtable +0x30): SkaterLoopingSound_TurnOn / _TurnOff (resumed / suspended) - 1; PlayAnim turns it on
    // when the skater (skating, not walking or driving) has animTags with WalkToSkateTransition in its tags - and,
    // like any other name, returns 2.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x00513BB0 (vtable +0x04): off: suspended. Otherwise the sound for the state (chosen again when it changed),
    // stopped when it changes or the skater hasn't moved; its volume from where the skater is and the speed (the air
    // sound's pitch falling while it lasts), played or its volume and pitch updated when they changed enough.
    void tick_frame() override;

    // 0x005138B0: the speed as a fraction of the top speed (at most 1).
    void set_pace_ratio(float fraction);
    // 0x00513AB0: the sound stopped (called by the skater).
    void stop_sound();

    uint32_t sound_id_{};                // +0x18 the playing sound's unique id (0: none)
    uint32_t playing_checksum_{};        // +0x1C
    float lean_pitch_angle_{};           // +0x20 the pitch worked out
    float prev_pitch_value_{};           // +0x24 the pitch last given
    sound::VolumeRef prev_levels_;       // +0x28 the volume last given
    float air_pitch_floor_{};            // +0x54 the air sound's pitch falls down to this (45)
    float air_pitch_fall_{};             // +0x58 by this much a second (18.33)
    RiderBoardPhysics* physics_control_{}; // +0x5C SkaterPhysicsControl (0xD9151D4D)
    uint8_t surface_type_{};               // +0x60 (the finalize physics component's: the state's +0xE1)
    std::array<uint8_t, 3> pad61{};
    maths::Vec4 last_pos_{}; // +0x64 the skater's position last frame
    int cur_phase_{kStateAir}; // +0x74 the skater's state (0 ground, 1 air, 4 rail)
    surface_kind::SoundInfo sound_{}; // +0x78 the terrain sound entry chosen (0x00444B10's; the air one at 0x0067F018)
    // (in sound_: +0xE0 the sound's checksum, +0xE4 / +0xE8 its pitch at full / no speed)
    uint8_t have_sound_{0};    // +0xFC
    uint8_t sound_changed_{1}; // +0xFD choose the sound again
    uint8_t force_update_{0};  // +0xFE (never set here)
    uint8_t no_rail_sound_{0}; // +0xFF (the state's +0x70)
    uint8_t sliding_{0};       // +0x100 on a rail: slide, not grind (the state's +0x58)
    std::array<uint8_t, 3> pad101{};
    float speed_ratio_{}; // +0x104
    float volume_mult_{1.0f}; // +0x108 volume_mult
    uint8_t on_{1};           // +0x10C
    std::array<uint8_t, 3> pad10d_{};
};

// An VolumeRef with its compiler-made assignment, which was placed here.
struct LoopingSoundVolume : sound::VolumeRef {
    // 0x00513990: `other` copied word for word. Returns this.
    LoopingSoundVolume* get_assign(const sound::VolumeRef* other);
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<script::MemberFunctionEntry, 1> g_looping_sound_member_functions;
} // namespace objects
