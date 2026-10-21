#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The car physics component (type 0xC695136D "carphysics", vtable 0x00645E6C, 0x58 bytes; made by 0x0041BD90, and by
// CreateCar through 0x0041BAD0): a car's look - its wheels spinning with its speed, the steering wheels turned
// towards its path, the body tilting with acceleration - written into its skeleton's bone matrices after each update.
// Angles in degrees. The tilt's limits and rates are script globals (re-read every update while the global 0x4CF13D8F
// is set) that `params` may override.
//
// Vtable: 0 the scalar deleting destructor, 1 Update, 2 InitFromStructure, 3 Finalize; the rest the base's / shared (4
// / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 8 / 10 0x005BAB90, 11 0x0042BB20, 12 0x0051ADC0).
class CCarPhysicsComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0xC695136D;

    // A bone of the model's skeleton description (0x50 bytes each; the model's 0x00493040).
    struct BoneDesc {
        uint32_t name;       // +0x00
        uint8_t unknown04[4];
        int16_t parent;      // +0x08 (the matrix it is relative to)
        int8_t index;        // +0x0A (its matrix in the skeleton)
        uint8_t unknown0B[5];
        Mth::Matrix matrix;  // +0x10 (relative to its parent)
    };

    // 0x0041BD90 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined; 0
    // if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041B910 (not a Ghidra function; registered with Create): its member functions' table (0x006846A8; the
    // functions are put in by the static initialiser at 0x00643E80).
    static void __cdecl RegisterMemberFunctions();
    // 0x0041BAD0: the base's constructor, its vtable and type, no spin or steering, the body shown. Returns this.
    CCarPhysicsComponent* Construct();
    // 0x0041B930: the base's body.
    void DestroyBody();
    // 0x0041BB00 (vtable slot 0)
    CCarPhysicsComponent* Destroy(unsigned int flags);
    // 0x0041B940 (vtable slot 2): the tilt's minimum (0x877DB8DA), maximum (0xFB3464DF), rate (0xFD8BC8D6) and
    // acceleration divisor (0xB2C7447B): the script globals of those names, overridden by `params`.
    void InitFromStructure(Script::CStruct* params);
    // 0x0041BE10 (vtable slot 3): the object's motion, suspend, skeleton and model components; with a skeleton, its
    // skeleton and the model, and the distance between the positions of bones 2 and 4 (each times bone 0's matrix).
    void Finalize();
    // 0x0041C1B0 (vtable slot 1): unless suspended (the suspend component's +0x30, or it says not to update), dead or
    // without a skeleton: UpdateAngles, then UpdateBones.
    void Update();

    // 0x0041BB30: the wheels' spin (the motion's speed; kept within 0-360), the steering (towards
    // -asin(m_wheelBase / the distance to the path follower's point) - negated for a negative turn, 0 without one or
    // without a turn -, by at most 60 degrees a second) and the tilt (towards -acceleration / divisor, within the
    // limits, by at most rate * 60 a second).
    void UpdateAngles();
    // 0x0041C0B0: UpdateSkeleton; with the body hidden, bone 0's matrix scaled to nothing.
    void UpdateBones();
    // 0x0041BF00 (cdecl): every bone's matrix from the model's description: the two front (0x80212262, 0x1E45B7C1)
    // and two back (0x69428757, 0xF04BD6ED) wheels turned about X by the spin, the steering wheels (0x6E2F434E,
    // 0xF72612F4) about Z by the steering then about X by the spin, the body (0x88C21962 "car") about X by the tilt;
    // each but the first then times its parent's matrix.
    static void __cdecl UpdateSkeleton(void* skeleton, void* model, float tilt, float spin, float steer);

    // --- the member functions scripts call (the table at 0x006846A8) ---
    // 0x0041B9E0 [0xA2331896]: the body shown when the unnamed integer isn't 0 (none: hidden). 1.
    int ShowBody(Script::CStruct* params, Script::CScript* script);
    // 0x0041BA20 [0xC9F0DB4B]: nothing. 1.
    int DoNothing(Script::CStruct* params, Script::CScript* script);

    float m_wheelSpin;     // +0x18 degrees, 0-360
    float m_steer;         // +0x1C
    float m_targetSteer;   // +0x20
    float m_tilt;          // +0x24
    float m_targetTilt;    // +0x28
    float m_wheelBase;     // +0x2C
    float m_lastSpeed;     // +0x30
    float m_minTilt;       // +0x34 0x877DB8DA
    float m_maxTilt;       // +0x38 0xFB3464DF
    float m_tiltRate;      // +0x3C 0xFD8BC8D6
    float m_tiltDivisor;   // +0x40 0xB2C7447B
    bool m_bodyShown;      // +0x44
    uint8_t pad45[3];
    void* mp_model;        // +0x48 the model component's model (+0x208)
    void* mp_skeleton;     // +0x4C the skeleton component's skeleton (+0x18)
    uint8_t* mp_motion;    // +0x50 the motion component
    uint8_t* mp_suspend;   // +0x54 the suspend component
};
static_assert(offsetof(CCarPhysicsComponent, m_wheelSpin) == 0x18 && offsetof(CCarPhysicsComponent, m_lastSpeed) == 0x30 &&
                  offsetof(CCarPhysicsComponent, m_tiltDivisor) == 0x40 &&
                  offsetof(CCarPhysicsComponent, m_bodyShown) == 0x44 &&
                  offsetof(CCarPhysicsComponent, mp_suspend) == 0x54 && sizeof(CCarPhysicsComponent) == 0x58,
              "layout must match the original");
static_assert(offsetof(CCarPhysicsComponent::BoneDesc, parent) == 0x08 &&
                  offsetof(CCarPhysicsComponent::BoneDesc, index) == 0x0A &&
                  offsetof(CCarPhysicsComponent::BoneDesc, matrix) == 0x10 &&
                  sizeof(CCarPhysicsComponent::BoneDesc) == 0x50,
              "layout must match the original");

} // namespace Obj
