// The network game's players: GameNet::PlayerInfo and NewPlayerInfo (see player_info.h).

#include "game/net/player_info.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_conn.h"
#include "game/obj/object.h"
#include "game/skate/profile_manager.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

#pragma warning(disable : 4996) // (the original's sprintf, kept)

namespace GameNet {

using namespace Detail;

namespace {

constexpr uint32_t kTagging = 0x5B512EDC;     // Tagging (the component)
constexpr uint32_t kSkaterScore = 0xD262802F; // SkaterScore (the component)
constexpr uint32_t kNetFirefight = 0xBFF33600; // netfirefight

constexpr uint32_t kHitWindow = 5000; // ms

inline void** const kPlayerVtable = reinterpret_cast<void**>(0x0064CCF4);     // PlayerInfo
inline void** const kPlayerNodeVtable = reinterpret_cast<void**>(0x0064C9E8); // Lst::Node<PlayerInfo>
inline void** const kScoreSearchVtable = reinterpret_cast<void**>(0x0064CCE0);
inline void** const kScoreNodeVtable = reinterpret_cast<void**>(0x0064CCE4);

Manager* TheManager() { return GameVariable<Manager*>(0x007CCE14); }

Obj::CBaseComponent* SkaterComponent(Obj::CSkater* skater, uint32_t type)
{
    return reinterpret_cast<Obj::CCompositeObject*>(skater)->GetComponent(type);
}

// A new skater profile on the skater info heap (null if out of memory).
Obj::CSkaterProfile* NewProfile()
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_skaterInfoHeap);
    void* block = Mem::Calloc(sizeof(Obj::CSkaterProfile));
    return block ? static_cast<Obj::CSkaterProfile*>(block)->Construct() : nullptr;
}

// A node's unlinking, as Lst::Node's destructor does it (the node linked to itself after).
void UnlinkNode(Mem::ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

} // namespace

// --- NewPlayerInfo ---

// 0x005434D0
NewPlayerInfo* NewPlayerInfo::Construct()
{
    m_profile = NewProfile();
    m_unknown24 = 0;
    m_unknown2C = 0;
    m_unknown30 = 0;
    m_unknown38 = 0;
    m_unknown40 = 0;
    Mem::Manager::Get()->PopContext();
    return this;
}

// 0x00543560
void NewPlayerInfo::DestroyBody()
{
    if (Obj::CSkaterProfile* profile = m_profile) {
        profile->DestroyBody();
        Mem::Manager::Get()->Free(profile);
    }
}

// --- PlayerInfo ---

// 0x00543180
void* ClearUpdateRecords(void* records)
{
    std::memset(static_cast<uint8_t*>(records) + 0xB8, 0, 16 * sizeof(uint32_t));
    return records;
}

// 0x00543740
PlayerInfo* PlayerInfo::Construct(uint32_t flags)
{
    data = this;
    priority = 0;
    next = this;
    prev = this;
    vtable = kPlayerVtable;
    ClearUpdateRecords(m_unknown50);
    m_flags = 0;
    std::sprintf(m_name, "<No Name>"); // (0x0064CCE8)
    for (int i = 0; i < kMaxPlayers; ++i) {
        m_sentUpdates[i] = -1;
        m_updateBytesA[i] = 0xFF;
        m_updateBytesB[i] = 0xFF;
        for (int j = 0; j < 3; ++j) {
            m_updateRecordsA[i][j] = 0xFFFF;
            m_updateRecordsB[i][j] = 0xFFFF;
        }
    }
    m_flags = flags;
    m_object1EC = nullptr;
    m_unknown1D8 = 0;
    m_faceTextureData = nullptr;
    m_skater = nullptr;
    m_profile = nullptr;
    m_joinTime = 0;
    m_lastHitTime = 0;
    m_team = 0;
    m_unknown28 = 0;
    m_rating = 0;
    m_unknown48 = 0;
    m_eliminatedTime = 0;
    m_unknown34 = 0;
    m_lastSentSkater = 7;
    return this;
}

// 0x00543810
void PlayerInfo::DestroyBody()
{
    vtable = kPlayerVtable;
    if (Obj::CSkaterProfile* profile = m_profile) {
        profile->DestroyBody();
        Mem::Manager::Get()->Free(profile);
    }
    if (m_object1EC) {
        void** objectVtable = *static_cast<void***>(m_object1EC);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(objectVtable[2])(m_object1EC, 1);
    }
    if (m_faceTextureData) {
        Mem::Manager::Get()->Free(m_faceTextureData);
        m_faceTextureData = nullptr;
    }
    vtable = kPlayerNodeVtable;
    UnlinkNode(this);
}

// 0x00544750
PlayerInfo* PlayerInfo::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005438D0
void PlayerInfo::SetSkater(Obj::CSkater* skater)
{
    Manager* manager = TheManager();
    m_skater = skater;
    if (!(m_flags & kPlayerLocal) && manager->GetCamPlayer() == this)
        Call(0x0053A910, manager, this); // (the camera follows it again)
}

// 0x00543270
void PlayerInfo::SetProfile(Obj::CSkaterProfile* profile)
{
    Obj::CSkaterProfile* copy = NewProfile();
    m_profile = copy;
    copy->CopyFrom(profile);
    Mem::Manager::Get()->PopContext();
}

// 0x005431F0
void PlayerInfo::SetFaceTextureData(const uint8_t* bytes, uint32_t size)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_netMiscHeap);
    uint8_t id = Field<uint8_t>(m_skater, 0x14); // (the skater's object id)
    m_faceTextureData = static_cast<uint8_t*>(Mem::New(size + 1));
    m_faceTextureData[0] = id;
    std::memcpy(m_faceTextureData + 1, bytes, size);
    Mem::Manager::Get()->PopContext();
}

// 0x00543060
uint8_t* PlayerInfo::GetFaceTextureData() { return m_faceTextureData; }

// 0x00543070
void PlayerInfo::SetCustomTag(bool custom)
{
    if (!m_skater)
        return;
    if (Obj::CBaseComponent* tagging = SkaterComponent(m_skater, kTagging))
        Field<bool>(tagging, 0x18) = custom; // (0x004E4000, a setter folded there)
}

// 0x00543090
bool PlayerInfo::HasCustomTag()
{
    if (!m_skater)
        return false;
    Obj::CBaseComponent* tagging = SkaterComponent(m_skater, kTagging);
    return tagging ? Field<bool>(tagging, 0x18) : false; // (0x0051D850)
}

// 0x005430B0
void* PlayerInfo::GetGraffitiTexture()
{
    if (!m_skater)
        return nullptr;
    Obj::CBaseComponent* tagging = SkaterComponent(m_skater, kTagging);
    return tagging ? Field<void*>(tagging, 0x1C) : nullptr; // (0x00553E90, a getter folded there)
}

// 0x005430D0
void PlayerInfo::SetReadyQueryOwed(uint32_t id) { m_readyQueryOwed = id; }

// 0x00544670
void PlayerInfo::AwaitReadyResponse(uint32_t id)
{
    if (!m_conn || !m_conn->IsRemote())
        return;
    if (id)
        m_readyQueryOwed = id;
    m_conn->m_status &= ~Net::Conn::kStatusReady;
    m_conn->m_status |= Net::Conn::kStatus2;
}

// 0x005446C0
void PlayerInfo::ReceiveReadyResponse(uint32_t id)
{
    if (m_readyQueryOwed != id)
        return;
    if (m_conn) {
        m_conn->m_status &= ~Net::Conn::kStatus2;
        m_conn->m_status |= Net::Conn::kStatusReady;
        m_conn->ClearResendCounts();
    }
    m_readyQueryOwed = 0;
}

// 0x00543110 / 0x00543100 / 0x005430E0
void PlayerInfo::SetLastHitTime(uint32_t time) { m_lastHitTime = time; }
void PlayerInfo::ClearLastHitTime() { m_lastHitTime = 0; }
int PlayerInfo::CanBeHitAgain() { return Tmr::GetTime() - m_lastHitTime > kHitWindow ? 1 : 0; }

// 0x00543120 / 0x00543130 / 0x00543140
int PlayerInfo::GetLastSentSkater() { return m_lastSentSkater; }
void PlayerInfo::SetLastSentSkater(int index) { m_lastSentSkater = index; }
int PlayerInfo::GetSkatersPerUpdate() { return m_conn->GetBandwidthType() != 0 ? 1 : 2; }

// 0x00543150 / 0x00543160
int PlayerInfo::GetAccumulatedScore() { return m_accumulatedScore; }
void PlayerInfo::AddToAccumulatedScore(int score) { m_accumulatedScore += score; }

// 0x00543300 - 0x005434C0: the flags.
int PlayerInfo::IsLocalPlayer() { return m_flags & kPlayerLocal; }
int PlayerInfo::IsServerPlayer() { return (m_flags >> 9) & 1; }
int PlayerInfo::IsObserver() { return (m_flags >> 2) & 1; }
int PlayerInfo::IsSurveying() { return (m_flags >> 16) & 1; }
void PlayerInfo::MarkFaceTextureSent() { m_flags |= kPlayerFaceTextureSent; }
void PlayerInfo::MarkJustJoined()
{
    m_joinTime = Tmr::GetTime();
    m_flags |= kPlayerJustJoined;
}
void PlayerInfo::ClearJustJoined() { m_flags &= ~kPlayerJustJoined; }
int PlayerInfo::IsFullyIn() { return (m_flags & (kPlayerFullyIn | kPlayerLocal)) != 0 ? 1 : 0; }
void PlayerInfo::MarkAsServerPlayer() { m_flags |= kPlayerServer; }
void PlayerInfo::ClearServerPlayer() { m_flags &= ~kPlayerServer; }
int PlayerInfo::IsCarryingFlag() { return (m_flags & kPlayerCarryingFlags) != 0 ? 1 : 0; }
int PlayerInfo::GetFlagTeam()
{
    for (int team = 0; team < 4; ++team) {
        if (m_flags & (kPlayerCarryingFlag0 << team))
            return team;
    }
    return -1;
}
void PlayerInfo::ClearFlags() { m_flags &= ~kPlayerCarryingFlags; }
void PlayerInfo::MarkEliminated()
{
    m_eliminatedTime = Call<uint32_t>(0x00553270, GameMode());
    m_flags |= kPlayerEliminated;
}
bool PlayerInfo::IsEliminated()
{
    if (m_flags & kPlayerEliminated)
        return true;
    if (GameType() != kNetFirefight || !m_skater)
        return false;
    Obj::CBaseComponent* score = SkaterComponent(m_skater, kSkaterScore);
    return score && Call<int>(0x00561B40, Field<void*>(score, 0x18)) == 0; // (its score's lives)
}
int PlayerInfo::IsKing() { return (m_flags >> 7) & 1; }
int PlayerInfo::GetConnHandle() { return m_conn ? Field<int>(m_conn, 0x3C) : 0; }
uint32_t PlayerInfo::GetSkaterValue2BC() { return Field<uint32_t>(m_skater, 0x2BC); }
int PlayerInfo::IsPendingObserver() { return (m_flags >> 3) & 1; }

// --- The score lines' list types ---

// 0x00543170 / 0x00543580
void ScoreSearch::DestroyBody() { m_vtable = kScoreSearchVtable; }
ScoreSearch* ScoreSearch::Destroy(unsigned int flags)
{
    m_vtable = kScoreSearchVtable;
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00544700 / 0x005435B0 / 0x00544720
void ScoreNode::DestroyBody()
{
    vtable = kScoreNodeVtable;
    UnlinkNode(this);
}
ScoreNode* ScoreNode::Destroy(unsigned int flags)
{
    vtable = kScoreNodeVtable;
    UnlinkNode(this);
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}
ScoreNode* ScoreNode::DestroyCalling(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

} // namespace GameNet

using GameNet::NewPlayerInfo;
using GameNet::PlayerInfo;
REPLACE(0x005434D0, &NewPlayerInfo::Construct);
REPLACE(0x00543560, &NewPlayerInfo::DestroyBody);
REPLACE(0x00543180, GameNet::ClearUpdateRecords);
REPLACE(0x00543740, &PlayerInfo::Construct);
REPLACE(0x00543810, &PlayerInfo::DestroyBody);
REPLACE(0x00544750, &PlayerInfo::Destroy);
REPLACE(0x005438D0, &PlayerInfo::SetSkater);
REPLACE(0x00543270, &PlayerInfo::SetProfile);
REPLACE(0x005431F0, &PlayerInfo::SetFaceTextureData);
REPLACE(0x00543060, &PlayerInfo::GetFaceTextureData);
REPLACE(0x00543070, &PlayerInfo::SetCustomTag);
REPLACE(0x00543090, &PlayerInfo::HasCustomTag);
REPLACE(0x005430B0, &PlayerInfo::GetGraffitiTexture);
REPLACE(0x005430D0, &PlayerInfo::SetReadyQueryOwed);
REPLACE(0x00544670, &PlayerInfo::AwaitReadyResponse);
REPLACE(0x005446C0, &PlayerInfo::ReceiveReadyResponse);
REPLACE(0x00543110, &PlayerInfo::SetLastHitTime); // (not a Ghidra function: between 0x00543100 and 0x00543120)
REPLACE(0x00543100, &PlayerInfo::ClearLastHitTime);
REPLACE(0x005430E0, &PlayerInfo::CanBeHitAgain);
REPLACE(0x00543120, &PlayerInfo::GetLastSentSkater);
REPLACE(0x00543130, &PlayerInfo::SetLastSentSkater);
REPLACE(0x00543140, &PlayerInfo::GetSkatersPerUpdate);
REPLACE_SOURCE_ONLY(0x00543150, &PlayerInfo::GetAccumulatedScore); // 4 bytes
REPLACE(0x00543160, &PlayerInfo::AddToAccumulatedScore);
REPLACE(0x00543300, &PlayerInfo::IsLocalPlayer);
REPLACE(0x00543310, &PlayerInfo::IsServerPlayer);
REPLACE(0x00543320, &PlayerInfo::IsObserver);
REPLACE(0x00543330, &PlayerInfo::IsSurveying);
REPLACE(0x00543340, &PlayerInfo::MarkFaceTextureSent);
REPLACE(0x00543350, &PlayerInfo::MarkJustJoined);
REPLACE(0x00543370, &PlayerInfo::ClearJustJoined);
REPLACE(0x00543380, &PlayerInfo::IsFullyIn);
REPLACE(0x00543390, &PlayerInfo::MarkAsServerPlayer);
REPLACE(0x005433A0, &PlayerInfo::ClearServerPlayer);
REPLACE(0x005433B0, &PlayerInfo::IsCarryingFlag);
REPLACE(0x005433D0, &PlayerInfo::GetFlagTeam);
REPLACE(0x00543410, &PlayerInfo::ClearFlags);
REPLACE(0x00543420, &PlayerInfo::MarkEliminated);
REPLACE(0x00543440, &PlayerInfo::IsEliminated);
REPLACE(0x00543490, &PlayerInfo::IsKing);
REPLACE(0x005434A0, &PlayerInfo::GetConnHandle);
REPLACE(0x005434B0, &PlayerInfo::GetSkaterValue2BC);
REPLACE(0x005434C0, &PlayerInfo::IsPendingObserver);
REPLACE(0x00543170, &GameNet::ScoreSearch::DestroyBody);
REPLACE(0x00543580, &GameNet::ScoreSearch::Destroy);
REPLACE(0x00544700, &GameNet::ScoreNode::DestroyBody);
REPLACE(0x005435B0, &GameNet::ScoreNode::Destroy);
REPLACE(0x00544720, &GameNet::ScoreNode::DestroyCalling);
