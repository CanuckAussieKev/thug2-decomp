#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"

namespace Scripts {
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
// Vtable: 0 Destroy, 1 the shared empty 0x0051B970, 2 ConfigureFromData, 11 the shared 0x0042BB20, 12 the shared
// InvokeMethodByName 0x0051ADC0.
namespace GameObjects {

class CRiderPoseState : public CObjectModule {
public:
    // The skater's state (+0x18).
    enum State : int { GROUND = 0, AIR = 1, WALL = 2, LIP = 3, RAIL = 4, WALLPLANT = 5 };
    // The physics running it (+0x1C).
    enum PhysicsMode : int { SKATING = 0, WALKING = 1 };

    // 0x00519740 (registered by RegisterComponentTypes): a new one (0 if out of memory), no powerups.
    static CObjectModule* __cdecl Fabricate();
    // 0x00519430 (registered with Create): its script member functions (the table at 0x00684A80) registered.
    static void __cdecl PublishMethodTable();
    // 0x00519450: the destructor body (the base's).
    void DestroyBody();
    // 0x00519620 (vtable slot 0)
    CRiderPoseState* Teardown(unsigned int flags);
    // 0x00519460 (vtable +0x08): in the air. `params` isn't read.
    void ConfigureFromData(Scripts::CStruct* params);

    // 0x00519660 / 0x00519680 / 0x005196A0: the state / the physics mode / the terrain; when it changes the skater
    // gets OnSkaterStateChange / OnSkaterPhysicsStateChange / OnSkaterTerrainChange.
    void ChangePhase(int state);
    void SetPhysicsMode(int mode);
    void SetSurfaceKind(uint8_t terrain);
    // 0x00519470: the powerups dropped (called by a script command).
    void ClearPowerups();
    // 0x00519650: +0xE0 (doingtrick) cleared (called by the skater).
    void ClearDoingTrick();

    // --- script member functions (the table at 0x00684A80; params, script) ---
    // (OnGround is 0x00525E90's, not here)
    bool InAir(Scripts::CStruct* params, Scripts::CScript* script);           // 0x00519480
    bool OnWall(Scripts::CStruct* params, Scripts::CScript* script);          // 0x00519490
    bool OnLip(Scripts::CStruct* params, Scripts::CScript* script);           // 0x005194A0
    bool OnRail(Scripts::CStruct* params, Scripts::CScript* script);          // 0x005194B0
    bool InWallplant(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005194C0
    bool InBailState(Scripts::CStruct* params, Scripts::CScript* script);     // 0x005196D0 (+0x70)
    bool OnFoot(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005194D0 (not driving)
    bool Skating(Scripts::CStruct* params, Scripts::CScript* script);         // 0x005194F0 (not driving)
    bool Driving(Scripts::CStruct* params, Scripts::CScript* script);         // 0x00519510
    bool SetDriving(Scripts::CStruct* params, Scripts::CScript* script);      // 0x00519520
    bool UnsetDriving(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00519530
    bool InAirMove(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005196E0 (+0xE0)
    // 0x00519540 / 0x005195B0: the powerup the unnamed checksum names (the one in the table at 0x0067F09C) - picked
    // up or not / picked up. Unknown names: false / nothing.
    bool HasPowerup(Scripts::CStruct* params, Scripts::CScript* script);
    bool PickedUpPowerup(Scripts::CStruct* params, Scripts::CScript* script);
    bool GetSurfaceKind(Scripts::CStruct* params, Scripts::CScript* script);      // 0x005196F0 (terrain)
    bool GetSparksPos(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00519720 (SparksPos)
    bool SetSparksPos(Scripts::CStruct* params, Scripts::CScript* script);    // 0x00519600 (the unnamed checksum)

    int m_curPhase;                // +0x18 State
    int m_physicsMode;          // +0x1C PhysicsMode (-1 until the physics control component sets it)
    uint8_t m_unknown20[0x50];  // +0x20 (the controls, the flip flag +0x28 and its time +0x2C... - the physics')
    uint8_t m_bailing;          // +0x70
    uint8_t m_unknown71[0x6F];
    uint8_t m_inTrickMove;       // +0xE0
    uint8_t m_surfaceType;          // +0xE1
    uint8_t m_unknownE2[0x36];
    uint8_t m_inVehicle;          // +0x118
    uint8_t m_unknown119[7];
    uint8_t m_boostItems[1];      // +0x120 (one per name in 0x0067F09C)
    uint8_t m_unknown121[3];
    uint32_t m_sparksPos;       // +0x124 a checksum
};
static_assert(offsetof(CRiderPoseState, m_curPhase) == 0x18 &&
                  offsetof(CRiderPoseState, m_physicsMode) == 0x1C &&
                  offsetof(CRiderPoseState, m_bailing) == 0x70 &&
                  offsetof(CRiderPoseState, m_inTrickMove) == 0xE0 &&
                  offsetof(CRiderPoseState, m_surfaceType) == 0xE1 &&
                  offsetof(CRiderPoseState, m_inVehicle) == 0x118 &&
                  offsetof(CRiderPoseState, m_boostItems) == 0x120 &&
                  offsetof(CRiderPoseState, m_sparksPos) == 0x124 && sizeof(CRiderPoseState) == 0x128,
              "layout must match the original (0x00519740 allocates 0x128 bytes)");

// The skater's state history (SkaterStateHistory, state_history.h): its state records (0x84 bytes; 20 of them from
// the component's +0x1C) keep their time as two halves. (Its destructor body 0x005197C0 is here too.)
struct SkaterStateHistoryFrame {
    // 0x005197D0 / 0x005197E0: the frame's time (+0x78 the low 16 bits, +0x7A the high).
    uint32_t GetTime();
    void ChangeMoment(uint32_t time);
};

} // namespace Obj
