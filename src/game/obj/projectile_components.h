#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/vector.h"
#include "game/obj/object.h"

namespace Script {
class CStruct;
} // namespace Script

namespace Obj {

class CPedLogicComponent;
class CSkater;

// The projectile collision component (type 0x7767D6D7 "projectilecollision", vtable 0x0064B830, 0x44 bytes; made by
// 0x004F3010): a thrown projectile (a snowball, a fireball) hitting skaters in a network game. Each update, unless
// it has hit something or its suspend component says it's far, the first skater other than its owner that can be
// hit again (not one with no score left, for a fireball; not a team mate unless friendly fire is on) and is within
// radius + 48 is hit: the projectile dies, death_script runs (pos, vel, scale), the hit skater's player and the owner's
// are sent message 0x84 / 0x85 (the owner / the hit skater's id, whether it's a fireball, the score it takes); a
// fireball takes ten times its scale off the hit skater's score - one with no score left is eliminated (hidden,
// announce_elimination), unless it's the local player. The collide-and-die component tells it when it hits the world.
// Outside a network game (or with player collision off, for anything but a fireball) it does nothing.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize (the collide-and-die
// component's 0x0041C330: the suspend component at +0x18); the rest the base's / shared (4 / 9 0x005480A0, 5 / 8
// 0x005BAB90, 6 0x0048DCD0, 7 0x0045D760, 10 0x005144C0, 11 0x0045D720, 12 0x0051ADC0).
class CProjectileCollisionComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x7767D6D7;
    static constexpr uint32_t kFireball = 0xD039432C; // projectile_type fireball

    // 0x004F3010 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory): the
    // base's constructor, its vtable and type, no radius, scale, death script or owner, not hit.
    static CBaseComponent* __cdecl Create();
    // 0x004F2F10: the base's body.
    void DestroyBody();
    // 0x004F2F50 (vtable slot 0)
    CProjectileCollisionComponent* Destroy(unsigned int flags);
    // 0x004F2F80 (vtable slot 2): radius, scale, owner_id, death_script, projectile_type (each asserted); vel
    // (0, 0, 1) normalised - not read from the parameters.
    void InitFromStructure(Script::CStruct* params);
    // 0x004F3090 (vtable slot 1): see the class.
    void Update();
    // 0x004F2F40: it has hit the world (the collide-and-die component's call): no more skaters are hit.
    void MarkHit();
    // 0x004F2F20 (not a Ghidra function): the score a hit takes - a fireball's scale (truncated) times ten, 0 for
    // anything else.
    int ScoreTaken();

    uint8_t* mp_suspend;      // +0x18 the suspend component (Finalize)
    float m_radius;           // +0x1C radius
    float m_scale;            // +0x20 scale
    uint32_t m_deathScript;   // +0x24 death_script
    uint32_t m_ownerId;       // +0x28 owner_id (the skater who threw it)
    uint32_t m_projectileType; // +0x2C projectile_type
    Mth::Vector m_vel;        // +0x30 (0, 0, 1) normalised
    bool m_hit;               // +0x40
    uint8_t pad41[3];
};
static_assert(offsetof(CProjectileCollisionComponent, mp_suspend) == 0x18 &&
                  offsetof(CProjectileCollisionComponent, m_projectileType) == 0x2C &&
                  offsetof(CProjectileCollisionComponent, m_vel) == 0x30 &&
                  offsetof(CProjectileCollisionComponent, m_hit) == 0x40 && sizeof(CProjectileCollisionComponent) == 0x44,
              "layout must match the original");

// The projectile proximity component (type 0x3618B878 "projectileproximity", vtable 0x0064B864, 0x24 bytes; made by
// 0x004F3550): a ped the local skater's projectiles can hit. Each update the first of the skater's five projectiles in
// flight within 48 of the ped (30 up from its position) hits it - unless the ped is already shot: the ped gets the
// ProjectileInAvoidRadius event, projectile_hit_ped runs on the skater and hit_ped_script on the ped (Ped_ID,
// projectile_id, ammo_type: the skater's core physics +0x2FC), and the projectile is spent. The skater's targeting
// (src/game/skater/skater.cpp) walks these components.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure; the rest the base's / shared (3 / 4
// 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 9 0x005480A0, 11 0x0045D720, 12
// 0x0051ADC0).
class CProjectileProximityComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x3618B878;

    // 0x004F3550 (registered with the composite object manager by 0x004E4860): a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004F34E0: the base's body.
    void DestroyBody();
    // 0x004F34F0 (vtable slot 0)
    CProjectileProximityComponent* Destroy(unsigned int flags);
    // 0x004F3520 (vtable slot 2): the local skater, the object's PedLogic component, 48 squared. (The parameters
    // aren't read.)
    void InitFromStructure(Script::CStruct* params);
    // 0x004F35B0 (vtable slot 1): see the class. With no skater yet, it's looked up instead; with no PedLogic
    // component yet (and an object id), that is.
    void Update();

    float m_radiusSqr;                 // +0x18 2304 (48 squared)
    CSkater* mp_skater;                // +0x1C the local skater
    CPedLogicComponent* mp_pedLogic;   // +0x20 the object's
};
static_assert(offsetof(CProjectileProximityComponent, m_radiusSqr) == 0x18 &&
                  offsetof(CProjectileProximityComponent, mp_pedLogic) == 0x20 &&
                  sizeof(CProjectileProximityComponent) == 0x24,
              "layout must match the original");

} // namespace Obj
