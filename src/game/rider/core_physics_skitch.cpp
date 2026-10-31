// Skitching: the skater grabbing onto a car (an object with a skitch component) and being pulled along at one of
// its skitch points.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/skitch.h"
#include "game/rider/thaw_bmx.h"

namespace skater {

using namespace physics;

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
constexpr uint32_t kTowAnchorIndex = 0x24C;           // the skitch point's index
constexpr uint32_t kSkitchDirection = 0x250;       // which way it moved (1 or -1, along the skater's side)
constexpr uint32_t kSkitchCar = 0x364;             // a smart pointer to the car

constexpr float kSkitchStep = 6.0f;                // 0x00646284
constexpr float kSkitchReach = 120.0f;             // 0x0064BD2C
constexpr float kSkitchLostDistanceSqr = 32400.0f; // 0x0064BD28 (180 squared)

objects::GameEntity* skater_object(void* component) { return static_cast<objects::GameEntity*>(object_of(component)); }

// 0x0051BA70 (GameObjects::TowRideHandler::get_nearest_hook_point): the index of its skitch point nearest `from` (-1 if none),
// its position in `point`.
int closest_tow_point(void* skitch, maths::Vec4* point, maths::Vec4* from)
{
    return static_cast<objects::TowRideHandler*>(skitch)->get_nearest_hook_point(point, from);
}

} // namespace

// 0x00503EC0
void SkaterPhysicsModule::try_start_skitch()
{
    if (get_at<uint8_t>(this, kDriving))
        return;
    uint8_t* button = input_of(this) + 0x20;
    // (not original: THAW 0x005D33A7 - on the bike R2 held, not up)
    uint8_t* hold = thaw_bmx::live_now(this) ? thaw_bmx::tow_button(this) : button;
    if (flags_of(this)[0x88] || !hold[0])
        return;
    uint32_t held = static_cast<uint32_t>(time_since_press(hold));
    if (held <= static_cast<uint32_t>(script::fetch_int_value(kSkitchHoldTime)) || get_at<int>(flags_of(this), kState) != 0)
        return;
    // The nearest skitch point of any other object's skitch component (that's on), within Skitch_Max_Distance.
    float nearest = script::fetch_float(kSkitchMaxDistance);
    objects::GameEntity* found = nullptr;
    for (objects::ObjectModule* skitch = objects::object_hub_slot()->find_first_module_of_kind(kSkitch); skitch;
         skitch = skitch->next_of_type_) {
        if (!get_at<uint8_t>(skitch, 0x18))
            continue;
        objects::GameEntity* car = skitch->entity_;
        if (car == object_of(this))
            continue;
        maths::Vec4 point = {0.0f, 0.0f, 0.0f, 1.0f};
        int index = closest_tow_point(skitch, &point, &vector_at(object_of(this), kPosition));
        if (index == -1)
            continue;
        double distance = length_unrounded(point - vector_at(object_of(this), kPosition));
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            found = car;
            get_at<int>(this, kTowAnchorIndex) = index;
        }
    }
    if (!found)
        return;
    take_press(button + 0x80);
    take_press(button + 0xE0);
    // (0x0041D1D0: the smart pointer set)
    get_at<objects::SharedRef<objects::SharedBase>>(this, kSkitchCar).assign_object(reinterpret_cast<objects::SharedBase*>(found));
    found->post_own_event(kSkitched, nullptr);
    skater_object(this)->post_own_event(kSkitched, nullptr);
}

// 0x00501DB0
void SkaterPhysicsModule::get_skitch()
{
    void* rider = get_at<void*>(get_at<void*>(this, kRide), 0x18);
    uint8_t* pad = input_of(this) + 0x20;
    objects::GameEntity* car = rider ? *static_cast<objects::GameEntity**>(rider) : nullptr;
    if (!car) {
        skater_object(this)->post_own_event(kOffMeterBottom, nullptr);
        return;
    }
    void* skitch = car->fetch_module(kSkitch);

    // A press of either pair of buttons moves to the next skitch point along the car, one way or the other.
    float direction = 0.0f;
    if (pad[0x90]) {
        take_press(pad + 0x80);
        direction = 1.0f;
    }
    if (pad[0xF0]) {
        take_press(pad + 0xE0);
        direction = -1.0f;
    }
    if (pad[0xB0]) {
        take_press(pad + 0xA0);
        direction = 1.0f;
    }
    bool move;
    if (pad[0x110]) {
        take_press(pad + 0x100);
        direction = -1.0f;
        move = true;
    } else {
        move = direction != 0.0f;
    }
    if (move && (!get_at<uint8_t>(this, kSkitchMoving) || direction != get_at<float>(this, kSkitchDirection))) {
        // Further and further along the skater's side (6 at a time, short of 120) until another point is nearest.
        float reach = kSkitchStep;
        double next;
        do {
            void* object = object_of(this);
            const maths::Vec4& side = vector_at(object, kMatrix);
            const maths::Vec4& position = vector_at(object, kPosition);
            float scale = reach * direction;
            float x = static_cast<float>(static_cast<double>(side.x) * scale);
            float y = static_cast<float>(static_cast<double>(side.y) * scale);
            float z = static_cast<float>(static_cast<double>(side.z) * scale);
            maths::Vec4 target;
            target.w = static_cast<float>(static_cast<double>(side.w) * scale + position.w);
            target.z = z + position.z;
            target.y = y + position.y;
            target.x = x + position.x;
            maths::Vec4 point = {0.0f, 0.0f, 0.0f, 1.0f};
            int index = closest_tow_point(skitch, &point, &target);
            if (index != get_at<int>(this, kTowAnchorIndex)) {
                get_at<int>(this, kTowAnchorIndex) = index;
                get_at<uint8_t>(this, kSkitchMoving) = 1;
                get_at<float>(this, kSkitchDirection) = direction;
                skater_object(this)->post_own_event(direction == 1.0f ? kSkitchLeft : kSkitchRight, nullptr);
                break;
            }
            next = static_cast<double>(reach) + kSkitchStep;
            reach = static_cast<float>(next);
        } while (next < kSkitchReach);
    }

    // Held at the skitch point, Skitch_Offset behind it along the car; while moving to a new one, pulled towards it
    // at skitch_suck_speed (let go of when 180 or more away).
    if (!get_at<void*>(get_at<void*>(this, kRide), 0x18) || !flags_of(this)[0x88])
        return;
    maths::Vec4 point;
    if (!static_cast<objects::TowRideHandler*>(skitch)->get_hook_point(&point, get_at<int>(this, kTowAnchorIndex)))
        return;
    void* object = object_of(this);
    vector_at(object, kPosition).w = 0.0f;
    maths::Vec4 back = -vector_at(car, kForward);
    float offset = script::fetch_float(kSkitchOffset);
    maths::Vec4 target = point + offset * back;
    if (get_at<uint8_t>(this, kSkitchMoving)) {
        maths::Vec4 moved = vector_at(get_at<void*>(get_at<void*>(this, kRide), 0x18), 0x5C); // (the ride's last move)
        vector_at(object_of(this), kPosition) += moved;
        maths::Vec4 toward = target - vector_at(object_of(this), kPosition);
        float length_sqr = static_cast<float>(length_sqr_unrounded(toward));
        float suck = static_cast<float>(static_cast<double>(tuning_float_lookup(kSkitchSuckSpeed, object)) *
                                        get_at<float>(this, kFrameLength));
        if (length_sqr > kSkitchLostDistanceSqr) {
            skater_object(this)->post_own_event(kOffMeterBottom, nullptr);
            return;
        }
        if (static_cast<double>(suck) * suck >= length_sqr) {
            vector_at(object_of(this), kPosition) = target;
            get_at<uint8_t>(this, kSkitchMoving) = 0;
        } else {
            toward *= static_cast<float>(suck / std::sqrt(static_cast<double>(length_sqr)));
            vector_at(object_of(this), kPosition) += toward;
        }
    } else {
        vector_at(object_of(this), kPosition) = target;
    }
    get_at<maths::Mat4>(object_of(this), kMatrix) = get_at<maths::Mat4>(car, 0xC0); // (the car's display matrix)
    get_at<maths::Mat4>(this, kDisplayMatrix) = get_at<maths::Mat4>(object_of(this), kMatrix);
}

} // namespace Skater
