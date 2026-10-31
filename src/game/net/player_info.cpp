// The network game's players: OnlinePlay::PlayerRecord and JoinerRecord (see player_info.h).

#include "game/net/player_info.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/obj/object.h"
#include "game/skate/profile_manager.h"
#include "game/sys/timer.h"
#include "game/core/code_address.h"
#include "game/net/gamenet_lists.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/goals/goal_ped.h"
#include "game/skate/score.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "game/gfx/face_texture.h"
#include "game/gfx/skeleton.h"
#include "game/ed/goal_editor.h"
#include "game/ed/rail_editor.h"
#include "game/net/server_list.h"
#include "game/net/score_list.h"
#include "game/net/buddy_man.h"
#include "game/net/stats_man.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace OnlinePlay {

using namespace Detail;

namespace {

constexpr uint32_t kTagging = 0x5B512EDC;     // Tagging (the component)
constexpr uint32_t kSkaterScore = 0xD262802F; // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600; // netfirefight

constexpr uint32_t kHitWindow = 5000; // ms

static VtableSlot kPlayerVtable_slots[] = {&PlayerRecord::Teardown};
void** const kPlayerVtable = reinterpret_cast<void**>(kPlayerVtable_slots); // PlayerRecord
static VtableSlot kScoreSearchVtable_slots[] = {&OnlinePlay::ScoreSearch::Teardown};
void** const kScoreSearchVtable = reinterpret_cast<void**>(kScoreSearchVtable_slots);
static VtableSlot kScoreNodeVtable_slots[] = {&OnlinePlay::ScoreNode::Teardown};
void** const kScoreNodeVtable = reinterpret_cast<void**>(kScoreNodeVtable_slots);

Hub* TheManager() { return g_gameNetManager; }

GameObjects::CObjectModule* SkaterComponent(GameObjects::CRider* skater, uint32_t type)
{
    return reinterpret_cast<GameObjects::CGameEntity*>(skater)->FetchModule(type);
}

// A new skater profile on the skater info heap (null if out of memory).
GameObjects::CRiderIdentity* NewProfile()
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_skaterInfoHeap);
    void* block = MemSys::AllocZeroed(sizeof(GameObjects::CRiderIdentity));
    return block ? static_cast<GameObjects::CRiderIdentity*>(block)->MakeNew() : nullptr;
}

// A node's unlinking, as ListKit::Elem's destructor does it (the node linked to itself after).
void UnlinkNode(MemSys::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// --- JoinerRecord ---

// 0x005434D0
JoinerRecord* JoinerRecord::MakeNew()
{
    m_account = NewProfile();
    m_unknown24 = 0;
    m_profileId = 0;
    m_skillPoints = 0;
    m_unknown38 = 0;
    m_vehicleControlType = 0;
    MemSys::Hub::Pull()->LeaveHeapScope();
    return this;
}

// 0x00543560
void JoinerRecord::DestroyBody()
{
    if (GameObjects::CRiderIdentity* profile = m_account) {
        profile->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(profile);
    }
}

// --- PlayerRecord ---

// 0x00543180
void* ClearUpdateRecords(void* records)
{
    std::memset(static_cast<uint8_t*>(records) + 0xB8, 0, 16 * sizeof(uint32_t));
    return records;
}

// 0x00543740
PlayerRecord* PlayerRecord::MakeNew(uint32_t flags)
{
    data = this;
    priority = 0;
    next = this;
    prev = this;
    vtable = kPlayerVtable;
    ClearUpdateRecords(m_unknown50);
    m_flagBits = 0;
    std::sprintf(m_label, "<No Name>"); // (0x0064CCE8)
    for (int i = 0; i < kMaxPlayers; ++i) {
        m_sentUpdates[i] = -1;
        m_updateBytesA[i] = 0xFF;
        m_updateBytesB[i] = 0xFF;
        for (int j = 0; j < 3; ++j) {
            m_updateRecordsA[i][j] = 0xFFFF;
            m_updateRecordsB[i][j] = 0xFFFF;
        }
    }
    m_flagBits = flags;
    m_object1EC = nullptr;
    m_unknown1D8 = 0;
    m_faceTextureData = nullptr;
    m_skater = nullptr;
    m_account = nullptr;
    m_joinTime = 0;
    m_lastHitTime = 0;
    m_team = 0;
    m_unknown28 = 0;
    m_skillPoints = 0;
    m_unknown48 = 0;
    m_eliminatedTime = 0;
    m_unknown34 = 0;
    m_lastSentSkater = 7;
    return this;
}

// 0x00543810
void PlayerRecord::DestroyBody()
{
    vtable = kPlayerVtable;
    if (GameObjects::CRiderIdentity* profile = m_account) {
        profile->DestroyBody();
        MemSys::Hub::Pull()->Dealloc(profile);
    }
    if (m_object1EC) {
        void** objectVtable = *static_cast<void***>(m_object1EC);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(objectVtable[2])(m_object1EC, 1);
    }
    if (m_faceTextureData) {
        MemSys::Hub::Pull()->Dealloc(m_faceTextureData);
        m_faceTextureData = nullptr;
    }
    vtable = kPlayerNodeVtable;
    UnlinkNode(this);
}

// 0x00544750
PlayerRecord* PlayerRecord::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005438D0
void PlayerRecord::BindRider(GameObjects::CRider* skater)
{
    Hub* manager = TheManager();
    m_skater = skater;
    if (!(m_flagBits & kPlayerLocal) && manager->GetCamPlayer() == this)
        manager->SpectatePlayer(reinterpret_cast<uint8_t*>(this)); // (the camera follows it again)
}

// 0x00543270
void PlayerRecord::SetProfile(GameObjects::CRiderIdentity* profile)
{
    GameObjects::CRiderIdentity* copy = NewProfile();
    m_account = copy;
    copy->CopyFrom(profile);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x005431F0
void PlayerRecord::SetFaceTextureData(const uint8_t* bytes, uint32_t size)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_netMiscHeap);
    uint8_t id = Prop<uint8_t>(m_skater, 0x14); // (the skater's object id)
    m_faceTextureData = static_cast<uint8_t*>(MemSys::Alloc(size + 1));
    m_faceTextureData[0] = id;
    std::memcpy(m_faceTextureData + 1, bytes, size);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00543060
uint8_t* PlayerRecord::GetFaceTextureData() { return m_faceTextureData; }

// 0x00543070
void PlayerRecord::SetCustomTag(bool custom)
{
    if (!m_skater)
        return;
    if (GameObjects::CObjectModule* tagging = SkaterComponent(m_skater, kTagging))
        Prop<bool>(tagging, 0x18) = custom; // (0x004E4000, a setter folded there)
}

// 0x00543090
bool PlayerRecord::HasCustomTag()
{
    if (!m_skater)
        return false;
    GameObjects::CObjectModule* tagging = SkaterComponent(m_skater, kTagging);
    return tagging ? Prop<bool>(tagging, 0x18) : false; // (0x0051D850)
}

// 0x005430B0
void* PlayerRecord::GetGraffitiTexture()
{
    if (!m_skater)
        return nullptr;
    GameObjects::CObjectModule* tagging = SkaterComponent(m_skater, kTagging);
    return tagging ? Prop<void*>(tagging, 0x1C) : nullptr; // (0x00553E90, a getter folded there)
}

// 0x005430D0
void PlayerRecord::SetReadyQueryOwed(uint32_t id) { m_readyQueryOwed = id; }

// 0x00544670
void PlayerRecord::AwaitReadyResponse(uint32_t id)
{
    if (!m_conn || !m_conn->IsOffMachine())
        return;
    if (id)
        m_readyQueryOwed = id;
    m_conn->m_curStanding &= ~NetLink::Conn::kStatusReady;
    m_conn->m_curStanding |= NetLink::Conn::kStatus2;
}

// 0x005446C0
void PlayerRecord::ReceiveReadyResponse(uint32_t id)
{
    if (m_readyQueryOwed != id)
        return;
    if (m_conn) {
        m_conn->m_curStanding &= ~NetLink::Conn::kStatus2;
        m_conn->m_curStanding |= NetLink::Conn::kStatusReady;
        m_conn->ClearResendCounts();
    }
    m_readyQueryOwed = 0;
}

// 0x00543110 / 0x00543100 / 0x005430E0
void PlayerRecord::SetLastHitTime(uint32_t time) { m_lastHitTime = time; }
void PlayerRecord::ClearLastHitTime() { m_lastHitTime = 0; }
int PlayerRecord::CanBeHitAgain() { return FrameClock::GetTime() - m_lastHitTime > kHitWindow ? 1 : 0; }

// 0x00543120 / 0x00543130 / 0x00543140
int PlayerRecord::GetLastSentSkater() { return m_lastSentSkater; }
void PlayerRecord::SetLastSentSkater(int index) { m_lastSentSkater = index; }
int PlayerRecord::GetSkatersPerUpdate() { return m_conn->GetLinkSpeedClass() != 0 ? 1 : 2; }

// 0x00543150 / 0x00543160
int PlayerRecord::GetAccumulatedScore() { return m_accumulatedScore; }
void PlayerRecord::AddToAccumulatedScore(int score) { m_accumulatedScore += score; }

// 0x00543300 - 0x005434C0: the flags.
int PlayerRecord::IsOwnPlayer() { return m_flagBits & kPlayerLocal; }
int PlayerRecord::IsHostPlayer() { return (m_flagBits >> 9) & 1; }
int PlayerRecord::IsObserver() { return (m_flagBits >> 2) & 1; }
int PlayerRecord::IsScoutingLevel() { return (m_flagBits >> 16) & 1; }
void PlayerRecord::MarkFaceTextureSent() { m_flagBits |= kPlayerFaceTextureSent; }
void PlayerRecord::MarkJustJoined()
{
    m_joinTime = FrameClock::GetTime();
    m_flagBits |= kPlayerJustJoined;
}
void PlayerRecord::ClearJustJoined() { m_flagBits &= ~kPlayerJustJoined; }
int PlayerRecord::HasFinishedJoining() { return (m_flagBits & (kPlayerFullyIn | kPlayerLocal)) != 0 ? 1 : 0; }
void PlayerRecord::SetHostPlayer() { m_flagBits |= kPlayerServer; }
void PlayerRecord::ClearServerPlayer() { m_flagBits &= ~kPlayerServer; }
int PlayerRecord::IsCarryingFlag() { return (m_flagBits & kPlayerCarryingFlags) != 0 ? 1 : 0; }
int PlayerRecord::GetFlagTeam()
{
    for (int team = 0; team < 4; ++team) {
        if (m_flagBits & (kPlayerCarryingFlag0 << team))
            return team;
    }
    return -1;
}
void PlayerRecord::ClearFlags() { m_flagBits &= ~kPlayerCarryingFlags; }
void PlayerRecord::MarkEliminated()
{
    m_eliminatedTime = Call<uint32_t>(CodeAddress(&GoalSystem::CMatchRules::GetRound), GameMode());
    m_flagBits |= kPlayerEliminated;
}
bool PlayerRecord::IsEliminated()
{
    if (m_flagBits & kPlayerEliminated)
        return true;
    if (GameType() != kNetFirefight || !m_skater)
        return false;
    GameObjects::CObjectModule* score = SkaterComponent(m_skater, kSkaterScore);
    return score && Call<int>(CodeAddress(&GameModules::PointTally::GetTotalScore), Prop<void*>(score, 0x18)) == 0; // (its score's lives)
}
int PlayerRecord::HoldsCrown() { return (m_flagBits >> 7) & 1; }
int PlayerRecord::GetLinkHandle() { return m_conn ? Prop<int>(m_conn, 0x3C) : 0; }
uint32_t PlayerRecord::GetSkaterValue2BC() { return Prop<uint32_t>(m_skater, 0x2BC); }
int PlayerRecord::IsPendingObserver() { return (m_flagBits >> 3) & 1; }

// --- The score lines' list types ---

// 0x00543170 / 0x00543580
void ScoreSearch::DestroyBody() { m_vtable = kScoreSearchVtable; }
ScoreSearch* ScoreSearch::Teardown(unsigned int flags)
{
    m_vtable = kScoreSearchVtable;
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00544700 / 0x005435B0 / 0x00544720
void ScoreNode::DestroyBody()
{
    vtable = kScoreNodeVtable;
    UnlinkNode(this);
}
ScoreNode* ScoreNode::Teardown(unsigned int flags)
{
    vtable = kScoreNodeVtable;
    UnlinkNode(this);
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}
ScoreNode* ScoreNode::DestroyCalling(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

} // namespace OnlinePlay

using OnlinePlay::JoinerRecord;
using OnlinePlay::PlayerRecord;
