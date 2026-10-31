#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace script {
class Struct;
} // namespace script

// The VehicleSound component (GameObjects::EngineAudioModule, type 0x28C97F3C, vtable 0x006467F4, 0xBC bytes; made by
// 0x0043CFC0): a driveable car's sounds - the engine, whose pitch and volume follow the slowest driven wheel's speed
// through a gearbox, the tyres' squeal while braking and sliding, and a crash sound for hard collisions (with an
// event to the skater). The same engine model as the skater's own vehicle sound component (game/rider/
// vehicle_sound.h). The settings are the structure named by the Vehicle component's sounds (sounds_name_) in the
// global structure PlayerVehicleSounds (0x3C2DA7F5); rates and limits in PlayerVehicleSoundGlobalParameters
// (0x38EC37AC, with the park editor's own sounds).
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize; 0x005480A0, 0x004E3F90, 0x0048DCD0, the base's
// Suspend, 0x005BAB90, 0x005480A0, 10 fetch_diagnostics, 11 0x0045D720, 12 invoke_method_by_name 0x0051ADC0 (not mine).
namespace objects {

class DrivableModule;

class EngineAudioModule final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x28C97F3C;
    static constexpr int kMaxGears = 6;

    // A gear (0x10 bytes from +0x38): the speed it shifts up above, the speed it shifts down below, and the engine's
    // pitch range across it (as the skater's: game/rider/vehicle_sound.h).
    struct Gear {
        float up_at_;
        float down_at_;
        float low_rpm_;
        float high_rpm_;
    };

    // 0x0043CFC0 (registered with the composite object manager): a new one (0 if out of memory).
    static ObjectModule* __cdecl fabricate();
    // 0x0043C590: its type; no components, settings or sounds.
    EngineAudioModule();
    // 0x0043C5F0: the destructor body: both sounds stopped, then the base's.
    ~EngineAudioModule() override;
    // 0x0043C670 (vtable slot 2): whether the level is the park editor's (its +0x40 isn't "off"); `params` isn't
    // read.
    void configure_from_data(script::Struct* params) override;
    // 0x0043D050 (vtable slot 3): the object's Vehicle and Sound components, then the settings read.
    void seal() override;
    // 0x0043D290 (vtable slot 1): with settings, the engine, the tyres and the crash sound.
    void tick_frame() override;
    // 0x0043C540 (vtable slot 10): the sounds' names (engine_sound 0x2A19E830, tire_sound 0xE798FE80) and the crash
    // sound's structure (0x36FA68D2) added to `info` (then the base's, empty: 0x005BAB90).
    void fetch_diagnostics(script::Struct* info) override;

    // 0x0043CE70: the settings: the vehicle's sounds' structure in PlayerVehicleSounds, its EngineSound, TireSound,
    // Gears (each: 0xFAD20901 up, 0xFCF5C8A2 down, 0x735B5CE4 / 0xE59A89A6 the pitch range) and CollideSound; in the
    // park editor's level the CAG ones from PlayerVehicleSoundGlobalParameters instead.
    void read_settings();
    // 0x0043C690: the engine: the slowest driven wheel's speed (in mph, |spin| * radius), lowered by 0xEC67BC97
    // unless driving (throttle or reverse, no brake); eased towards by 0x02A7095B, each step limited by 0xCA5EFD91 /
    // 0x77C8E7C8 (as the original compares them, only when it points the wrong way); the gear shifted at the gear's
    // points while a wheel touches, back to the first after 0xC0F451D4 seconds in the air; just after a shift the
    // engine is quieter (0xBFA32092 of full, back to full over 0xFB6BD20E ms); the engine's speed between the gear's
    // pitch range, eased by 0x3DD9C040; its pitch and volume from where it is between the settings' limits.
    void tick_engine();
    // 0x0043CBC0: the tyres: while braking or with the handbrake, the fastest slip of a sliding wheel (dynamic, or
    // handbrake past its max_static_grip_velocity); eased by tire_slip_lerp_rate; the level its fraction of
    // FullTireSlipVelocity. A silent volume stops the sound.
    void update_skid();
    // 0x0043D090: the crash sound: once collision_mute_delay has passed since the last, a collision stronger than
    // collision_cutoff_factor (the vehicle's strongest impulse over FullCollision) plays CollideSound at that
    // percentage (Sound component's 0x00433240), and the skater gets event 0xC4A60423 with its Strength (0-1).
    void update_collide_sound();

    script::Struct* settings_; // +0x18
    bool park_editor_level_;   // +0x1C
    uint8_t pad1d_[3];
    uint32_t engine_sound_;         // +0x20 the playing engine sound's id (0: none)
    uint32_t skid_sound_;           // +0x24 the playing tyre sound's id
    script::Struct* collide_sound_; // +0x28 CollideSound
    uint32_t engine_sound_name_;    // +0x2C EngineSound
    uint32_t skid_sound_name_;      // +0x30 TireSound
    int gear_count_;                // +0x34
    Gear gear_steps_[kMaxGears];    // +0x38
    float speed_;                   // +0x98 the smoothed speed (mph)
    int gear_;                      // +0x9C
    float rpm_;                     // +0xA0
    int shift_time_;                // +0xA4 the clock at the last shift
    uint32_t unknowna8_;
    float skid_;                  // +0xAC the tyre sound's level
    int collide_time_;            // +0xB0 the clock at the last crash sound
    DrivableModule* vehicle_ptr_; // +0xB4
    ObjectModule* sound_ptr_;     // +0xB8
};
static_assert(offsetof(EngineAudioModule, engine_sound_) == 0x20 && offsetof(EngineAudioModule, gear_count_) == 0x34 &&
                  offsetof(EngineAudioModule, gear_steps_) == 0x38 && offsetof(EngineAudioModule, speed_) == 0x98 &&
                  offsetof(EngineAudioModule, skid_) == 0xAC && offsetof(EngineAudioModule, vehicle_ptr_) == 0xB4 &&
                  sizeof(EngineAudioModule) == 0xBC,
              "layout must match the original");

} // namespace objects
