#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CStruct;
} // namespace Script

// The VehicleSound component (Obj::CVehicleSoundComponent, type 0x28C97F3C, vtable 0x006467F4, 0xBC bytes; made by
// 0x0043CFC0): a driveable car's sounds - the engine, whose pitch and volume follow the slowest driven wheel's speed
// through a gearbox, the tyres' squeal while braking and sliding, and a crash sound for hard collisions (with an
// event to the skater). The same engine model as the skater's own vehicle sound component (game/skater/
// vehicle_sound.h). The settings are the structure named by the Vehicle component's sounds (m_soundsName) in the
// global structure PlayerVehicleSounds (0x3C2DA7F5); rates and limits in PlayerVehicleSoundGlobalParameters
// (0x38EC37AC, with the park editor's own sounds).
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 3 Finalize; 0x005480A0, 0x004E3F90, 0x0048DCD0, the base's
// Suspend, 0x005BAB90, 0x005480A0, 10 GetDebugInfo, 11 0x0045D720, 12 CallMemberFunction 0x0051ADC0 (not mine).
namespace Obj {

class CVehicleComponent;

class CVehicleSoundComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x28C97F3C;
    static constexpr int kMaxGears = 6;

    // A gear (0x10 bytes from +0x38): the speed it shifts up above, the speed it shifts down below, and the engine's
    // pitch range across it (as the skater's: game/skater/vehicle_sound.h).
    struct Gear {
        float upAt;
        float downAt;
        float lowRpm;
        float highRpm;
    };

    // 0x0043CFC0 (registered with the composite object manager): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0043C590: the base's constructor, its vtable and type; no components, settings or sounds. Returns this.
    CVehicleSoundComponent* Construct();
    // 0x0043C5F0: the destructor body: both sounds stopped, then the base's.
    void DestroyBody();
    // 0x0043D020 (vtable slot 0)
    CVehicleSoundComponent* Destroy(unsigned int flags);
    // 0x0043C670 (vtable slot 2): whether the level is the park editor's (its +0x40 isn't "off"); `params` isn't
    // read.
    void InitFromStructure(Script::CStruct* params);
    // 0x0043D050 (vtable slot 3): the object's Vehicle and Sound components, then the settings read.
    void Finalize();
    // 0x0043D290 (vtable slot 1): with settings, the engine, the tyres and the crash sound.
    void Update();
    // 0x0043C540 (vtable slot 10): the sounds' names (engine_sound 0x2A19E830, tire_sound 0xE798FE80) and the crash
    // sound's structure (0x36FA68D2) added to `info` (then the base's, empty: 0x005BAB90).
    void GetDebugInfo(Script::CStruct* info);

    // 0x0043CE70: the settings: the vehicle's sounds' structure in PlayerVehicleSounds, its EngineSound, TireSound,
    // Gears (each: 0xFAD20901 up, 0xFCF5C8A2 down, 0x735B5CE4 / 0xE59A89A6 the pitch range) and CollideSound; in the
    // park editor's level the CAG ones from PlayerVehicleSoundGlobalParameters instead.
    void ReadSettings();
    // 0x0043C690: the engine: the slowest driven wheel's speed (in mph, |spin| * radius), lowered by 0xEC67BC97
    // unless driving (throttle or reverse, no brake); eased towards by 0x02A7095B, each step limited by 0xCA5EFD91 /
    // 0x77C8E7C8 (as the original compares them, only when it points the wrong way); the gear shifted at the gear's
    // points while a wheel touches, back to the first after 0xC0F451D4 seconds in the air; just after a shift the
    // engine is quieter (0xBFA32092 of full, back to full over 0xFB6BD20E ms); the engine's speed between the gear's
    // pitch range, eased by 0x3DD9C040; its pitch and volume from where it is between the settings' limits.
    void UpdateEngine();
    // 0x0043CBC0: the tyres: while braking or with the handbrake, the fastest slip of a sliding wheel (dynamic, or
    // handbrake past its max_static_grip_velocity); eased by tire_slip_lerp_rate; the level its fraction of
    // FullTireSlipVelocity. A silent volume stops the sound.
    void UpdateSkid();
    // 0x0043D090: the crash sound: once collision_mute_delay has passed since the last, a collision stronger than
    // collision_cutoff_factor (the vehicle's strongest impulse over FullCollision) plays CollideSound at that
    // percentage (Sound component's 0x00433240), and the skater gets event 0xC4A60423 with its Strength (0-1).
    void UpdateCollideSound();

    Script::CStruct* m_settings;   // +0x18
    bool m_parkEditorLevel;        // +0x1C
    uint8_t pad1D[3];
    uint32_t m_engineSound;        // +0x20 the playing engine sound's id (0: none)
    uint32_t m_skidSound;          // +0x24 the playing tyre sound's id
    Script::CStruct* m_collideSound; // +0x28 CollideSound
    uint32_t m_engineSoundName;    // +0x2C EngineSound
    uint32_t m_skidSoundName;      // +0x30 TireSound
    int m_gearCount;               // +0x34
    Gear m_gears[kMaxGears];       // +0x38
    float m_speed;                 // +0x98 the smoothed speed (mph)
    int m_gear;                    // +0x9C
    float m_rpm;                   // +0xA0
    int m_shiftTime;               // +0xA4 the clock at the last shift
    uint32_t m_unknownA8;
    float m_skid;                  // +0xAC the tyre sound's level
    int m_collideTime;             // +0xB0 the clock at the last crash sound
    CVehicleComponent* mp_vehicle; // +0xB4
    CBaseComponent* mp_sound;      // +0xB8
};
static_assert(offsetof(CVehicleSoundComponent, m_engineSound) == 0x20 &&
                  offsetof(CVehicleSoundComponent, m_gearCount) == 0x34 &&
                  offsetof(CVehicleSoundComponent, m_gears) == 0x38 &&
                  offsetof(CVehicleSoundComponent, m_speed) == 0x98 && offsetof(CVehicleSoundComponent, m_skid) == 0xAC &&
                  offsetof(CVehicleSoundComponent, mp_vehicle) == 0xB4 && sizeof(CVehicleSoundComponent) == 0xBC,
              "layout must match the original");

} // namespace Obj
