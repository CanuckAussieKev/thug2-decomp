#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace GameNet {
class Manager;
}

namespace Script {
class CStruct;
} // namespace Script

namespace Obj {

// The collide-and-die component (type 0x6259B52B "collideanddie", vtable 0x00645EB4, 0x54 bytes; made by 0x0041C370):
// a projectile's end - each update a collision feeler from where its object was (24 up) to where it is (24 up, plus
// `radius` along its velocity); on a hit its projectile collision component (0x7767D6D7) is told, death_script runs
// (pos = the hit point, vel, scale) and the object dies a second later. It dies anyway ten seconds after it was made.
// Suspended (unless in a network game), nothing happens.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize; the rest the base's / shared (4
// / 9 0x005480A0, 5 / 8 0x005BAB90, 6 0x0048DCD0, 7 0x0045D760, 10 0x005144C0, 11 0x0045D720, 12 0x0051ADC0).
class CCollideAndDieComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x6259B52B;

    // 0x0041C370 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041C210: the base's constructor, its vtable and type; no radius, scale or script, made now (the game clock),
    // not dying, the first update to come. Returns this.
    CCollideAndDieComponent* Construct();
    // 0x0041C1F0: the base's body.
    void DestroyBody();
    // 0x0041C280 (vtable slot 0)
    CCollideAndDieComponent* Destroy(unsigned int flags);
    // 0x0041C2B0 (vtable slot 2): radius, scale, death_script (each asserted), vel (asserted; default (0, 0, 1),
    // normalised).
    void InitFromStructure(Script::CStruct* params);
    // 0x0041C330 (vtable slot 3): the object's suspend component.
    void Finalize();
    // 0x0041C3D0 (vtable slot 1): see the class. The times are the game clock's, compared unsigned.
    void Update();

    uint8_t* mp_suspend;    // +0x18 the suspend component (+0x30: suspended)
    float m_radius;         // +0x1C radius
    float m_scale;          // +0x20 scale (given to death_script)
    uint32_t m_madeTime;    // +0x24
    uint32_t m_deathScript; // +0x28 death_script
    uint32_t m_deathTime;   // +0x2C (a second after the hit)
    bool m_dying;           // +0x30
    bool m_firstUpdate;     // +0x31
    uint8_t pad32[2];
    Mth::Vector m_vel;      // +0x34 vel (normalised)
    Mth::Vector m_lastPos;  // +0x44 the object's position at the last update
};
static_assert(offsetof(CCollideAndDieComponent, m_radius) == 0x1C &&
                  offsetof(CCollideAndDieComponent, m_deathTime) == 0x2C &&
                  offsetof(CCollideAndDieComponent, m_firstUpdate) == 0x31 &&
                  offsetof(CCollideAndDieComponent, m_vel) == 0x34 &&
                  offsetof(CCollideAndDieComponent, m_lastPos) == 0x44 && sizeof(CCollideAndDieComponent) == 0x54,
              "layout must match the original");

extern GameNet::Manager* g_gameNetManager;

} // namespace Obj
