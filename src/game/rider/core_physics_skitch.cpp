// Skitching: the skater grabbing onto a car (an object with a skitch component) and being pulled along at one of
// its skitch points.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/smtptr.h"
#include "engine/script/param_struct.h"
#include "game/rider/core_physics.h"
#include "game/rider/core_physics_fields.h"
#include "game/rider/skitch.h"
#include "game/rider/thaw_bmx.h"
#include "engine/objects/movable_contact.h"
#include "engine/objects/ride_tracker.h"
#include "game/rider/skater_state.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"

namespace skater {

using namespace physics;

namespace {

constexpr uint32_t kSkitch = 0x3506CE64;            // skitch (a component)
constexpr uint32_t kSkitchHoldTime = 0x19B2C752;    // skitch_hold_time (ms)
constexpr uint32_t kSkitchMaxDistance = 0x2928F080; // Skitch_Max_Distance
constexpr uint32_t kSkitched = 0x2F184EB1;          // Skitched (an event)
constexpr uint32_t kSkitchLeft = 0x74BF80CF;        // SkitchLeft (an event)
constexpr uint32_t kSkitchRight = 0x2E7474B5;       // SkitchRight (an event)
constexpr uint32_t kSkitchOffset = 0x21FB182C;      // Skitch_Offset
constexpr uint32_t kSkitchSuckSpeed = 0x97496256;   // skitch_suck_speed
constexpr uint32_t kOffMeterBottom = 0x47D44B84;    // OffMeterBottom (an event)

constexpr float kSkitchStep = 6.0F;                // 0x00646284
constexpr float kSkitchReach = 120.0F;             // 0x0064BD2C
constexpr float kSkitchLostDistanceSqr = 32400.0F; // 0x0064BD28 (180 squared)

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
    if (on_vehicle_ != 0U) {
        return;
    }
    objects::ControlButton* button = &input_->pad_primary().up_dir();
    // (not original: THAW 0x005D33A7 - on the bike R2 held, not up)
    objects::ControlButton* hold = thaw_bmx::live_now(this) ? thaw_bmx::tow_button(this) : button;
    if ((state_->flag(PoseFlag::kSkitching).on != 0U) || (hold->held_down() == 0U)) {
        return;
    }
    auto held = static_cast<uint32_t>(time_since_press(hold));
    if (held <= ints::as_unsigned(script::fetch_int_value(kSkitchHoldTime)) || state_->cur_phase() != 0) {
        return;
    }
    // The nearest skitch point of any other object's skitch component (that's on), within Skitch_Max_Distance.
    float nearest = script::fetch_float(kSkitchMaxDistance);
    objects::GameEntity* found = nullptr;
    for (objects::ObjectModule* skitch = objects::object_hub_slot()->find_first_module_of_kind(kSkitch); skitch;
         skitch = skitch->next_of_type()) {
        if (!core::down_cast<objects::TowRideHandler>(skitch)->allowed()) {
            continue;
        }
        objects::GameEntity* car = skitch->entity();
        if (car == object_of(this)) {
            continue;
        }
        maths::Vec4 point = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
        const int index = closest_tow_point(skitch, &point, &entity()->at_point());
        if (index == -1) {
            continue;
        }
        const double distance = length_unrounded(point - entity()->at_point());
        if (distance < nearest) {
            nearest = static_cast<float>(distance);
            found = car;
            skitch_.point = index;
        }
    }
    if (!found) {
        return;
    }
    take_press(&input_->pad_primary().shoulder_left_a());
    take_press(&input_->pad_primary().shoulder_right_a());
    // (0x0041D1D0: the smart pointer set)
    skitch_target_.assign_object(found);
    found->post_own_event(kSkitched, nullptr);
    object_of(this)->post_own_event(kSkitched, nullptr);
}

// 0x00501DB0
void SkaterPhysicsModule::get_skitch()
{
    const objects::RideTracker* rider = ride_->ride().get();
    objects::PadSnapshot* pad = &input_->pad_primary();
    objects::GameEntity* car = rider ? rider->entity().get_pull() : nullptr;
    if (!car) {
        object_of(this)->post_own_event(kOffMeterBottom, nullptr);
        return;
    }
    void* skitch = car->fetch_module(kSkitch);

    // A press of either pair of buttons moves to the next skitch point along the car, one way or the other.
    float direction = 0.0F;
    if (pad->shoulder_left_a().fired() != 0U) {
        take_press(&pad->shoulder_left_a());
        direction = 1.0F;
    }
    if (pad->shoulder_right_a().fired() != 0U) {
        take_press(&pad->shoulder_right_a());
        direction = -1.0F;
    }
    if (pad->shoulder_left_b().fired() != 0U) {
        take_press(&pad->shoulder_left_b());
        direction = 1.0F;
    }
    bool move = false;
    if (pad->shoulder_right_b().fired() != 0U) {
        take_press(&pad->shoulder_right_b());
        direction = -1.0F;
        move = true;
    } else {
        move = direction != 0.0F;
    }
    if (move && ((skitch_moving_ == 0U) || direction != skitch_.direction)) {
        // Further and further along the skater's side (6 at a time, short of 120) until another point is nearest.
        float reach = kSkitchStep;
        double next = 0.0;
        for (bool again = true; again; again = next < kSkitchReach) {
            const objects::GameEntity* object = object_of(this);
            const maths::Vec4& side = core::at(object->basis_frame().row, 0);
            const maths::Vec4& position = object->at_point();
            const float scale = reach * direction;
            auto x = static_cast<float>(static_cast<double>(side.x) * scale);
            auto y = static_cast<float>(static_cast<double>(side.y) * scale);
            auto z = static_cast<float>(static_cast<double>(side.z) * scale);
            maths::Vec4 target;
            target.w = static_cast<float>((static_cast<double>(side.w) * scale) + position.w);
            target.z = z + position.z;
            target.y = y + position.y;
            target.x = x + position.x;
            maths::Vec4 point = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
            const int index = closest_tow_point(skitch, &point, &target);
            if (index != skitch_.point) {
                skitch_.point = index;
                skitch_moving_ = 1;
                skitch_.direction = direction;
                object_of(this)->post_own_event(direction == 1.0F ? kSkitchLeft : kSkitchRight, nullptr);
                break;
            }
            next = static_cast<double>(reach) + kSkitchStep;
            reach = static_cast<float>(next);
        }
    }

    // Held at the skitch point, Skitch_Offset behind it along the car; while moving to a new one, pulled towards it
    // at skitch_suck_speed (let go of when 180 or more away).
    if (!ride_->ride() || (state_->flag(PoseFlag::kSkitching).on == 0U)) {
        return;
    }
    maths::Vec4 point;
    if (!static_cast<objects::TowRideHandler*>(skitch)->get_hook_point(&point, skitch_.point)) {
        return;
    }
    objects::GameEntity* object = object_of(this);
    object->at_point().w = 0.0F;
    const maths::Vec4 back = -core::at(car->basis_frame().row, 2);
    const float offset = script::fetch_float(kSkitchOffset);
    const maths::Vec4 target = point + offset * back;
    if (skitch_moving_ != 0U) {
        const maths::Vec4 moved = ride_->ride()->carried(); // (the ride's last move)
        entity()->at_point() += moved;
        maths::Vec4 toward = target - entity()->at_point();
        auto length_sqr = static_cast<float>(length_sqr_unrounded(toward));
        auto suck =
            static_cast<float>(static_cast<double>(tuning_float_lookup(kSkitchSuckSpeed, object)) * frame_.length);
        if (length_sqr > kSkitchLostDistanceSqr) {
            object_of(this)->post_own_event(kOffMeterBottom, nullptr);
            return;
        }
        if (static_cast<double>(suck) * suck >= length_sqr) {
            entity()->at_point() = target;
            skitch_moving_ = 0;
        } else {
            toward *= static_cast<float>(suck / std::sqrt(static_cast<double>(length_sqr)));
            entity()->at_point() += toward;
        }
    } else {
        entity()->at_point() = target;
    }
    entity()->basis_frame() = car->display_matrix(); // (the car's display matrix)
    ground_.display_matrix = entity()->basis_frame();
}

} // namespace skater
