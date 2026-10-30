// The projectile components (Obj::CProjectileCollisionComponent, Obj::CProjectileProximityComponent): see
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
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kCollisionVtable = reinterpret_cast<void**>(0x0064B830);
void** const kProximityVtable = reinterpret_cast<void**>(0x0064B864);

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

GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
Mdl::Skate* SkateModule() { return GameVariable<Mdl::Skate*>(0x007CE478); }

// The skate module's local skater (0x00566520, a register stub on our side).
CSkater* LocalSkater()
{
    return SkateModule()->GetLocalSkater();
}

// The object dies (its vtable slot 6, MarkAsDead).
void Kill(CCompositeObject* object) { reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(object->m_vtable[6])(object); }

Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

Mdl::Score* ScoreOf(CCompositeObject* skater)
{
    return reinterpret_cast<Mdl::Score*>(static_cast<CSkaterScoreComponent*>(skater->GetComponent(kSkaterScore))->m_score);
}

} // namespace

// --- the projectile collision component ---

// 0x004F3010
CBaseComponent* __cdecl CProjectileCollisionComponent::Create()
{
    auto* component = static_cast<CProjectileCollisionComponent*>(Mem::Calloc(sizeof(CProjectileCollisionComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_radius = 0.0f;
    component->m_ownerId = 0;
    component->m_deathScript = 0;
    component->m_scale = 0.0f;
    component->m_hit = false;
    component->m_vtable = kCollisionVtable;
    component->CBaseComponent::m_type = kType;
    return component;
}

// 0x004F2F10
void CProjectileCollisionComponent::DestroyBody()
{
    m_vtable = kCollisionVtable;
    CBaseComponent::DestroyBody();
}

// 0x004F2F50
CProjectileCollisionComponent* CProjectileCollisionComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004F2F80
void CProjectileCollisionComponent::InitFromStructure(Script::CStruct* params)
{
    params->GetFloat(kRadius, &m_radius, 1);
    params->GetFloat(kScale, &m_scale, 1);
    params->GetChecksum(kOwnerId, &m_ownerId, 1);
    params->GetChecksum(kDeathScript, &m_deathScript, 1);
    params->GetChecksum(kProjectileType, &m_projectileType, 1);
    m_vel = {0.0f, 0.0f, 1.0f, 1.0f};
    m_vel.Normalize();
}

// 0x004F2F40
void CProjectileCollisionComponent::MarkHit() { m_hit = true; }

// 0x004F2F20
int CProjectileCollisionComponent::ScoreTaken()
{
    if (m_projectileType != kFireball)
        return 0;
    return static_cast<int>(m_scale) * 10;
}

// 0x004F3090
void CProjectileCollisionComponent::Update()
{
    GameNet::Manager* net = NetManager();
    if (net->InNetGame() && m_projectileType != kFireball && !net->PlayerCollisionEnabled())
        return;
    float reach = static_cast<float>(static_cast<double>(m_radius) + kReach);
    if (reinterpret_cast<CSuspendComponent*>(mp_suspend)->IsFar() || m_hit)
        return;
    Mdl::Skate* skate = SkateModule();
    uint32_t count = static_cast<uint32_t>(skate->GetNumSkaters());
    bool teamGame = GameVariable<Game::CGameMode*>(0x007CE470)->IsTeamGame() != 0;
    for (uint32_t i = 0; i < count; ++i) {
        CCompositeObject* skater = skate->GetSkater(static_cast<int>(i));
        if (skater->m_id == m_ownerId)
            continue;
        auto* owner = reinterpret_cast<GameNet::PlayerInfo*>(net->PlayerByObjectId(static_cast<uint16_t>(m_ownerId)));
        auto* victim = reinterpret_cast<GameNet::PlayerInfo*>(net->PlayerByObjectId(static_cast<uint16_t>(skater->m_id)));
        if (!victim->CanBeHitAgain())
            continue;
        Mdl::Score* score = ScoreOf(skater);
        if (m_projectileType == kFireball && score->GetTotalScore() <= 0)
            continue;
        if (teamGame && *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(owner) + 0x30) ==
                            *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(victim) + 0x30) &&
            net->GetNetworkPreferences()->GetPreferenceChecksum(kFriendlyFire, kChecksum) != kBooleanTrue)
            continue;
        CCompositeObject* object = m_object;
        double dz = static_cast<double>(skater->m_pos.z) - object->m_pos.z;
        double dy = static_cast<double>(skater->m_pos.y) - object->m_pos.y;
        double dx = static_cast<double>(skater->m_pos.x) - object->m_pos.x;
        if (!(std::sqrt(dx * dx + dy * dy + dz * dz) < reach))
            continue;

        // Hit.
        Net::MsgDesc msg;
        msg.Construct();
        auto* server = reinterpret_cast<Net::App*>(reinterpret_cast<GameNet::FoldedGetters*>(net)->Word04());
        Kill(m_object);
        if (m_deathScript) {
            Mth::Vector pos = m_object->m_pos;
            Script::CStruct* params = NewParams();
            params->AddVector(kPos, Script::VectorValue{pos.x, pos.y, pos.z, pos.w});
            params->AddVector(kVel, Script::VectorValue{m_vel.x, m_vel.y, m_vel.z, m_vel.w});
            params->AddFloat(kScale, m_scale);
            Script::RunScript(m_deathScript, params, nullptr, false, nullptr);
            DeleteParams(params);
        }
        // (The original builds it over its loop counter's slot: the two bytes after the flag are the count's high
        // bytes, 0.)
        HitMessage hit = {};
        hit.id = static_cast<uint8_t>(m_ownerId);
        hit.score = m_projectileType == kFireball ? static_cast<int>(m_scale) * 10 : 0;
        hit.fireball = m_projectileType == kFireball;
        msg.m_id = kMsgHit;
        msg.m_data = &hit;
        msg.m_length = sizeof(hit);
        msg.m_queue = Net::MsgDesc::kQueueSequenced;
        msg.m_groupId = 8;
        victim->SetLastHitTime(Tmr::GetTime());
        server->EnqueueMessage(victim->GetConnHandle(), &msg);
        hit.id = static_cast<uint8_t>(skater->m_id);
        msg.m_id = kMsgHitOwner;
        server->EnqueueMessage(owner->GetConnHandle(), &msg);
        if (m_projectileType != kFireball)
            return;
        // A fireball takes from the hit skater's score; with none left the skater is out.
        if (score->GetTotalScore() - ScoreTaken() > 0) {
            score->SetTotalScore(score->GetTotalScore() - ScoreTaken());
            return;
        }
        if (score->GetTotalScore() > 0 && !victim->IsLocalPlayer()) {
            Script::CStruct* params = NewParams();
            params->AddString(kName, static_cast<CSkater*>(skater)->GetDisplayName());
            Script::RunScript(kAnnounceElimination, params, nullptr, false, nullptr);
            DeleteParams(params);
            skate->HideSkater(static_cast<CSkater*>(skater), true);
        }
        score->SetTotalScore(0);
        return;
    }
}

// --- the projectile proximity component ---

// 0x004F3550
CBaseComponent* __cdecl CProjectileProximityComponent::Create()
{
    auto* component = static_cast<CProjectileProximityComponent*>(Mem::Calloc(sizeof(CProjectileProximityComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kProximityVtable;
    component->m_type = kType;
    return component;
}

// 0x004F34E0
void CProjectileProximityComponent::DestroyBody()
{
    m_vtable = kProximityVtable;
    CBaseComponent::DestroyBody();
}

// 0x004F34F0
CProjectileProximityComponent* CProjectileProximityComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004F3520
void CProjectileProximityComponent::InitFromStructure(Script::CStruct*)
{
    mp_skater = LocalSkater();
    mp_pedLogic = static_cast<CPedLogicComponent*>(m_object->GetComponent(kPedLogic));
    m_radiusSqr = kReach * kReach;
}

// 0x004F35B0
void CProjectileProximityComponent::Update()
{
    if (!mp_skater) {
        mp_skater = LocalSkater();
        return;
    }
    if (!m_object->m_id)
        return;
    if (!mp_pedLogic) {
        mp_pedLogic = static_cast<CPedLogicComponent*>(m_object->GetComponent(kPedLogic));
        return;
    }
    if (mp_pedLogic->GetLogicState() == kShot)
        return;
    for (int i = 0; i < 5; ++i) {
        CSkater::Projectile& projectile = mp_skater->m_projectiles[i];
        if (!projectile.active)
            continue;
        const auto* pos = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(&projectile) + 8);
        const Mth::Vector& at = m_object->m_pos;
        double middle = static_cast<double>(at.y) + kAbovePed;
        float dz = static_cast<float>(static_cast<double>(pos[2]) - at.z);
        double dy = pos[1] - middle;
        double dx = static_cast<double>(pos[0]) - at.x;
        if (!(dx * dx + dy * dy + static_cast<double>(dz) * dz <= m_radiusSqr))
            continue;
        // In reach: the ped is hit.
        m_object->SelfEvent(kProjectileInAvoidRadius, nullptr);
        Script::CStruct* params = NewParams();
        params->AddChecksum(kPedId, m_object->m_id);
        params->AddChecksum(kProjectileId, projectile.name);
        Script::RunScript(kProjectileHitPed, params, FindObject(mp_skater->m_id), false, nullptr);
        params->AddChecksum(kAmmoType,
                            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(
                                                             mp_skater->GetComponent(kSkaterCorePhysics)) +
                                                         0x2FC));
        Script::RunScript(kHitPedScript, params, m_object, false, nullptr);
        DeleteParams(params);
        projectile.active = false;
        return;
    }
}

} // namespace Obj

REPLACE(0x004F3010, Obj::CProjectileCollisionComponent::Create);
REPLACE(0x004F2F10, &Obj::CProjectileCollisionComponent::DestroyBody);
REPLACE(0x004F2F50, &Obj::CProjectileCollisionComponent::Destroy);
REPLACE(0x004F2F80, &Obj::CProjectileCollisionComponent::InitFromStructure);
REPLACE(0x004F2F40, &Obj::CProjectileCollisionComponent::MarkHit);
REPLACE(0x004F2F20, &Obj::CProjectileCollisionComponent::ScoreTaken);
REPLACE(0x004F3090, &Obj::CProjectileCollisionComponent::Update);
REPLACE(0x004F3550, Obj::CProjectileProximityComponent::Create);
REPLACE(0x004F34E0, &Obj::CProjectileProximityComponent::DestroyBody);
REPLACE(0x004F34F0, &Obj::CProjectileProximityComponent::Destroy);
REPLACE(0x004F3520, &Obj::CProjectileProximityComponent::InitFromStructure);
REPLACE(0x004F35B0, &Obj::CProjectileProximityComponent::Update);
