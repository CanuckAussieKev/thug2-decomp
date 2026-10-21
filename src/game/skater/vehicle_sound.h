#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's vehicle sound component (SkaterVehicleSound, type 0x1859F772, vtable 0x0064C1E4, 0xC8 bytes; made by
// 0x0051B880): while the skater drives (the core physics' +0x2ED), an engine sound whose pitch and volume follow the
// speed through a gearbox (the gears' shift points and pitch ranges from the settings), and a skid sound while the
// brake (the input's +0x40) is held. The settings are the structure named by +0xC0 in the global structure
// 0x3C2DA7F5 (and, in the park editor's levels, sounds from the global 0x38EC37AC); the rates and limits are in the
// global structure 0x38EC37AC.
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 3 Finalize; 11 0x0042BB20 (member function entries by table:
// SetSkaterVehicleSounds / NoSkaterVehicleSounds / StopSkaterVehicleSounds, the table at 0x00684648); 12
// CallMemberFunction 0x0051ADC0 (shared: "not mine").
namespace Obj {

class CSkaterVehicleSoundComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x1859F772;
    static constexpr int kMaxGears = 6;

    // A gear (0x10 bytes from +0x44): the speed it shifts up above (0xFAD20901), the speed it shifts down below
    // (0xFCF5C8A2), and the engine's pitch range across it (0x735B5CE4 at its bottom, 0xE59A89A6 at its top).
    struct Gear {
        float upAt;
        float downAt;
        float lowRpm;
        float highRpm;
    };

    // 0x0051B880 (registered with the composite object manager): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0051AD90 (not a Ghidra function; registered with Create): its script member functions (the table at
    // 0x00684648) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x0051ADD0: the base's constructor, its vtable and type; no sounds, no settings, the settings' name 0x1CA1FF20,
    // silenced. Returns this.
    CSkaterVehicleSoundComponent* Construct();
    // 0x0051ADB0: the destructor body (the base's).
    void DestroyBody();
    // 0x0051AE30 (vtable slot 0)
    CSkaterVehicleSoundComponent* Destroy(unsigned int flags);

    // 0x0051AE60 (vtable slot 2): whether the level is the park editor's (its +0x40 isn't "off"); `params` isn't
    // read.
    void InitFromStructure(Script::CStruct* params);
    // 0x0051B8E0 (vtable slot 3): the skater's core physics, sound and input components, then the settings read.
    void Finalize();
    // 0x0051B920 (vtable slot 1): the engine and skid sounds while driving (not silenced, with settings).
    void Update();

    // 0x0051B670: the settings read (see the class).
    void ReadSettings();
    // 0x0051AE80: the engine sound: the speed (in mph) smoothed, the gear shifted up or down (back to the first
    // after a while without driving), and the sound's pitch and volume from where the speed is in the gear's range.
    void UpdateEngine();
    // 0x0051B3E0: the skid sound: its level follows the speed while braking; silent: stopped.
    void UpdateSkid();
    // 0x0051B3B0: reads a setting (0x71B4B9FE) and the clock, and does nothing with them.
    void UpdateUnused();

    // --- script member functions (the table at 0x00684648) ---
    // 0x0051B7C0 SetSkaterVehicleSounds: the settings named by the unnamed checksum (if any) read again, the engine
    // sound stopped, not silenced. True.
    bool SetSkaterVehicleSounds(Script::CStruct* params, Script::CScript* script);
    // 0x0051B820 NoSkaterVehicleSounds: the engine sound stopped, the settings dropped, silenced. True.
    bool NoSkaterVehicleSounds(Script::CStruct* params, Script::CScript* script);
    // 0x0051B860 StopSkaterVehicleSounds (not a Ghidra function): the engine sound stopped (its id kept). True.
    bool StopSkaterVehicleSounds(Script::CStruct* params, Script::CScript* script);

    CBaseComponent* m_corePhysics; // +0x18
    CBaseComponent* m_sound;       // +0x1C
    uint8_t* m_input;              // +0x20 (+0x40 the brake held; +0x1C0 a flag)
    Script::CStruct* m_settings;   // +0x24
    bool m_parkEditorLevel;        // +0x28
    uint8_t pad29[3];
    uint32_t m_engineSound;        // +0x2C the playing engine sound's id (0: none)
    uint32_t m_skidSound;          // +0x30 the playing skid sound's id
    Script::CStruct* m_extra;      // +0x34 (0xBF8E0ACE of the settings)
    uint32_t m_engineSoundName;    // +0x38
    uint32_t m_skidSoundName;      // +0x3C
    int m_gearCount;               // +0x40
    Gear m_gears[kMaxGears];       // +0x44
    float m_speed;                 // +0xA4 the smoothed speed (mph)
    int m_gear;                    // +0xA8
    float m_rpm;                   // +0xAC
    int m_shiftTime;               // +0xB0 the clock at the last shift
    uint32_t m_unknownB4;
    float m_skid;                  // +0xB8 the skid sound's level
    uint32_t m_unknownBC;
    uint32_t m_settingsName;       // +0xC0
    bool m_silenced;               // +0xC4
    uint8_t padC5[3];
};
static_assert(offsetof(CSkaterVehicleSoundComponent, m_settings) == 0x24 &&
                  offsetof(CSkaterVehicleSoundComponent, m_engineSound) == 0x2C &&
                  offsetof(CSkaterVehicleSoundComponent, m_gearCount) == 0x40 &&
                  offsetof(CSkaterVehicleSoundComponent, m_gears) == 0x44 &&
                  offsetof(CSkaterVehicleSoundComponent, m_speed) == 0xA4 &&
                  offsetof(CSkaterVehicleSoundComponent, m_skid) == 0xB8 &&
                  offsetof(CSkaterVehicleSoundComponent, m_settingsName) == 0xC0 &&
                  sizeof(CSkaterVehicleSoundComponent) == 0xC8,
              "layout must match the original");

// The components with no member functions of their own by name (the state history, the vehicle sound, the stats
// manager, the matrix, collision and avoid components...).
struct ComponentWithoutMemberFunctions {
    // 0x0051ADC0 (vtable +0x30): 2 - not mine.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);
};

} // namespace Obj
