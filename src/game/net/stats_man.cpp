// The online stats (GameNet::StatsMan). See stats_man.h.

#include "game/net/stats_man.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/core/task.h"
#include "game/lst/head.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/online_identity.h"
#include "game/net/online_text.h"
#include "game/net/vault_man.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace GameNet {

GAME_GLOBAL(float, g_rankScale, 0x0064CC1C, 0.00028571428f);

namespace {

using Detail::DeleteParams;
using Detail::NewParams;

void** const kEntryVtable = reinterpret_cast<void**>(0x0064D640);      // TopEntry (and the entries' heads)
void** const kLevelsHeadVtable = reinterpret_cast<void**>(0x0064D644); // a top list's levels head
void** const kLevelVtable = reinterpret_cast<void**>(0x0064D648);      // LevelScores
void** const kSortVtable = reinterpret_cast<void**>(0x0064D64C);       // SortEntry (and the sorted list's head)
void** const kEntryNodeVtable = reinterpret_cast<void**>(0x0064D634);  // Lst::Node<TopEntry>
void** const kLevelNodeVtable = reinterpret_cast<void**>(0x0064D638);  // Lst::Node<LevelScores>
void** const kSortNodeVtable = reinterpret_cast<void**>(0x0064D63C);   // Lst::Node<SortEntry>
void** const kSortSearchVtable = reinterpret_cast<void**>(0x0064D580);
void** const kEntrySearchVtable = reinterpret_cast<void**>(0x0064D584);
void** const kLevelSearchVtable = reinterpret_cast<void**>(0x0064D588);
void** const kManagerTaskVtable = reinterpret_cast<void**>(0x006476F0); // Tsk::Task<StatsMan>

constexpr uint32_t kLoginTimeout = 10000;  // ms
constexpr uint32_t kReportInterval = 60000; // ms between reports of a game
constexpr int kPersonalStatsLevels = 15;  // GetPersistDataValues' indexes: 0 the rating, 1-15 the levels
constexpr int kMaxSorted = 50;            // lines of a sorted list into the menu
constexpr int kLevelBest = 3;             // a level's best lines into the menu
constexpr int kMaxRank = 10;
constexpr int kStatsPort = 0x1421;

// The rank's factors (0x0064CC1C, 0x00646710).
float RankScale() { return g_rankScale; } // 0.000286
float RankTimes() { return GameVariable<float>(0x00646710); } // 10
// rating * 0.000286 * 10 truncated, at most 10 (x87 at 53 bits: each product rounded to double).
int Rank(double rating)
{
    int rank = static_cast<int>(rating * RankScale() * RankTimes());
    return rank > kMaxRank ? kMaxRank : rank;
}

GAME_GLOBAL(uint32_t, g_statsLoginStart, 0x007CE460);

// The login's start (StatsTask's limit).
uint32_t& LoginStart() { return g_statsLoginStart; }
// Set by the cheat script commands (career.cpp): the game reported as a cheat's.
bool CheatFlag() { return g_cheatFlag; }

Manager* NetManager() { return g_gameNetManager; }
StatsMan* Stats() { return static_cast<StatsMan*>(NetManager()->m_statsMan); }
Mem::Manager* Memory() { return Mem::Manager::Get(); }
void PushTopDown() { Memory()->PushContext(Memory()->m_internetTopDownHeap); }
void PushBottomUp() { Memory()->PushContext(Memory()->m_internetBottomUpHeap); }
void PopHeap() { Memory()->PopContext(); }
Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(GameVariable<uint8_t*>(0x00696D68) + 0x04); }

uint32_t Crc(const char* name) { return Crc::GenerateCRCFromString(name); }
void Run(uint32_t script, Script::CStruct* params = nullptr) { Script::RunScript(script, params, nullptr, false, nullptr); }

// GameSpy's stats and persistent storage SDK (library code; cdecl).
int IsStatsConnected() { return GameFunction<int(__cdecl*)()>(0x00600710)(); }
void CloseStatsConnection() { GameFunction<void(__cdecl*)()>(0x00601280)(); }
int InitStatsConnection(int port) { return GameFunction<int(__cdecl*)(int)>(0x00602550)(port); }
int PersistThink() { return GameFunction<int(__cdecl*)()>(0x00602210)(); }
void* NewGame(int usebuckets) { return GameFunction<void*(__cdecl*)(int)>(0x00601A70)(usebuckets); }
void FreeGame(void* game) { GameFunction<void(__cdecl*)(void*)>(0x00600720)(game); }
const char* GetChallenge(void* game) { return GameFunction<const char*(__cdecl*)(void*)>(0x006012E0)(game); }
void NewPlayer(void* game, int number, const char* name)
{
    GameFunction<int(__cdecl*)(void*, int, const char*)>(0x00600780)(game, number, name);
}
void RemovePlayerFromGame(void* game, int number) { GameFunction<int(__cdecl*)(void*, int)>(0x00600850)(game, number); }
void SendGameSnapShot(void* game, const char* snapshot, int final)
{
    GameFunction<int(__cdecl*)(void*, const char*, int)>(0x00601CE0)(game, snapshot, final);
}
void GetPersistDataValues(int localId, int profile, int type, int index, const char* keys, const void* callback,
                          void* instance)
{
    GameFunction<int(__cdecl*)(int, int, int, int, const char*, const void*, void*)>(0x006021E0)(
        localId, profile, type, index, keys, callback, instance);
}
// The SDK's bucket operations, kept by it as function pointers (game / player keys, ints / strings), with its "set"
// operator (also kept by it).
using IntOp = void*(__cdecl*)(void*, const char*, void*, int, int);
using StringOp = void*(__cdecl*)(void*, const char*, void*, const char*, int);
void* SetOperator() { return GameVariable<void*>(0x0068E818); }
void SetGameInt(void* game, const char* key, int value) { GameVariable<IntOp>(0x0068E834)(game, key, SetOperator(), value, 0); }
void SetGameString(void* game, const char* key, const char* value)
{
    GameVariable<StringOp>(0x0068E83C)(game, key, SetOperator(), value, 0);
}
void SetPlayerInt(void* game, const char* key, int value, int player)
{
    GameVariable<IntOp>(0x0068E84C)(game, key, SetOperator(), value, player);
}
void SetPlayerString(void* game, const char* key, const char* value, int player)
{
    GameVariable<StringOp>(0x0068E854)(game, key, SetOperator(), value, player);
}
void GhttpCleanup() { GameFunction<void(__cdecl*)()>(0x005FF860)(); }
// The SDK's names for the stats connection (written before InitStatsConnection).
char* GcdGameName() { return &GameVariable<char>(0x007D7A78); }
char* GcdSecretKey() { return &GameVariable<char>(0x007D7B78); }
// The secret key (data the online identity writes over when OnlineGameKey is set: online_identity.cpp).
const char* StatsKey() { return reinterpret_cast<const char*>(0x0064D58C); }

// The manager's calls.
const char* GameModeName(Manager* manager) { return manager->GetGameTypeName(); }
uint32_t LevelChecksum(Manager* manager) { return manager->GetLevel(); }
uint32_t GameModeChecksum(Manager* manager) { return manager->GetGameTypePreference(); }
int PlayerScore(Manager* manager, int id) { return manager->GetPlayerScore(static_cast<uint32_t>(id)); }

void DeleteNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}
void InitHead(Mem::ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
void InitNode(Mem::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}
void Append(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
Mem::ListNode* NextNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}
// Delete every item of a list (each one's next fetched first).
void DeleteAll(Mem::ListNode* head)
{
    for (Mem::ListNode* node = NextNode(head); node;) {
        Mem::ListNode* next = NextNode(node);
        if (node->data)
            DeleteNode(node->data);
        node = next;
    }
}
int Count(const Mem::ListNode* head)
{
    int count = 0;
    for (const Mem::ListNode* node = head->next; node->data != reinterpret_cast<void*>(-1) && node; node = node->next)
        ++count;
    return count;
}

TopEntry* NewEntry()
{
    auto* entry = static_cast<TopEntry*>(Mem::Calloc(sizeof(TopEntry)));
    if (entry)
        InitNode(&entry->m_node, kEntryVtable);
    return entry;
}
SortEntry* NewSortEntry()
{
    auto* entry = static_cast<SortEntry*>(Mem::Calloc(sizeof(SortEntry)));
    if (entry) {
        InitNode(&entry->m_node, kSortVtable);
        entry->m_score = 0;
        entry->m_level = 0;
    }
    return entry;
}
// A local sorted list (Lst::Head<SortEntry>, highest priority first), emptied and unlinked as the original does.
struct SortedList {
    Mem::ListNode head;
    SortedList() { InitHead(&head, kSortVtable); }
    void Add(SortEntry* entry) { reinterpret_cast<Lst::Head*>(&head)->AddNode(&entry->m_node); }
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
void StatsNode::DestroyEntryNode() { m_node.UnlinkAs(kEntryNodeVtable); }
void StatsNode::DestroyLevelNode() { m_node.UnlinkAs(kLevelNodeVtable); }
void StatsNode::DestroySortNode() { m_node.UnlinkAs(kSortNodeVtable); }

// 0x0054D420
StatsNode* StatsNode::DeleteEntryNode(unsigned int flags)
{
    DestroyEntryNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054D460
StatsNode* StatsNode::DeleteLevelNode(unsigned int flags)
{
    DestroyLevelNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054D740
StatsNode* StatsNode::DeleteSortNode(unsigned int flags)
{
    DestroySortNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054D710
StatsNode* StatsNode::DeleteLevelsHead(unsigned int flags)
{
    DestroyLevelNode();
    if (flags & 1)
        Memory()->Free(this);
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
        Memory()->Free(this);
    return this;
}

// 0x0054D3A0
StatsSearch* StatsSearch::DeleteEntrySearch(unsigned int flags)
{
    m_vtable = kEntrySearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054D3D0
StatsSearch* StatsSearch::DeleteLevelSearch(unsigned int flags)
{
    m_vtable = kLevelSearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054D6E0
TopEntry* TopEntry::Destroy(unsigned int flags)
{
    reinterpret_cast<StatsNode*>(this)->DestroyEntryNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054D780
SortEntry* SortEntry::Destroy(unsigned int flags)
{
    reinterpret_cast<StatsNode*>(this)->DestroySortNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054DFE0
void LevelScores::DestroyBody()
{
    m_node.vtable = kLevelVtable;
    DeleteAll(&m_entries);
    m_entries.UnlinkAs(kEntryNodeVtable);
    m_node.UnlinkAs(kLevelNodeVtable);
}

// 0x0054EBB0
LevelScores* LevelScores::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// --- the top lists ---

// 0x0054D8B0
TopList* TopList::Construct()
{
    InitHead(&m_levels, kLevelsHeadVtable);
    InitHead(&m_entries, kEntryVtable);
    return this;
}

// 0x0054D5C0
void TopList::DestroyBody()
{
    m_entries.UnlinkAs(kEntryNodeVtable);
    m_levels.UnlinkAs(kLevelNodeVtable);
}

// 0x0054E920
void TopList::Free()
{
    DeleteAll(&m_levels);
    DeleteAll(&m_entries);
}

// 0x0054E0D0
void TopList::FillLevelScores()
{
    for (Mem::ListNode* node = NextNode(&m_levels); node; node = NextNode(node)) {
        auto* level = static_cast<LevelScores*>(node->data);
        if (!level)
            break;
        if (Count(&level->m_entries) > 0) {
            Script::CStruct* params = NewParams();
            params->AddString(0xC4745838, StatsMan::LevelName(level->m_level));
            Run(0x1AADEE21, params);
            DeleteParams(params);
        }
        int shown = 0;
        for (Mem::ListNode* line = NextNode(&level->m_entries); line && shown < kLevelBest; line = NextNode(line)) {
            auto* entry = static_cast<TopEntry*>(line->data);
            if (!entry)
                break;
            Script::CStruct* params = NewParams();
            params->AddInteger(0x2776D9DD, entry->m_rating);
            params->AddInteger(0xCD66C8AE, entry->m_score);
            // (here the rating is read as unsigned)
            params->AddInteger(0x7786171A, Rank(static_cast<double>(static_cast<uint32_t>(entry->m_rating))));
            params->AddString(0xA1DC81F9, entry->m_name);
            params->AddString(0x651533EC, StatsMan::LevelName(level->m_level));
            Run(0x19F7BC28, params);
            DeleteParams(params);
            ++shown;
        }
    }
    Script::CStruct* params = NewParams();
    params->AddString(0xC4745838, Script::GetString(0xC2283319u));
    Run(0x1AADEE21, params);
    DeleteParams(params);
}

// 0x0054E4A0
void TopList::FillSortedScores(bool ratings)
{
    SortedList sorted;
    if (ratings) {
        for (Mem::ListNode* node = NextNode(&m_entries); node; node = NextNode(node)) {
            auto* entry = static_cast<TopEntry*>(node->data);
            if (!entry)
                break;
            SortEntry* line = NewSortEntry();
            line->m_node.priority = entry->m_rating;
            line->m_score = entry->m_score;
            line->m_rating = entry->m_rating;
            Text::Copy(line->m_name, entry->m_name);
            sorted.Add(line);
        }
    } else {
        for (Mem::ListNode* node = NextNode(&m_levels); node; node = NextNode(node)) {
            auto* level = static_cast<LevelScores*>(node->data);
            if (!level)
                break;
            for (Mem::ListNode* item = NextNode(&level->m_entries); item; item = NextNode(item)) {
                auto* entry = static_cast<TopEntry*>(item->data);
                if (!entry)
                    break;
                SortEntry* line = NewSortEntry();
                line->m_node.priority = entry->m_score;
                line->m_score = entry->m_score;
                line->m_level = level->m_level;
                line->m_rating = entry->m_rating;
                Text::Copy(line->m_name, entry->m_name);
                sorted.Add(line); // (inlined in the original)
            }
        }
    }
    int shown = 0;
    for (Mem::ListNode* node = NextNode(&sorted.head); node; node = NextNode(node)) {
        auto* line = static_cast<SortEntry*>(node->data);
        if (!line || line->m_score == 0 || shown == kMaxSorted)
            break;
        Script::CStruct* params = NewParams();
        params->AddInteger(0x2776D9DD, line->m_rating);
        params->AddInteger(0xCD66C8AE, line->m_score);
        params->AddInteger(0x7786171A, Rank(static_cast<double>(line->m_rating)));
        params->AddString(0xA1DC81F9, line->m_name);
        if (ratings) {
            Run(0x0AC09E3B, params);
        } else {
            params->AddString(0x651533EC, StatsMan::LevelName(line->m_level));
            Run(0x19F7BC28, params);
        }
        DeleteParams(params);
        ++shown;
    }
}

// 0x0054DDB0
void PersonalStats::Fill()
{
    const auto* levels = reinterpret_cast<const uint32_t*>(0x0067F260);
    SortedList sorted;
    for (int i = 0; i < 15; ++i) {
        int score = m_highScores[i];
        if (score == 0)
            continue;
        SortEntry* line = NewSortEntry();
        line->m_node.priority = score;
        line->m_score = score;
        line->m_level = levels[i];
        sorted.Add(line);
    }
    for (Mem::ListNode* node = NextNode(&sorted.head); node; node = NextNode(node)) {
        auto* line = static_cast<SortEntry*>(node->data);
        if (!line)
            break;
        Script::CStruct* params = NewParams(); // (never freed - as in the original)
        params->AddInteger(0xCD66C8AE, line->m_score);
        params->AddString(0x651533EC, StatsMan::LevelName(line->m_level));
        Run(0xA6111F30, params);
    }
}

// --- the stats manager ---

// 0x0054DBD0
StatsMan* StatsMan::Construct()
{
    m_personal.m_rating = 0;
    std::memset(m_personal.m_highScores, 0, sizeof(m_personal.m_highScores));
    std::memset(m_personal.m_highCombos, 0, sizeof(m_personal.m_highCombos));
    for (TopList& list : m_topLists)
        list.Construct();
    Memory()->PushContext(Memory()->m_networkHeap);
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (task) {
        task->Construct(0);
        task->m_vtable = kManagerTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&StatsTask); // +0x18 the callback
        reinterpret_cast<void**>(task)[7] = this;                                // +0x1C the data
    }
    m_task = task;
    m_loggedIn = false;
    m_state = kNone;
    m_game = nullptr;
    m_needTopStats = true;
    PopHeap();
    return this;
}

// 0x0054D610
void StatsMan::DestroyBody()
{
    if (m_task) // (its scalar deleting destructor, vtable slot 2)
        reinterpret_cast<void*(__thiscall*)(Tsk::BaseTask*, unsigned int)>(m_task->m_vtable[2])(m_task, 1);
    for (int i = 4; i >= 0; --i)
        m_topLists[i].DestroyBody();
}

// 0x0054CBE0
void StatsMan::LogIn()
{
    PushBottomUp();
    m_state = kLoggingIn;
    LoginStart() = Tmr::GetTime();
    Run(0x23343580);
    DWORD id;
    HANDLE thread = CreateThread(nullptr, 0x8000, &LogInThread, this, CREATE_SUSPENDED, &id);
    NetManager()->SetLobbyId(id);
    ResumeThread(thread); // (never closed)
    if (!m_task->InList())
        LogicTasks()->AddTask(m_task);
    PopHeap();
}

// 0x0054C9D0
void StatsMan::LogOff()
{
    CloseStatsConnection();
    m_task->Remove();
    m_loggedIn = false;
    m_state = kNone;
}

// 0x0054C9B0
bool StatsMan::UpdateLoggedIn()
{
    m_loggedIn = IsStatsConnected() == 1;
    return m_loggedIn;
}

// 0x0054CBA0
void StatsMan::OnLoggedIn()
{
    PushTopDown();
    m_loggedIn = true;
    Run(0x6307C08F);
    PopHeap();
}

// 0x0054CE80
void StatsMan::StartGame()
{
    Manager* manager = NetManager();
    Detail::PlayerSearch search;
    PushBottomUp();
    m_game = NewGame(1);
    const char* challenge = GetChallenge(m_game);
    auto* server = static_cast<Net::App*>(manager->m_server);
    Prefs::Preferences* prefs = manager->GetNetworkPreferences();
    const char* hostName;
    prefs->GetPreference(0x2A5CF019)->GetString(0x96875C0Fu, &hostName, 1);
    int timeLimit = prefs->GetPreferenceValue(0xC51BF6E3, 0x906B67BA);
    int fragLimit = prefs->GetPreferenceValue(0x71CD53A2, 0xCD66C8AE);
    SetGameString(m_game, "hostname", hostName);
    SetGameString(m_game, "mapname", manager->GetLevelName(true));
    SetGameString(m_game, "gametype", GameModeName(manager));
    SetGameInt(m_game, "mapcrc", static_cast<int>(LevelChecksum(manager)));
    SetGameInt(m_game, "gametypecrc", static_cast<int>(GameModeChecksum(manager)));
    SetGameInt(m_game, "maxplayers", manager->GetMaxPlayers());
    SetGameInt(m_game, "timelimit", timeLimit);
    SetGameInt(m_game, "fraglimit", fragLimit);
    for (uint8_t* player = Detail::FirstPlayer(manager, &search, false); player;
         player = Detail::NextPlayer(manager, &search, false)) {
        Net::MsgDesc desc;
        desc.Construct();
        int id = static_cast<int>(Detail::SkaterId(Detail::PlayerSkater(player)));
        const char* name = Detail::PlayerName(player);
        NewPlayer(m_game, id, name);
        char key[64];
        std::snprintf(key, sizeof(key), "%s", "player");
        SetPlayerString(m_game, key, name, id);
        std::snprintf(key, sizeof(key), "%s", "pid");
        int profile = Detail::Field<int>(player, 0x28);
        SetPlayerInt(m_game, key, profile, id);
        if (profile > 0) {
            // The player's client answers with its response to the challenge.
            desc.m_data = const_cast<char*>(challenge);
            desc.m_length = static_cast<uint16_t>(std::strlen(challenge) + 1);
            desc.m_id = 0x7C;
            desc.m_queue = 2;
            desc.m_groupId = 8;
            server->EnqueueMessage(Detail::PlayerHandle(player), &desc);
        }
    }
    PopHeap();
}

// 0x0054D170
void StatsMan::ReportStats(bool final)
{
    Manager* manager = NetManager();
    Detail::PlayerSearch search;
    if (!m_game)
        return;
    PushBottomUp();
    for (uint8_t* player = Detail::FirstPlayer(manager, &search, false); player;
         player = Detail::NextPlayer(manager, &search, false)) {
        auto* skater = reinterpret_cast<Obj::CCompositeObject*>(Detail::PlayerSkater(player));
        skater->GetComponent(0xD262802F); // (unused)
        int id = static_cast<int>(Detail::SkaterId(skater));
        SetPlayerInt(m_game, "score", PlayerScore(manager, id), id);
        SetPlayerInt(m_game, "combo", Detail::Field<int>(player, 0x24), id);
    }
    SetGameInt(m_game, "highscore", manager->CheatingNoted() | CheatFlag());
    SendGameSnapShot(m_game, nullptr, final);
    if (final) {
        m_state = kReady;
        if (m_game) {
            FreeGame(m_game);
            m_game = nullptr;
        }
    }
    m_lastReport = Tmr::GetTime();
    PopHeap();
}

// 0x0054C9F0
void StatsMan::RemovePlayer(int id)
{
    if (m_state == kReporting)
        RemovePlayerFromGame(m_game, id);
}

// 0x0054CA30
void StatsMan::SetPlayerAuth(int player, const char* auth)
{
    char key[64];
    std::snprintf(key, sizeof(key), "%s", "auth");
    SetPlayerString(m_game, key, auth, player);
}

// 0x0054CA80
PersonalStats* StatsMan::GetPersonalStats() { return &m_personal; }

// 0x0054D2D0
int StatsMan::GetRank() { return Rank(static_cast<double>(m_personal.m_rating)); }

// 0x0054D7B0
void StatsMan::ParseRatings(char* text)
{
    PushTopDown();
    for (char* token = Text::Token(text, ":"); token; token = Text::Token(nullptr, ":")) {
        TopEntry* entry = NewEntry();
        Append(&m_topLists[4].m_entries, &entry->m_node);
        entry->m_score = std::atol(token);
        if (!(token = Text::Token(nullptr, ":")))
            break;
        entry->m_rating = std::atol(token);
        if (!(token = Text::Token(nullptr, "\n")))
            break;
        Text::Copy(entry->m_name, token);
    }
    PopHeap();
}

// 0x0054D910
void StatsMan::ParseTopList(int index, char* text, bool titled)
{
    TopList& list = m_topLists[index];
    PushTopDown();
    if (titled) {
        Text::Copy(list.m_title, Text::Token(text, "\n"));
        text = nullptr;
    }
    char* token = Text::Token(text, ":");
    // "Level:<checksum>\n", then its lines "score:rating:name\n" until the next "Level" (anything else first ends it).
    while (token && Str::CompareCaseless(token, "Level") == 0) {
        char* value = Text::Token(nullptr, "\n");
        if (!value)
            break;
        int level = std::atol(value);
        auto* scores = static_cast<LevelScores*>(Mem::Calloc(sizeof(LevelScores)));
        if (scores) {
            InitNode(&scores->m_node, kLevelVtable);
            InitHead(&scores->m_entries, kEntryVtable);
        }
        scores->m_level = static_cast<uint32_t>(level);
        Append(&list.m_levels, &scores->m_node);
        token = Text::Token(nullptr, ":");
        while (token && Str::CompareCaseless(token, "Level") != 0) {
            TopEntry* entry = NewEntry();
            Append(&scores->m_entries, &entry->m_node);
            entry->m_score = std::atol(token);
            if (!(token = Text::Token(nullptr, ":")))
                break;
            entry->m_rating = std::atol(token);
            if (!(token = Text::Token(nullptr, "\n")))
                break;
            Text::Copy(entry->m_name, token);
            token = Text::Token(nullptr, ":");
        }
    }
    PopHeap();
}

// 0x0054D4A0
void __cdecl StatsMan::StatsTask(Tsk::BaseTask* task)
{
    PushTopDown();
    auto* stats = reinterpret_cast<StatsMan*>(reinterpret_cast<void**>(task)[7]);
    bool failed = false;
    switch (stats->m_state) {
    case kLoggingIn:
        failed = Tmr::GetTime() - LoginStart() > kLoginTimeout;
        break;
    case kLoggedIn:
        stats->m_state = kReady;
        stats->OnLoggedIn();
        break;
    case kLoginFailed:
        Run(0xCBA113AF);
        CloseStatsConnection();
        stats->m_task->Remove();
        stats->m_loggedIn = false;
        stats->m_state = kNone;
        break;
    case kGettingStats:
        failed = Tmr::GetTime() - LoginStart() > kLoginTimeout || PersistThink() == 0;
        break;
    case kReporting:
        if (Tmr::GetTime() - stats->m_lastReport > kReportInterval)
            stats->ReportStats(false);
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
unsigned long __stdcall StatsMan::LogInThread(void* param)
{
    PushBottomUp();
    StatsMan* stats = Stats();
    (void)param;
    // The game name: the online identity's (OnlineGameName; the original pushes "thps6pc", which online_identity
    // used to write over). The key: what online_identity leaves in the stats key's data (OnlineGameKey).
    std::snprintf(GcdGameName(), 0x100, "%s", Net::GameName());
    std::snprintf(GcdSecretKey(), 0x100, "%s", StatsKey());
    switch (InitStatsConnection(kStatsPort)) {
    case 0: // connected
        stats->m_state = kLoggedIn;
        break;
    case 1:
    case 2:
    case 3:
    case 5:
        stats->m_state = kLoginFailed;
        break;
    default: // 4 and others
        break;
    }
    PopHeap();
    return 0;
}

// 0x0054CCF0
void __cdecl StatsMan::OnPersonalStats(int, int, int, int index, int success, uint32_t, char* data, int, void*)
{
    if (success < 1)
        return;
    StatsMan* stats = Stats();
    int* values = &stats->m_personal.m_rating; // (index 0 the rating, then the high scores; combos 15 further)
    if (index == 0) {
        if (Text::Token(data, "\\")) // "THUG2Rating"
            values[0] = std::atol(Text::Token(nullptr, "\\"));
        return;
    }
    char* key = Text::Token(data, "\\");
    if (key) {
        if (Str::CompareCaseless(key, "THUG2HighScore") == 0) {
            if (char* value = Text::Token(nullptr, "\\"))
                values[index] = std::atol(value);
            key = Text::Token(nullptr, "\\");
        }
        if (Str::CompareCaseless(key, "THUG2HighCombo") == 0) { // (null key: a crash, as in the original)
            if (char* value = Text::Token(nullptr, "\\"))
                values[index + 15] = std::atol(value);
        }
    }
    if (index == 14) {
        Run(0x955801C6);
        stats->m_state = kReady;
    }
}

// 0x0054DB20
int __cdecl StatsMan::OnTopStats(void*, int result, char* buffer, int64_t, void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    vault->m_progress = 100; // (the setters folded at 0x0048DF10, 0x00428270, 0x004076E0)
    if (result != 0) {
        vault->m_result = 2;
        vault->m_error = result;
        return 1;
    }
    vault->m_result = 1;
    vault->m_error = 0;
    StatsMan* stats = Stats();
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
        stats->ParseRatings(buffer);
        break;
    default:
        break;
    }
    return 1;
}

// 0x0054D2F0
const char* __stdcall StatsMan::LevelName(uint32_t level)
{
    Script::CArray* levels = Script::GetArray(0xB8D3EEECu);
    // (the found level is read into one variable: an entry without one counts as the last one read - at first
    // the level asked for)
    uint32_t found = level;
    for (uint32_t i = 0; i < levels->m_size; ++i) {
        auto* entry = reinterpret_cast<Script::CStruct*>(levels->GetElement(i));
        entry->GetChecksum(0x651533ECu, &found, 0);
        if (found == level) {
            const char* name = nullptr;
            entry->GetString(0xC4745838u, &name, 1);
            return name;
        }
    }
    return nullptr;
}

// 0x0054CA10
void __stdcall StatsMan::GenerateAuth(const char* challenge, const char* password, char* response)
{
    GameFunction<void(__cdecl*)(const char*, const char*, char*)>(0x00600640)(challenge, password, response);
}

// --- the script commands ---

// 0x0054CC90
bool __cdecl StatsLoggedIn(Script::CStruct*, Script::CScript*) { return Stats()->UpdateLoggedIn(); }

// 0x0054CCB0
bool __cdecl StatsLogIn(Script::CStruct*, Script::CScript*)
{
    Stats()->LogIn();
    return true;
}

// 0x0054CCC0
bool __cdecl StatsLogOff(Script::CStruct*, Script::CScript*)
{
    Stats()->LogOff();
    return true;
}

// 0x0054CE00: the player's rating and the levels' high scores and combos from persistent storage (16 requests,
// indexes 0-15; state 5 until the answer to 14).
bool __cdecl RetrievePersonalStats(Script::CStruct*, Script::CScript*)
{
    Manager* manager = NetManager();
    const void* callback = reinterpret_cast<const void*>(&StatsMan::OnPersonalStats);
    int profile = static_cast<BuddyMan*>(manager->m_buddyMan)->m_profile;
    GetPersistDataValues(0, profile, 2, 0, "\\THUG2Rating", callback, nullptr);
    for (int index = 1; index <= kPersonalStatsLevels; ++index) { // (1-15: the answer to 14 ends the wait)
        profile = static_cast<BuddyMan*>(manager->m_buddyMan)->m_profile;
        GetPersistDataValues(0, profile, 2, index, "\\THUG2HighScore\\THUG2HighCombo", callback, nullptr);
    }
    static_cast<StatsMan*>(manager->m_statsMan)->m_state = StatsMan::kGettingStats;
    return true;
}

// 0x0054CE70
bool __cdecl NeedToRetrieveTopStats(Script::CStruct*, Script::CScript*) { return Stats()->m_needTopStats; }

// 0x0054D670: ReportStats [0x132AA339: the final report]
bool __cdecl ReportStats(Script::CStruct* params, Script::CScript*)
{
    bool final = params->ContainsFlag(0x132AA339u);
    Stats()->ReportStats(final);
    return true;
}

// 0x0054D6A0: the rank (0..10) into the script's parameters (0x7786171A).
bool __cdecl GetRank(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(0x7786171A, Rank(static_cast<double>(Stats()->m_personal.m_rating)));
    return true;
}

// 0x0054EA40: the top lists and the stats menus' arrays emptied (once: until the top stats are needed again).
bool __cdecl CleanUpTopStats(Script::CStruct*, Script::CScript*)
{
    StatsMan* stats = Stats();
    if (stats->m_needTopStats)
        return true;
    for (TopList& list : stats->m_topLists)
        list.Free();
    stats->m_needTopStats = true;
    Script::CleanUpArray(Script::GetArray("high_scores_array_all_time"));
    Script::CleanUpArray(Script::GetArray("best_combos_array_all_time"));
    Script::CleanUpArray(Script::GetArray("high_scores_array_monthly"));
    Script::CleanUpArray(Script::GetArray("best_combos_array_monthly"));
    Script::CleanUpArray(Script::GetArray("top_players_array"));
    Script::CleanUpArray(Script::GetArray("personal_stats_array"));
    GhttpCleanup();
    return true;
}

// 0x0054EB00: FillStatsArrays <0x5EF31148: which>: the personal stats, the ratings, or a top list (its levels' best,
// then all sorted).
bool __cdecl FillStatsArrays(Script::CStruct* params, Script::CScript*)
{
    StatsMan* stats = Stats();
    uint32_t which = 0;
    params->GetChecksum(0x5EF31148u, &which, 1);
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

} // namespace GameNet

using GameNet::StatsMan;
REPLACE(0x0054C9B0, &StatsMan::UpdateLoggedIn);
REPLACE(0x0054C9D0, &StatsMan::LogOff);
REPLACE(0x0054C9F0, &StatsMan::RemovePlayer);
REPLACE(0x0054CA10, StatsMan::GenerateAuth);
REPLACE(0x0054CA30, &StatsMan::SetPlayerAuth);
REPLACE_SOURCE_ONLY(0x0054CA80, &StatsMan::GetPersonalStats); // 4 bytes
REPLACE(0x0054CA90, &GameNet::StatsSearch::DestroySortSearch);
REPLACE(0x0054CAA0, &GameNet::StatsSearch::DestroyEntrySearch);
REPLACE(0x0054CAB0, &GameNet::StatsSearch::DestroyLevelSearch);
REPLACE(0x0054CAC0, StatsMan::LogInThread);
REPLACE(0x0054CBA0, &StatsMan::OnLoggedIn);
REPLACE(0x0054CBE0, &StatsMan::LogIn);
REPLACE(0x0054CC90, GameNet::StatsLoggedIn);
REPLACE(0x0054CCB0, GameNet::StatsLogIn);
REPLACE(0x0054CCC0, GameNet::StatsLogOff);
REPLACE(0x0054CCF0, StatsMan::OnPersonalStats);
REPLACE(0x0054CE00, GameNet::RetrievePersonalStats);
REPLACE(0x0054CE70, GameNet::NeedToRetrieveTopStats);
REPLACE(0x0054CE80, &StatsMan::StartGame);
REPLACE(0x0054D170, &StatsMan::ReportStats);
REPLACE(0x0054D2D0, &StatsMan::GetRank);
REPLACE(0x0054D2F0, StatsMan::LevelName);
REPLACE(0x0054D370, &GameNet::StatsSearch::DeleteSortSearch);
REPLACE(0x0054D3A0, &GameNet::StatsSearch::DeleteEntrySearch);
REPLACE(0x0054D3D0, &GameNet::StatsSearch::DeleteLevelSearch);
REPLACE(0x0054D400, &GameNet::StatsNode::DestroyLevelNode);
REPLACE(0x0054D420, &GameNet::StatsNode::DeleteEntryNode);
REPLACE(0x0054D460, &GameNet::StatsNode::DeleteLevelNode);
REPLACE(0x0054D4A0, StatsMan::StatsTask);
REPLACE(0x0054D5C0, &GameNet::TopList::DestroyBody);
REPLACE(0x0054D610, &StatsMan::DestroyBody);
REPLACE(0x0054D670, GameNet::ReportStats);
REPLACE(0x0054D6A0, GameNet::GetRank);
REPLACE(0x0054D6E0, &GameNet::TopEntry::Destroy);
REPLACE(0x0054D710, &GameNet::StatsNode::DeleteLevelsHead);
REPLACE(0x0054D740, &GameNet::StatsNode::DeleteSortNode);
REPLACE(0x0054D780, &GameNet::SortEntry::Destroy);
REPLACE(0x0054D7B0, &StatsMan::ParseRatings);
REPLACE(0x0054D8B0, &GameNet::TopList::Construct);
REPLACE(0x0054D8F0, &GameNet::StatsNode::DestroyEntryNode);
REPLACE(0x0054D910, &StatsMan::ParseTopList);
REPLACE(0x0054DB20, StatsMan::OnTopStats);
REPLACE(0x0054DBD0, &StatsMan::Construct);
REPLACE(0x0054DDB0, &GameNet::PersonalStats::Fill);
REPLACE(0x0054DFE0, &GameNet::LevelScores::DestroyBody);
REPLACE(0x0054E0D0, &GameNet::TopList::FillLevelScores);
REPLACE(0x0054E4A0, &GameNet::TopList::FillSortedScores);
REPLACE(0x0054E920, &GameNet::TopList::Free);
REPLACE(0x0054EA20, &GameNet::StatsNode::DestroySortNode);
REPLACE(0x0054EA40, GameNet::CleanUpTopStats);
REPLACE(0x0054EB00, GameNet::FillStatsArrays);
REPLACE(0x0054EBB0, &GameNet::LevelScores::Destroy);
