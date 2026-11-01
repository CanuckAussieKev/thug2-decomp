#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace skater {
class SkaterPhysicsModule;
} // namespace skater
namespace script {
class Script;
class Struct;
} // namespace script

// The skater's vehicle sound component (SkaterVehicleSound, type 0x1859F772, vtable 0x0064C1E4, 0xC8 bytes; made by
// 0x0051B880): while the skater drives (the core physics' +0x2ED), an engine sound whose pitch and volume follow the
// speed through a gearbox (the gears' shift points and pitch ranges from the settings), and a skid sound while the
// brake (the input's +0x40) is held. The settings are the structure named by +0xC0 in the global structure
// 0x3C2DA7F5 (and, in the park editor's levels, sounds from the global 0x38EC37AC); the rates and limits are in the
// global structure 0x38EC37AC.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 Finalize; 11 0x0042BB20 (member function entries by table:
// SetSkaterVehicleSounds / NoSkaterVehicleSounds / StopSkaterVehicleSounds, the table at 0x00684648); 12
// invoke_method_by_name 0x0051ADC0 (shared: "not mine").
namespace objects {

class PadInputModule;

class SkaterVehicleSoundComponent final : public ObjectModule {
public:
    SkaterVehicleSoundComponent(const SkaterVehicleSoundComponent&) = delete;
    SkaterVehicleSoundComponent& operator=(const SkaterVehicleSoundComponent&) = delete;
    static constexpr uint32_t kType = 0x1859F772;
    static constexpr int kMaxGears = 6;

    // A gear (0x10 bytes from +0x44): the speed it shifts up above (0xFAD20901), the speed it shifts down below
    // (0xFCF5C8A2), and the engine's pitch range across it (0x735B5CE4 at its bottom, 0xE59A89A6 at its top).
    struct Gear {
        float up_at_;
        float down_at_;
        float low_rpm_;
        float high_rpm_;
    };

    // 0x0051B880 (registered with the composite object manager): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // 0x0051AD90 (not a Ghidra function; registered with Create): its script member functions (the table at
    // 0x00684648) registered.
    static void publish_method_table();
    // 0x0051ADD0: its type; no sounds, no settings, the settings' name 0x1CA1FF20,
    // silenced.
    SkaterVehicleSoundComponent();
    // 0x0051ADB0: the destructor body (the base's).
    ~SkaterVehicleSoundComponent() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }

    // 0x0051AE60 (vtable slot 2): whether the level is the park editor's (its +0x40 isn't "off"); `params` isn't
    // read.
    void configure_from_data(script::Struct* params) override;
    // 0x0051B8E0 (vtable slot 3): the skater's core physics, sound and input components, then the settings read.
    void seal() override;
    // 0x0051B920 (vtable slot 1): the engine and skid sounds while driving (not silenced, with settings).
    void tick_frame() override;

    // 0x0051B670: the settings read (see the class).
    void read_settings();
    // 0x0051AE80: the engine sound: the speed (in mph) smoothed, the gear shifted up or down (back to the first
    // after a while without driving), and the sound's pitch and volume from where the speed is in the gear's range.
    void tick_engine();
    // 0x0051B3E0: the skid sound: its level follows the speed while braking; silent: stopped.
    void update_skid();
    // 0x0051B3B0: reads a setting (0x71B4B9FE) and the clock, and does nothing with them.
    void update_unused();

    // --- script member functions (the table at 0x00684648) ---
    // 0x0051B7C0 SetSkaterVehicleSounds: the settings named by the unnamed checksum (if any) read again, the engine
    // sound stopped, not silenced. True.
    bool set_skater_vehicle_sounds(script::Struct* params, script::Script* script);
    // 0x0051B820 NoSkaterVehicleSounds: the engine sound stopped, the settings dropped, silenced. True.
    bool no_skater_vehicle_sounds(script::Struct* params, script::Script* script);
    // 0x0051B860 StopSkaterVehicleSounds (not a Ghidra function): the engine sound stopped (its id kept). True.
    bool stop_skater_vehicle_sounds(script::Struct* params, script::Script* script);

    skater::SkaterPhysicsModule* core_physics_{}; // +0x18
    ObjectModule* sound_{nullptr};                // +0x1C
    PadInputModule* input_{};                   // +0x20
    script::Struct* settings_{nullptr};         // +0x24
    bool park_editor_level_{false};             // +0x28
    uint8_t pad29[3]{};
    uint32_t engine_sound_{0};   // +0x2C the playing engine sound's id (0: none)
    uint32_t skid_sound_{0};     // +0x30 the playing skid sound's id
    script::Struct* extra_{};    // +0x34 (0xBF8E0ACE of the settings)
    uint32_t engine_sound_name_{}; // +0x38
    uint32_t skid_sound_name_{};   // +0x3C
    int gear_count_{};             // +0x40
    Gear gear_steps_[kMaxGears]{}; // +0x44
    float speed_{0.0f};            // +0xA4 the smoothed speed (mph)
    int gear_{0};                  // +0xA8
    float rpm_{};                // +0xAC
    int shift_time_{0};          // +0xB0 the clock at the last shift
    uint32_t unknownb4_{0};
    float skid_{0.0f}; // +0xB8 the skid sound's level
    uint32_t unknownbc_{0};
    uint32_t settings_name_{0x1CA1FF20}; // +0xC0
    bool silenced_{true};                // +0xC4
    uint8_t padc5_[3]{};
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern uint32_t g_vehicle_sound_member_functions[24];
} // namespace objects
