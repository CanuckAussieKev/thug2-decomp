// The collide-and-die component (GameObjects::ImpactDestructModule): see collide_and_die.h. From the disassembly of
// 0x0041C1F0-0x0041C707. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "engine/objects/collide_and_die.h"
#include "engine/objects/projectile_components.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/render/probe_ray.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/suspend_component.h"

namespace objects {

online::Hub* g_online_hub;

namespace {

constexpr uint32_t kRadius = 0xC48391A5;          // radius
constexpr uint32_t kScale = 0x13B9DA7B;           // scale
constexpr uint32_t kOnDestroyScript = 0x6647ADC3; // death_script
constexpr uint32_t kVel = 0x0C4C809E;             // vel
constexpr uint32_t kPos = 0x7F261953;             // pos
constexpr uint32_t kSuspend = 0xCE0CA665;
constexpr uint32_t kProjectileCollision = 0x7767D6D7;

constexpr uint32_t kLifetime = 10000; // milliseconds
constexpr uint32_t kDyingTime = 1000; // milliseconds after the hit
constexpr float kAbove = 24.0f;       // 0x00645EE8

// The object dies.
void get_kill(GameEntity* object) { object->flag_as_dead(); }

// The projectile collision component's 0x004F2F40 (no source yet): told of the hit.
void projectile_hit(ObjectModule* projectile) { static_cast<ShotImpactModule*>(projectile)->mark_hit(); }

} // namespace

// 0x0041C370
memory::owner<ObjectModule*> ImpactDestructModule::fabricate() { return new ImpactDestructModule; }

// 0x0041C210
ImpactDestructModule::ImpactDestructModule()
{
    kind_tag_ = kType;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    made_time_ = static_cast<uint32_t>(frame_clock::game_clock_elapsed() + g_clock_offset);
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x0041C1F0 (0x0041C280 the deleting one)
ImpactDestructModule::~ImpactDestructModule() = default;

// 0x0041C2B0
void ImpactDestructModule::configure_from_data(script::Struct* params)
{
    params->fetch_float(kRadius, &radius_, 1);
    params->fetch_float(kScale, &size_factor_, 1);
    params->read_hash_value(kOnDestroyScript, &on_collide_script_, 1);
    vel_ = {0.0f, 0.0f, 1.0f, 1.0f};
    params->fetch_vec(kVel, reinterpret_cast<script::VectorValue*>(&vel_), 1);
    vel_.make_unit();
}

// 0x0041C330
void ImpactDestructModule::seal() { suspend_ = reinterpret_cast<uint8_t*>(entity_->fetch_module(kSuspend)); }

// 0x0041C3D0
void ImpactDestructModule::tick_frame()
{
    if (!g_online_hub->in_net_game() && reinterpret_cast<DormancyModule*>(suspend_)->far_)
        return;
    int& offset = g_clock_offset;
    if (static_cast<uint32_t>(frame_clock::game_clock_elapsed()) + (static_cast<uint32_t>(offset) - made_time_) >
        kLifetime) {
        get_kill(entity_);
    } else if (fading_out_) {
        if (static_cast<uint32_t>(frame_clock::game_clock_elapsed() + offset) > death_time_)
            get_kill(entity_);
    } else {
        render::ProbeRay feeler;
        GameEntity* object = entity_;
        maths::Vec4 direction = object->vel_;
        direction.make_unit();
        // From where it was (24 up)...
        maths::Vec4 start = first_update_ ? object->at_point_ : last_pos_;
        start.y = start.y + kAbove;
        // ...to where it is (24 up), `radius` on along its velocity.
        double up = static_cast<double>(object->at_point_.y) + kAbove;
        float along_w = radius_ * direction.w;
        float along_z = radius_ * direction.z;
        float along_y = radius_ * direction.y;
        maths::Vec4 end{};
        end.x = static_cast<float>(static_cast<double>(radius_) * direction.x + object->at_point_.x);
        end.y = static_cast<float>(up + along_y);
        end.z = object->at_point_.z + along_z;
        end.w = object->at_point_.w + along_w;
        feeler.change_origin(&start);
        feeler.change_finish(&end);
        if (feeler.get_hit_shape(true, false) && !fading_out_) {
            if (ObjectModule* projectile = entity_->fetch_module(kProjectileCollision))
                projectile_hit(projectile);
            fading_out_ = true;
            death_time_ =
                static_cast<uint32_t>(frame_clock::game_clock_elapsed() + offset + static_cast<int>(kDyingTime));
            if (on_collide_script_) {
                maths::Vec4 point = feeler.contact_pos_;
                script::Struct* params = script::spawn_param_bag();
                if (params)
                    new (params) script::Struct;
                params->put_vec3(kPos, *reinterpret_cast<script::VectorValue*>(&point));
                params->put_vec3(kVel, *reinterpret_cast<script::VectorValue*>(&vel_));
                params->push_float(kScale, size_factor_);
                script::launch_script(on_collide_script_, params, nullptr, false, nullptr);
                if (params) {
                    params->get_clear(); // (the destructor)
                    script::delete_struct(params);
                }
            }
        }
    } // (the feeler's destructor lets go of the object it hit)
    last_pos_ = entity_->at_point_;
    first_update_ = false;
}

} // namespace objects
