// Script commands asking about the skaters, 0x005AC5F0-0x005AD82F (see skater_query_cfuncs.h).

#include "game/skater/skater_query_cfuncs.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/maths/vec4.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/rider.h"
#include "game/skater/skater_score.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skater/skater_camera_component.h"

namespace ScriptCommands {

namespace {

using namespace SkateCalls;
using OnlinePlay::PlayerRecord;

constexpr uint32_t kLastScoreLanded = 0xB9072B65;   // last_score_landed
constexpr uint32_t kPointScale = 0xB08C5AE8;        // point_scale
constexpr uint32_t kScorePot = 0x95E84391;          // scorepot
constexpr uint32_t kTotalScore = 0xEE5B2B48;        // totalscore
constexpr uint32_t kLastScoreLandedInfo = 0xEA60AC69; // lastscorelanded
constexpr uint32_t kSkaterScore = 0xD262802F;       // SkaterScore (the component)
constexpr uint32_t kSkaterCamera = 0x5E43A604;      // skatercamera (the component)
constexpr uint32_t kSkater = 0x5B8AB877;            // skater
constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kIsMale = 0x3F813177;            // is_male
constexpr uint32_t kObjId = 0x0830ECAF;             // objID
constexpr uint32_t kIntId = 0xDB1E28CD;             // intId
constexpr uint8_t kMsgEliminated = 0x8D;            // the player of an object id eliminated (-1: none, a tie)
constexpr uint32_t kVelX = 0x6187906D, kVelY = 0x1680A0FB, kVelZ = 0x8F89F141; // vel_x, vel_y, vel_z

// The network manager and its player lists (ListKit::Hunt<PlayerRecord>, vtable 0x0064C1A8; JoinerRecord's 0x0064C82C).
OnlinePlay::Hub* NetManager() { return OnlinePlay::g_gameNetManager; }
struct PlayerSearch {
    void** m_vtable = GameModules::kPlayerSearchVtable;
    void* m_listLink = nullptr;
};
struct NewPlayerSearch {
    void** m_vtable = OnlinePlay::kNewPlayerSearchVtable;
    void* m_listLink = nullptr;
};
PlayerRecord* FirstPlayer(OnlinePlay::Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<PlayerRecord*>(manager->BeginPlayerWalk(search, observers));
}
PlayerRecord* NextPlayer(OnlinePlay::Hub* manager, PlayerSearch* search, bool observers)
{
    return static_cast<PlayerRecord*>(manager->StepPlayerWalk(search, observers));
}

// 0x005535F0: a team game.
bool IsSquadMatch() { return static_cast<GoalSystem::CMatchRules*>(GameMode())->IsSquadMatch() != 0; }

// A skater's score (GameModules::PointTally, not rewritten yet): 0x00561B40 its total; +0x14 the trick's base score, +0x60 its
// multiplier, +0x20 the last trick's score.
uint8_t* ScoreOf(GameObjects::CGameEntity* skater)
{
    return static_cast<uint8_t*>(static_cast<GameObjects::CRider*>(skater)->GetPointsTotal());
}
// (a player's skater's score through its SkaterScore component)
uint8_t* ComponentScoreOf(GameObjects::CGameEntity* skater)
{
    return static_cast<GameObjects::CRiderPointKeeper*>(skater->FetchModule(kSkaterScore))->m_pointsHeld;
}
int TotalScore(uint8_t* score) { return reinterpret_cast<GameModules::PointTally*>(score)->GetTotalScore(); }
int LastScoreLanded(uint8_t* score) { return *reinterpret_cast<int*>(score + 0x20); }
// The score pot: the base score times the multiplier, truncated (x87 at 53 bits, then the original's _ftol).
int ScorePot(uint8_t* score)
{
    double pot = static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60);
    return static_cast<int>(static_cast<int64_t>(pot));
}
// The local skater's speed: the length of its velocity (as the original's x87: in double).
double SpeedOf(GameObjects::CGameEntity* skater)
{
    double x = skater->m_vel.x, y = skater->m_vel.y, z = skater->m_vel.z;
    return std::sqrt(x * x + y * y + z * z);
}
// An integer parameter, 0 if missing.
int IntegerOrZero(Scripts::CStruct* params)
{
    int value = 0;
    return params->FetchIntValue(0u, &value, 0) ? value : 0;
}

// Message 0x8D (the eliminated player's object id, -1 for a tie) to the players.
void PrepareEliminated(NetLink::NetMessageInfo* desc, uint32_t* data)
{
    desc->m_ident = kMsgEliminated;
    desc->m_byteCount = 4;
    desc->m_content = data;
    desc->m_queue = NetLink::NetMessageInfo::kQueueSequenced;
    desc->m_groupId = 8;
}
void SendToPlayers(OnlinePlay::Hub* manager, NetLink::NetMessageInfo* desc, bool observers)
{
    PlayerSearch search;
    for (PlayerRecord* player = FirstPlayer(manager, &search, observers); player;
         player = NextPlayer(manager, &search, observers))
        manager->m_host->PutOnOutbox(player->GetLinkHandle(), desc);
}

} // namespace

// 0x005AC5F0 / 0x005AC660: SkaterSpeedGreaterThan / SkaterSpeedLessThan <speed> - the local skater's (none, or no
// speed given: false).
bool __cdecl CFunc_SkaterSpeedGreaterThan(Scripts::CStruct* params, Scripts::CScript*)
{
    float speed;
    if (!params->FetchFloat(0u, &speed, 0))
        return false;
    GameObjects::CGameEntity* skater = GetHomeRider();
    return skater && SpeedOf(skater) > speed;
}

bool __cdecl CFunc_SkaterSpeedLessThan(Scripts::CStruct* params, Scripts::CScript*)
{
    float speed;
    if (!params->FetchFloat(0u, &speed, 0))
        return false;
    GameObjects::CGameEntity* skater = GetHomeRider();
    return skater && SpeedOf(skater) < speed;
}

// 0x005AC6D0 / 0x005AC720: SkaterLastScoreLandedGreaterThan / LessThan <score> (0 if missing) - the local skater's
// last trick's score.
bool __cdecl CFunc_SkaterLastScoreLandedGreaterThan(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = IntegerOrZero(params);
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && LastScoreLanded(score) > value;
}

bool __cdecl CFunc_SkaterLastScoreLandedLessThan(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = IntegerOrZero(params);
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && LastScoreLanded(score) < value;
}

// 0x005AC770: SkaterGetLastScoreLanded - the local skater's last trick's score as last_score_landed (false without).
bool __cdecl CFunc_SkaterGetLastScoreLanded(Scripts::CStruct*, Scripts::CScript* script)
{
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    if (!score)
        return false;
    script->m_args->PutInt(kLastScoreLanded, LastScoreLanded(score));
    return true;
}

// 0x005AC7B0: AccumulateScores - each player's total score added to the score accumulated over the session.
bool __cdecl CFunc_AccumulateScores(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    PlayerSearch unused; // (made and dropped by the original)
    (void)unused;
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false))
        player->AddToAccumulatedScore(TotalScore(ScoreOf(player->m_skater)));
    return true;
}

// 0x005AC850: EliminateLastPlacePlayer - with more than one player still in: if all have the same score, everyone is
// told nobody goes (-1); otherwise each player on the lowest score is marked eliminated and everyone (observers too)
// told its skater's id.
bool __cdecl CFunc_EliminateLastPlacePlayer(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    PlayerSearch others;
    int lowest = -1;
    int highest = 0;
    int inGame = 0;
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated())
            continue;
        ++inGame;
        uint8_t* score = ScoreOf(player->m_skater);
        if (lowest == -1 || TotalScore(score) < lowest)
            lowest = TotalScore(score);
        if (TotalScore(score) > highest)
            highest = TotalScore(score);
    }
    if (inGame <= 1)
        return true;
    NetLink::NetMessageInfo desc;
    uint32_t data;
    if (highest == lowest) {
        desc.MakeNew();
        data = 0xFFFFFFFF;
        PrepareEliminated(&desc, &data);
        SendToPlayers(manager, &desc, false);
        return true;
    }
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated() || TotalScore(ScoreOf(player->m_skater)) != lowest)
            continue;
        desc.MakeNew();
        player->MarkEliminated();
        data = player->m_skater->m_ident;
        PrepareEliminated(&desc, &data);
        SendToPlayers(manager, &desc, true);
    }
    return true;
}

// 0x005ACAA0: OnlyOnePlayerLeft - a team game: whether at most one team has players not eliminated; otherwise
// whether at most one player isn't (then num_players: how many).
bool __cdecl CFunc_OnlyOnePlayerLeft(Scripts::CStruct*, Scripts::CScript* script)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    if (IsSquadMatch()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerRecord* player = FirstPlayer(manager, &search, false); player;
             player = NextPlayer(manager, &search, false)) {
            if (!player->IsEliminated())
                ++teams[player->m_team];
        }
        int withPlayers = 0;
        for (int count : teams)
            withPlayers += count > 0 ? 1 : 0;
        return withPlayers <= 1;
    }
    int players = 0;
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (!player->IsEliminated())
            ++players;
    }
    if (players > 1)
        return false;
    script->m_args->PutInt("num_players", players);
    return true;
}

// 0x005ACC20: AnySkaterTotalScoreAtLeast <score> (0 if missing) - whether any player's (a team game: any team's)
// total score reaches it.
bool __cdecl CFunc_AnySkaterTotalScoreAtLeast(Scripts::CStruct* params, Scripts::CScript*)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    int target = IntegerOrZero(params);
    if (IsSquadMatch()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerRecord* player = FirstPlayer(manager, &search, false); player;
             player = NextPlayer(manager, &search, false)) {
            uint8_t* score = ScoreOf(player->m_skater);
            teams[player->m_team] += TotalScore(score);
        }
        for (int total : teams) {
            if (total >= target)
                return true;
        }
        return false;
    }
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->m_skater && TotalScore(ScoreOf(player->m_skater)) >= target)
            return true;
    }
    return false;
}

// 0x005ACD80: OnlyOneSkaterLeft - how many skaters (a team game: teams) have scored: false while players are still
// joining; with none scored the server in host mode 1 or 2 says true; otherwise whether exactly one has. (Not a team
// game: also false with exactly one skater.)
bool __cdecl CFunc_OnlyOneSkaterLeft(Scripts::CStruct*, Scripts::CScript*)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    NewPlayerSearch joining;
    int scored = 0;
    if (IsSquadMatch()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerRecord* player = FirstPlayer(manager, &search, false); player;
             player = NextPlayer(manager, &search, false)) {
            uint8_t* score = ScoreOf(player->m_skater);
            teams[player->m_team] += TotalScore(score);
        }
        for (int total : teams)
            scored += total > 0 ? 1 : 0;
        if (manager->BeginJoinerWalk(&joining))
            return false;
    } else {
        int skaters = 0;
        for (PlayerRecord* player = FirstPlayer(manager, &search, false); player;
             player = NextPlayer(manager, &search, false)) {
            if (!player->m_skater)
                continue;
            ++skaters;
            if (TotalScore(ScoreOf(player->m_skater)) > 0)
                ++scored;
        }
        if (manager->BeginJoinerWalk(&joining) || skaters == 1)
            return false;
    }
    if (manager->OnServer() && (manager->GetHostingRole() == 1 || manager->GetHostingRole() == 2) && scored == 0)
        return true;
    return scored == 1;
}

// 0x005ACF50 / 0x005ACFA0: SkaterTotalScoreGreaterThan / LessThan <score> (0 if missing) - the local skater's total.
bool __cdecl CFunc_SkaterTotalScoreGreaterThan(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = IntegerOrZero(params);
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && TotalScore(score) > value;
}

bool __cdecl CFunc_SkaterTotalScoreLessThan(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = IntegerOrZero(params);
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && TotalScore(score) < value;
}

// 0x005ACFF0: SkaterCurrentScorePotGreaterThan <score> [point_scale=<n>] - the local skater's score pot above score
// times point_scale (1). 0x005AD070: SkaterCurrentScorePotLessThan <score> (no scale).
bool __cdecl CFunc_SkaterCurrentScorePotGreaterThan(Scripts::CStruct* params, Scripts::CScript*)
{
    int scale = 1;
    int value = IntegerOrZero(params);
    params->FetchIntValue(kPointScale, &scale, 0);
    value *= scale;
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && ScorePot(score) > value;
}

bool __cdecl CFunc_SkaterCurrentScorePotLessThan(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = IntegerOrZero(params);
    GameObjects::CGameEntity* skater = GetHomeRider();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && ScorePot(score) < value;
}

// 0x005AD0D0: SkaterGetScoreInfo - the local skater's scorepot, totalscore and lastscorelanded. (Without a local
// skater the original reads a null score: a crash, kept.)
bool __cdecl CFunc_SkaterGetScoreInfo(Scripts::CStruct*, Scripts::CScript* script)
{
    GameObjects::CGameEntity* skater = GetHomeRider();
    uint8_t* score = skater ? ScoreOf(skater) : nullptr;
    script->m_args->PutInt(kScorePot, ScorePot(score));
    script->m_args->PutInt(kTotalScore, TotalScore(score));
    script->m_args->PutInt(kLastScoreLandedInfo, LastScoreLanded(score));
    return true;
}

// 0x005AD140: InFirstPlace - whether no other player still in has a higher total than the local player (who must be
// in the game - not eliminated, surveying or observing - with a skater, and with at least one other player in);
// points: the best other player's total.
bool __cdecl CFunc_InFirstPlace(Scripts::CStruct*, Scripts::CScript* script)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    int better = 0;
    int best = 0;
    auto* local = static_cast<PlayerRecord*>(manager->GetOwnPlayer());
    if (!local || local->IsEliminated() || local->IsScoutingLevel() || local->IsObserver() || !local->m_skater)
        return false;
    int mine = TotalScore(ComponentScoreOf(local->m_skater));
    int inGame = 0;
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated())
            continue;
        ++inGame;
        uint8_t* score = ComponentScoreOf(player->m_skater);
        if (TotalScore(score) > mine)
            ++better;
        if (!player->IsOwnPlayer() && TotalScore(score) > best)
            best = TotalScore(score);
    }
    if (inGame < 2)
        return false;
    script->m_args->PutInt("points", best);
    return better == 0;
}

// 0x005AD2D0: InLastPlace - whether no other player still in has a lower total than the local player (as
// InFirstPlace); then points: the lowest other player's total.
bool __cdecl CFunc_InLastPlace(Scripts::CStruct*, Scripts::CScript* script)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    int worse = 0;
    int lowest = -1;
    auto* local = static_cast<PlayerRecord*>(manager->GetOwnPlayer());
    if (!local || local->IsEliminated() || local->IsScoutingLevel() || local->IsObserver() || !local->m_skater)
        return false;
    int mine = TotalScore(ComponentScoreOf(local->m_skater));
    int inGame = 0;
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated())
            continue;
        ++inGame;
        uint8_t* score = ComponentScoreOf(player->m_skater);
        if (TotalScore(score) < mine)
            ++worse;
        if (player->IsOwnPlayer())
            continue;
        int total = TotalScore(score);
        if (lowest == -1)
            lowest = total;
        else if (total < lowest)
            lowest = TotalScore(score);
    }
    if (inGame < 2 || worse != 0)
        return false;
    script->m_args->PutInt("points", lowest);
    return true;
}

// 0x005AD450: InSecondToLastPlace - whether exactly one player still in has a lower total than the local player (as
// InFirstPlace); then points: the local total less the lowest total (the local player's own counted).
bool __cdecl CFunc_InSecondToLastPlace(Scripts::CStruct*, Scripts::CScript* script)
{
    OnlinePlay::Hub* manager = NetManager();
    PlayerSearch search;
    int worse = 0;
    int lowest = -1;
    auto* local = static_cast<PlayerRecord*>(manager->GetOwnPlayer());
    if (!local || local->IsEliminated() || local->IsScoutingLevel() || local->IsObserver() || !local->m_skater)
        return false;
    int mine = TotalScore(ComponentScoreOf(local->m_skater));
    int inGame = 0;
    for (PlayerRecord* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated())
            continue;
        ++inGame;
        uint8_t* score = ComponentScoreOf(player->m_skater);
        if (TotalScore(score) < mine)
            ++worse;
        if (lowest == -1)
            lowest = TotalScore(score);
        else if (TotalScore(score) < lowest)
            lowest = TotalScore(score);
    }
    if (inGame < 2 || worse != 1)
        return false;
    script->m_args->PutInt("points", mine - lowest);
    return true;
}

// 0x005AD5C0 / 0x005AD610: GoalsGreaterThan / GoalsEqualTo <n> - the career's goals got in all (false without n).
bool __cdecl CFunc_GoalsGreaterThan(Scripts::CStruct* params, Scripts::CScript*)
{
    float goals;
    if (!params->FetchFloat(0u, &goals, 0))
        return false;
    return static_cast<double>(Career()->CountTotalGoals()) > goals;
}

bool __cdecl CFunc_GoalsEqualTo(Scripts::CStruct* params, Scripts::CScript*)
{
    float goals;
    if (!params->FetchFloat(0u, &goals, 0))
        return false;
    return static_cast<double>(Career()->CountTotalGoals()) == goals;
}

// 0x005AD660: ToggleSkaterCamMode [skater=<n>] - skater n's (none or 0: the local player's) skater camera's mode
// toggled (0x004FA7D0).
bool __cdecl CFunc_ToggleSkaterCamMode(Scripts::CStruct* params, Scripts::CScript*)
{
    int index = 0; // (uninitialised in the original when missing: the game's scripts always give skater=0)
    params->FetchIntValue(kSkater, &index, 0);
    GameObjects::CGameEntity* skater;
    if (index == 0) {
        auto* local = static_cast<PlayerRecord*>(NetManager()->GetOwnPlayer());
        if (!local)
            return true;
        skater = local->m_skater;
    } else {
        skater = GetRiderAt(index);
    }
    if (skater) {
        auto* camera = static_cast<GameObjects::CGameEntity*>(static_cast<GameObjects::CRider*>(skater)->GetSkaterCamera());
        static_cast<GameObjects::CRiderChaseView*>(camera->FetchModule(kSkaterCamera))->NextMode();
    }
    return true;
}

// 0x005AD6C0: GetSkaterID [skater=<n>] - skater n's (none: the local skater's) object id as objID, if it's local.
bool __cdecl CFunc_GetSkaterID(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index = 0;
    GameObjects::CGameEntity* skater = params->FetchIntValue(kSkater, &index, 0) ? GetRiderAt(index) : GetHomeRider();
    if (!skater || !skater->m_unknownBC)
        return false;
    script->m_args->PutNameKey(kObjId, skater->m_ident);
    return true;
}

// 0x005AD730: SkaterExists skater=<n> - whether skater n exists; its object id as objID.
bool __cdecl CFunc_SkaterExists(Scripts::CStruct* params, Scripts::CScript* script)
{
    int index = 0; // (uninitialised in the original)
    if (!params->FetchIntValue(kSkater, &index, 1))
        return false;
    GameObjects::CGameEntity* skater = GetRiderAt(index);
    if (!skater)
        return false;
    script->m_args->PutNameKey(kObjId, skater->m_ident);
    return true;
}

// 0x005AD780: GetSkaterSex player=<n> - player n's profile's is_male.
bool __cdecl CFunc_GetSkaterSex(Scripts::CStruct* params, Scripts::CScript* script)
{
    int player = 0; // (uninitialised in the original, as is_male)
    params->FetchIntValue(kPlayer, &player, 1);
    GameObjects::CRiderIdentity* profile = GameObjects::g_profileManager->FetchAccount(player);
    int male = 0;
    profile->InfoStructure()->FetchIntValue(kIsMale, &male, 1);
    script->m_args->PutInt(kIsMale, male);
    return true;
}

// 0x005AD7E0: GetCurrentSkaterID - the script's object's id as objID and intId, if it's a local skater (any object
// with the composite object's +0xBC set).
bool __cdecl CFunc_GetCurrentSkaterID(Scripts::CStruct*, Scripts::CScript* script)
{
    auto* object = static_cast<GameObjects::CGameEntity*>(script->m_entity.Pull());
    if (!object || !object->m_unknownBC)
        return false;
    uint32_t id = object->m_ident;
    script->m_args->PutNameKey(kObjId, id);
    script->m_args->PutInt(kIntId, static_cast<int>(id));
    return true;
}

// 0x005B0070: GetSkaterPosition - the script's object's position as integers x, y, z (truncated); false without an
// object.
bool __cdecl CFunc_GetSkaterPosition(Scripts::CStruct*, Scripts::CScript* script)
{
    auto* object = static_cast<GameObjects::CGameEntity*>(script->m_entity.Pull());
    if (!object)
        return false;
    float y = object->m_atPoint.y, z = object->m_atPoint.z;
    script->m_args->PutInt("x", static_cast<int>(static_cast<int64_t>(object->m_atPoint.x))); // (the original's _ftol)
    script->m_args->PutInt("y", static_cast<int>(static_cast<int64_t>(y)));
    script->m_args->PutInt("z", static_cast<int>(static_cast<int64_t>(z)));
    return true;
}

// 0x005B00F0: SetSkaterVelocity <speed> | vel_x= vel_y= vel_z= - the script's object's velocity: its direction kept
// at the speed given (all four components scaled), or the vector given (w 1; built on the heap and freed, as the
// original's new Maths::Vec4). False without an object.
bool __cdecl CFunc_SetSkaterVelocity(Scripts::CStruct* params, Scripts::CScript* script)
{
    auto* object = static_cast<GameObjects::CGameEntity*>(script->m_entity.Pull());
    if (!object)
        return false;
    float speed = 0.0f;
    if (params->FetchFloat(0u, &speed, 0)) {
        Maths::Vec4 direction = object->m_vel;
        direction.MakeUnit();
        object->m_vel.x = direction.x * speed;
        object->m_vel.y = direction.y * speed;
        object->m_vel.z = direction.z * speed;
        object->m_vel.w = direction.w * speed;
        return true;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    params->FetchFloat(kVelX, &x, 1);
    params->FetchFloat(kVelY, &y, 1);
    params->FetchFloat(kVelZ, &z, 1);
    if (!MemSys::g_managerReady)
        MemSys::EnsureManager();
    auto* velocity = static_cast<Maths::Vec4*>(MemSys::Hub::Pull()->Carve(sizeof(Maths::Vec4), 1, nullptr));
    velocity->x = x;
    velocity->y = y;
    velocity->z = z;
    velocity->w = 1.0f;
    object->m_vel = *velocity;
    MemSys::Hub::Pull()->Dealloc(velocity);
    return true;
}

} // namespace ScriptCommands
