// The WaitAndDie component: see wait_and_die.h. From the disassembly of 0x0043D840-0x0043D928.

#include "engine/objects/wait_and_die.h"

#include <cstdint>

#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr uint32_t kWaitFor = 0x1869F559;
constexpr uint32_t kLifeTime = 0xC218CF77;
const float kMilliseconds = 1000.0f; // 0x006453C0

// The original's _ftol (0x006236FC): truncated towards zero (the low half of the 64-bit result).
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// The object's flag_as_dead, as the original's tail jump.
void die(Entity* object) { object->flag_as_dead(); }

} // namespace

// 0x0043D840 (0x0043D850 the deleting one)
WaitAndDieComponent::~WaitAndDieComponent() = default;

// 0x0043D880
void WaitAndDieComponent::configure_from_data(script::Struct* params)
{
    float seconds = 0.0f;
    if (!params->read_hash_value(kWaitFor, &wait_for_, 0))
        params->fetch_float(kLifeTime, &seconds, 1);
    lifespan_ = static_cast<uint32_t>(ftol(static_cast<double>(seconds) * kMilliseconds));
    const int clock = frame_clock::game_clock_elapsed();
    outset_ = clock + clock_offset();
}

// 0x0043D8E0
void WaitAndDieComponent::tick_frame()
{
    const int start = outset_;
    const int clock = frame_clock::game_clock_elapsed();
    elapsed_ = static_cast<uint32_t>(clock) + (static_cast<uint32_t>(clock_offset()) - static_cast<uint32_t>(start));
    if (wait_for_) {
        if (!find_object(wait_for_))
            die(entity());
    } else if (elapsed_ > lifespan_) {
        die(entity());
    }
}

} // namespace objects

namespace objects {
namespace {} // namespace
} // namespace objects

namespace objects {} // namespace objects
