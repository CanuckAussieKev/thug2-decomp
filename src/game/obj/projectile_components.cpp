// The projectile components (GameObjects::CShotImpactModule, GameObjects::CProjectileProximityComponent): see
// projectile_components.h. From the disassembly of 0x004F2F10-0x004F37AD (0x004F2F20 isn't a Ghidra function). The
// x87 roundings are kept: values the original stores are rounded to float, the ones it keeps on the FPU stack are
// worked in double.

#include "game/obj/projectile_components.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/buddy_status.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/obj/ped_logic.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skater/skater_score.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/obj/collide_and_die.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/node_components.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kCollisionVtable;
extern void** const kProximityVtable;

constexpr uint32_t kRadius = 0xC48391A5;          // radius
constexpr uint32_t kScale = 0x13B9DA7B;           // scale
constexpr uint32_t kOwnerId = 0x81C39E06;         // owner_id
constexpr uint32_t kDeathScript = 0x6647ADC3;     // death_script
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
    int score;        // what the hit takes (ScoreTaken)
};
static_assert(sizeof(HitMessage) == 8, "the message is 8 bytes");

OnlinePlay::Manager* NetManager() { return OnlinePlay::g_gameNetManager; }
GameModules::Skate* SkateModule() { return GameModules::g_skateModule; }

// The skate module's local skater (0x00566520, a register stub on our side).
CRider* LocalSkater()
{
    return SkateModule()->GetHomeRider();
}

// The object dies (its vtable slot 6, FlagAsDead).
void Kill(CGameEntity* object) { reinterpret_cast<void(__thiscall*)(CGameEntity*)>(object->m_vtable[6])(object); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

GameModules::Score* ScoreOf(CGameEntity* skater)
{
    return reinterpret_cast<GameModules::Score*>(static_cast<CRiderPointKeeper*>(skater->GetComponent(kSkaterScore))->m_pointsHeld);
}

} // namespace

// --- the projectile collision component ---

// 0x004F3010
CObjectModule* __cdecl CShotImpactModule::Create()
{
    auto* component = static_cast<CShotImpactModule*>(MemSys::AllocZeroed(sizeof(CShotImpactModule)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_radius = 0.0f;
    component->m_holderKey = 0;
    component->m_deathScript = 0;
    component->m_sizeFactor = 0.0f;
    component->m_hit = false;
    component->m_vtable = kCollisionVtable;
    component->CObjectModule::m_type = kType;
    return component;
}

// 0x004F2F10
void CShotImpactModule::DestroyBody()
{
    m_vtable = kCollisionVtable;
    CObjectModule::DestroyBody();
}

// 0x004F2F50
CShotImpactModule* CShotImpactModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004F2F80
void CShotImpactModule::ConfigureFromData(Scripts::CStruct* params)
{
    params->GetFloat(kRadius, &m_radius, 1);
    params->GetFloat(kScale, &m_sizeFactor, 1);
    params->ReadHashValue(kOwnerId, &m_holderKey, 1);
    params->ReadHashValue(kDeathScript, &m_deathScript, 1);
    params->ReadHashValue(kProjectileType, &m_projectileType, 1);
    m_vel = {0.0f, 0.0f, 1.0f, 1.0f};
    m_vel.Normalize();
}

// 0x004F2F40
void CShotImpactModule::MarkHit() { m_hit = true; }

// 0x004F2F20
int CShotImpactModule::ScoreTaken()
{
    if (m_projectileType != kFireball)
        return 0;
    return static_cast<int>(m_sizeFactor) * 10;
}

// 0x004F3090
void CShotImpactModule::Update()
{
    OnlinePlay::Manager* net = NetManager();
    if (net->InNetGame() && m_projectileType != kFireball && !net->PlayersCanCollide())
        return;
    float reach = static_cast<float>(static_cast<double>(m_radius) + kReach);
    if (reinterpret_cast<CDormancyModule*>(mp_suspend)->IsFar() || m_hit)
        return;
    GameModules::Skate* skate = SkateModule();
    uint32_t count = static_cast<uint32_t>(skate->CountRiders());
    bool teamGame = GoalSystem::g_gameMode->IsSquadMatch() != 0;
    for (uint32_t i = 0; i < count; ++i) {
        CGameEntity* skater = skate->GetRiderAt(static_cast<int>(i));
        if (skater->m_id == m_holderKey)
            continue;
        auto* owner = reinterpret_cast<OnlinePlay::PlayerRecord*>(net->PlayerByObjectId(static_cast<uint16_t>(m_holderKey)));
        auto* victim = reinterpret_cast<OnlinePlay::PlayerRecord*>(net->PlayerByObjectId(static_cast<uint16_t>(skater->m_id)));
        if (!victim->CanBeHitAgain())
            continue;
        GameModules::Score* score = ScoreOf(skater);
        if (m_projectileType == kFireball && score->GetTotalScore() <= 0)
            continue;
        if (teamGame && *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(owner) + 0x30) ==
                            *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(victim) + 0x30) &&
            net->GetOnlineSettings()->GetPreferenceChecksum(kFriendlyFire, kChecksum) != kBooleanTrue)
            continue;
        CGameEntity* object = m_object;
        double dz = static_cast<double>(skater->m_pos.z) - object->m_pos.z;
        double dy = static_cast<double>(skater->m_pos.y) - object->m_pos.y;
        double dx = static_cast<double>(skater->m_pos.x) - object->m_pos.x;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) < reach))
            continue;

        // Hit.
        NetLink::NetMessageInfo msg;
        msg.Construct();
        auto* server = reinterpret_cast<NetLink::App*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(net)->Word04());
        Kill(m_object);
        if (m_deathScript) {
            Maths::Vector pos = m_object->m_pos;
            Scripts::CStruct* params = NewParams();
            params->PutVec3(kPos, Scripts::VectorValue{pos.x, pos.y, pos.z, pos.w});
            params->PutVec3(kVel, Scripts::VectorValue{m_vel.x, m_vel.y, m_vel.z, m_vel.w});
            params->PushFloat(kScale, m_sizeFactor);
            Scripts::LaunchScript(m_deathScript, params, nullptr, false, nullptr);
            DeleteParams(params);
        }
        // (The original builds it over its loop counter's slot: the two bytes after the flag are the count's high
        // bytes, 0.)
        HitMessage hit = {};
        hit.id = static_cast<uint8_t>(m_holderKey);
        hit.score = m_projectileType == kFireball ? static_cast<int>(m_sizeFactor) * 10 : 0;
        hit.fireball = m_projectileType == kFireball;
        msg.m_id = kMsgHit;
        msg.m_data = &hit;
        msg.m_byteCount = sizeof(hit);
        msg.m_queue = NetLink::NetMessageInfo::kQueueSequenced;
        msg.m_groupId = 8;
        victim->SetLastHitTime(FrameClock::GetTime());
        server->PutOnOutbox(victim->GetLinkHandle(), &msg);
        hit.id = static_cast<uint8_t>(skater->m_id);
        msg.m_id = kMsgHitOwner;
        server->PutOnOutbox(owner->GetLinkHandle(), &msg);
        if (m_projectileType != kFireball)
            return;
        // A fireball takes from the hit skater's score; with none left the skater is out.
        if (score->GetTotalScore() - ScoreTaken() > 0) {
            score->SetGrandTotal(score->GetTotalScore() - ScoreTaken());
            return;
        }
        if (score->GetTotalScore() > 0 && !victim->IsOwnPlayer()) {
            Scripts::CStruct* params = NewParams();
            params->AddString(kName, static_cast<CRider*>(skater)->GetDisplayName());
            Scripts::LaunchScript(kAnnounceElimination, params, nullptr, false, nullptr);
            DeleteParams(params);
            skate->ConcealSkater(static_cast<CRider*>(skater), true);
        }
        score->SetGrandTotal(0);
        return;
    }
}

// --- the projectile proximity component ---

// 0x004F3550
CObjectModule* __cdecl CProjectileProximityComponent::Create()
{
    auto* component = static_cast<CProjectileProximityComponent*>(MemSys::AllocZeroed(sizeof(CProjectileProximityComponent)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_vtable = kProximityVtable;
    component->m_type = kType;
    return component;
}

// 0x004F34E0
void CProjectileProximityComponent::DestroyBody()
{
    m_vtable = kProximityVtable;
    CObjectModule::DestroyBody();
}

// 0x004F34F0
CProjectileProximityComponent* CProjectileProximityComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004F3520
void CProjectileProximityComponent::ConfigureFromData(Scripts::CStruct*)
{
    m_riderRef = LocalSkater();
    mp_pedLogic = static_cast<CPedestrianBrainModule*>(m_object->GetComponent(kPedLogic));
    m_radiusSqr = kReach * kReach;
}

// 0x004F35B0
void CProjectileProximityComponent::Update()
{
    if (!m_riderRef) {
        m_riderRef = LocalSkater();
        return;
    }
    if (!m_object->m_id)
        return;
    if (!mp_pedLogic) {
        mp_pedLogic = static_cast<CPedestrianBrainModule*>(m_object->GetComponent(kPedLogic));
        return;
    }
    if (mp_pedLogic->GetLogicState() == kShot)
        return;
    for (int i = 0; i < 5; ++i) {
        CRider::Projectile& projectile = m_riderRef->m_projectiles[i];
        if (!projectile.active)
            continue;
        const auto* pos = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(&projectile) + 8);
        const Maths::Vector& at = m_object->m_pos;
        double middle = static_cast<double>(at.y) + kAbovePed;
        float dz = static_cast<float>(static_cast<double>(pos[2]) - at.z);
        double dy = pos[1] - middle;
        double dx = static_cast<double>(pos[0]) - at.x;
        if (!(dx * dx + dy * dy + static_cast<double>(dz) * dz <= m_radiusSqr))
            continue;
        // In reach: the ped is hit.
        m_object->PostOwnEvent(kProjectileInAvoidRadius, nullptr);
        Scripts::CStruct* params = NewParams();
        params->PutNameKey(kPedId, m_object->m_id);
        params->PutNameKey(kProjectileId, projectile.name);
        Scripts::LaunchScript(kProjectileHitPed, params, FindObject(m_riderRef->m_id), false, nullptr);
        params->PutNameKey(kAmmoType,
                            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(
                                                             m_riderRef->GetComponent(kSkaterCorePhysics)) +
                                                         0x2FC));
        Scripts::LaunchScript(kHitPedScript, params, m_object, false, nullptr);
        DeleteParams(params);
        projectile.active = false;
        return;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kCollisionVtable_slots[] = {&GameObjects::CShotImpactModule::Destroy,
         &GameObjects::CShotImpactModule::Update,
         &GameObjects::CShotImpactModule::ConfigureFromData,
         &GameObjects::CImpactDestructModule::Finalize,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kCollisionVtable = reinterpret_cast<void**>(kCollisionVtable_slots);
static VtableSlot kProximityVtable_slots[] = {&GameObjects::CProjectileProximityComponent::Destroy,
         &GameObjects::CProjectileProximityComponent::Update,
         &GameObjects::CProjectileProximityComponent::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kProximityVtable = reinterpret_cast<void**>(kProximityVtable_slots);

} // namespace
} // namespace Obj
