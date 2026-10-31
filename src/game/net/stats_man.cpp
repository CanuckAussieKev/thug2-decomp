// The online stats (OnlinePlay::RankingsManager). See stats_man.h.

#include "game/net/stats_man.h"
#include "game/net/gamespy.h"
#include "game/core/code_address.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/checksums.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/core/task_list.h"
#include "game/lists/chain_start.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/online_identity.h"
#include "game/net/online_text.h"
#include "game/net/vault_man.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/param_struct.h"
#include "game/sys/stopwatch.h"
#include "game/gfx/loading_bar.h"
#include "game/assets/resource_registry.h"
#include "game/net/task_vtable.h"

namespace OnlinePlay {

float g_rankScale{0.00028571428f};

namespace {

using Detail::DeleteParams;
using Detail::NewParams;

static VtableSlot kEntryVtable_slots[] = {&OnlinePlay::TopEntry::Teardown};
void** const kEntryVtable = reinterpret_cast<void**>(kEntryVtable_slots); // TopEntry (and the entries' heads)
static VtableSlot kLevelsHeadVtable_slots[] = {&OnlinePlay::StatsNode::DeleteLevelsHead};
void** const kLevelsHeadVtable = reinterpret_cast<void**>(kLevelsHeadVtable_slots); // a top list's levels head
static VtableSlot kLevelVtable_slots[] = {&OnlinePlay::LevelScores::Teardown};
void** const kLevelVtable = reinterpret_cast<void**>(kLevelVtable_slots); // LevelScores
static VtableSlot kSortVtable_slots[] = {&OnlinePlay::SortEntry::Teardown};
void** const kSortVtable = reinterpret_cast<void**>(kSortVtable_slots); // SortEntry (and the sorted list's head)
static VtableSlot kEntryNodeVtable_slots[] = {&OnlinePlay::StatsNode::DeleteEntryNode};
void** const kEntryNodeVtable = reinterpret_cast<void**>(kEntryNodeVtable_slots); // ListKit::Elem<TopEntry>
static VtableSlot kLevelNodeVtable_slots[] = {&OnlinePlay::StatsNode::DeleteLevelNode};
void** const kLevelNodeVtable = reinterpret_cast<void**>(kLevelNodeVtable_slots); // ListKit::Elem<LevelScores>
static VtableSlot kSortNodeVtable_slots[] = {&OnlinePlay::StatsNode::DeleteSortNode};
void** const kSortNodeVtable = reinterpret_cast<void**>(kSortNodeVtable_slots); // ListKit::Elem<SortEntry>
static VtableSlot kSortSearchVtable_slots[] = {&OnlinePlay::StatsSearch::DeleteSortSearch};
void** const kSortSearchVtable = reinterpret_cast<void**>(kSortSearchVtable_slots);
static VtableSlot kEntrySearchVtable_slots[] = {&OnlinePlay::StatsSearch::DeleteEntrySearch};
void** const kEntrySearchVtable = reinterpret_cast<void**>(kEntrySearchVtable_slots);
static VtableSlot kLevelSearchVtable_slots[] = {&OnlinePlay::StatsSearch::DeleteLevelSearch};
void** const kLevelSearchVtable = reinterpret_cast<void**>(kLevelSearchVtable_slots);

constexpr uint32_t kLoginTimeout = 10000;  // ms
constexpr uint32_t kReportInterval = 60000; // ms between reports of a game
constexpr int kPersonalStatsLevels = 15;  // GetPersistDataValues' indexes: 0 the rating, 1-15 the levels
constexpr int kMaxSorted = 50;            // lines of a sorted list into the menu
constexpr int kLevelBest = 3;             // a level's best lines into the menu
constexpr int kMaxRank = 10;
constexpr int kStatsPort = 0x1421;

// The rank's factors (0x0064CC1C, 0x00646710).
float RankScale() { return g_rankScale; } // 0.000286
float RankTimes() { return Engine3D::g_floatTen; } // 10
// rating * 0.000286 * 10 truncated, at most 10 (x87 at 53 bits: each product rounded to double).
int Rank(double rating)
{
    int rank = static_cast<int>(rating * RankScale() * RankTimes());
    return rank > kMaxRank ? kMaxRank : rank;
}

uint32_t g_statsLoginStart;

// The login's start (StatsTask's limit).
uint32_t& LoginStart() { return g_statsLoginStart; }
// Set by the cheat script commands (career.cpp): the game reported as a cheat's.
bool CheatFlag() { return g_cheatFlag; }

Hub* NetSessionSlot() { return g_onlineHub; }
RankingsManager* Attributes() { return static_cast<RankingsManager*>(NetSessionSlot()->m_ratingService); }
MemSys::Hub* RamStats() { return MemSys::Hub::Pull(); }
void PushTopDown() { RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap); }
void PushBottomUp() { RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap); }
void PopHeap() { RamStats()->LeaveHeapScope(); }
Scheduling::PileUp* LogicTasks() { return reinterpret_cast<Scheduling::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04); }

uint32_t Checksums(const char* name) { return Checksums::HashFromText(name); }
void Run(uint32_t script, Scripts::CStruct* params = nullptr) { Scripts::LaunchScript(script, params, nullptr, false, nullptr); }

// GameSpy's stats and persistent storage SDK (library code; cdecl).
int IsStatsConnected() { return GameSpy::StatsIsConnected(); }
void CloseStatsConnection() { GameSpy::StatsCloseConnection(); }
int InitStatsConnection(int port) { return GameSpy::StatsInitConnection(port); }
int PersistThink() { return GameSpy::StatsPersistThink(); }
void* NewGame(int usebuckets) { return GameSpy::StatsNewGame(usebuckets); }
void FreeGame(void* game) { GameSpy::StatsFreeGame(game); }
const char* GetChallenge(void* game) { return GameSpy::StatsGetChallenge(game); }
void NewPlayer(void* game, int number, const char* name)
{
    GameSpy::StatsNewPlayer(game, number, name);
}
void RemovePlayerFromGame(void* game, int number) { GameSpy::StatsRemovePlayer(game, number); }
void SendGameSnapShot(void* game, const char* snapshot, int final)
{
    GameSpy::StatsSendSnapShot(game, snapshot, final);
}
void GetPersistDataValues(int localId, int profile, int type, int index, const char* keys, const void* callback,
                          void* instance)
{
    GameSpy::StatsGetPersistDataValues(
        localId, profile, type, index, keys, callback, instance);
}
// The SDK's bucket "set" operations (game / player keys, ints / strings).
void SetGameInt(void* game, const char* key, int value) { GameSpy::SetServerIntBucket(game, key, value); }
void SetGameString(void* game, const char* key, const char* value) { GameSpy::SetServerStringBucket(game, key, value); }
void SetPlayerInt(void* game, const char* key, int value, int player)
{
    GameSpy::SetPlayerIntBucket(game, key, value, player);
}
void SetPlayerString(void* game, const char* key, const char* value, int player)
{
    GameSpy::SetPlayerStringBucket(game, key, value, player);
}
void GhttpCleanup() { GameSpy::HttpCleanup(); }
// The SDK's names for the stats connection (written before InitStatsConnection).
char* GcdGameName() { return GameSpy::StatsGameName(); }
char* GcdSecretKey() { return GameSpy::StatsSecretKey(); }
const char* StatsKey() { return NetLink::GameKey(); } // (OnlineGameKey or the game's own)

// The manager's calls.
const char* ActiveRulesetLabel(Hub* manager) { return manager->GetGameTypeName(); }
uint32_t LevelKeyHash(Hub* manager) { return manager->FetchStage(); }
uint32_t GameModeChecksum(Hub* manager) { return manager->GetGameTypePreference(); }
int PlayerPoints(Hub* manager, int id) { return manager->GetEntrantPoints(static_cast<uint32_t>(id)); }

void UnlinkListNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}
void SetupListSentinel(MemSys::ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
void InitNode(MemSys::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}
void Append(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
MemSys::ListNode* NextNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
// Delete every item of a list (each one's next fetched first).
void DeleteAll(MemSys::ListNode* head)
{
    for (MemSys::ListNode* node = NextNode(head); node;) {
        MemSys::ListNode* next = NextNode(node);
        if (node->data)
            UnlinkListNode(node->data);
        node = next;
    }
}
int TotalOf(const MemSys::ListNode* head)
{
    int count = 0;
    for (const MemSys::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

TopEntry* NewEntry()
{
    auto* entry = static_cast<TopEntry*>(MemSys::AllocZeroed(sizeof(TopEntry)));
    if (entry)
        InitNode(&entry->m_listLink, kEntryVtable);
    return entry;
}
SortEntry* NewSortEntry()
{
    auto* entry = static_cast<SortEntry*>(MemSys::AllocZeroed(sizeof(SortEntry)));
    if (entry) {
        InitNode(&entry->m_listLink, kSortVtable);
        entry->m_pointsHeld = 0;
        entry->m_level = 0;
    }
    return entry;
}
// A local sorted list (ListKit::Foremost<SortEntry>, highest priority first), emptied and unlinked as the original does.
struct SortedList {
    MemSys::ListNode head;
    SortedList() { SetupListSentinel(&head, kSortVtable); }
    void AppendOne(SortEntry* entry) { reinterpret_cast<ListKit::Foremost*>(&head)->AppendVertex(&entry->m_listLink); }
    ~SortedList()
    {
        DeleteAll(&head);
        head.prev->next = head.next;
        head.next->prev = head.prev;
    }
};

} // namespace

// --- the list nodes ---

// 0x0054D8F0 / 0x0054D400 / 0x0054EA20
void StatsNode::DestroyEntryNode() { m_listLink.UnlinkAs(kEntryNodeVtable); }
void StatsNode::DestroyLevelNode() { m_listLink.UnlinkAs(kLevelNodeVtable); }
void StatsNode::DestroySortNode() { m_listLink.UnlinkAs(kSortNodeVtable); }

// 0x0054D420
StatsNode* StatsNode::DeleteEntryNode(unsigned int flags)
{
    DestroyEntryNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D460
StatsNode* StatsNode::DeleteLevelNode(unsigned int flags)
{
    DestroyLevelNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D740
StatsNode* StatsNode::DeleteSortNode(unsigned int flags)
{
    DestroySortNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D710
StatsNode* StatsNode::DeleteLevelsHead(unsigned int flags)
{
    DestroyLevelNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054CA90 / 0x0054CAA0 / 0x0054CAB0
void StatsSearch::DestroySortSearch() { m_vtable = kSortSearchVtable; }
void StatsSearch::DestroyEntrySearch() { m_vtable = kEntrySearchVtable; }
void StatsSearch::DestroyLevelSearch() { m_vtable = kLevelSearchVtable; }

// 0x0054D370
StatsSearch* StatsSearch::DeleteSortSearch(unsigned int flags)
{
    m_vtable = kSortSearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D3A0
StatsSearch* StatsSearch::DeleteEntrySearch(unsigned int flags)
{
    m_vtable = kEntrySearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D3D0
StatsSearch* StatsSearch::DeleteLevelSearch(unsigned int flags)
{
    m_vtable = kLevelSearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D6E0
TopEntry* TopEntry::Teardown(unsigned int flags)
{
    reinterpret_cast<StatsNode*>(this)->DestroyEntryNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054D780
SortEntry* SortEntry::Teardown(unsigned int flags)
{
    reinterpret_cast<StatsNode*>(this)->DestroySortNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054DFE0
void LevelScores::DestroyBody()
{
    m_listLink.vtable = kLevelVtable;
    DeleteAll(&m_entries);
    m_entries.UnlinkAs(kEntryNodeVtable);
    m_listLink.UnlinkAs(kLevelNodeVtable);
}

// 0x0054EBB0
LevelScores* LevelScores::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// --- the top lists ---

// 0x0054D8B0
TopList* TopList::MakeNew()
{
    SetupListSentinel(&m_levels, kLevelsHeadVtable);
    SetupListSentinel(&m_entries, kEntryVtable);
    return this;
}

// 0x0054D5C0
void TopList::DestroyBody()
{
    m_entries.UnlinkAs(kEntryNodeVtable);
    m_levels.UnlinkAs(kLevelNodeVtable);
}

// 0x0054E920
void TopList::Dealloc()
{
    DeleteAll(&m_levels);
    DeleteAll(&m_entries);
}

// 0x0054E0D0
void TopList::FillLevelScores()
{
    for (MemSys::ListNode* node = NextNode(&m_levels); node; node = NextNode(node)) {
        auto* level = static_cast<LevelScores*>(node->data);
        if (!level)
            break;
        if (TotalOf(&level->m_entries) > 0) {
            Scripts::CStruct* params = NewParams();
            params->AppendWord(0xC4745838, RankingsManager::LevelName(level->m_level));
            Run(0x1AADEE21, params);
            DeleteParams(params);
        }
        int shown = 0;
        for (MemSys::ListNode* line = NextNode(&level->m_entries); line && shown < kLevelBest; line = NextNode(line)) {
            auto* entry = static_cast<TopEntry*>(line->data);
            if (!entry)
                break;
            Scripts::CStruct* params = NewParams();
            params->PutInt(0x2776D9DD, entry->m_skillPoints);
            params->PutInt(0xCD66C8AE, entry->m_pointsHeld);
            // (here the rating is read as unsigned)
            params->PutInt(0x7786171A, Rank(static_cast<double>(static_cast<uint32_t>(entry->m_skillPoints))));
            params->AppendWord(0xA1DC81F9, entry->m_label);
            params->AppendWord(0x651533EC, RankingsManager::LevelName(level->m_level));
            Run(0x19F7BC28, params);
            DeleteParams(params);
            ++shown;
        }
    }
    Scripts::CStruct* params = NewParams();
    params->AppendWord(0xC4745838, Scripts::FetchText(0xC2283319u));
    Run(0x1AADEE21, params);
    DeleteParams(params);
}

// 0x0054E4A0
void TopList::FillSortedScores(bool ratings)
{
    SortedList sorted;
    if (ratings) {
        for (MemSys::ListNode* node = NextNode(&m_entries); node; node = NextNode(node)) {
            auto* entry = static_cast<TopEntry*>(node->data);
            if (!entry)
                break;
            SortEntry* line = NewSortEntry();
            line->m_listLink.priority = entry->m_skillPoints;
            line->m_pointsHeld = entry->m_pointsHeld;
            line->m_skillPoints = entry->m_skillPoints;
            Text::DupFrom(line->m_label, entry->m_label);
            sorted.AppendOne(line);
        }
    } else {
        for (MemSys::ListNode* node = NextNode(&m_levels); node; node = NextNode(node)) {
            auto* level = static_cast<LevelScores*>(node->data);
            if (!level)
                break;
            for (MemSys::ListNode* item = NextNode(&level->m_entries); item; item = NextNode(item)) {
                auto* entry = static_cast<TopEntry*>(item->data);
                if (!entry)
                    break;
                SortEntry* line = NewSortEntry();
                line->m_listLink.priority = entry->m_pointsHeld;
                line->m_pointsHeld = entry->m_pointsHeld;
                line->m_level = level->m_level;
                line->m_skillPoints = entry->m_skillPoints;
                Text::DupFrom(line->m_label, entry->m_label);
                sorted.AppendOne(line); // (inlined in the original)
            }
        }
    }
    int shown = 0;
    for (MemSys::ListNode* node = NextNode(&sorted.head); node; node = NextNode(node)) {
        auto* line = static_cast<SortEntry*>(node->data);
        if (!line || line->m_pointsHeld == 0 || shown == kMaxSorted)
            break;
        Scripts::CStruct* params = NewParams();
        params->PutInt(0x2776D9DD, line->m_skillPoints);
        params->PutInt(0xCD66C8AE, line->m_pointsHeld);
        params->PutInt(0x7786171A, Rank(static_cast<double>(line->m_skillPoints)));
        params->AppendWord(0xA1DC81F9, line->m_label);
        if (ratings) {
            Run(0x0AC09E3B, params);
        } else {
            params->AppendWord(0x651533EC, RankingsManager::LevelName(line->m_level));
            Run(0x19F7BC28, params);
        }
        DeleteParams(params);
        ++shown;
    }
}

// The level checksums the personal stats list their high scores by.
uint32_t g_levelChecksums[15]{0x55894059, 0x2A179957, 0xCDAFB450, 0xCAC27049, 0xFC5833EE,
    0x86A2D65B, 0xF3AB73AB, 0x991B5359, 0xDAF9A383, 0x7078F66C, 0xCC877C27, 0x0C27CA5E, 0x532C99DE,
    0xD4B48511, 0x8C32C761};

// 0x0054DDB0
void PersonalStats::Fill()
{
    const uint32_t* levels = g_levelChecksums;
    SortedList sorted;
    for (int i = 0; i < 15; ++i) {
        int score = m_highScores[i];
        if (score == 0)
            continue;
        SortEntry* line = NewSortEntry();
        line->m_listLink.priority = score;
        line->m_pointsHeld = score;
        line->m_level = levels[i];
        sorted.AppendOne(line);
    }
    for (MemSys::ListNode* node = NextNode(&sorted.head); node; node = NextNode(node)) {
        auto* line = static_cast<SortEntry*>(node->data);
        if (!line)
            break;
        Scripts::CStruct* params = NewParams(); // (never freed - as in the original)
        params->PutInt(0xCD66C8AE, line->m_pointsHeld);
        params->AppendWord(0x651533EC, RankingsManager::LevelName(line->m_level));
        Run(0xA6111F30, params);
    }
}

// --- the stats manager ---

// 0x0054DBD0
RankingsManager* RankingsManager::MakeNew()
{
    m_personal.m_skillPoints = 0;
    std::memset(m_personal.m_highScores, 0, sizeof(m_personal.m_highScores));
    std::memset(m_personal.m_highCombos, 0, sizeof(m_personal.m_highCombos));
    for (TopList& list : m_topLists)
        list.MakeNew();
    RamStats()->EnterHeapScope(RamStats()->m_networkHeap);
    auto* task = static_cast<Scheduling::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (task) {
        task->MakeNew(0);
        task->m_vtable = GameCode::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&StatsTask); // +0x18 the callback
        reinterpret_cast<void**>(task)[7] = this;                                // +0x1C the data
    }
    m_task = task;
    m_authenticated = false;
    m_curPhase = kNone;
    m_game = nullptr;
    m_needTopStats = true;
    PopHeap();
    return this;
}

// 0x0054D610
void RankingsManager::DestroyBody()
{
    if (m_task) // (its scalar deleting destructor, vtable slot 2)
        reinterpret_cast<void*(__thiscall*)(Scheduling::SchedulerTask*, unsigned int)>(m_task->m_vtable[2])(m_task, 1);
    for (int i = 4; i >= 0; --i)
        m_topLists[i].DestroyBody();
}

// 0x0054CBE0
void RankingsManager::LogIn()
{
    PushBottomUp();
    m_curPhase = kLoggingIn;
    LoginStart() = FrameClock::GetTime();
    Run(0x23343580);
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, &LogInThread, this, CREATE_SUSPENDED, &id);
    NetSessionSlot()->SetRoomId(id);
    ResumeThread(thread); // (never closed)
    if (!m_task->IsOnTaskList())
        LogicTasks()->QueueJob(m_task);
    PopHeap();
}

// 0x0054C9D0
void RankingsManager::LogOff()
{
    CloseStatsConnection();
    m_task->DropOut();
    m_authenticated = false;
    m_curPhase = kNone;
}

// 0x0054C9B0
bool RankingsManager::UpdateLoggedIn()
{
    m_authenticated = IsStatsConnected() == 1;
    return m_authenticated;
}

// 0x0054CBA0
void RankingsManager::OnLoggedIn()
{
    PushTopDown();
    m_authenticated = true;
    Run(0x6307C08F);
    PopHeap();
}

// 0x0054CE80
void RankingsManager::StartGame()
{
    Hub* manager = NetSessionSlot();
    Detail::PlayerSearch search;
    PushBottomUp();
    m_game = NewGame(1);
    const char* challenge = GetChallenge(m_game);
    auto* server = static_cast<NetLink::AppHost*>(manager->m_host);
    OptionStore::Preferences* prefs = manager->GetOnlineSettings();
    const char* hostName;
    prefs->FetchSetting(0x2A5CF019)->FetchText(0x96875C0Fu, &hostName, 1);
    int timeLimit = prefs->GetSettingValue(0xC51BF6E3, 0x906B67BA);
    int fragLimit = prefs->GetSettingValue(0x71CD53A2, 0xCD66C8AE);
    SetGameString(m_game, "hostname", hostName);
    SetGameString(m_game, "mapname", manager->GetMapName(true));
    SetGameString(m_game, "gametype", ActiveRulesetLabel(manager));
    SetGameInt(m_game, "mapcrc", static_cast<int>(LevelKeyHash(manager)));
    SetGameInt(m_game, "gametypecrc", static_cast<int>(GameModeChecksum(manager)));
    SetGameInt(m_game, "maxplayers", manager->GetPlayerLimit());
    SetGameInt(m_game, "timelimit", timeLimit);
    SetGameInt(m_game, "fraglimit", fragLimit);
    for (uint8_t* player = Detail::StartRosterSweep(manager, &search, false); player;
         player = Detail::StepRosterSweep(manager, &search, false)) {
        NetLink::NetMessageInfo desc;
        desc.MakeNew();
        int id = static_cast<int>(Detail::SkaterId(Detail::PlayerSkater(player)));
        const char* name = Detail::PlayerName(player);
        NewPlayer(m_game, id, name);
        char key[64];
        std::snprintf(key, sizeof(key), "%s", "player");
        SetPlayerString(m_game, key, name, id);
        std::snprintf(key, sizeof(key), "%s", "pid");
        int profile = Detail::Prop<int>(player, 0x28);
        SetPlayerInt(m_game, key, profile, id);
        if (profile > 0) {
            // The player's client answers with its response to the challenge.
            desc.m_content = const_cast<char*>(challenge);
            desc.m_byteCount = static_cast<uint16_t>(std::strlen(challenge) + 1);
            desc.m_ident = 0x7C;
            desc.m_queue = 2;
            desc.m_groupId = 8;
            server->PutOnOutbox(Detail::PlayerHandle(player), &desc);
        }
    }
    PopHeap();
}

// 0x0054D170
void RankingsManager::RaiseTallies(bool final)
{
    Hub* manager = NetSessionSlot();
    Detail::PlayerSearch search;
    if (!m_game)
        return;
    PushBottomUp();
    for (uint8_t* player = Detail::StartRosterSweep(manager, &search, false); player;
         player = Detail::StepRosterSweep(manager, &search, false)) {
        auto* skater = reinterpret_cast<GameObjects::CGameEntity*>(Detail::PlayerSkater(player));
        skater->FetchModule(0xD262802F); // (unused)
        int id = static_cast<int>(Detail::SkaterId(skater));
        SetPlayerInt(m_game, "score", PlayerPoints(manager, id), id);
        SetPlayerInt(m_game, "combo", Detail::Prop<int>(player, 0x24), id);
    }
    SetGameInt(m_game, "highscore", manager->CheatingNoted() | CheatFlag());
    SendGameSnapShot(m_game, nullptr, final);
    if (final) {
        m_curPhase = kReady;
        if (m_game) {
            FreeGame(m_game);
            m_game = nullptr;
        }
    }
    m_lastReport = FrameClock::GetTime();
    PopHeap();
}

// 0x0054C9F0
void RankingsManager::RemovePlayer(int id)
{
    if (m_curPhase == kReporting)
        RemovePlayerFromGame(m_game, id);
}

// 0x0054CA30
void RankingsManager::SetPlayerAuth(int player, const char* auth)
{
    char key[64];
    std::snprintf(key, sizeof(key), "%s", "auth");
    SetPlayerString(m_game, key, auth, player);
}

// 0x0054CA80
PersonalStats* RankingsManager::GetPersonalStats() { return &m_personal; }

// 0x0054D2D0
int RankingsManager::GetRank() { return Rank(static_cast<double>(m_personal.m_skillPoints)); }

// 0x0054D7B0
void RankingsManager::ReadSkillRatings(char* text)
{
    PushTopDown();
    for (char* token = Text::Token(text, ":"); token; token = Text::Token(nullptr, ":")) {
        TopEntry* entry = NewEntry();
        Append(&m_topLists[4].m_entries, &entry->m_listLink);
        entry->m_pointsHeld = std::atol(token);
        if (!(token = Text::Token(nullptr, ":")))
            break;
        entry->m_skillPoints = std::atol(token);
        if (!(token = Text::Token(nullptr, "\n")))
            break;
        Text::DupFrom(entry->m_label, token);
    }
    PopHeap();
}

// 0x0054D910
void RankingsManager::ParseTopList(int index, char* text, bool titled)
{
    TopList& list = m_topLists[index];
    PushTopDown();
    if (titled) {
        Text::DupFrom(list.m_title, Text::Token(text, "\n"));
        text = nullptr;
    }
    char* token = Text::Token(text, ":");
    // "Level:<checksum>\n", then its lines "score:rating:name\n" until the next "Level" (anything else first ends it).
    while (token && TextUtil::CompareCaseless(token, "Level") == 0) {
        char* value = Text::Token(nullptr, "\n");
        if (!value)
            break;
        int level = std::atol(value);
        auto* scores = static_cast<LevelScores*>(MemSys::AllocZeroed(sizeof(LevelScores)));
        if (scores) {
            InitNode(&scores->m_listLink, kLevelVtable);
            SetupListSentinel(&scores->m_entries, kEntryVtable);
        }
        scores->m_level = static_cast<uint32_t>(level);
        Append(&list.m_levels, &scores->m_listLink);
        token = Text::Token(nullptr, ":");
        while (token && TextUtil::CompareCaseless(token, "Level") != 0) {
            TopEntry* entry = NewEntry();
            Append(&scores->m_entries, &entry->m_listLink);
            entry->m_pointsHeld = std::atol(token);
            if (!(token = Text::Token(nullptr, ":")))
                break;
            entry->m_skillPoints = std::atol(token);
            if (!(token = Text::Token(nullptr, "\n")))
                break;
            Text::DupFrom(entry->m_label, token);
            token = Text::Token(nullptr, ":");
        }
    }
    PopHeap();
}

// 0x0054D4A0
void __cdecl RankingsManager::StatsTask(Scheduling::SchedulerTask* task)
{
    PushTopDown();
    auto* stats = reinterpret_cast<RankingsManager*>(reinterpret_cast<void**>(task)[7]);
    bool failed = false;
    switch (stats->m_curPhase) {
    case kLoggingIn:
        failed = FrameClock::GetTime() - LoginStart() > kLoginTimeout;
        break;
    case kAuthDone:
        stats->m_curPhase = kReady;
        stats->OnLoggedIn();
        break;
    case kLoginFailed:
        Run(0xCBA113AF);
        CloseStatsConnection();
        stats->m_task->DropOut();
        stats->m_authenticated = false;
        stats->m_curPhase = kNone;
        break;
    case kGettingStats:
        failed = FrameClock::GetTime() - LoginStart() > kLoginTimeout || PersistThink() == 0;
        break;
    case kReporting:
        if (FrameClock::GetTime() - stats->m_lastReport > kReportInterval)
            stats->RaiseTallies(false);
        break;
    default: // kReady
        break;
    }
    if (failed) {
        Run(0xD1EA7744);
        stats->LogOff();
    }
    PopHeap();
}

// 0x0054CAC0
unsigned long __stdcall RankingsManager::LogInThread(void* param)
{
    PushBottomUp();
    RankingsManager* stats = Attributes();
    (void)param;
    // The game name: the online identity's (OnlineGameName; the original pushes "thps6pc", which online_identity
    // used to write over). The key: what online_identity leaves in the stats key's data (OnlineGameKey).
    std::snprintf(GcdGameName(), 0x100, "%s", NetLink::GameName());
    std::snprintf(GcdSecretKey(), 0x100, "%s", StatsKey());
    switch (InitStatsConnection(kStatsPort)) {
    case 0: // connected
        stats->m_curPhase = kAuthDone;
        break;
    case 1:
    case 2:
    case 3:
    case 5:
        stats->m_curPhase = kLoginFailed;
        break;
    default: // 4 and others
        break;
    }
    PopHeap();
    return 0;
}

// 0x0054CCF0
void __cdecl RankingsManager::OnPersonalStats(int, int, int, int index, int success, uint32_t, char* data, int, void*)
{
    if (success < 1)
        return;
    RankingsManager* stats = Attributes();
    int* values = &stats->m_personal.m_skillPoints; // (index 0 the rating, then the high scores; combos 15 further)
    if (index == 0) {
        if (Text::Token(data, "\\")) // "THUG2Rating"
            values[0] = std::atol(Text::Token(nullptr, "\\"));
        return;
    }
    char* key = Text::Token(data, "\\");
    if (key) {
        if (TextUtil::CompareCaseless(key, "THUG2HighScore") == 0) {
            if (char* value = Text::Token(nullptr, "\\"))
                values[index] = std::atol(value);
            key = Text::Token(nullptr, "\\");
        }
        if (TextUtil::CompareCaseless(key, "THUG2HighCombo") == 0) { // (null key: a crash, as in the original)
            if (char* value = Text::Token(nullptr, "\\"))
                values[index + 15] = std::atol(value);
        }
    }
    if (index == 14) {
        Run(0x955801C6);
        stats->m_curPhase = kReady;
    }
}

// 0x0054DB20
int __cdecl RankingsManager::OnTopStats(void*, int result, char* buffer, int64_t, void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    vault->m_progress = 100; // (the setters folded at 0x0048DF10, 0x00428270, 0x004076E0)
    if (result != 0) {
        vault->m_outcomeVal = 2;
        vault->m_fault = result;
        return 1;
    }
    vault->m_outcomeVal = 1;
    vault->m_fault = 0;
    RankingsManager* stats = Attributes();
    switch (stats->m_topFile) {
    case 0:
    case 1:
        stats->ParseTopList(stats->m_topFile, buffer, false);
        break;
    case 2:
    case 3:
        stats->ParseTopList(stats->m_topFile, buffer, true);
        break;
    case 4:
        stats->ReadSkillRatings(buffer);
        break;
    default:
        break;
    }
    return 1;
}

// 0x0054D2F0
const char* __stdcall RankingsManager::LevelName(uint32_t level)
{
    Scripts::CArray* levels = Scripts::FetchList(0xB8D3EEECu);
    // (the found level is read into one variable: an entry without one counts as the last one read - at first
    // the level asked for)
    uint32_t found = level;
    for (uint32_t i = 0; i < levels->m_extent; ++i) {
        auto* entry = reinterpret_cast<Scripts::CStruct*>(levels->FetchPart(i));
        entry->ReadHashValue(0x651533ECu, &found, 0);
        if (found == level) {
            const char* name = nullptr;
            entry->FetchText(0xC4745838u, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// 0x0054CA10
void __stdcall RankingsManager::GenerateAuth(const char* challenge, const char* password, char* response)
{
    GameSpy::StatsGenerateAuth(challenge, password, response);
}

// --- the script commands ---

// 0x0054CC90
bool __cdecl StatsLoggedIn(Scripts::CStruct*, Scripts::CScript*) { return Attributes()->UpdateLoggedIn(); }

// 0x0054CCB0
bool __cdecl StatsLogIn(Scripts::CStruct*, Scripts::CScript*)
{
    Attributes()->LogIn();
    return true;
}

// 0x0054CCC0
bool __cdecl StatsLogOff(Scripts::CStruct*, Scripts::CScript*)
{
    Attributes()->LogOff();
    return true;
}

// 0x0054CE00: the player's rating and the levels' high scores and combos from persistent storage (16 requests,
// indexes 0-15; state 5 until the answer to 14).
bool __cdecl RetrievePersonalStats(Scripts::CStruct*, Scripts::CScript*)
{
    Hub* manager = NetSessionSlot();
    const void* callback = reinterpret_cast<const void*>(&RankingsManager::OnPersonalStats);
    int profile = static_cast<FriendsManager*>(manager->m_presenceService)->m_account;
    GetPersistDataValues(0, profile, 2, 0, "\\THUG2Rating", callback, nullptr);
    for (int index = 1; index <= kPersonalStatsLevels; ++index) { // (1-15: the answer to 14 ends the wait)
        profile = static_cast<FriendsManager*>(manager->m_presenceService)->m_account;
        GetPersistDataValues(0, profile, 2, index, "\\THUG2HighScore\\THUG2HighCombo", callback, nullptr);
    }
    static_cast<RankingsManager*>(manager->m_ratingService)->m_curPhase = RankingsManager::kGettingStats;
    return true;
}

// 0x0054CE70
bool __cdecl NeedToRetrieveTopStats(Scripts::CStruct*, Scripts::CScript*) { return Attributes()->m_needTopStats; }

// 0x0054D670: RaiseTallies [0x132AA339: the final report]
bool __cdecl RaiseTallies(Scripts::CStruct* params, Scripts::CScript*)
{
    bool final = params->HasBareFlag(0x132AA339u);
    Attributes()->RaiseTallies(final);
    return true;
}

// 0x0054D6A0: the rank (0..10) into the script's parameters (0x7786171A).
bool __cdecl GetRank(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(0x7786171A, Rank(static_cast<double>(Attributes()->m_personal.m_skillPoints)));
    return true;
}

// 0x0054EA40: the top lists and the stats menus' arrays emptied (once: until the top stats are needed again).
bool __cdecl CleanUpTopStats(Scripts::CStruct*, Scripts::CScript*)
{
    RankingsManager* stats = Attributes();
    if (stats->m_needTopStats)
        return true;
    for (TopList& list : stats->m_topLists)
        list.Dealloc();
    stats->m_needTopStats = true;
    Scripts::ReleaseArrayStorage(Scripts::FetchList("high_scores_array_all_time"));
    Scripts::ReleaseArrayStorage(Scripts::FetchList("best_combos_array_all_time"));
    Scripts::ReleaseArrayStorage(Scripts::FetchList("high_scores_array_monthly"));
    Scripts::ReleaseArrayStorage(Scripts::FetchList("best_combos_array_monthly"));
    Scripts::ReleaseArrayStorage(Scripts::FetchList("top_players_array"));
    Scripts::ReleaseArrayStorage(Scripts::FetchList("personal_stats_array"));
    GhttpCleanup();
    return true;
}

// 0x0054EB00: FillStatsArrays <0x5EF31148: which>: the personal stats, the ratings, or a top list (its levels' best,
// then all sorted).
bool __cdecl FillStatsArrays(Scripts::CStruct* params, Scripts::CScript*)
{
    RankingsManager* stats = Attributes();
    uint32_t which = 0;
    params->ReadHashValue(0x5EF31148u, &which, 1);
    TopList* list = nullptr;
    switch (which) {
    case 0xEF20D3A1: list = &stats->m_topLists[0]; break;
    case 0x9FE64312: list = &stats->m_topLists[1]; break;
    case 0xCB95928F: list = &stats->m_topLists[2]; break;
    case 0x67F1AE8A: list = &stats->m_topLists[3]; break;
    case 0x5299291B:
        stats->m_topLists[4].FillSortedScores(true);
        return true;
    case 0xFEF84FFF:
        stats->m_personal.Fill();
        return true;
    default:
        return true;
    }
    list->FillLevelScores();
    list->FillSortedScores(false);
    return true;
}

} // namespace OnlinePlay

using OnlinePlay::RankingsManager;
