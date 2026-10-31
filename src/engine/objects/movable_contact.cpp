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
#include "engine/core/code_address.h"
#include "game/rider/skitch.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

extern void** const kVtable;

constexpr float kFeelerTop = 400.0f;   // 0x00646288
constexpr float kFeelerBottom = 6.0f;  // 0x00646284
constexpr float kNudge = 0.1f;         // 0x00645D74 (as the float it is)

float get_sum(float a, double b) { return static_cast<float>(static_cast<double>(a) + b); }

} // namespace

// 0x00427AB0
ObjectModule* __cdecl MovingSurfaceModule::fabricate()
{
    auto* contact = static_cast<MovingSurfaceModule*>(memory::alloc_zeroed(sizeof(MovingSurfaceModule)));
    if (!contact)
        return nullptr;
    contact->ObjectModule::make_new();
    contact->vtable_ = kVtable;
    contact->kind_tag_ = kType;
    contact->ride_ = nullptr;
    contact->stop_time_ = 0;
    return contact;
}

// 0x004276C0
void MovingSurfaceModule::destroy_body()
{
    vtable_ = kVtable;
    ObjectModule::destroy_body();
}

// 0x004276D0
MovingSurfaceModule* MovingSurfaceModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00427700
GameEntity* MovingSurfaceModule::find_contact(const maths::Vec4* point, maths::Vec4* hit)
{
    render::ProbeRay down;
    down.make_new();
    down.line_.start = {point->x, get_sum(point->y, kFeelerTop), point->z, point->w};
    down.line_.end = {point->x, get_sum(point->y, kFeelerBottom), point->z, point->w};
    render::ProbeRay up;
    up.make_new();
    for (int tries = 1;;) {
        if (!down.get_hit_shape(true, false) || !down.hit_object_alive())
            return nullptr;
        // (nothing between the point and just over the hit: it's what the point is on)
        up.line_.start = *point;
        up.line_.end = {down.contact_pos_.x, get_sum(down.contact_pos_.y, kNudge), down.contact_pos_.z, down.contact_pos_.w};
        if (!up.get_hit_shape(true, false)) {
            *hit = down.contact_pos_;
            return down.get_hit_object();
        }
        down.line_.start = {down.contact_pos_.x, get_sum(down.contact_pos_.y, -static_cast<double>(kNudge)), down.contact_pos_.z,
                             down.contact_pos_.w};
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
    double frame = frame_clock::get_slomo_frame_seconds();
    const maths::Vec4& vel = object->vel_;
    double x = frame * vel.x;
    float y = static_cast<float>(frame * vel.y);
    float z = static_cast<float>(frame * vel.z);
    float w = static_cast<float>(frame * vel.w);
    maths::Vec4 ahead = {get_sum(pos->x, x + x), get_sum(pos->y, static_cast<double>(y) + y), get_sum(pos->z, static_cast<double>(z) + z),
                         get_sum(pos->w, static_cast<double>(w) + w)};
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
        maths::Mat4 matrix;
        matrix = last_object_->basis_frame_;
        maths::Vec4 turned;
        matrix.rotate_vector(turned, point);
        const maths::Vec4& at = last_object_->at_point_;
        point = {turned.x + at.x, turned.y + at.y, turned.z + at.z, turned.w + at.w};
    }
    *out = point;
    return out;
}

// 0x00427C70
void MovingSurfaceModule::stop_riding()
{
    RideTracker* ride = ride_;
    if (!ride)
        return;
    last_object_ = ride->entity_.owner_object_;
    reinterpret_cast<SharedRef<SharedBase>*>(&ride->entity_)->get_release();
    memory::Hub::get_pull()->dealloc(ride);
    ride_ = nullptr;
    stop_time_ = frame_clock::game_clock_elapsed() + g_clock_offset;
}

// 0x00427CB0
bool MovingSurfaceModule::is_riding()
{
    if (!ride_)
        return false;
    if (ride_->entity_.owner_object_)
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
        GameEntity* riding = ride_->entity_.owner_object_;
        if (!riding || riding != feeler->get_hit_object())
            stop_riding();
    }
    if (ride_) {
        if (ride_->entity_.owner_object_)
            return false; // (already riding it)
        stop_riding();
    }
    if (!memory::g_manager_ready) // (the memory manager not set up yet)
        memory::ensure_manager();
    void* memory = memory::Hub::get_pull()->carve(sizeof(RideTracker), 1, nullptr);
    if (!memory) {
        ride_ = nullptr;
        return true;
    }
    ride_ = static_cast<RideTracker*>(memory)->make_new(feeler->get_hit_object());
    return true;
}

// 0x00427DD0
void MovingSurfaceModule::update_contact_point()
{
    GameEntity* object;
    if (ride_ && ride_->entity_.owner_object_) {
        object = ride_->entity_.owner_object_;
    } else {
        if (ride_)
            stop_riding();
        object = last_object_;
        if (!object)
            return;
    }
    maths::Vec4 at = object->at_point_;
    maths::Mat4 inverse;
    inverse = object->basis_frame_;
    const maths::Vec4& pos = entity_->at_point_;
    contact_point_ = {static_cast<float>(static_cast<double>(pos.x) - at.x),
                      static_cast<float>(static_cast<double>(pos.y) - at.y),
                      static_cast<float>(static_cast<double>(pos.z) - at.z),
                      static_cast<float>(static_cast<double>(pos.w) - at.w)};
    inverse.upend();
    maths::Vec4 local;
    inverse.rotate_vector(local, contact_point_);
    contact_point_ = local;
}

} // namespace Obj

using objects::MovingSurfaceModule;

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&MovingSurfaceModule::teardown,
         &objects::ComponentWithoutUpdate::tick_frame,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace Obj
