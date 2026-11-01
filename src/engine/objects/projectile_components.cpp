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
#include "engine/objects/collide_and_die.h"
#include "engine/objects/car_physics.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/node_components.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/core_physics.h"
#include "engine/net/play_messages.h"
#include "engine/util/byte_stream.h"

namespace objects {

namespace {

constexpr uint32_t kRadius = 0xC48391A5;              // radius
constexpr uint32_t kScale = 0x13B9DA7B;               // scale
constexpr uint32_t kOwnerId = 0x81C39E06;             // owner_id
constexpr uint32_t kOnDestroyScript = 0x6647ADC3;     // death_script
constexpr uint32_t kProjectileType = 0xB4D91E49;      // projectile_type
constexpr uint32_t kPos = 0x7F261953;                 // Pos
constexpr uint32_t kVel = 0x0C4C809E;                 // Vel
constexpr uint32_t kSkaterScore = 0xD262802F;         // SkaterScore
constexpr uint32_t kFriendlyFire = 0xE959C43A;        // friendly_fire
constexpr uint32_t kChecksum = 0x21902065;            // checksum
constexpr uint32_t kBooleanTrue = 0xF81BC89B;         // boolean_true
constexpr uint32_t kName = 0xA1DC81F9;                // Name
constexpr uint32_t kAnnounceElimination = 0x9B043179; // announce_elimination
constexpr uint32_t kPedLogic = 0x6EFDDFB8;
constexpr uint32_t kShot = 0x54F87744; // shot (a PedLogic state)
constexpr uint32_t kProjectileInAvoidRadius = 0x261029C1;
constexpr uint32_t kPedId = 0x3989D7D6;            // Ped_ID
constexpr uint32_t kProjectileId = 0x31916493;     // projectile_id
constexpr uint32_t kProjectileHitPed = 0x9B1FFB83; // projectile_hit_ped
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kAmmoType = 0x1D1A8B59;     // ammo_type
constexpr uint32_t kHitPedScript = 0x186E040D; // hit_ped_script

constexpr float kReach = 48.0f;    // 0x006461CC: added to the radius
constexpr float kAbovePed = 30.0f; // 0x00646240: the ped's middle, above its position

// The network message a hit sends (0x84 to the hit skater's player, 0x85 to the owner's: engine/net/play_messages.h).
using online::kMsgHit;
using online::kMsgHitOwner;

online::Hub* net_session_slot() { return online::g_online_hub; }
modules::Skate* skate_module() { return modules::g_skate_module; }

// The skate module's local skater (0x00566520, a register stub on our side).
Rider* local_skater() { return skate_module()->get_home_rider(); }

// The object dies.
void get_kill(GameEntity* object) { object->flag_as_dead(); }

script::Struct* new_params()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
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
    return reinterpret_cast<modules::PointTally*>(
        static_cast<RiderPointKeeper*>(skater->fetch_module(kSkaterScore))->points_held_);
}

} // namespace

// --- the projectile collision component ---

// 0x004F3010
memory::owner<ObjectModule*> ShotImpactModule::fabricate() { return new ShotImpactModule; }

// (the constructor: inlined in fabricate in the original)
ShotImpactModule::ShotImpactModule() { kind_tag_ = kType; }

// 0x004F2F10 (0x004F2F50 the deleting one)
ShotImpactModule::~ShotImpactModule() = default;

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
    auto reach = static_cast<float>(static_cast<double>(radius_) + kReach);
    if (reinterpret_cast<DormancyModule*>(suspend_)->is_far() || hit_)
        return;
    modules::Skate* skate = skate_module();
    auto count = static_cast<uint32_t>(skate->count_riders());
    const bool team_game = goals::g_game_mode->is_squad_match() != 0;
    for (uint32_t i = 0; i < count; ++i) {
        GameEntity* skater = skate->get_rider_at(static_cast<int>(i));
        if (skater->ident_ == holder_key_)
            continue;
        auto* owner =
            reinterpret_cast<online::PlayerRecord*>(net->find_player_slot_by_obj(static_cast<uint16_t>(holder_key_)));
        auto* victim = reinterpret_cast<online::PlayerRecord*>(
            net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident_)));
        if (!victim->can_be_hit_again())
            continue;
        modules::PointTally* score = get_score_of(skater);
        if (projectile_type_ == kFireball && score->get_total_score() <= 0)
            continue;
        if (team_game && owner->team_ == victim->team_ &&
            net->get_online_settings()->get_preference_checksum(kFriendlyFire, kChecksum) != kBooleanTrue)
            continue;
        const GameEntity* object = entity_;
        const double dz = static_cast<double>(skater->at_point_.z) - object->at_point_.z;
        const double dy = static_cast<double>(skater->at_point_.y) - object->at_point_.y;
        const double dx = static_cast<double>(skater->at_point_.x) - object->at_point_.x;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) < reach))
            continue;

        // Hit.
        net::NetMessageInfo msg;
        net::AppHost* server = static_cast<online::Hub*>(net)->host_;
        get_kill(entity_);
        if (on_collide_script_) {
            const maths::Vec4 pos = entity_->at_point_;
            script::Struct* params = new_params();
            params->put_vec3(kPos, script::VectorValue{pos.x, pos.y, pos.z, pos.w});
            params->put_vec3(kVel, script::VectorValue{vel_.x, vel_.y, vel_.z, vel_.w});
            params->push_float(kScale, size_factor_);
            script::launch_script(on_collide_script_, params, nullptr, false, nullptr);
            delete_params(params);
        }
        // (The original builds it over its loop counter's slot: the two bytes after the flag are the count's high
        // bytes, 0.)
        online::Hit hit; // (the owner's object id / the hit skater's (low byte); what the hit takes: score_taken)
        hit.object_id = static_cast<uint8_t>(holder_key_);
        hit.score = projectile_type_ == kFireball ? static_cast<int>(size_factor_) * 10 : 0;
        hit.fireball = projectile_type_ == kFireball;
        uint8_t bytes[online::kHitSize];
        util::ByteWriter writer(bytes, sizeof(bytes));
        online::write_hit(writer, hit);
        msg.ident_ = kMsgHit;
        msg.content_ = bytes;
        msg.byte_count_ = sizeof(bytes);
        msg.queue_ = net::NetMessageInfo::kLaneOrdered;
        msg.group_id_ = 8;
        victim->change_recent_hit_stamp(frame_clock::get_time());
        server->put_on_outbox(victim->get_link_handle(), &msg);
        hit.object_id = static_cast<uint8_t>(skater->ident_);
        util::ByteWriter owner_writer(bytes, sizeof(bytes));
        online::write_hit(owner_writer, hit);
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
memory::owner<ObjectModule*> ProjectileProximityComponent::fabricate() { return new ProjectileProximityComponent; }

// (the constructor: inlined in fabricate in the original)
ProjectileProximityComponent::ProjectileProximityComponent() { kind_tag_ = kType; }

// 0x004F34E0 (0x004F34F0 the deleting one)
ProjectileProximityComponent::~ProjectileProximityComponent() = default;

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
    for (auto& projectile : rider_ref_->projectiles_) {
        if (!projectile.active)
            continue;
        const float* pos = &projectile.pos.x;
        const maths::Vec4& at = entity_->at_point_;
        const double middle = static_cast<double>(at.y) + kAbovePed;
        auto dz = static_cast<float>(static_cast<double>(pos[2]) - at.z);
        const double dy = pos[1] - middle;
        const double dx = static_cast<double>(pos[0]) - at.x;
        if (!(dx * dx + dy * dy + static_cast<double>(dz) * dz <= reach_squared_))
            continue;
        // In reach: the ped is hit.
        entity_->post_own_event(kProjectileInAvoidRadius, nullptr);
        script::Struct* params = new_params();
        params->put_name_key(kPedId, entity_->ident_);
        params->put_name_key(kProjectileId, projectile.name);
        script::launch_script(kProjectileHitPed, params, find_object(rider_ref_->ident_), false, nullptr);
        params->put_name_key(
            kAmmoType,
            static_cast<skater::SkaterPhysicsModule*>(rider_ref_->fetch_module(kSkaterCorePhysics))->ammo_type_);
        script::launch_script(kHitPedScript, params, entity_, false, nullptr);
        delete_params(params);
        projectile.active = false;
        return;
    }
}

} // namespace objects
