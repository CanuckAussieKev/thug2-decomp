// Script commands (CFunctions) 0x005B5770-0x005B5BE0: skater set-up, scores, the nearest skater (see
// skater_score_cfuncs.h).

#include "game/skater/skater_score_cfuncs.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

constexpr uint32_t kResetAll = 0x81740BB6;      // "reset_all"
constexpr uint32_t kNetKing = 0x6EF8FDA0;       // "netking"
constexpr uint32_t kKing = 0x5D32129C;          // "king"
constexpr uint32_t kUseBailStyle = 0x929EB4D5;  // "UseBailStyle"
constexpr uint32_t kExcludeTeam = 0x6C2A140F;   // "exclude_team"
constexpr uint32_t kRadius = 0xC48391A5;        // "radius"
constexpr uint32_t kTeam = 0x3B1F59E0;          // "team"
constexpr uint32_t kPlayer = 0x67E6859A;        // "player"

Mdl::Skate* Skate() { return Mdl::Skate::Instance(); }
Obj::CSkater* SkaterAt(int index) { return static_cast<Obj::CSkater*>(Skate()->GetSkater(index)); }
bool IsLocal(Obj::CSkater* skater) { return reinterpret_cast<uint8_t*>(skater)[0xBC] != 0; }

// The skater's score (the SkaterScore component's object): 0x00564A10 resets it, 0x00561B40 / 0x00561A80 get and set
// its total, 0x00564860 resets its pot (bail style or not). None has source yet.
void* ScoreOf(Obj::CSkater* skater) { return skater->GetScore(); }

// The game mode's name (0x005532A0, no source yet).
uint32_t GameModeName()
{
    return static_cast<Game::CGameMode*>(static_cast<void*>(Game::g_gameMode))->GetNameChecksum();
}

} // namespace

// 0x005B5770: InitializeSkaters: each skater - a local one's input reset first (vtable slot 14, ResetInput) - taken
// out of the world (the skate module's 0x00564C60 with false). Always true.
bool __cdecl CFunc_InitializeSkaters(Script::CStruct*, Script::CScript*)
{
    for (int i = 0; i < Skate()->GetNumSkaters(); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (IsLocal(skater)) {
            auto** vtable = *reinterpret_cast<void***>(skater);
            reinterpret_cast<void(__thiscall*)(Obj::CSkater*)>(vtable[14])(skater);
        }
        Skate()->HideSkater(skater, false);
    }
    return true;
}

// 0x005B57E0: ReinsertSkaters: each skater shown again, in its own skater heap. Always true.
bool __cdecl CFunc_ReinsertSkaters(Script::CStruct*, Script::CScript*)
{
    for (int i = 0; i < Skate()->GetNumSkaters(); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        Mem::Manager* manager = Mem::Manager::Get();
        manager->PushContext(manager->m_skaterHeaps[skater->GetSkaterNumber()]);
        skater->ShowSkater();
        Mem::Manager::Get()->PopContext();
    }
    return true;
}

// 0x005B5860: ResetScore [reset_all]: the local skaters' scores (with reset_all, every skater's) reset and their
// score text shown as "0" - "0:00" in king of the hill. Always true.
bool __cdecl CFunc_ResetScore(Script::CStruct* params, Script::CScript*)
{
    bool all = params->ContainsComponentNamed(kResetAll);
    for (int i = 0; i < Skate()->GetNumSkaters(); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (!IsLocal(skater) && !all)
            continue;
        static_cast<Mdl::Score*>(ScoreOf(skater))->Reset();
        if (GameModeName() == kNetKing || GameModeName() == kKing)
            Front::SetScoreText(reinterpret_cast<const char*>(0x00657D18), skater->GetSkaterNumber()); // "0:00"
        else
            Front::SetScoreText(reinterpret_cast<const char*>(0x00657D20), skater->GetSkaterNumber()); // "0"
    }
    return true;
}

// 0x005B5950: UpdateScore: each local skater's score total set again to itself (so it's shown). Always true.
bool __cdecl CFunc_UpdateScore(Script::CStruct*, Script::CScript*)
{
    for (int i = 0; i < Skate()->GetNumSkaters(); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (!IsLocal(skater))
            continue;
        void* score = ScoreOf(skater);
        static_cast<Mdl::Score*>(score)->SetTotalScore(static_cast<Mdl::Score*>(score)->GetTotalScore());
    }
    return true;
}

// 0x005B59C0: ResetScorePot [UseBailStyle]: each local skater's score pot reset (0x00564860 with true unless
// UseBailStyle). Always true.
bool __cdecl CFunc_ResetScorePot(Script::CStruct* params, Script::CScript*)
{
    bool bailStyle = params->ContainsFlag(kUseBailStyle);
    for (int i = 0; i < Skate()->GetNumSkaters(); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (IsLocal(skater))
            static_cast<Mdl::Score*>(ScoreOf(skater))->Bail(!bailStyle);
    }
    return true;
}

// 0x005B5A40: GetCollidingPlayerAndTeam exclude_team=t radius=feet: player, the skater nearest the script's object
// within the radius (in inches: * 12) that is in another team without the flag or in team t with it, and team, its
// team (-1 for both if none). Always true.
bool __cdecl CFunc_GetCollidingPlayerAndTeam(Script::CStruct* params, Script::CScript* script)
{
    Script::CStruct* results = script->m_params;
    auto* object = static_cast<Obj::CCompositeObject*>(script->m_object.mp_object);
    int team = -1;
    uint32_t player = 0xFFFFFFFF;
    float nearest = 1000000.0f;
    int excludeTeam;
    params->GetInteger(kExcludeTeam, &excludeTeam, 1);
    float radius;
    params->GetFloat(kRadius, &radius, 1);
    double inches = static_cast<double>(radius) * 12.0f; // x87: the square taken before rounding
    float radiusSquared = static_cast<float>(inches * inches);
    GameNet::Manager* net = GameVariable<GameNet::Manager*>(0x007CCE14);
    for (int i = 0; i < Skate()->GetNumSkaters(); ++i) {
        Obj::CSkater* skater = SkaterAt(i);
        if (!skater)
            continue;
        auto* info = reinterpret_cast<GameNet::PlayerInfo*>(net->PlayerByObjectId(static_cast<uint16_t>(skater->m_id)));
        if (!info)
            continue;
        if (info->m_team == excludeTeam && !info->IsCarryingFlag())
            continue;
        if (info->m_team != excludeTeam && info->IsCarryingFlag())
            continue;
        double dx = static_cast<double>(skater->m_pos.x) - object->m_pos.x;
        double dy = static_cast<double>(skater->m_pos.y) - object->m_pos.y;
        double dz = static_cast<double>(skater->m_pos.z) - object->m_pos.z;
        double distanceSquared = dz * dz + dx * dx + dy * dy;
        if (distanceSquared < radiusSquared && distanceSquared < nearest) {
            nearest = static_cast<float>(distanceSquared);
            player = skater->m_id;
        }
    }
    if (static_cast<int>(player) > -1)
        team = reinterpret_cast<GameNet::PlayerInfo*>(net->PlayerByObjectId(static_cast<uint16_t>(player)))->m_team;
    results->AddInteger(kTeam, team);
    results->AddInteger(kPlayer, static_cast<int>(player));
    return true;
}

} // namespace CFuncs

REPLACE(0x005B5770, CFuncs::CFunc_InitializeSkaters);
REPLACE(0x005B57E0, CFuncs::CFunc_ReinsertSkaters);
REPLACE(0x005B5860, CFuncs::CFunc_ResetScore);
REPLACE(0x005B5950, CFuncs::CFunc_UpdateScore);
REPLACE(0x005B59C0, CFuncs::CFunc_ResetScorePot);
REPLACE(0x005B5A40, CFuncs::CFunc_GetCollidingPlayerAndTeam);
