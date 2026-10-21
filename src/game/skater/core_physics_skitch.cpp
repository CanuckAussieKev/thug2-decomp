// Skitching: the skater grabbing onto a car (an object with a skitch component) and being pulled along at one of
// its skitch points.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

constexpr uint32_t kSkitch = 0x3506CE64;           // skitch (a component)
constexpr uint32_t kSkitchHoldTime = 0x19B2C752;   // skitch_hold_time (ms)
constexpr uint32_t kSkitchMaxDistance = 0x2928F080; // Skitch_Max_Distance
constexpr uint32_t kSkitched = 0x2F184EB1;         // Skitched (an event)
constexpr uint32_t kSkitchLeft = 0x74BF80CF;       // SkitchLeft (an event)
constexpr uint32_t kSkitchRight = 0x2E7474B5;      // SkitchRight (an event)
constexpr uint32_t kSkitchOffset = 0x21FB182C;     // Skitch_Offset
constexpr uint32_t kSkitchSuckSpeed = 0x97496256;  // skitch_suck_speed
constexpr uint32_t kOffMeterBottom = 0x47D44B84;   // OffMeterBottom (an event)

constexpr uint32_t kDriving = 0x2ED;               // (driving a vehicle)
constexpr uint32_t kSkitchMoving = 0x24A;          // moving to another skitch point
constexpr uint32_t kSkitchPoint = 0x24C;           // the skitch point's index
constexpr uint32_t kSkitchDirection = 0x250;       // which way it moved (1 or -1, along the skater's side)
constexpr uint32_t kSkitchCar = 0x364;             // a smart pointer to the car

constexpr float kSkitchStep = 6.0f;                // 0x00646284
constexpr float kSkitchReach = 120.0f;             // 0x0064BD2C
constexpr float kSkitchLostDistanceSqr = 32400.0f; // 0x0064BD28 (180 squared)

Obj::CCompositeObject* SkaterObject(void* component) { return static_cast<Obj::CCompositeObject*>(ObjectOf(component)); }

// 0x0051BA70 (the skitch component's, not rewritten yet): the index of its skitch point nearest `from` (-1 if none),
// its position in `point`.
int NearestSkitchPoint(void* skitch, Mth::Vector* point, Mth::Vector* from)
{
    return GameFunction<int(__thiscall*)(void*, Mth::Vector*, Mth::Vector*)>(0x0051BA70)(skitch, point, from);
}

} // namespace

// 0x00503EC0
void CorePhysicsComponent::TryStartSkitch()
{
    if (At<uint8_t>(this, kDriving))
        return;
    uint8_t* button = InputOf(this) + 0x20;
    if (FlagsOf(this)[0x88] || !button[0])
        return;
    uint32_t held = static_cast<uint32_t>(TimeSincePress(button));
    if (held <= static_cast<uint32_t>(Script::GetInteger(kSkitchHoldTime)) || At<int>(FlagsOf(this), kState) != 0)
        return;
    // The nearest skitch point of any other object's skitch component (that's on), within Skitch_Max_Distance.
    float nearest = Script::GetFloat(kSkitchMaxDistance);
    Obj::CCompositeObject* found = nullptr;
    for (Obj::CBaseComponent* skitch = Obj::CompositeObjectManager()->GetFirstComponentByType(kSkitch); skitch;
         skitch = skitch->mp_nextOfType) {
        if (!At<uint8_t>(skitch, 0x18))
            continue;
        Obj::CCompositeObject* car = skitch->m_object;
        if (car == ObjectOf(this))
            continue;
        Mth::Vector point = {0.0f, 0.0f, 0.0f, 1.0f};
        int index = NearestSkitchPoint(skitch, &point, &VectorAt(ObjectOf(this), kPosition));
        if (index == -1)
            continue;
        double distance = LengthUnrounded(point - VectorAt(ObjectOf(this), kPosition));
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            found = car;
            At<int>(this, kSkitchPoint) = index;
        }
    }
    if (!found)
        return;
    TakePress(button + 0x80);
    TakePress(button + 0xE0);
    // (0x0041D1D0: the smart pointer set)
    GameFunction<void*(__thiscall*)(void*, void*)>(0x0041D1D0)(&At<uint8_t>(this, kSkitchCar), found);
    found->SelfEvent(kSkitched, nullptr);
    SkaterObject(this)->SelfEvent(kSkitched, nullptr);
}

// 0x00501DB0
void CorePhysicsComponent::Skitch()
{
    void* rider = At<void*>(At<void*>(this, kRide), 0x18);
    uint8_t* pad = InputOf(this) + 0x20;
    Obj::CCompositeObject* car = rider ? *static_cast<Obj::CCompositeObject**>(rider) : nullptr;
    if (!car) {
        SkaterObject(this)->SelfEvent(kOffMeterBottom, nullptr);
        return;
    }
    void* skitch = car->GetComponent(kSkitch);

    // A press of either pair of buttons moves to the next skitch point along the car, one way or the other.
    float direction = 0.0f;
    if (pad[0x90]) {
        TakePress(pad + 0x80);
        direction = 1.0f;
    }
    if (pad[0xF0]) {
        TakePress(pad + 0xE0);
        direction = -1.0f;
    }
    if (pad[0xB0]) {
        TakePress(pad + 0xA0);
        direction = 1.0f;
    }
    bool move;
    if (pad[0x110]) {
        TakePress(pad + 0x100);
        direction = -1.0f;
        move = true;
    } else {
        move = direction != 0.0f;
    }
    if (move && (!At<uint8_t>(this, kSkitchMoving) || direction != At<float>(this, kSkitchDirection))) {
        // Further and further along the skater's side (6 at a time, short of 120) until another point is nearest.
        float reach = kSkitchStep;
        double next;
        do {
            void* object = ObjectOf(this);
            const Mth::Vector& side = VectorAt(object, kMatrix);
            const Mth::Vector& position = VectorAt(object, kPosition);
            float scale = reach * direction;
            float x = static_cast<float>(static_cast<double>(side.x) * scale);
            float y = static_cast<float>(static_cast<double>(side.y) * scale);
            float z = static_cast<float>(static_cast<double>(side.z) * scale);
            Mth::Vector target;
            target.w = static_cast<float>(static_cast<double>(side.w) * scale + position.w);
            target.z = z + position.z;
            target.y = y + position.y;
            target.x = x + position.x;
            Mth::Vector point = {0.0f, 0.0f, 0.0f, 1.0f};
            int index = NearestSkitchPoint(skitch, &point, &target);
            if (index != At<int>(this, kSkitchPoint)) {
                At<int>(this, kSkitchPoint) = index;
                At<uint8_t>(this, kSkitchMoving) = 1;
                At<float>(this, kSkitchDirection) = direction;
                SkaterObject(this)->SelfEvent(direction == 1.0f ? kSkitchLeft : kSkitchRight, nullptr);
                break;
            }
            next = static_cast<double>(reach) + kSkitchStep;
            reach = static_cast<float>(next);
        } while (next < kSkitchReach);
    }

    // Held at the skitch point, Skitch_Offset behind it along the car; while moving to a new one, pulled towards it
    // at skitch_suck_speed (let go of when 180 or more away).
    if (!At<void*>(At<void*>(this, kRide), 0x18) || !FlagsOf(this)[0x88])
        return;
    Mth::Vector point;
    if (!GameFunction<bool(__thiscall*)(void*, Mth::Vector*, int)>(0x0051BC70)(skitch, &point,
                                                                              At<int>(this, kSkitchPoint)))
        return;
    void* object = ObjectOf(this);
    VectorAt(object, kPosition).w = 0.0f;
    Mth::Vector back = -VectorAt(car, kForward);
    float offset = Script::GetFloat(kSkitchOffset);
    Mth::Vector target = point + offset * back;
    if (At<uint8_t>(this, kSkitchMoving)) {
        Mth::Vector moved = VectorAt(At<void*>(At<void*>(this, kRide), 0x18), 0x5C); // (the ride's last move)
        VectorAt(ObjectOf(this), kPosition) += moved;
        Mth::Vector toward = target - VectorAt(ObjectOf(this), kPosition);
        float lengthSqr = static_cast<float>(LengthSqrUnrounded(toward));
        float suck = static_cast<float>(static_cast<double>(PhysicsFloat(kSkitchSuckSpeed, object)) *
                                        At<float>(this, kFrameLength));
        if (lengthSqr > kSkitchLostDistanceSqr) {
            SkaterObject(this)->SelfEvent(kOffMeterBottom, nullptr);
            return;
        }
        if (static_cast<double>(suck) * suck >= lengthSqr) {
            VectorAt(ObjectOf(this), kPosition) = target;
            At<uint8_t>(this, kSkitchMoving) = 0;
        } else {
            toward *= static_cast<float>(suck / std::sqrt(static_cast<double>(lengthSqr)));
            VectorAt(ObjectOf(this), kPosition) += toward;
        }
    } else {
        VectorAt(ObjectOf(this), kPosition) = target;
    }
    At<Mth::Matrix>(ObjectOf(this), kMatrix) = At<Mth::Matrix>(car, 0xC0); // (the car's display matrix)
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
}

} // namespace Skater

REPLACE(0x00503EC0, &Skater::CorePhysicsComponent::TryStartSkitch);
REPLACE(0x00501DB0, &Skater::CorePhysicsComponent::Skitch);
