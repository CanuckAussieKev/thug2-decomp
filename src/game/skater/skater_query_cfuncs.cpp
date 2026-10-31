// Script commands asking about the skaters, 0x005AC5F0-0x005AD82F (see skater_query_cfuncs.h).

#include "game/skater/skater_query_cfuncs.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/player_info.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "game/skater/skater_score.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skater/skater_camera_component.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using namespace SkateCalls;
using GameNet::PlayerInfo;

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

// The network manager and its player lists (Lst::Search<PlayerInfo>, vtable 0x0064C1A8; NewPlayerInfo's 0x0064C82C).
GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
struct PlayerSearch {
    void** m_vtable = Mdl::kPlayerSearchVtable;
    void* m_node = nullptr;
};
struct NewPlayerSearch {
    void** m_vtable = GameNet::kNewPlayerSearchVtable;
    void* m_node = nullptr;
};
PlayerInfo* FirstPlayer(GameNet::Manager* manager, PlayerSearch* search, bool observers)
{
    return static_cast<PlayerInfo*>(manager->FirstPlayerInfo(search, observers));
}
PlayerInfo* NextPlayer(GameNet::Manager* manager, PlayerSearch* search, bool observers)
{
    return static_cast<PlayerInfo*>(manager->NextPlayerInfo(search, observers));
}

// 0x005535F0: a team game.
bool IsTeamGame() { return static_cast<Game::CGameMode*>(GameMode())->IsTeamGame() != 0; }

// A skater's score (Mdl::Score, not rewritten yet): 0x00561B40 its total; +0x14 the trick's base score, +0x60 its
// multiplier, +0x20 the last trick's score.
uint8_t* ScoreOf(Obj::CCompositeObject* skater)
{
    return static_cast<uint8_t*>(static_cast<Obj::CSkater*>(skater)->GetScore());
}
// (a player's skater's score through its SkaterScore component)
uint8_t* ComponentScoreOf(Obj::CCompositeObject* skater)
{
    return static_cast<Obj::CSkaterScoreComponent*>(skater->GetComponent(kSkaterScore))->m_score;
}
int TotalScore(uint8_t* score) { return reinterpret_cast<Mdl::Score*>(score)->GetTotalScore(); }
int LastScoreLanded(uint8_t* score) { return *reinterpret_cast<int*>(score + 0x20); }
// The score pot: the base score times the multiplier, truncated (x87 at 53 bits, then the original's _ftol).
int ScorePot(uint8_t* score)
{
    double pot = static_cast<double>(*reinterpret_cast<int*>(score + 0x14)) * *reinterpret_cast<float*>(score + 0x60);
    return static_cast<int>(static_cast<int64_t>(pot));
}
// The local skater's speed: the length of its velocity (as the original's x87: in double).
double SpeedOf(Obj::CCompositeObject* skater)
{
    double x = skater->m_vel.x, y = skater->m_vel.y, z = skater->m_vel.z;
    return std::sqrt(x * x + y * y + z * z);
}
// An integer parameter, 0 if missing.
int IntegerOrZero(Script::CStruct* params)
{
    int value = 0;
    return params->GetInteger(0u, &value, 0) ? value : 0;
}

// Message 0x8D (the eliminated player's object id, -1 for a tie) to the players.
void PrepareEliminated(Net::MsgDesc* desc, uint32_t* data)
{
    desc->m_id = kMsgEliminated;
    desc->m_length = 4;
    desc->m_data = data;
    desc->m_queue = Net::MsgDesc::kQueueSequenced;
    desc->m_groupId = 8;
}
void SendToPlayers(GameNet::Manager* manager, Net::MsgDesc* desc, bool observers)
{
    PlayerSearch search;
    for (PlayerInfo* player = FirstPlayer(manager, &search, observers); player;
         player = NextPlayer(manager, &search, observers))
        manager->m_server->EnqueueMessage(player->GetConnHandle(), desc);
}

} // namespace

// 0x005AC5F0 / 0x005AC660: SkaterSpeedGreaterThan / SkaterSpeedLessThan <speed> - the local skater's (none, or no
// speed given: false).
bool __cdecl CFunc_SkaterSpeedGreaterThan(Script::CStruct* params, Script::CScript*)
{
    float speed;
    if (!params->GetFloat(0u, &speed, 0))
        return false;
    Obj::CCompositeObject* skater = GetLocalSkater();
    return skater && SpeedOf(skater) > speed;
}

bool __cdecl CFunc_SkaterSpeedLessThan(Script::CStruct* params, Script::CScript*)
{
    float speed;
    if (!params->GetFloat(0u, &speed, 0))
        return false;
    Obj::CCompositeObject* skater = GetLocalSkater();
    return skater && SpeedOf(skater) < speed;
}

// 0x005AC6D0 / 0x005AC720: SkaterLastScoreLandedGreaterThan / LessThan <score> (0 if missing) - the local skater's
// last trick's score.
bool __cdecl CFunc_SkaterLastScoreLandedGreaterThan(Script::CStruct* params, Script::CScript*)
{
    int value = IntegerOrZero(params);
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && LastScoreLanded(score) > value;
}

bool __cdecl CFunc_SkaterLastScoreLandedLessThan(Script::CStruct* params, Script::CScript*)
{
    int value = IntegerOrZero(params);
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && LastScoreLanded(score) < value;
}

// 0x005AC770: SkaterGetLastScoreLanded - the local skater's last trick's score as last_score_landed (false without).
bool __cdecl CFunc_SkaterGetLastScoreLanded(Script::CStruct*, Script::CScript* script)
{
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    if (!score)
        return false;
    script->m_params->AddInteger(kLastScoreLanded, LastScoreLanded(score));
    return true;
}

// 0x005AC7B0: AccumulateScores - each player's total score added to the score accumulated over the session.
bool __cdecl CFunc_AccumulateScores(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    PlayerSearch unused; // (made and dropped by the original)
    (void)unused;
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false))
        player->AddToAccumulatedScore(TotalScore(ScoreOf(player->m_skater)));
    return true;
}

// 0x005AC850: EliminateLastPlacePlayer - with more than one player still in: if all have the same score, everyone is
// told nobody goes (-1); otherwise each player on the lowest score is marked eliminated and everyone (observers too)
// told its skater's id.
bool __cdecl CFunc_EliminateLastPlacePlayer(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    PlayerSearch others;
    int lowest = -1;
    int highest = 0;
    int inGame = 0;
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
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
    Net::MsgDesc desc;
    uint32_t data;
    if (highest == lowest) {
        desc.Construct();
        data = 0xFFFFFFFF;
        PrepareEliminated(&desc, &data);
        SendToPlayers(manager, &desc, false);
        return true;
    }
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated() || TotalScore(ScoreOf(player->m_skater)) != lowest)
            continue;
        desc.Construct();
        player->MarkEliminated();
        data = player->m_skater->m_id;
        PrepareEliminated(&desc, &data);
        SendToPlayers(manager, &desc, true);
    }
    return true;
}

// 0x005ACAA0: OnlyOnePlayerLeft - a team game: whether at most one team has players not eliminated; otherwise
// whether at most one player isn't (then num_players: how many).
bool __cdecl CFunc_OnlyOnePlayerLeft(Script::CStruct*, Script::CScript* script)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    if (IsTeamGame()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerInfo* player = FirstPlayer(manager, &search, false); player;
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
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (!player->IsEliminated())
            ++players;
    }
    if (players > 1)
        return false;
    script->m_params->AddInteger("num_players", players);
    return true;
}

// 0x005ACC20: AnySkaterTotalScoreAtLeast <score> (0 if missing) - whether any player's (a team game: any team's)
// total score reaches it.
bool __cdecl CFunc_AnySkaterTotalScoreAtLeast(Script::CStruct* params, Script::CScript*)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    int target = IntegerOrZero(params);
    if (IsTeamGame()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerInfo* player = FirstPlayer(manager, &search, false); player;
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
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->m_skater && TotalScore(ScoreOf(player->m_skater)) >= target)
            return true;
    }
    return false;
}

// 0x005ACD80: OnlyOneSkaterLeft - how many skaters (a team game: teams) have scored: false while players are still
// joining; with none scored the server in host mode 1 or 2 says true; otherwise whether exactly one has. (Not a team
// game: also false with exactly one skater.)
bool __cdecl CFunc_OnlyOneSkaterLeft(Script::CStruct*, Script::CScript*)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    NewPlayerSearch joining;
    int scored = 0;
    if (IsTeamGame()) {
        int teams[4] = {0, 0, 0, 0};
        for (PlayerInfo* player = FirstPlayer(manager, &search, false); player;
             player = NextPlayer(manager, &search, false)) {
            uint8_t* score = ScoreOf(player->m_skater);
            teams[player->m_team] += TotalScore(score);
        }
        for (int total : teams)
            scored += total > 0 ? 1 : 0;
        if (manager->FirstNewPlayerInfo(&joining))
            return false;
    } else {
        int skaters = 0;
        for (PlayerInfo* player = FirstPlayer(manager, &search, false); player;
             player = NextPlayer(manager, &search, false)) {
            if (!player->m_skater)
                continue;
            ++skaters;
            if (TotalScore(ScoreOf(player->m_skater)) > 0)
                ++scored;
        }
        if (manager->FirstNewPlayerInfo(&joining) || skaters == 1)
            return false;
    }
    if (manager->OnServer() && (manager->GetHostMode() == 1 || manager->GetHostMode() == 2) && scored == 0)
        return true;
    return scored == 1;
}

// 0x005ACF50 / 0x005ACFA0: SkaterTotalScoreGreaterThan / LessThan <score> (0 if missing) - the local skater's total.
bool __cdecl CFunc_SkaterTotalScoreGreaterThan(Script::CStruct* params, Script::CScript*)
{
    int value = IntegerOrZero(params);
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && TotalScore(score) > value;
}

bool __cdecl CFunc_SkaterTotalScoreLessThan(Script::CStruct* params, Script::CScript*)
{
    int value = IntegerOrZero(params);
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && TotalScore(score) < value;
}

// 0x005ACFF0: SkaterCurrentScorePotGreaterThan <score> [point_scale=<n>] - the local skater's score pot above score
// times point_scale (1). 0x005AD070: SkaterCurrentScorePotLessThan <score> (no scale).
bool __cdecl CFunc_SkaterCurrentScorePotGreaterThan(Script::CStruct* params, Script::CScript*)
{
    int scale = 1;
    int value = IntegerOrZero(params);
    params->GetInteger(kPointScale, &scale, 0);
    value *= scale;
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && ScorePot(score) > value;
}

bool __cdecl CFunc_SkaterCurrentScorePotLessThan(Script::CStruct* params, Script::CScript*)
{
    int value = IntegerOrZero(params);
    Obj::CCompositeObject* skater = GetLocalSkater();
    if (!skater)
        return false;
    uint8_t* score = ScoreOf(skater);
    return score && ScorePot(score) < value;
}

// 0x005AD0D0: SkaterGetScoreInfo - the local skater's scorepot, totalscore and lastscorelanded. (Without a local
// skater the original reads a null score: a crash, kept.)
bool __cdecl CFunc_SkaterGetScoreInfo(Script::CStruct*, Script::CScript* script)
{
    Obj::CCompositeObject* skater = GetLocalSkater();
    uint8_t* score = skater ? ScoreOf(skater) : nullptr;
    script->m_params->AddInteger(kScorePot, ScorePot(score));
    script->m_params->AddInteger(kTotalScore, TotalScore(score));
    script->m_params->AddInteger(kLastScoreLandedInfo, LastScoreLanded(score));
    return true;
}

// 0x005AD140: InFirstPlace - whether no other player still in has a higher total than the local player (who must be
// in the game - not eliminated, surveying or observing - with a skater, and with at least one other player in);
// points: the best other player's total.
bool __cdecl CFunc_InFirstPlace(Script::CStruct*, Script::CScript* script)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    int better = 0;
    int best = 0;
    auto* local = static_cast<PlayerInfo*>(manager->GetLocalPlayer());
    if (!local || local->IsEliminated() || local->IsSurveying() || local->IsObserver() || !local->m_skater)
        return false;
    int mine = TotalScore(ComponentScoreOf(local->m_skater));
    int inGame = 0;
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated())
            continue;
        ++inGame;
        uint8_t* score = ComponentScoreOf(player->m_skater);
        if (TotalScore(score) > mine)
            ++better;
        if (!player->IsLocalPlayer() && TotalScore(score) > best)
            best = TotalScore(score);
    }
    if (inGame < 2)
        return false;
    script->m_params->AddInteger("points", best);
    return better == 0;
}

// 0x005AD2D0: InLastPlace - whether no other player still in has a lower total than the local player (as
// InFirstPlace); then points: the lowest other player's total.
bool __cdecl CFunc_InLastPlace(Script::CStruct*, Script::CScript* script)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    int worse = 0;
    int lowest = -1;
    auto* local = static_cast<PlayerInfo*>(manager->GetLocalPlayer());
    if (!local || local->IsEliminated() || local->IsSurveying() || local->IsObserver() || !local->m_skater)
        return false;
    int mine = TotalScore(ComponentScoreOf(local->m_skater));
    int inGame = 0;
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
        if (player->IsEliminated())
            continue;
        ++inGame;
        uint8_t* score = ComponentScoreOf(player->m_skater);
        if (TotalScore(score) < mine)
            ++worse;
        if (player->IsLocalPlayer())
            continue;
        int total = TotalScore(score);
        if (lowest == -1)
            lowest = total;
        else if (total < lowest)
            lowest = TotalScore(score);
    }
    if (inGame < 2 || worse != 0)
        return false;
    script->m_params->AddInteger("points", lowest);
    return true;
}

// 0x005AD450: InSecondToLastPlace - whether exactly one player still in has a lower total than the local player (as
// InFirstPlace); then points: the local total less the lowest total (the local player's own counted).
bool __cdecl CFunc_InSecondToLastPlace(Script::CStruct*, Script::CScript* script)
{
    GameNet::Manager* manager = NetManager();
    PlayerSearch search;
    int worse = 0;
    int lowest = -1;
    auto* local = static_cast<PlayerInfo*>(manager->GetLocalPlayer());
    if (!local || local->IsEliminated() || local->IsSurveying() || local->IsObserver() || !local->m_skater)
        return false;
    int mine = TotalScore(ComponentScoreOf(local->m_skater));
    int inGame = 0;
    for (PlayerInfo* player = FirstPlayer(manager, &search, false); player; player = NextPlayer(manager, &search, false)) {
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
    script->m_params->AddInteger("points", mine - lowest);
    return true;
}

// 0x005AD5C0 / 0x005AD610: GoalsGreaterThan / GoalsEqualTo <n> - the career's goals got in all (false without n).
bool __cdecl CFunc_GoalsGreaterThan(Script::CStruct* params, Script::CScript*)
{
    float goals;
    if (!params->GetFloat(0u, &goals, 0))
        return false;
    return static_cast<double>(Career()->CountTotalGoals()) > goals;
}

bool __cdecl CFunc_GoalsEqualTo(Script::CStruct* params, Script::CScript*)
{
    float goals;
    if (!params->GetFloat(0u, &goals, 0))
        return false;
    return static_cast<double>(Career()->CountTotalGoals()) == goals;
}

// 0x005AD660: ToggleSkaterCamMode [skater=<n>] - skater n's (none or 0: the local player's) skater camera's mode
// toggled (0x004FA7D0).
bool __cdecl CFunc_ToggleSkaterCamMode(Script::CStruct* params, Script::CScript*)
{
    int index = 0; // (uninitialised in the original when missing: the game's scripts always give skater=0)
    params->GetInteger(kSkater, &index, 0);
    Obj::CCompositeObject* skater;
    if (index == 0) {
        auto* local = static_cast<PlayerInfo*>(NetManager()->GetLocalPlayer());
        if (!local)
            return true;
        skater = local->m_skater;
    } else {
        skater = GetSkater(index);
    }
    if (skater) {
        auto* camera = static_cast<Obj::CCompositeObject*>(static_cast<Obj::CSkater*>(skater)->GetSkaterCamera());
        static_cast<Obj::CSkaterCameraComponent*>(camera->GetComponent(kSkaterCamera))->NextMode();
    }
    return true;
}

// 0x005AD6C0: GetSkaterID [skater=<n>] - skater n's (none: the local skater's) object id as objID, if it's local.
bool __cdecl CFunc_GetSkaterID(Script::CStruct* params, Script::CScript* script)
{
    int index = 0;
    Obj::CCompositeObject* skater = params->GetInteger(kSkater, &index, 0) ? GetSkater(index) : GetLocalSkater();
    if (!skater || !skater->m_unknownBC)
        return false;
    script->m_params->AddChecksum(kObjId, skater->m_id);
    return true;
}

// 0x005AD730: SkaterExists skater=<n> - whether skater n exists; its object id as objID.
bool __cdecl CFunc_SkaterExists(Script::CStruct* params, Script::CScript* script)
{
    int index = 0; // (uninitialised in the original)
    if (!params->GetInteger(kSkater, &index, 1))
        return false;
    Obj::CCompositeObject* skater = GetSkater(index);
    if (!skater)
        return false;
    script->m_params->AddChecksum(kObjId, skater->m_id);
    return true;
}

// 0x005AD780: GetSkaterSex player=<n> - player n's profile's is_male.
bool __cdecl CFunc_GetSkaterSex(Script::CStruct* params, Script::CScript* script)
{
    int player = 0; // (uninitialised in the original, as is_male)
    params->GetInteger(kPlayer, &player, 1);
    Obj::CSkaterProfile* profile = Obj::g_profileManager->GetProfile(player);
    int male = 0;
    profile->InfoStructure()->GetInteger(kIsMale, &male, 1);
    script->m_params->AddInteger(kIsMale, male);
    return true;
}

// 0x005AD7E0: GetCurrentSkaterID - the script's object's id as objID and intId, if it's a local skater (any object
// with the composite object's +0xBC set).
bool __cdecl CFunc_GetCurrentSkaterID(Script::CStruct*, Script::CScript* script)
{
    auto* object = static_cast<Obj::CCompositeObject*>(script->m_object.Get());
    if (!object || !object->m_unknownBC)
        return false;
    uint32_t id = object->m_id;
    script->m_params->AddChecksum(kObjId, id);
    script->m_params->AddInteger(kIntId, static_cast<int>(id));
    return true;
}

// 0x005B0070: GetSkaterPosition - the script's object's position as integers x, y, z (truncated); false without an
// object.
bool __cdecl CFunc_GetSkaterPosition(Script::CStruct*, Script::CScript* script)
{
    auto* object = static_cast<Obj::CCompositeObject*>(script->m_object.Get());
    if (!object)
        return false;
    float y = object->m_pos.y, z = object->m_pos.z;
    script->m_params->AddInteger("x", static_cast<int>(static_cast<int64_t>(object->m_pos.x))); // (the original's _ftol)
    script->m_params->AddInteger("y", static_cast<int>(static_cast<int64_t>(y)));
    script->m_params->AddInteger("z", static_cast<int>(static_cast<int64_t>(z)));
    return true;
}

// 0x005B00F0: SetSkaterVelocity <speed> | vel_x= vel_y= vel_z= - the script's object's velocity: its direction kept
// at the speed given (all four components scaled), or the vector given (w 1; built on the heap and freed, as the
// original's new Mth::Vector). False without an object.
bool __cdecl CFunc_SetSkaterVelocity(Script::CStruct* params, Script::CScript* script)
{
    auto* object = static_cast<Obj::CCompositeObject*>(script->m_object.Get());
    if (!object)
        return false;
    float speed = 0.0f;
    if (params->GetFloat(0u, &speed, 0)) {
        Mth::Vector direction = object->m_vel;
        direction.Normalize();
        object->m_vel.x = direction.x * speed;
        object->m_vel.y = direction.y * speed;
        object->m_vel.z = direction.z * speed;
        object->m_vel.w = direction.w * speed;
        return true;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f;
    params->GetFloat(kVelX, &x, 1);
    params->GetFloat(kVelY, &y, 1);
    params->GetFloat(kVelZ, &z, 1);
    if (!Mem::g_managerReady)
        Mem::EnsureManager();
    auto* velocity = static_cast<Mth::Vector*>(Mem::Manager::Get()->Allocate(sizeof(Mth::Vector), 1, nullptr));
    velocity->x = x;
    velocity->y = y;
    velocity->z = z;
    velocity->w = 1.0f;
    object->m_vel = *velocity;
    Mem::Manager::Get()->Free(velocity);
    return true;
}

} // namespace CFuncs

REPLACE(0x005B0070, CFuncs::CFunc_GetSkaterPosition);
REPLACE(0x005B00F0, CFuncs::CFunc_SetSkaterVelocity);
REPLACE(0x005AC5F0, CFuncs::CFunc_SkaterSpeedGreaterThan);
REPLACE(0x005AC660, CFuncs::CFunc_SkaterSpeedLessThan);
REPLACE(0x005AC6D0, CFuncs::CFunc_SkaterLastScoreLandedGreaterThan);
REPLACE(0x005AC720, CFuncs::CFunc_SkaterLastScoreLandedLessThan);
REPLACE(0x005AC770, CFuncs::CFunc_SkaterGetLastScoreLanded);
REPLACE(0x005AC7B0, CFuncs::CFunc_AccumulateScores);
REPLACE(0x005AC850, CFuncs::CFunc_EliminateLastPlacePlayer);
REPLACE(0x005ACAA0, CFuncs::CFunc_OnlyOnePlayerLeft);
REPLACE(0x005ACC20, CFuncs::CFunc_AnySkaterTotalScoreAtLeast);
REPLACE(0x005ACD80, CFuncs::CFunc_OnlyOneSkaterLeft);
REPLACE(0x005ACF50, CFuncs::CFunc_SkaterTotalScoreGreaterThan);
REPLACE(0x005ACFA0, CFuncs::CFunc_SkaterTotalScoreLessThan);
REPLACE(0x005ACFF0, CFuncs::CFunc_SkaterCurrentScorePotGreaterThan);
REPLACE(0x005AD070, CFuncs::CFunc_SkaterCurrentScorePotLessThan);
REPLACE(0x005AD0D0, CFuncs::CFunc_SkaterGetScoreInfo);
REPLACE(0x005AD140, CFuncs::CFunc_InFirstPlace);
REPLACE(0x005AD2D0, CFuncs::CFunc_InLastPlace);
REPLACE(0x005AD450, CFuncs::CFunc_InSecondToLastPlace);
REPLACE(0x005AD5C0, CFuncs::CFunc_GoalsGreaterThan);
REPLACE(0x005AD610, CFuncs::CFunc_GoalsEqualTo);
REPLACE(0x005AD660, CFuncs::CFunc_ToggleSkaterCamMode);
REPLACE(0x005AD6C0, CFuncs::CFunc_GetSkaterID);
REPLACE(0x005AD730, CFuncs::CFunc_SkaterExists);
REPLACE(0x005AD780, CFuncs::CFunc_GetSkaterSex);
REPLACE(0x005AD7E0, CFuncs::CFunc_GetCurrentSkaterID);
