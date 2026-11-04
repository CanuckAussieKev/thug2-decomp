// The collide-and-die component (GameObjects::ImpactDestructModule): see collide_and_die.h. From the disassembly of
// 0x0041C1F0-0x0041C707. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "engine/objects/collide_and_die.h"
#include "engine/objects/game_hooks.h"

#include <cstdint>
#include <gsl/pointers>

#include "engine/net/session_query.h"
#include "engine/render/probe_ray.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sys/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"
#include "engine/script/fwd.h"
#include "engine/objects/suspend_component.h"
#include "base/core/ints.h"
#include "base/core/down_cast.h"

namespace objects {

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
constexpr float kAbove = 24.0F;       // 0x00645EE8

// The object dies.
void get_kill(GameEntity* object) { object->flag_as_dead(); }

} // namespace

// 0x0041C370
gsl::owner<ObjectModule*> ImpactDestructModule::fabricate() { return new ImpactDestructModule; }

// 0x0041C210
ImpactDestructModule::ImpactDestructModule()
    : made_time_(static_cast<uint32_t>(frame_clock::game_clock_elapsed() + clock_offset()))
{
    set_kind_tag(kType);
}

// 0x0041C1F0 (0x0041C280 the deleting one)
ImpactDestructModule::~ImpactDestructModule() = default;

// 0x0041C2B0
void ImpactDestructModule::configure_from_data(script::Struct* params)
{
    params->fetch_float(kRadius, &radius_, 1);
    params->fetch_float(kScale, &size_factor_, 1);
    params->read_hash_value(kOnDestroyScript, &on_collide_script_, 1);
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 1.0F};
    params->fetch_vec(kVel, &vel_, 1);
    vel_.make_unit();
}

// 0x0041C330
void ImpactDestructModule::seal() { suspend_ = core::down_cast<DormancyModule>(entity()->fetch_module(kSuspend)); }

// 0x0041C3D0
void ImpactDestructModule::tick_frame()
{
    if (!net::in_net_game() && suspend_->far()) {
        return;
    }
    const int offset = clock_offset();
    if (static_cast<uint32_t>(frame_clock::game_clock_elapsed()) + (static_cast<uint32_t>(offset) - made_time_) >
        kLifetime) {
        get_kill(entity());
    } else if (fading_out_) {
        if (ints::as_unsigned(frame_clock::game_clock_elapsed() + offset) > death_time_) {
            get_kill(entity());
        }
    } else {
        render::ProbeRay feeler;
        const GameEntity* object = entity();
        maths::Vec4 direction = object->vel();
        direction.make_unit();
        // From where it was (24 up)...
        maths::Vec4 start = first_update_ ? object->at_point() : last_pos_;
        start.y = start.y + kAbove;
        // ...to where it is (24 up), `radius` on along its velocity.
        const double up = static_cast<double>(object->at_point().y) + kAbove;
        const float along_w = radius_ * direction.w;
        const float along_z = radius_ * direction.z;
        const float along_y = radius_ * direction.y;
        maths::Vec4 end{};
        end.x = static_cast<float>((static_cast<double>(radius_) * direction.x) + object->at_point().x);
        end.y = static_cast<float>(up + along_y);
        end.z = object->at_point().z + along_z;
        end.w = object->at_point().w + along_w;
        feeler.change_origin(&start);
        feeler.change_finish(&end);
        if ((feeler.get_hit_shape(true, false) != 0) && !fading_out_) {
            if (ObjectModule* projectile = entity()->fetch_module(kProjectileCollision)) {
                game_hooks().projectile_hit(projectile); // (the projectile collision component told)
            }
            fading_out_ = true;
            death_time_ =
                static_cast<uint32_t>(frame_clock::game_clock_elapsed() + offset + static_cast<int>(kDyingTime));
            if (on_collide_script_ != 0U) {
                maths::Vec4 point = feeler.contact_pos();
                gsl::owner<script::Struct*> params = script::spawn_param_bag();
                if (params) {
                    new (params) script::Struct;
                }
                params->put_vec3(checksums::Name{kPos}, *&point);
                params->put_vec3(checksums::Name{kVel}, *&vel_);
                params->push_float(checksums::Name{kScale}, size_factor_);
                script::launch_script(on_collide_script_, params, nullptr, false, nullptr);
                if (params) {
                    params->get_clear(); // (the destructor)
                    script::delete_struct(params);
                }
            }
        }
    } // (the feeler's destructor lets go of the object it hit)
    last_pos_ = entity()->at_point();
    first_update_ = false;
}

} // namespace objects
