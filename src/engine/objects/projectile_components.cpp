// The projectile components (GameObjects::ShotImpactModule, GameObjects::ProjectileProximityComponent): see
// projectile_components.h. From the disassembly of 0x004F2F10-0x004F37AD (0x004F2F20 isn't a Ghidra function). The
// x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the FPU stack are
// worked in double.

#include "engine/objects/projectile_components.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/net/buddy_status.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/player_info.h"
#include "engine/objects/entity.h"
#include "engine/objects/ped_logic.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/rider/skater_score.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/objects/collide_and_die.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/node_components.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

extern void** const kCollisionVtable;
extern void** const kProximityVtable;

constexpr uint32_t kRadius = 0xC48391A5;          // radius
constexpr uint32_t kScale = 0x13B9DA7B;           // scale
constexpr uint32_t kOwnerId = 0x81C39E06;         // owner_id
constexpr uint32_t kOnDestroyScript = 0x6647ADC3;     // death_script
constexpr uint32_t kProjectileType = 0xB4D91E49;  // projectile_type
constexpr uint32_t kPos = 0x7F261953;             // Pos
constexpr uint32_t kVel = 0x0C4C809E;             // Vel
constexpr uint32_t kSkaterScore = 0xD262802F;     // SkaterScore
constexpr uint32_t kFriendlyFire = 0xE959C43A;    // friendly_fire
constexpr uint32_t kChecksum = 0x21902065;        // checksum
constexpr uint32_t kBooleanTrue = 0xF81BC89B;     // boolean_true
constexpr uint32_t kName = 0xA1DC81F9;            // Name
constexpr uint32_t kAnnounceElimination = 0x9B043179; // announce_elimination
constexpr uint32_t kPedLogic = 0x6EFDDFB8;
constexpr uint32_t kShot = 0x54F87744;            // shot (a PedLogic state)
constexpr uint32_t kProjectileInAvoidRadius = 0x261029C1;
constexpr uint32_t kPedId = 0x3989D7D6;           // Ped_ID
constexpr uint32_t kProjectileId = 0x31916493;    // projectile_id
constexpr uint32_t kProjectileHitPed = 0x9B1FFB83; // projectile_hit_ped
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kAmmoType = 0x1D1A8B59;        // ammo_type
constexpr uint32_t kHitPedScript = 0x186E040D;    // hit_ped_script

constexpr float kReach = 48.0f;     // 0x006461CC: added to the radius
constexpr float kAbovePed = 30.0f;  // 0x00646240: the ped's middle, above its position

// The network message a hit sends (0x84 to the hit skater's player, 0x85 to the owner's).
constexpr uint8_t kMsgHit = 0x84, kMsgHitOwner = 0x85;
struct HitMessage {
    uint8_t id;       // the owner's object id / the hit skater's (low byte)
    bool fireball;
    uint8_t pad[2];
    int score;        // what the hit takes (score_taken)
};
static_assert(sizeof(HitMessage) == 8, "the message is 8 bytes");

online::Hub* net_session_slot() { return online::g_online_hub; }
modules::Skate* skate_module() { return modules::g_skate_module; }

// The skate module's local skater (0x00566520, a register stub on our side).
Rider* local_skater()
{
    return skate_module()->get_home_rider();
}

// The object dies (its vtable slot 6, flag_as_dead).
void get_kill(GameEntity* object) { reinterpret_cast<void(__thiscall*)(GameEntity*)>(object->vtable_[6])(object); }

script::Struct* new_params()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory->prepare_all() : nullptr;
}
void delete_params(script::Struct* params)
{
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

modules::PointTally* get_score_of(GameEntity* skater)
{
    return reinterpret_cast<modules::PointTally*>(static_cast<RiderPointKeeper*>(skater->fetch_module(kSkaterScore))->points_held_);
}

} // namespace

// --- the projectile collision component ---

// 0x004F3010
ObjectModule* __cdecl ShotImpactModule::fabricate()
{
    auto* component = static_cast<ShotImpactModule*>(memory::alloc_zeroed(sizeof(ShotImpactModule)));
    if (!component)
        return nullptr;
    component->ObjectModule::make_new();
    component->radius_ = 0.0f;
    component->holder_key_ = 0;
    component->on_collide_script_ = 0;
    component->size_factor_ = 0.0f;
    component->hit_ = false;
    component->vtable_ = kCollisionVtable;
    component->ObjectModule::kind_tag_ = kType;
    return component;
}

// 0x004F2F10
void ShotImpactModule::destroy_body()
{
    vtable_ = kCollisionVtable;
    ObjectModule::destroy_body();
}

// 0x004F2F50
ShotImpactModule* ShotImpactModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004F2F80
void ShotImpactModule::configure_from_data(script::Struct* params)
{
    params->fetch_float(kRadius, &radius_, 1);
    params->fetch_float(kScale, &size_factor_, 1);
    params->read_hash_value(kOwnerId, &holder_key_, 1);
    params->read_hash_value(kOnDestroyScript, &on_collide_script_, 1);
    params->read_hash_value(kProjectileType, &projectile_type_, 1);
    vel_ = {0.0f, 0.0f, 1.0f, 1.0f};
    vel_.make_unit();
}

// 0x004F2F40
void ShotImpactModule::mark_hit() { hit_ = true; }

// 0x004F2F20
int ShotImpactModule::score_taken()
{
    if (projectile_type_ != kFireball)
        return 0;
    return static_cast<int>(size_factor_) * 10;
}

// 0x004F3090
void ShotImpactModule::tick_frame()
{
    online::Hub* net = net_session_slot();
    if (net->in_net_game() && projectile_type_ != kFireball && !net->players_can_collide())
        return;
    float reach = static_cast<float>(static_cast<double>(radius_) + kReach);
    if (reinterpret_cast<DormancyModule*>(suspend_)->is_far() || hit_)
        return;
    modules::Skate* skate = skate_module();
    uint32_t count = static_cast<uint32_t>(skate->count_riders());
    bool team_game = goals::g_game_mode->is_squad_match() != 0;
    for (uint32_t i = 0; i < count; ++i) {
        GameEntity* skater = skate->get_rider_at(static_cast<int>(i));
        if (skater->ident_ == holder_key_)
            continue;
        auto* owner = reinterpret_cast<online::PlayerRecord*>(net->find_player_slot_by_obj(static_cast<uint16_t>(holder_key_)));
        auto* victim = reinterpret_cast<online::PlayerRecord*>(net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_)));
        if (!victim->can_be_hit_again())
            continue;
        modules::PointTally* score = get_score_of(skater);
        if (projectile_type_ == kFireball && score->get_total_score() <= 0)
            continue;
        if (team_game && *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(owner) + 0x30) ==
                            *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(victim) + 0x30) &&
            net->get_online_settings()->get_preference_checksum(kFriendlyFire, kChecksum) != kBooleanTrue)
            continue;
        GameEntity* object = entity_;
        double dz = static_cast<double>(skater->at_point_.z) - object->at_point_.z;
        double dy = static_cast<double>(skater->at_point_.y) - object->at_point_.y;
        double dx = static_cast<double>(skater->at_point_.x) - object->at_point_.x;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) < reach))
            continue;

        // Hit.
        net::NetMessageInfo msg;
        msg.make_new();
        auto* server = reinterpret_cast<net::AppHost*>(reinterpret_cast<online::FoldedGetters*>(net)->word04());
        get_kill(entity_);
        if (on_collide_script_) {
            maths::Vec4 pos = entity_->at_point_;
            script::Struct* params = new_params();
            params->put_vec3(kPos, script::VectorValue{pos.x, pos.y, pos.z, pos.w});
            params->put_vec3(kVel, script::VectorValue{vel_.x, vel_.y, vel_.z, vel_.w});
            params->push_float(kScale, size_factor_);
            script::launch_script(on_collide_script_, params, nullptr, false, nullptr);
            delete_params(params);
        }
        // (The original builds it over its loop counter's slot: the two bytes after the flag are the count's high
        // bytes, 0.)
        HitMessage hit = {};
        hit.id = static_cast<uint8_t>(holder_key_);
        hit.score = projectile_type_ == kFireball ? static_cast<int>(size_factor_) * 10 : 0;
        hit.fireball = projectile_type_ == kFireball;
        msg.ident_ = kMsgHit;
        msg.content_ = &hit;
        msg.byte_count_ = sizeof(hit);
        msg.queue_ = net::NetMessageInfo::kLaneOrdered;
        msg.group_id_ = 8;
        victim->change_recent_hit_stamp(frame_clock::get_time());
        server->put_on_outbox(victim->get_link_handle(), &msg);
        hit.id = static_cast<uint8_t>(skater->ident_);
        msg.ident_ = kMsgHitOwner;
        server->put_on_outbox(owner->get_link_handle(), &msg);
        if (projectile_type_ != kFireball)
            return;
        // A fireball takes from the hit skater's score; with none left the skater is out.
        if (score->get_total_score() - score_taken() > 0) {
            score->set_grand_total(score->get_total_score() - score_taken());
            return;
        }
        if (score->get_total_score() > 0 && !victim->is_own_player()) {
            script::Struct* params = new_params();
            params->append_word(kName, static_cast<Rider*>(skater)->fetch_shown_name());
            script::launch_script(kAnnounceElimination, params, nullptr, false, nullptr);
            delete_params(params);
            skate->conceal_skater(static_cast<Rider*>(skater), true);
        }
        score->set_grand_total(0);
        return;
    }
}

// --- the projectile proximity component ---

// 0x004F3550
ObjectModule* __cdecl ProjectileProximityComponent::fabricate()
{
    auto* component = static_cast<ProjectileProximityComponent*>(memory::alloc_zeroed(sizeof(ProjectileProximityComponent)));
    if (!component)
        return nullptr;
    component->ObjectModule::make_new();
    component->vtable_ = kProximityVtable;
    component->kind_tag_ = kType;
    return component;
}

// 0x004F34E0
void ProjectileProximityComponent::destroy_body()
{
    vtable_ = kProximityVtable;
    ObjectModule::destroy_body();
}

// 0x004F34F0
ProjectileProximityComponent* ProjectileProximityComponent::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004F3520
void ProjectileProximityComponent::configure_from_data(script::Struct*)
{
    rider_ref_ = local_skater();
    ped_logic_ = static_cast<PedestrianBrainModule*>(entity_->fetch_module(kPedLogic));
    reach_squared_ = kReach * kReach;
}

// 0x004F35B0
void ProjectileProximityComponent::tick_frame()
{
    if (!rider_ref_) {
        rider_ref_ = local_skater();
        return;
    }
    if (!entity_->ident_)
        return;
    if (!ped_logic_) {
        ped_logic_ = static_cast<PedestrianBrainModule*>(entity_->fetch_module(kPedLogic));
        return;
    }
    if (ped_logic_->get_logic_state() == kShot)
        return;
    for (int i = 0; i < 5; ++i) {
        Rider::Projectile& projectile = rider_ref_->projectiles_[i];
        if (!projectile.active)
            continue;
        const auto* pos = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(&projectile) + 8);
        const maths::Vec4& at = entity_->at_point_;
        double middle = static_cast<double>(at.y) + kAbovePed;
        float dz = static_cast<float>(static_cast<double>(pos[2]) - at.z);
        double dy = pos[1] - middle;
        double dx = static_cast<double>(pos[0]) - at.x;
        if (!(dx * dx + dy * dy + static_cast<double>(dz) * dz <= reach_squared_))
            continue;
        // In reach: the ped is hit.
        entity_->post_own_event(kProjectileInAvoidRadius, nullptr);
        script::Struct* params = new_params();
        params->put_name_key(kPedId, entity_->ident_);
        params->put_name_key(kProjectileId, projectile.name);
        script::launch_script(kProjectileHitPed, params, find_object(rider_ref_->ident_), false, nullptr);
        params->put_name_key(kAmmoType,
                            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(
                                                             rider_ref_->fetch_module(kSkaterCorePhysics)) +
                                                         0x2FC));
        script::launch_script(kHitPedScript, params, entity_, false, nullptr);
        delete_params(params);
        projectile.active = false;
        return;
    }
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_collision_vtable_slots[] = {&objects::ShotImpactModule::teardown,
         &objects::ShotImpactModule::tick_frame,
         &objects::ShotImpactModule::configure_from_data,
         &objects::ImpactDestructModule::seal,
         stubs::do_nothing,
         stubs::ignore_argument,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         stubs::do_nothing,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name};
void** const kCollisionVtable = reinterpret_cast<void**>(g_collision_vtable_slots);
static VtableSlot g_proximity_vtable_slots[] = {&objects::ProjectileProximityComponent::teardown,
         &objects::ProjectileProximityComponent::tick_frame,
         &objects::ProjectileProximityComponent::configure_from_data,
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
void** const kProximityVtable = reinterpret_cast<void**>(g_proximity_vtable_slots);

} // namespace
} // namespace Obj
