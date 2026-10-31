#pragma once

#include <cstddef>
#include <cstdint>

#include "game/maths/vec4.h"
#include "game/obj/entity.h"
#include "game/sound/sfx.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The skater's looping sound component (SkaterLoopingSound, type 0xB5F1E9D2, vtable 0x0064BF38, 0x110 bytes): the
// wheels' roll on the ground (the terrain's TABLE_WHEELROLL sound), the air sound, and a grind or slide on a rail
// (TABLE_GRIND / TABLE_SLIDE) - one looping sound, its volume and pitch following the skater's speed (the finalize
// physics component gives the fraction of the top speed each frame, and the state, terrain and rail flags). From
// the disassembly of 0x00513860-0x00513FBE (0x00513860 isn't a Ghidra function).
//
// Vtable: 0 Destroy, 1 Update, 2 ConfigureFromData, 3 Finalize, 7 Suspend, 11 the shared 0x0042BB20, 12
// InvokeMethodByName.
namespace GameObjects {

class CRiderRollingAudio : public CObjectModule {
public:
    // 0x00513AD0 (registered by RegisterComponentTypes): a new one (0 if out of memory).
    static CObjectModule* __cdecl Fabricate();
    // 0x00513860 (registered with Create): its script member functions (the table at 0x00684A68) registered.
    static void __cdecl PublishMethodTable();
    // 0x005138E0: the base, its vtable and type; on, the sound to be chosen (changed), in the air, volume_mult 1.
    // Returns this.
    CRiderRollingAudio* MakeNew();
    // 0x00513B30: the sound stopped, then the base's.
    void DestroyBody();
    // 0x00513FA0 (vtable slot 0)
    CRiderRollingAudio* Teardown(unsigned int flags);
    // 0x00513880 (vtable +0x08): the air sound's pitch fall (45 and 18.33 a second) and volume_mult.
    void ConfigureFromData(Scripts::CStruct* params);
    // 0x00513970 (vtable +0x0C): the physics control component found.
    void Seal();
    // 0x00513F60 (vtable +0x1C): the base's; suspended, the sound stops; resumed, the sound is chosen again.
    void HoldOff(bool suspend);
    // 0x005139E0 (vtable +0x30): SkaterLoopingSound_TurnOn / _TurnOff (resumed / suspended) - 1; PlayAnim turns it on
    // when the skater (skating, not walking or driving) has animTags with WalkToSkateTransition in its tags - and,
    // like any other name, returns 2.
    int InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script);
    // 0x00513BB0 (vtable +0x04): off: suspended. Otherwise the sound for the state (chosen again when it changed),
    // stopped when it changes or the skater hasn't moved; its volume from where the skater is and the speed (the air
    // sound's pitch falling while it lasts), played or its volume and pitch updated when they changed enough.
    void TickFrame();

    // 0x005138B0: the speed as a fraction of the top speed (at most 1).
    void SetPaceRatio(float fraction);
    // 0x00513AB0: the sound stopped (called by the skater).
    void StopSound();

    uint32_t m_soundId;            // +0x18 the playing sound's unique id (0: none)
    uint32_t m_playingChecksum;    // +0x1C
    float m_leanPitchAngle;                 // +0x20 the pitch worked out
    float m_prevPitchValue;             // +0x24 the pitch last given
    SoundFx::volumeRef m_prevLevels;     // +0x28 the volume last given
    float m_airPitchFloor;         // +0x54 the air sound's pitch falls down to this (45)
    float m_airPitchFall;          // +0x58 by this much a second (18.33)
    uint8_t* m_physicsControl;     // +0x5C SkaterPhysicsControl (0xD9151D4D)
    uint8_t m_surfaceType;             // +0x60 (the finalize physics component's: the state's +0xE1)
    uint8_t pad61[3];
    Maths::Vec4 m_lastPos;         // +0x64 the skater's position last frame
    int m_curPhase;                   // +0x74 the skater's state (0 ground, 1 air, 4 rail)
    uint8_t m_sound[0x84];         // +0x78 the terrain sound entry chosen (0x00444B10's; the air one at 0x0067F018)
    // (in m_sound: +0xE0 the sound's checksum, +0xE4 / +0xE8 its pitch at full / no speed)
    uint8_t m_haveSound;           // +0xFC
    uint8_t m_soundChanged;        // +0xFD choose the sound again
    uint8_t m_forceUpdate;         // +0xFE (never set here)
    uint8_t m_noRailSound;         // +0xFF (the state's +0x70)
    uint8_t m_sliding;             // +0x100 on a rail: slide, not grind (the state's +0x58)
    uint8_t pad101[3];
    float m_speedRatio;         // +0x104
    float m_volumeMult;            // +0x108 volume_mult
    uint8_t m_on;                  // +0x10C
    uint8_t pad10D[3];
};
static_assert(offsetof(CRiderRollingAudio, m_soundId) == 0x18 &&
                  offsetof(CRiderRollingAudio, m_prevLevels) == 0x28 &&
                  offsetof(CRiderRollingAudio, m_airPitchFloor) == 0x54 &&
                  offsetof(CRiderRollingAudio, m_physicsControl) == 0x5C &&
                  offsetof(CRiderRollingAudio, m_lastPos) == 0x64 &&
                  offsetof(CRiderRollingAudio, m_curPhase) == 0x74 &&
                  offsetof(CRiderRollingAudio, m_sound) == 0x78 &&
                  offsetof(CRiderRollingAudio, m_haveSound) == 0xFC &&
                  offsetof(CRiderRollingAudio, m_speedRatio) == 0x104 &&
                  offsetof(CRiderRollingAudio, m_on) == 0x10C &&
                  sizeof(CRiderRollingAudio) == 0x110,
              "layout must match the original (0x00513AD0 allocates 0x110 bytes)");

// An volumeRef with its compiler-made assignment, which was placed here.
struct LoopingSoundVolume : SoundFx::volumeRef {
    // 0x00513990: `other` copied word for word. Returns this.
    LoopingSoundVolume* Assign(const SoundFx::volumeRef* other);
};

} // namespace Obj
