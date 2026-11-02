// The movable contact component (GameObjects::MovingSurfaceModule, movable_contact.h): riding moving objects.

#include "engine/objects/movable_contact.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/ride_tracker.h"
#include "engine/objects/smtptr.h"
#include "platform/game_clock.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"
#include "game/rider/skitch.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>

namespace objects {

namespace {

constexpr float kFeelerTop = 400.0f;  // 0x00646288
constexpr float kFeelerBottom = 6.0f; // 0x00646284
constexpr float kNudge = 0.1f;        // 0x00645D74 (as the float it is)

float get_sum(float a, double b) { return static_cast<float>(static_cast<double>(a) + b); }

} // namespace

// 0x00427AB0
gsl::owner<ObjectModule*> MovingSurfaceModule::fabricate() { return new MovingSurfaceModule; }

// (the constructor: inlined in fabricate in the original)
MovingSurfaceModule::MovingSurfaceModule() { kind_tag_ = kType; }

// 0x004276C0 (0x004276D0 the deleting one)
MovingSurfaceModule::~MovingSurfaceModule() { memory::let_go(ride_); } // (a ride it's on is left, as the original)

// 0x00427700
GameEntity* MovingSurfaceModule::find_contact(const maths::Vec4* point, maths::Vec4* hit)
{
    render::ProbeRay down;
    down.line_.start = {.x = point->x, .y = get_sum(point->y, kFeelerTop), .z = point->z, .w = point->w};
    down.line_.end = {.x = point->x, .y = get_sum(point->y, kFeelerBottom), .z = point->z, .w = point->w};
    render::ProbeRay up;
    for (int tries = 1;;) {
        if (!down.get_hit_shape(true, false) || !down.hit_object_alive())
            return nullptr;
        // (nothing between the point and just over the hit: it's what the point is on)
        up.line_.start = *point;
        up.line_.end = {.x = down.contact_pos_.x, .y = get_sum(down.contact_pos_.y, kNudge), .z = down.contact_pos_.z,
                        .w = down.contact_pos_.w};
        if (!up.get_hit_shape(true, false)) {
            *hit = down.contact_pos_;
            return down.get_hit_object();
        }
        down.line_.start = {.x = down.contact_pos_.x, .y = get_sum(down.contact_pos_.y, -static_cast<double>(kNudge)),
                            .z = down.contact_pos_.z, .w = down.contact_pos_.w};
        if (++tries == 10)
            return nullptr;
    }
}

// 0x00427B30
GameEntity* MovingSurfaceModule::find_contact_ahead(maths::Vec4* pos, const maths::Vec4* next)
{
    maths::Vec4 hit, other;
    GameEntity* object = find_contact(pos, &hit);
    if (!object)
        return nullptr;
    if (!find_contact(next, &other)) {
        *pos = *next;
        return object;
    }
    // (two frames of the object's velocity ahead: x through the unrounded product, the others rounded first)
    const double frame = frame_clock::get_slomo_frame_seconds();
    const maths::Vec4& vel = object->vel_;
    const double x = frame * vel.x;
    auto y = static_cast<float>(frame * vel.y);
    auto z = static_cast<float>(frame * vel.z);
    auto w = static_cast<float>(frame * vel.w);
    const maths::Vec4 ahead = {.x = get_sum(pos->x, x + x), .y = get_sum(pos->y, static_cast<double>(y) + y),
                               .z = get_sum(pos->z, static_cast<double>(z) + z),
                               .w = get_sum(pos->w, static_cast<double>(w) + w)};
    if (!find_contact(&ahead, &other)) {
        *pos = ahead;
        return object;
    }
    *pos = hit;
    pos->y = get_sum(pos->y, 1.0f);
    return object;
}

// 0x00427A00
maths::Vec4* MovingSurfaceModule::get_contact_point(maths::Vec4* out)
{
    maths::Vec4 point = contact_point_;
    if (last_object_) {
        maths::Mat4 matrix{};
        matrix = last_object_->basis_frame_;
        maths::Vec4 turned;
        matrix.rotate_vector(turned, point);
        const maths::Vec4& at = last_object_->at_point_;
        point = {.x = turned.x + at.x, .y = turned.y + at.y, .z = turned.z + at.z, .w = turned.w + at.w};
    }
    *out = point;
    return out;
}

// 0x00427C70
void MovingSurfaceModule::stop_riding()
{
    if (!ride_)
        return;
    last_object_ = ride_->entity_.get_pull();
    memory::destroy_then_clear(ride_); // (destroyed - its smart pointer lets go - and given back)
    stop_time_ = frame_clock::game_clock_elapsed() + g_clock_offset;
}

// 0x00427CB0
bool MovingSurfaceModule::is_riding()
{
    if (!ride_)
        return false;
    if (ride_->entity_.get_pull())
        return true;
    stop_riding();
    return false;
}

// 0x00427CD0
bool MovingSurfaceModule::start_riding(render::ProbeRay* feeler)
{
    if (!feeler->hit_object_alive()) {
        if (ride_)
            stop_riding();
        return false;
    }
    if (ride_) {
        const GameEntity* riding = ride_->entity_.get_pull();
        if (!riding || riding != feeler->get_hit_object())
            stop_riding();
    }
    if (ride_) {
        if (ride_->entity_.get_pull())
            return false; // (already riding it)
        stop_riding();
    }
    if (!memory::manager_ready_flag()) // (the memory manager not set up yet)
        memory::ensure_manager();
    void* memory = memory::Hub::get_pull()->carve(sizeof(RideTracker), 1, nullptr);
    if (!memory) {
        ride_ = nullptr;
        return true;
    }
    ride_ = memory::take_unique<RideTracker, memory::HeapDestroy>(
        memory::construct_in<RideTracker>(memory, feeler->get_hit_object()));
    return true;
}

// 0x00427DD0
void MovingSurfaceModule::update_contact_point()
{
    const GameEntity* object = nullptr;
    if (ride_ && ride_->entity_.get_pull()) {
        object = ride_->entity_.get_pull();
    } else {
        if (ride_)
            stop_riding();
        object = last_object_;
        if (!object)
            return;
    }
    const maths::Vec4 at = object->at_point_;
    maths::Mat4 inverse{};
    inverse = object->basis_frame_;
    const maths::Vec4& pos = entity_->at_point_;
    contact_point_ = {
        .x = static_cast<float>(static_cast<double>(pos.x) - at.x), .y = static_cast<float>(static_cast<double>(pos.y) - at.y),
        .z = static_cast<float>(static_cast<double>(pos.z) - at.z), .w = static_cast<float>(static_cast<double>(pos.w) - at.w)};
    inverse.upend();
    maths::Vec4 local;
    inverse.rotate_vector(local, contact_point_);
    contact_point_ = local;
}

} // namespace objects

