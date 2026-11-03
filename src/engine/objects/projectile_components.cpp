// The projectile components (GameObjects::ShotImpactModule, GameObjects::ProjectileProximityComponent): see
// projectile_components.h. From the disassembly of 0x004F2F10-0x004F37AD (0x004F2F20 isn't a Ghidra function). The
// x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the FPU stack are
// worked in double.

#include "engine/objects/projectile_components.h"

#include <cmath>
#include <cstdint>
#include <gsl/pointers>
#include <array>

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
#include "wire/play_messages.h"
#include "formats/byte_stream.h"
#include "engine/core/down_cast.h"
#include "engine/net/net_game_state.h"
#include "wire/message_ids.h"

namespace objects {

namespace {
constexpr int kScorePerSize = 10;

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

constexpr float kReach = 48.0F;    // 0x006461CC: added to the radius
constexpr float kAbovePed = 30.0F; // 0x00646240: the ped's middle, above its position

// The network message a hit sends (0x84 to the hit skater's player, 0x85 to the owner's: wire/play_messages.h).
using online::MessageId;


// The skate module's local skater (0x00566520, a register stub on our side).
Rider* local_skater() { return modules::Skate::sole()->get_home_rider(); }

// The object dies.
void get_kill(GameEntity* object) { object->flag_as_dead(); }

modules::PointTally* get_score_of(GameEntity* skater)
{
    return (core::down_cast<RiderPointKeeper>(skater->fetch_module(kSkaterScore))->points_held());
}

} // namespace

// --- the projectile collision component ---

// 0x004F3010
gsl::owner<ObjectModule*> ShotImpactModule::fabricate() { return new ShotImpactModule; }

// (the constructor: inlined in fabricate in the original)
ShotImpactModule::ShotImpactModule() { set_kind_tag(kType); }

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
    vel_ = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 1.0F};
    vel_.make_unit();
}

// 0x004F2F40
void ShotImpactModule::seal() { suspend_ = core::down_cast<DormancyModule>(entity()->fetch_module(DormancyModule::kType)); }

void ShotImpactModule::mark_hit() { hit_ = true; }

// 0x004F2F20
int ShotImpactModule::score_taken() const
{
    if (projectile_type_ != kFireball)
        return 0;
    return static_cast<int>(size_factor_) * kScorePerSize;
}

// 0x004F3090
void ShotImpactModule::tick_frame()
{
    online::Hub* net = online::online_hub();
    if (net->in_net_game() && projectile_type_ != kFireball && !net->players_can_collide())
        return;
    auto reach = static_cast<float>(static_cast<double>(radius_) + kReach);
    if (suspend_->is_far() || hit_)
        return;
    modules::Skate* skate = modules::Skate::sole();
    auto count = static_cast<uint32_t>(skate->count_riders());
    const bool team_game = goals::game_mode()->is_squad_match() != 0;
    for (uint32_t i = 0; i < count; ++i) {
        GameEntity* skater = skate->get_rider_at(static_cast<int>(i));
        if (skater->ident() == holder_key_)
            continue;
        auto* owner =
            (net->find_player_slot_by_obj(static_cast<uint16_t>(holder_key_)));
        auto* victim = (net->find_player_slot_by_obj(static_cast<uint16_t>(skater->ident())));
        if (!victim->can_be_hit_again())
            continue;
        modules::PointTally* score = get_score_of(skater);
        if (projectile_type_ == kFireball && score->get_total_score() <= 0)
            continue;
        if (team_game && owner->team() == victim->team() &&
            net->get_online_settings()->get_preference_checksum({.preference = kFriendlyFire, .field = kChecksum}) != kBooleanTrue)
            continue;
        const GameEntity* object = entity();
        const double dz = static_cast<double>(skater->at_point().z) - object->at_point().z;
        const double dy = static_cast<double>(skater->at_point().y) - object->at_point().y;
        const double dx = static_cast<double>(skater->at_point().x) - object->at_point().x;
        if (!(std::sqrt((dx * dx) + (dy * dy) + (dz * dz)) < reach))
            continue;

        // Hit.
        net::NetMessageInfo msg;
        net::AppHost* server = net->host();
        get_kill(entity());
        if (on_collide_script_) {
            const maths::Vec4 pos = entity()->at_point();
            const script::StructPtr params = script::make_struct();
            params->put_vec3(checksums::Name{kPos}, script::VectorValue{.x = pos.x, .y = pos.y, .z = pos.z, .w = pos.w});
            params->put_vec3(checksums::Name{kVel}, script::VectorValue{.x = vel_.x, .y = vel_.y, .z = vel_.z, .w = vel_.w});
            params->push_float(checksums::Name{kScale}, size_factor_);
            script::launch_script(on_collide_script_, params.get(), nullptr, false, nullptr);
        }
        // (The original builds it over its loop counter's slot: the two bytes after the flag are the count's high
        // bytes, 0.)
        online::Hit hit; // (the owner's object id / the hit skater's (low byte); what the hit takes: score_taken)
        hit.object_id = static_cast<uint8_t>(holder_key_);
        hit.score = projectile_type_ == kFireball ? static_cast<int>(size_factor_) * kScorePerSize : 0;
        hit.fireball = projectile_type_ == kFireball;
        std::array<uint8_t, online::kHitSize> bytes{};
        util::ByteWriter writer(bytes.data(), sizeof(bytes));
        online::write_hit(writer, hit);
        msg.set_ident(MessageId::kHit);
        msg.set_content(bytes.data());
        msg.set_byte_count(sizeof(bytes));
        msg.set_queue(net::NetMessageInfo::kLaneOrdered);
        msg.set_group_id(online::MessageGroup::kGame);
        victim->change_recent_hit_stamp(frame_clock::get_time());
        server->put_on_outbox(victim->get_link_handle(), &msg);
        hit.object_id = static_cast<uint8_t>(skater->ident());
        util::ByteWriter owner_writer(bytes.data(), sizeof(bytes));
        online::write_hit(owner_writer, hit);
        msg.set_ident(MessageId::kHitOwner);
        server->put_on_outbox(owner->get_link_handle(), &msg);
        if (projectile_type_ != kFireball)
            return;
        // A fireball takes from the hit skater's score; with none left the skater is out.
        if (score->get_total_score() - score_taken() > 0) {
            score->set_grand_total(score->get_total_score() - score_taken());
            return;
        }
        if (score->get_total_score() > 0 && !victim->is_own_player()) {
            script::StructPtr params = script::make_struct();
            params->append_word(kName, core::down_cast<Rider>(skater)->fetch_shown_name());
            script::launch_script(kAnnounceElimination, params.get(), nullptr, false, nullptr);
            params.reset();
            modules::Skate::conceal_skater(core::down_cast<Rider>(skater), true);
        }
        score->set_grand_total(0);
        return;
    }
}

// --- the projectile proximity component ---

// 0x004F3550
gsl::owner<ObjectModule*> ProjectileProximityComponent::fabricate() { return new ProjectileProximityComponent; }

// (the constructor: inlined in fabricate in the original)
ProjectileProximityComponent::ProjectileProximityComponent() { set_kind_tag(kType); }

// 0x004F34E0 (0x004F34F0 the deleting one)
ProjectileProximityComponent::~ProjectileProximityComponent() = default;

// 0x004F3520
void ProjectileProximityComponent::configure_from_data(script::Struct* /*params*/)
{
    rider_ref_ = local_skater();
    ped_logic_ = core::down_cast<PedestrianBrainModule>(entity()->fetch_module(kPedLogic));
    reach_squared_ = kReach * kReach;
}

// 0x004F35B0
void ProjectileProximityComponent::tick_frame()
{
    if (!rider_ref_) {
        rider_ref_ = local_skater();
        return;
    }
    if (!entity()->ident())
        return;
    if (!ped_logic_) {
        ped_logic_ = core::down_cast<PedestrianBrainModule>(entity()->fetch_module(kPedLogic));
        return;
    }
    if (ped_logic_->get_logic_state() == kShot)
        return;
    for (auto& projectile : rider_ref_->projectiles()) {
        if (!projectile.active)
            continue;
        const auto& pos = projectile.pos;
        const maths::Vec4& at = entity()->at_point();
        const double middle = static_cast<double>(at.y) + kAbovePed;
        auto dz = static_cast<float>(static_cast<double>(pos[2]) - at.z);
        const double dy = pos[1] - middle;
        const double dx = static_cast<double>(pos[0]) - at.x;
        if (!((dx * dx) + (dy * dy) + (static_cast<double>(dz) * dz) <= reach_squared_))
            continue;
        // In reach: the ped is hit.
        entity()->post_own_event(kProjectileInAvoidRadius, nullptr);
        script::StructPtr params = script::make_struct();
        params->put_name_key(checksums::Name{kPedId}, entity()->ident());
        params->put_name_key(checksums::Name{kProjectileId}, projectile.name);
        script::launch_script(kProjectileHitPed, params.get(), find_object(rider_ref_->ident()), false, nullptr);
        params->put_name_key(checksums::Name{kAmmoType},
            core::down_cast<skater::SkaterPhysicsModule>(rider_ref_->fetch_module(kSkaterCorePhysics))->ammo_type());
        script::launch_script(kHitPedScript, params.get(), entity(), false, nullptr);
        params.reset();
        projectile.active = false;
        return;
    }
}

} // namespace objects
