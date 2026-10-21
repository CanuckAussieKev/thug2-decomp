#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The skater's state component (SkaterState, type 0x43686585, vtable 0x0064C140, 0x128 bytes): where the skater is
// (on the ground, in the air, on a wall / lip / rail, in a wallplant), whether it skates, walks or drives, its
// terrain, the powerups it picked up and where its sparks come from - read by the physics components and the scripts
// (OnGround ... GetSparksPos), each change of state, physics or terrain sent to the skater as an event. Most of its
// fields are written by the core physics code (not rewritten yet). From the disassembly of 0x00519430-0x005197B4
// (0x00519430-0x00519600, 0x00519650 and 0x005196D0-0x00519720 aren't Ghidra functions). Also here: three
// functions of the skater's state history (0x005197C0-0x005197EF).
//
// Vtable: 0 Destroy, 1 the shared empty 0x0051B970, 2 InitFromStructure, 11 the shared 0x0042BB20, 12 the shared
// CallMemberFunction 0x0051ADC0.
namespace Obj {

class CSkaterStateComponent : public CBaseComponent {
public:
    // The skater's state (+0x18).
    enum State : int { GROUND = 0, AIR = 1, WALL = 2, LIP = 3, RAIL = 4, WALLPLANT = 5 };
    // The physics running it (+0x1C).
    enum PhysicsMode : int { SKATING = 0, WALKING = 1 };

    // 0x00519740 (registered by RegisterComponentTypes): a new one (0 if out of memory), no powerups.
    static CBaseComponent* __cdecl Create();
    // 0x00519430 (registered with Create): its script member functions (the table at 0x00684A80) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x00519450: the destructor body (the base's).
    void DestroyBody();
    // 0x00519620 (vtable slot 0)
    CSkaterStateComponent* Destroy(unsigned int flags);
    // 0x00519460 (vtable +0x08): in the air. `params` isn't read.
    void InitFromStructure(Script::CStruct* params);

    // 0x00519660 / 0x00519680 / 0x005196A0: the state / the physics mode / the terrain; when it changes the skater
    // gets OnSkaterStateChange / OnSkaterPhysicsStateChange / OnSkaterTerrainChange.
    void SetState(int state);
    void SetPhysicsMode(int mode);
    void SetTerrain(uint8_t terrain);
    // 0x00519470: the powerups dropped (called by a script command).
    void ClearPowerups();
    // 0x00519650: +0xE0 (doingtrick) cleared (called by the skater).
    void ClearDoingTrick();

    // --- script member functions (the table at 0x00684A80; params, script) ---
    // (OnGround is 0x00525E90's, not here)
    bool InAir(Script::CStruct* params, Script::CScript* script);           // 0x00519480
    bool OnWall(Script::CStruct* params, Script::CScript* script);          // 0x00519490
    bool OnLip(Script::CStruct* params, Script::CScript* script);           // 0x005194A0
    bool OnRail(Script::CStruct* params, Script::CScript* script);          // 0x005194B0
    bool InWallplant(Script::CStruct* params, Script::CScript* script);     // 0x005194C0
    bool InBailState(Script::CStruct* params, Script::CScript* script);     // 0x005196D0 (+0x70)
    bool Walking(Script::CStruct* params, Script::CScript* script);         // 0x005194D0 (not driving)
    bool Skating(Script::CStruct* params, Script::CScript* script);         // 0x005194F0 (not driving)
    bool Driving(Script::CStruct* params, Script::CScript* script);         // 0x00519510
    bool SetDriving(Script::CStruct* params, Script::CScript* script);      // 0x00519520
    bool UnsetDriving(Script::CStruct* params, Script::CScript* script);    // 0x00519530
    bool DoingTrick(Script::CStruct* params, Script::CScript* script);      // 0x005196E0 (+0xE0)
    // 0x00519540 / 0x005195B0: the powerup the unnamed checksum names (the one in the table at 0x0067F09C) - picked
    // up or not / picked up. Unknown names: false / nothing.
    bool HasPowerup(Script::CStruct* params, Script::CScript* script);
    bool PickedUpPowerup(Script::CStruct* params, Script::CScript* script);
    bool GetTerrain(Script::CStruct* params, Script::CScript* script);      // 0x005196F0 (terrain)
    bool GetSparksPos(Script::CStruct* params, Script::CScript* script);    // 0x00519720 (SparksPos)
    bool SetSparksPos(Script::CStruct* params, Script::CScript* script);    // 0x00519600 (the unnamed checksum)

    int m_state;                // +0x18 State
    int m_physicsMode;          // +0x1C PhysicsMode (-1 until the physics control component sets it)
    uint8_t m_unknown20[0x50];  // +0x20 (the controls, the flip flag +0x28 and its time +0x2C... - the physics')
    uint8_t m_bailing;          // +0x70
    uint8_t m_unknown71[0x6F];
    uint8_t m_doingTrick;       // +0xE0
    uint8_t m_terrain;          // +0xE1
    uint8_t m_unknownE2[0x36];
    uint8_t m_driving;          // +0x118
    uint8_t m_unknown119[7];
    uint8_t m_powerups[1];      // +0x120 (one per name in 0x0067F09C)
    uint8_t m_unknown121[3];
    uint32_t m_sparksPos;       // +0x124 a checksum
};
static_assert(offsetof(CSkaterStateComponent, m_state) == 0x18 &&
                  offsetof(CSkaterStateComponent, m_physicsMode) == 0x1C &&
                  offsetof(CSkaterStateComponent, m_bailing) == 0x70 &&
                  offsetof(CSkaterStateComponent, m_doingTrick) == 0xE0 &&
                  offsetof(CSkaterStateComponent, m_terrain) == 0xE1 &&
                  offsetof(CSkaterStateComponent, m_driving) == 0x118 &&
                  offsetof(CSkaterStateComponent, m_powerups) == 0x120 &&
                  offsetof(CSkaterStateComponent, m_sparksPos) == 0x124 && sizeof(CSkaterStateComponent) == 0x128,
              "layout must match the original (0x00519740 allocates 0x128 bytes)");

// The skater's state history (SkaterStateHistory, type 0x18223FD6, vtable 0x0064C174 - the rest of it is after this
// range): its frame records (0x84 bytes; 20 of them from the component's +0x1C) keep their time as two halves.
struct SkaterStateHistoryFrame {
    // 0x005197D0 / 0x005197E0: the frame's time (+0x78 the low 16 bits, +0x7A the high).
    uint32_t GetTime();
    void SetTime(uint32_t time);
};
struct SkaterStateHistoryBase : CBaseComponent {
    // 0x005197C0: the state history component's destructor body (the base's).
    void DestroyBody();
};

} // namespace Obj
