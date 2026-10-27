#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's physics control component (SkaterPhysicsControl, type 0xD9151D4D, vtable 0x0064BFE0, 0x30 bytes):
// which physics runs the skater - skating (the core physics, rotate, adjust and finalize physics components) or
// walking (the walk component) - the switches between them (and their cameras), pausing the physics, and the board
// missing / force walk flags. From the disassembly of 0x005176A0-0x00517CB1 (0x005176A0, 0x005176E0-0x00517730,
// 0x005177A0, 0x005177F0, 0x00517A80 and 0x00517AA0 aren't Ghidra functions).
//
// Vtable: 0 Destroy, 1 Update, 2 InitFromStructure, 3 Finalize, 11 the shared 0x0042BB20, 12 CallMemberFunction.
namespace Obj {

class CSkaterPhysicsControlComponent : public CBaseComponent {
public:
    // 0x00517830 (registered by RegisterComponentTypes): a new one (0 if out of memory), no core physics yet.
    static CBaseComponent* __cdecl Create();
    // 0x005176A0 (registered with Create): its script member functions (the table at 0x006846F0) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x005176C0: the destructor body (the base's).
    void DestroyBody();
    // 0x00517740 (vtable slot 0)
    CSkaterPhysicsControlComponent* Destroy(unsigned int flags);
    // 0x005176D0 (vtable +0x08): not paused. `params` isn't read.
    void InitFromStructure(Script::CStruct* params);
    // 0x00517770 (vtable +0x0C): the core physics and state components found; the state's physics mode (+0x1C) -1.
    void Finalize();
    // 0x00517AC0 (vtable +0x04): while paused the display matrix follows the physics' matrix (skating) or the object's
    // (walking), and the physics is paused again if something resumed it.
    void Update();
    // 0x00517C80 (vtable +0x30): SkaterPhysicsControl_SwitchWalkingToSkating / _SwitchSkatingToWalking - 1; 2 for
    // any other name.
    int CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script);

    // 0x005178B0: the physics paused or not: the core physics, rotate, adjust and finalize physics components
    // (skating) or the walk component (walking) suspended or resumed - not resumed while the object itself is
    // suspended.
    void SetPaused(bool paused);
    // 0x00517940: to walking (nothing if walking): the time switched noted; the skating components and the skater
    // camera suspended (the core physics told first - 0x004FDD50), the state walking (0x00519680), the walk
    // component resumed (told whether the skater was on the ground) and the walk camera, the camera's state handed
    // over; paused again if it was.
    void SwitchSkatingToWalking();
    // 0x00517B20: to skating (nothing if skating): the same the other way (the core physics given the walk's
    // ground state and the ladder or bar it's on - 0x0050C670).
    void SwitchWalkingToSkating();

    // --- script member functions (the table at 0x006846F0; params, script; all true) ---
    // 0x004FC420 (with the skater's core physics): walking - the state's physics mode 1 and not driving.
    bool IsWalking();
    bool PausePhysics(Script::CStruct* params, Script::CScript* script);                     // 0x00517A80
    bool UnPausePhysics(Script::CStruct* params, Script::CScript* script);                   // 0x00517AA0
    bool GetTimeSincePhysicsSwitch(Script::CStruct* params, Script::CScript* script);        // 0x005177A0
    bool GetPreviousPhysicsStateDuration(Script::CStruct* params, Script::CScript* script);  // 0x005177F0
    bool SetBoardMissing(Script::CStruct* params, Script::CScript* script);                  // 0x005176E0
    bool UnsetBoardMissing(Script::CStruct* params, Script::CScript* script);                // 0x005176F0
    bool IsBoardMissing(Script::CStruct* params, Script::CScript* script);                   // 0x00517700
    bool SetForceWalk(Script::CStruct* params, Script::CScript* script);                     // 0x00517710
    bool UnSetForceWalk(Script::CStruct* params, Script::CScript* script);                   // 0x00517720
    bool IsForceWalkOn(Script::CStruct* params, Script::CScript* script);                    // 0x00517730

    bool m_paused;            // +0x18
    uint8_t pad19[3];
    int m_switchTime;         // +0x1C the game clock when the physics last switched
    int m_previousDuration;   // +0x20 how long the physics before it ran (ms)
    bool m_boardMissing;      // +0x24
    bool m_forceWalk;         // +0x25
    uint8_t m_flag26;         // +0x26 (cleared each update)
    uint8_t pad27;
    uint8_t* m_corePhysics;   // +0x28 SkaterCorePhysics (0x5BD63B29)
    uint8_t* m_state;         // +0x2C SkaterState (0x43686585)
};
static_assert(offsetof(CSkaterPhysicsControlComponent, m_paused) == 0x18 &&
                  offsetof(CSkaterPhysicsControlComponent, m_switchTime) == 0x1C &&
                  offsetof(CSkaterPhysicsControlComponent, m_boardMissing) == 0x24 &&
                  offsetof(CSkaterPhysicsControlComponent, m_corePhysics) == 0x28 &&
                  offsetof(CSkaterPhysicsControlComponent, m_state) == 0x2C &&
                  sizeof(CSkaterPhysicsControlComponent) == 0x30,
              "layout must match the original (0x00517830 allocates 0x30 bytes)");

} // namespace Obj
