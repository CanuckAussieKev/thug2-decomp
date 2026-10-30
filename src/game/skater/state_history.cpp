// The skater's state history component and its network collisions. See state_history.h.

#include "game/skater/state_history.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_dispatcher.h"
#include "game/net/player_info.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "game/skater/walk_physics.h"
#include "game/sys/timer.h"
#include "game/script/game_util_cfuncs.h"
#include "game/skate/game_mode.h"
#include "game/skate/score.h"
#include "game/skate/skate_module.h"
#include "game/skater/core_physics.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064C174);
void** const kPlayerSearchVtable = reinterpret_cast<void**>(0x0064C1A8);

// Components of the skater this code asks for.
constexpr uint32_t kSkaterState = 0x43686585;          // +0x70 in a bail, +0x118 driving
constexpr uint32_t kSkaterPhysicsControl = 0xD9151D4D; // +0x25 a flag; +0x2C its state component
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kWalk = 0x726E85AA;
constexpr uint32_t kInput = 0x27D7CD28;

// Game types (the game mode's 0x005532A0).
constexpr uint32_t kGameTag = 0x6EF8FDA0;
constexpr uint32_t kGameTag2 = 0x5D32129C;
constexpr uint32_t kGameTeamTag = 0x6C5FF266;

// Network messages.
constexpr uint8_t kMsgRanInto = 0x43;  // to the player run into: 0x14 bytes (the other's id | driving, the push)
constexpr uint8_t kMsgRanIntoAck = 0x44; // to the one running into it: 1 byte (the other's id | driving)
constexpr uint8_t kMsgHit = 0x84;
constexpr uint8_t kMsgHitAck = 0x85;
constexpr uint8_t kMsgTagged = 0x55;     // a game of tag: who was tagged (1 byte), to every other player
constexpr uint8_t kMsgTeamTagged = 0x73; // team tag, the same team: who (and the team), 2 bytes
constexpr uint8_t kMsgTeamConverted = 0x74; // team tag, another team

// The default name in the events: "Someone" (0x0064C1C4); the names the code looks up.
const char* const kSomeone = reinterpret_cast<const char*>(0x0064C1C4);
const char* const kUnbindFireballTricks = reinterpret_cast<const char*>(0x0064C1AC); // "unbind_fireball_tricks"
const char* const kDrivingRadiusBoost = reinterpret_cast<const char*>(0x0064C1CC);   // "driving_radius_boost"

// a - b as the original's x87 code rounds it when it stores it.
float Difference(float a, float b) { return static_cast<float>(static_cast<double>(a) - b); }

template <typename T>
T& Field(const void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
}

// --- the network game manager (0x007CCE14), its players and the game mode ---
GameNet::Manager* NetManager() { return GameNet::g_gameNetManager; }
void* GameMode() { return static_cast<void*>(Game::g_gameMode); }
void* SkaterManager() { return static_cast<void*>(Mdl::g_skateModule); }

uint8_t* PlayerByObjectId(int id) { return NetManager()->PlayerByObjectId(static_cast<uint16_t>(id)); }
uint8_t* FirstPlayer(PlayerSearchDestructors* search)
{
    return static_cast<uint8_t*>(NetManager()->FirstPlayerInfo(search, true));
}
uint8_t* NextPlayer(PlayerSearchDestructors* search)
{
    return static_cast<uint8_t*>(NetManager()->NextPlayerInfo(search, true));
}
bool OnServer() { return NetManager()->OnServer(); }
bool ManagerFlag4() { return NetManager()->InInternetMode(); }
bool ManagerMayCollide() { return NetManager()->PlayerCollisionEnabled(); }
bool ManagerFlag3A() { return NetManager()->IsGameOver(); }
// The manager's +0x6C (the network code's GetCrown, 0x00533210).
CCompositeObject* LocalSkater() { return static_cast<CCompositeObject*>(NetManager()->GetCrown()); }
// The manager's server app (+0x04: the folded getter 0x005480C0).
Net::App* ManagerApp() { return NetManager()->m_server; }

// A player (PlayerInfo): its object (+0x14), team field (+0x30) and flags (+0x1D0: 0x1 local, 0x80 "it" - IsKing,
// 0x7800 a team - IsCarryingFlag / GetFlagTeam). Its announcements: MarkAsKing makes it "it", RetrievedFlag tags it
// on its own team, StoleFlag converts it to another.
GameNet::PlayerInfo* Info(uint8_t* player) { return reinterpret_cast<GameNet::PlayerInfo*>(player); }
CCompositeObject* PlayerObject(uint8_t* player) { return Field<CCompositeObject*>(player, 0x14); }
bool IsObserving(uint8_t* player) { return Info(player)->IsInvulnerable(); }
bool IsIt(uint8_t* player) { return Info(player)->IsKing() != 0; }
bool OnATeam(uint8_t* player) { return Info(player)->IsCarryingFlag() != 0; }
int Team(uint8_t* player) { return Info(player)->GetFlagTeam(); }
bool IsLocal(uint8_t* player) { return Info(player)->IsLocalPlayer() != 0; }
int Connection(uint8_t* player) { return Info(player)->GetConnHandle(); }
void MarkTagTime(uint8_t* player) { Info(player)->MarkJustJoined(); }
void MakeIt(uint8_t* player, bool it) { Info(player)->MarkAsKing(it); }
void TagOnTeam(uint8_t* player) { Info(player)->RetrievedFlag(); }
void ConvertToTeam(uint8_t* player, int team) { Info(player)->StoleFlag(team); }

uint32_t GameType() { return static_cast<Game::CGameMode*>(GameMode())->GetNameChecksum(); }
bool TeamGame() { return static_cast<Game::CGameMode*>(GameMode())->IsTeamGame() != 0; }
bool TagGameOver() { return static_cast<Game::CGameMode*>(GameMode())->EndConditionsMet(); }
bool InSlapGame() { return CFuncs::CFunc_InSlapGame(nullptr, nullptr); }

CCompositeObject* SkaterByIndex(int index)
{
    return static_cast<Mdl::Skate*>(SkaterManager())->GetSkater(index);
}
const char* SkaterName(CCompositeObject* skater) { return static_cast<CSkater*>(skater)->GetDisplayName(); }
// A skater's score (its +0x278's +0x18).
void* SkaterScore(CCompositeObject* skater) { return Field<void*>(Field<void*>(skater, 0x278), 0x18); }
int GetScore(void* score) { return static_cast<Mdl::Score*>(score)->GetTotalScore(); }
void SetScore(void* score, int value) { static_cast<Mdl::Score*>(score)->SetTotalScore(value); }

// Knocked over by `other`: a bail on the core physics, or the walk component's when walking or driving.
void KnockDown(CBaseComponent* physics, CCompositeObject* other)
{
    reinterpret_cast<Skater::CorePhysicsComponent*>(physics)->ResetToAir(other);
}
void KnockDownWalking(CBaseComponent* walk, CCompositeObject* other)
{
    static_cast<CWalkComponent*>(walk)->GoToAirFor(other);
}

// A vector's length as 0x004023C0 leaves it on the x87 stack (x, y, then z; unrounded).
double LengthOf(const Mth::Vector& v)
{
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y + static_cast<double>(v.z) * v.z);
}

// The knock-back velocity both handlers give: the unit direction away from the other times 300, then 1.5, each
// component rounded to a float after each (except where noted), and 200 up.
constexpr float kKnockBack = 300.0f;  // 0x0064675C
constexpr float kKnockScale = 1.5f;   // 0x00646744
constexpr float kKnockUp = 200.0f;
constexpr float kTooClose = 0.0001f;  // 0x006453D4

// strncpy(name, from, 0x20) (the game's C runtime's 0x006239E0): up to 0x20 characters, the rest zeroed.
void CopyName(char (&name)[0x20], const char* from)
{
    size_t i = 0;
    for (; i < sizeof(name) && from[i]; ++i)
        name[i] = from[i];
    for (; i < sizeof(name); ++i)
        name[i] = 0;
}

// Both handlers' event: the other's name (and what the caller adds) to the skater.
void SendEvent(CCompositeObject* skater, uint32_t type, const char* name, uint32_t flag, bool withFlag,
               const Script::VectorValue* push)
{
    Script::CStruct params;
    params.Init();
    params.AddString(0xA4B08520, name); // name
    if (push)
        params.AddVector(0xA6F5352F, *push);
    if (withFlag)
        params.AddChecksum(0u, flag);
    skater->SelfEvent(type, &params);
    params.Clear();
}

} // namespace

// --- StateHistoryMessage ---

// 0x00519800
uint32_t StateHistoryMessage::GetTime() { return static_cast<uint32_t>(m_timeHigh) << 16 | m_time; }

// 0x00519810
void StateHistoryMessage::SetTime(uint32_t time)
{
    m_time = static_cast<uint16_t>(time);
    m_timeHigh = static_cast<uint16_t>(time >> 16);
}

// --- PlayerSearchDestructors ---

// 0x00519830
void PlayerSearchDestructors::DestroyBody() { m_vtable = kPlayerSearchVtable; }

// 0x0051A120
PlayerSearchDestructors* PlayerSearchDestructors::Destroy(unsigned int flags)
{
    m_vtable = kPlayerSearchVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// --- CSkaterStateHistoryComponent ---

// 0x0051AD30
CBaseComponent* __cdecl CSkaterStateHistoryComponent::Create()
{
    void* memory = Mem::Calloc(0x1108);
    return memory ? static_cast<CSkaterStateHistoryComponent*>(memory)->Construct() : nullptr;
}

// 0x0051ACB0
CSkaterStateHistoryComponent* CSkaterStateHistoryComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    // (The original clears each state entry's +0x68 / +0x6C and the entry, the messages, the counts, then the
    // entries and the messages again.)
    std::memset(reinterpret_cast<uint8_t*>(this) + 0x1C, 0, kEntries * 0x84);
    std::memset(reinterpret_cast<uint8_t*>(this) + 0xA70, 0, kEntries * sizeof(StateHistoryMessage));
    Field<uint32_t>(this, 0x1100) = 0;
    Field<int>(this, 0x18) = 0;  // the states received
    Field<int>(this, 0xA6C) = 0; // the messages received
    m_type = kType;
    return this;
}

// 0x00519840
CSkaterStateHistoryComponent* CSkaterStateHistoryComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00519FA0
void CSkaterStateHistoryComponent::GetFrameMovement(Mth::Vector* out)
{
    const Mth::Vector& vel = m_object->m_vel;
    double frame = Tmr::GetFrameSeconds();
    float rounded = static_cast<float>(frame);
    out->x = rounded * vel.x;
    out->y = rounded * vel.y;
    out->z = rounded * vel.z;
    out->w = static_cast<float>(frame * vel.w);
}

// 0x00519FF0
void CSkaterStateHistoryComponent::GetPosition(Mth::Vector* out) { *out = m_object->m_pos; }

// 0x0051A020
double CSkaterStateHistoryComponent::GetCollisionReach(bool driving)
{
    float reach = Script::GetFloat(m_object->m_unknownBC ? 0x0D24273Bu : 0x25F78481u);
    if (!driving)
        return reach;
    return (Script::GetFloat(0x0A49FF23u) + 1.0) * reach;
}

// 0x0051A080
double CSkaterStateHistoryComponent::GetCollisionRadius(bool myDriving, bool theirDriving)
{
    uint32_t name;
    if (m_object->m_unknownBC)
        name = ManagerFlag4() ? 0xF2E35FA3u : 0xECB41860u;
    else
        name = ManagerFlag4() ? 0xDBD6D58Au : 0xC5819249u;
    float radius = Script::GetFloat(name);
    float drivers = myDriving ? 1.0f : 0.0f;
    if (theirDriving)
        drivers = static_cast<float>(drivers + 1.0);
    return (Script::GetFloat(0xEEF4AFB3u) * static_cast<double>(drivers) + 1.0) * radius;
}

// 0x0051A150
bool CSkaterStateHistoryComponent::IsNearLocalSkater()
{
    CCompositeObject* local = LocalSkater();
    const Mth::Vector& pos = m_object->m_pos;
    float dx = Difference(pos.x, local->m_pos.x);
    double dy = static_cast<double>(pos.y) - local->m_pos.y;
    double dz = static_cast<double>(pos.z) - local->m_pos.z;
    double distance = std::sqrt((dz * dz + dy * dy) + static_cast<double>(dx) * dx);
    return distance < 96.0f; // 0x0064EC40
}

// 0x0051A1C0
void CSkaterStateHistoryComponent::CheckForCollisions(int first)
{
    uint8_t* me = PlayerByObjectId(static_cast<int>(m_object->m_id));
    if (!me || IsObserving(me))
        return;
    if (!ManagerMayCollide() && !IsIt(me) && !OnATeam(me))
        return;
    CBaseComponent* myState = m_object->GetComponent(kSkaterState);
    // Whether this machine tells the players of a hit (not when the skater is in a bail, outside a slap game).
    bool tellPlayers;
    if (!Field<bool>(myState, 0x70) || InSlapGame()) {
        tellPlayers = true;
    } else {
        tellPlayers = false;
        if (!IsIt(me) && !OnATeam(me))
            return;
    }
    bool myDriving = Field<bool>(myState, 0x118);
    Mth::Vector myPos = m_object->m_pos;
    Mth::Vector myMove;
    GetFrameMovement(&myMove);
    Mth::Vector myDir = myMove;
    myDir.Normalize();
    Mth::Line mine;
    mine.start = myPos;

    for (int i = first; i < 8; ++i) {
        CCompositeObject* skater = SkaterByIndex(i);
        if (!skater || skater == m_object)
            continue;
        CBaseComponent* theirState = skater->GetComponent(kSkaterState);
        if (Field<bool>(theirState, 0x70))
            continue;
        uint8_t* them = PlayerByObjectId(static_cast<int>(skater->m_id));
        if (IsObserving(them) || IsIt(them))
            continue;
        if (OnATeam(me) && OnATeam(them) && Team(me) != Field<int>(them, 0x30))
            continue;
        if (TeamGame() && Field<int>(them, 0x30) == Field<int>(me, 0x30))
            continue;

        auto* theirHistory = Field<CSkaterStateHistoryComponent*>(skater, 0x27C);
        bool theirDriving = Field<bool>(theirState, 0x118);
        Mth::Vector theirPos;
        theirHistory->GetPosition(&theirPos);
        Mth::Vector theirMove;
        theirHistory->GetFrameMovement(&theirMove);
        Mth::Vector theirDir = theirMove;
        theirDir.Normalize();
        if (LengthOf(theirMove) > 100.0f) // 0x006463D4 (NaN carries on)
            continue;

        float radius = static_cast<float>(GetCollisionRadius(myDriving, theirDriving));
        // (The original looks driving_radius_boost up for each driver and drops it.)
        if (myDriving)
            Script::GetFloat(kDrivingRadiusBoost);
        if (theirDriving)
            Script::GetFloat(kDrivingRadiusBoost);
        mine.end = mine.start + myDir * static_cast<float>(GetCollisionReach(myDriving));
        Mth::Line theirs;
        theirs.start = theirPos;
        theirs.end = theirs.start + theirDir * static_cast<float>(theirHistory->GetCollisionReach(theirDriving));

        // Where they meet: the segments' closest points, else the first of the four pairs of ends close enough.
        Mth::Vector myPoint, theirPoint;
        float scratch;
        if (Mth::ClosestPointsBetweenLines(mine, theirs, myPoint, theirPoint, scratch, scratch, true)) {
            if (!(LengthOf(myPoint - theirPoint) < radius))
                continue;
        } else if (LengthOf(theirs.start - mine.start) < radius) {
            theirPoint = theirs.start;
            myPoint = mine.start;
        } else if (LengthOf(theirs.start - mine.end) < radius) {
            theirPoint = theirs.start;
            myPoint = mine.end;
        } else if (LengthOf(theirs.end - mine.start) < radius) {
            theirPoint = theirs.end;
            myPoint = mine.start;
        } else if (LengthOf(theirs.end - mine.end) < radius) {
            theirPoint = theirs.end;
            myPoint = mine.end;
        } else {
            continue;
        }

        // Who hits whom: the faster skater (a driver beats one on foot); otherwise only the game's "it" does.
        bool mustBeIt;
        if (myDriving == theirDriving) {
            float mySpeed = static_cast<float>(LengthOf(myMove));
            mustBeIt = LengthOf(theirMove) < mySpeed;
        } else {
            mustBeIt = myDriving && !theirDriving;
        }
        if (mustBeIt && !IsIt(me))
            continue;

        Net::App* app = ManagerApp();
        if (tellPlayers) {
            Net::MsgDesc msg;
            msg.Construct();
            struct {
                uint8_t id;
                uint8_t pad[3];
                Mth::Vector push;
            } ranInto = {};
            ranInto.id = static_cast<uint8_t>(theirDriving * 0x80 | static_cast<uint8_t>(skater->m_id));
            ranInto.push = myPoint - theirPoint;
            if (CBaseComponent* input = m_object->GetComponent(kInput))
                Field<uint8_t>(input, 0x498) = 1;
            MarkTagTime(me);
            msg.m_id = kMsgRanInto;
            msg.m_data = &ranInto;
            msg.m_length = 0x14;
            msg.m_queue = Net::MsgDesc::kQueueSequenced;
            msg.m_groupId = 8;
            app->EnqueueMessage(Connection(me), &msg);
            uint8_t ranIntoBy = static_cast<uint8_t>(myDriving * 0x80 | static_cast<uint8_t>(m_object->m_id));
            msg.m_id = kMsgRanIntoAck;
            msg.m_data = &ranIntoBy;
            msg.m_length = 1;
            app->EnqueueMessage(Connection(them), &msg);
            if (InSlapGame()) {
                void* score = SkaterScore(skater);
                SetScore(score, GetScore(score) + 1);
            }
        }

        uint32_t type = GameType();
        if (type == kGameTag || GameType() == kGameTag2) {
            if (TagGameOver())
                return;
            if (!IsIt(me))
                continue;
            MakeIt(them, true);
            PlayerSearchDestructors search{kPlayerSearchVtable, nullptr};
            for (uint8_t* player = FirstPlayer(&search); player; player = NextPlayer(&search)) {
                if (IsLocal(player))
                    continue;
                Net::MsgDesc msg;
                msg.Construct();
                uint8_t tagged = static_cast<uint8_t>(skater->m_id);
                msg.m_data = &tagged;
                msg.m_id = kMsgTagged;
                msg.m_length = 1;
                msg.m_queue = Net::MsgDesc::kQueueSequenced;
                msg.m_groupId = 8;
                app->EnqueueMessage(Connection(player), &msg);
            }
            search.m_vtable = kPlayerSearchVtable;
        } else if (GameType() == kGameTeamTag) {
            if (ManagerFlag3A())
                return;
            if (!OnATeam(me))
                continue;
            Net::MsgDesc msg;
            msg.Construct();
            uint8_t tagged[2];
            tagged[0] = static_cast<uint8_t>(skater->m_id);
            tagged[1] = static_cast<uint8_t>(Team(me));
            bool sameTeam = Team(me) == Field<int>(them, 0x30);
            PlayerSearchDestructors search{kPlayerSearchVtable, nullptr};
            for (uint8_t* player = FirstPlayer(&search); player; player = NextPlayer(&search)) {
                if (IsLocal(player)) {
                    if (sameTeam)
                        TagOnTeam(them);
                    else
                        ConvertToTeam(them, Team(me));
                    continue;
                }
                msg.m_id = sameTeam ? kMsgTeamTagged : kMsgTeamConverted;
                msg.m_data = tagged;
                msg.m_groupId = 8;
                msg.m_queue = Net::MsgDesc::kQueueSequenced;
                msg.m_length = 2;
                app->EnqueueMessage(Connection(player), &msg);
            }
            search.m_vtable = kPlayerSearchVtable;
        }
    }
}

// 0x00519870
int __cdecl CSkaterStateHistoryComponent::HandleHitMessages(Net::MsgHandlerContext* context)
{
    uint8_t* msg = reinterpret_cast<uint8_t*>(context->m_Msg);
    bool otherDriving = msg[1] != 0;
    auto* skater = static_cast<CCompositeObject*>(context->m_data);
    char name[0x20] = {};
    std::memcpy(name, kSomeone, 8);
    if (context->m_MsgId == kMsgHit) {
        if (uint8_t* player = PlayerByObjectId(static_cast<int8_t>(msg[0]))) {
            CCompositeObject* other = PlayerObject(player);
            // Away from the other skater (straight along x and z if they're on top of each other).
            double dx = static_cast<double>(skater->m_pos.x) - other->m_pos.x;
            float dy = Difference(skater->m_pos.y, other->m_pos.y);
            float dz = Difference(skater->m_pos.z, other->m_pos.z);
            float dw = Difference(skater->m_pos.w, other->m_pos.w);
            double length = std::sqrt((dx * dx + static_cast<double>(dy) * dy) + static_cast<double>(dz) * dz);
            double x = 1.0, y = 0.0, z = 1.0, w = 1.0;
            if (!(length < kTooClose)) {
                float inverse = static_cast<float>(1.0 / length);
                x = dx * inverse;
                y = static_cast<double>(inverse) * dy;
                z = static_cast<double>(inverse) * dz;
                w = static_cast<double>(inverse) * dw;
            }
            float wk = static_cast<float>(w * kKnockBack), zk = static_cast<float>(z * kKnockBack);
            float yk = static_cast<float>(y * kKnockBack), xk = static_cast<float>(x * kKnockBack);
            skater->m_vel.x = xk * kKnockScale;
            skater->m_vel.y = yk * kKnockScale;
            skater->m_vel.z = zk * kKnockScale;
            skater->m_vel.w = wk * kKnockScale;
            skater->m_vel.y = kKnockUp;
            CBaseComponent* control = skater->GetComponent(kSkaterPhysicsControl);
            uint8_t* state = Field<uint8_t*>(control, 0x2C);
            if (!Field<bool>(state, 0x118) && Field<int>(state, 0x1C) == 0)
                KnockDown(skater->GetComponent(kSkaterCorePhysics), other);
            else
                KnockDownWalking(skater->GetComponent(kWalk), other);
            CopyName(name, SkaterName(PlayerObject(player)));
            if (!OnServer()) {
                void* score = SkaterScore(skater);
                int lost = Field<int>(msg, 4);
                if (GetScore(score) - lost < 0) {
                    SetScore(score, 0);
                    Script::RunScript(kUnbindFireballTricks, nullptr, nullptr, false);
                } else {
                    SetScore(score, GetScore(score) - lost);
                }
            }
        }
        SendEvent(skater, 0x915E5E39, name, otherDriving ? 0xD039432Cu : 0xC304BD92u, true, nullptr);
    } else if (context->m_MsgId == kMsgHitAck) {
        if (uint8_t* player = PlayerByObjectId(static_cast<int8_t>(msg[0])))
            CopyName(name, SkaterName(PlayerObject(player)));
        SendEvent(skater, 0xA1021AF0, name, otherDriving ? 0xD039432Cu : 0xC304BD92u, true, nullptr);
    }
    return Net::kHandlerMsgDone;
}

// 0x00519C10
int __cdecl CSkaterStateHistoryComponent::HandleCollisionMessages(Net::MsgHandlerContext* context)
{
    auto* skater = static_cast<CCompositeObject*>(context->m_data);
    uint8_t id = context->m_MsgId;
    if (id != kMsgRanInto && id != kMsgRanIntoAck)
        return Net::kHandlerContinue;
    uint8_t* msg = reinterpret_cast<uint8_t*>(context->m_Msg);
    char name[0x20] = {};
    std::memcpy(name, kSomeone, 8);
    bool otherDriving = (msg[0] >> 7) & 1;
    msg[0] &= 0x7F;
    uint8_t* player = PlayerByObjectId(static_cast<int8_t>(msg[0]));
    if (id == kMsgRanIntoAck) {
        if (player)
            CopyName(name, SkaterName(PlayerObject(player)));
        SendEvent(skater, 0xA1021AF0, name, 0x7FD0663D, otherDriving, nullptr);
        return Net::kHandlerContinue;
    }
    if (player) {
        CBaseComponent* control = skater->GetComponent(kSkaterPhysicsControl);
        if (!Field<bool>(Field<uint8_t*>(control, 0x2C), 0x118)) {
            CCompositeObject* other = PlayerObject(player);
            double dw = static_cast<double>(skater->m_pos.w) - other->m_pos.w;
            double dz = static_cast<double>(skater->m_pos.z) - other->m_pos.z;
            double dy = static_cast<double>(skater->m_pos.y) - other->m_pos.y;
            float away[4] = {Difference(skater->m_pos.x, other->m_pos.x), static_cast<float>(dy), static_cast<float>(dz),
                             static_cast<float>(dw)};
            double length = std::sqrt((static_cast<double>(away[0]) * away[0] + dy * dy) + dz * dz);
            double w, z;
            if (!(length < kTooClose)) {
                // (0x00423650: each component times 1 / the length rounded to a float)
                double inverse = 1.0 / static_cast<float>(length);
                for (float& c : away)
                    c = static_cast<float>(inverse * c);
                z = away[2];
                w = away[3];
            } else {
                away[0] = 1.0f;
                away[1] = 0.0f;
                z = 1.0;
                w = 1.0;
            }
            double wk = w * kKnockBack; // (kept unrounded)
            float zk = static_cast<float>(z * kKnockBack);
            float yk = away[1] * kKnockBack;
            float xk = away[0] * kKnockBack;
            skater->m_vel.x = xk * kKnockScale;
            skater->m_vel.y = yk * kKnockScale;
            skater->m_vel.z = zk * kKnockScale;
            skater->m_vel.w = static_cast<float>(wk * kKnockScale);
            skater->m_vel.y = kKnockUp;
            uint8_t* state = Field<uint8_t*>(control, 0x2C);
            if (!Field<bool>(state, 0x118) && Field<int>(state, 0x1C) == 0 && !Field<bool>(control, 0x25))
                KnockDown(skater->GetComponent(kSkaterCorePhysics), other);
            else
                KnockDownWalking(skater->GetComponent(kWalk), other);
        }
        CopyName(name, SkaterName(PlayerObject(player)));
    }
    Script::VectorValue push;
    std::memcpy(&push, msg + 4, sizeof(push));
    SendEvent(skater, 0x915E5E39, name, 0x2F679251, otherDriving, &push);
    return Net::kHandlerContinue;
}

} // namespace Obj

REPLACE(0x00519800, &Obj::StateHistoryMessage::GetTime);
REPLACE(0x00519810, &Obj::StateHistoryMessage::SetTime);
REPLACE(0x00519830, &Obj::PlayerSearchDestructors::DestroyBody);
REPLACE(0x0051A120, &Obj::PlayerSearchDestructors::Destroy);
REPLACE(0x0051AD30, Obj::CSkaterStateHistoryComponent::Create);
REPLACE(0x0051ACB0, &Obj::CSkaterStateHistoryComponent::Construct);
REPLACE(0x00519840, &Obj::CSkaterStateHistoryComponent::Destroy);
REPLACE(0x00519FA0, &Obj::CSkaterStateHistoryComponent::GetFrameMovement);
REPLACE(0x00519FF0, &Obj::CSkaterStateHistoryComponent::GetPosition);
REPLACE(0x0051A020, &Obj::CSkaterStateHistoryComponent::GetCollisionReach);
REPLACE(0x0051A080, &Obj::CSkaterStateHistoryComponent::GetCollisionRadius);
REPLACE(0x0051A150, &Obj::CSkaterStateHistoryComponent::IsNearLocalSkater);
REPLACE(0x0051A1C0, &Obj::CSkaterStateHistoryComponent::CheckForCollisions);
REPLACE(0x00519870, Obj::CSkaterStateHistoryComponent::HandleHitMessages);
REPLACE(0x00519C10, Obj::CSkaterStateHistoryComponent::HandleCollisionMessages);

namespace Obj {

// 0x00569680
uint8_t* CSkaterStateHistoryComponent::GetLatestState()
{
    int count = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(this) + 0x18);
    return reinterpret_cast<uint8_t*>(this) + 0x1C + ((count + 19) % kEntries) * 0x84;
}

// 0x005696A0
StateHistoryMessage* CSkaterStateHistoryComponent::GetLatestMessage()
{
    int count = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(this) + 0xA6C);
    return reinterpret_cast<StateHistoryMessage*>(reinterpret_cast<uint8_t*>(this) + 0xA70 + (count % kEntries) * 0x54);
}

} // namespace Obj

REPLACE(0x00569680, &Obj::CSkaterStateHistoryComponent::GetLatestState);
REPLACE(0x005696A0, &Obj::CSkaterStateHistoryComponent::GetLatestMessage);
