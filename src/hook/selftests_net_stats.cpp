// Self-tests for the online stats (src/game/net/stats_man.cpp, 0x0054C9B0-0x0054EBE0): the original and ours on
// identical stand-in stats managers, top lists and players in GameNetKit's arena, with GameSpy's stats / persistent
// storage SDK (its bucket operations are function pointers it keeps: stood in by thunks), the manager's calls,
// scripts, structures, arrays, the clock, tasks and threads stood in for by recording code (net4_testkit.h); the
// calls, results and arena are compared case by case. Nothing goes online: the server's answers are written here.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/crc.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/stats_man.h"
#include "game/net/vault_man.h"
#include "game/obj/object.h"
#include "hook/net4_testkit.h"

namespace {

using namespace Net4Kit;
using GameNet::LevelScores;
using GameNet::SortEntry;
using GameNet::StatsMan;
using GameNet::TopEntry;
using GameNet::TopList;

// Arena layout.
constexpr uint32_t kBuddies = 0x12000;   // the BuddyMan (its profile at +4)
constexpr uint32_t kTask = 0x12040;
constexpr uint32_t kTaskVtable = 0x12060;
constexpr uint32_t kParams = 0x12100;
constexpr uint32_t kScript = 0x12140;
constexpr uint32_t kScriptParams = 0x12180;
constexpr uint32_t kGame = 0x12200;      // the SDK's game
constexpr uint32_t kChallenge = 0x12240;
constexpr uint32_t kServer = 0x12300;    // the manager's server app
constexpr uint32_t kVault = 0x12400;     // the VaultMan's first fields (OnTopStats)
constexpr uint32_t kPrefStruct = 0x12480;
constexpr uint32_t kLevelArray = 0x12500; // the level list (CArray) and its entries
constexpr uint32_t kLevelEntries = 0x12540;
constexpr uint32_t kStats = 0x14000;     // the StatsMan (0x200)
constexpr uint32_t kLevels = 0x14400;    // LevelScores, 0x30 apart (8)
constexpr uint32_t kEntries = 0x14600;   // TopEntries, 0x40 apart (60)
constexpr uint32_t kText = 0x16000;      // a downloaded file (0x1000)

StatsMan* Stats() { return &Word<StatsMan>(kStats); }
LevelScores* LevelAt(int i) { return &Word<LevelScores>(kLevels + 0x30 * i); }
TopEntry* EntryAt(int i) { return &Word<TopEntry>(kEntries + 0x40 * i); }
char* Text() { return &Word<char>(kText); }

void Link(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
void InitHead(Mem::ListNode* head, uint32_t vtable)
{
    *head = {reinterpret_cast<void**>(vtable), reinterpret_cast<void*>(-1), 0, head, head};
}
void SetUpStats(int state = 4)
{
    StatsMan* stats = Stats();
    std::memset(stats, 0, 0x200);
    stats->m_task = &Word<Tsk::BaseTask>(kTask);
    stats->m_loggedIn = true;
    stats->m_state = state;
    stats->m_game = &Word<uint8_t>(kGame);
    stats->m_lastReport = 40000;
    stats->m_personal.m_rating = 2000;
    for (int i = 0; i < 15; ++i) {
        stats->m_personal.m_highScores[i] = 1000 * (i % 4) + i;
        stats->m_personal.m_highCombos[i] = 500 + i;
    }
    stats->m_topFile = 0;
    for (TopList& list : stats->m_topLists) {
        InitHead(&list.m_levels, 0x0064D644);
        InitHead(&list.m_entries, 0x0064D640);
        std::snprintf(list.m_title, sizeof(list.m_title), "old title");
    }
    stats->m_needTopStats = false;
    Word<uint32_t>(kTask) = At(kTaskVtable);
    Word<uint32_t>(kTask + 0x1C) = At(kStats);
    Word<uint32_t>(kManager + 0x04) = At(kServer);
    Word<uint32_t>(kManager + 0x14) = At(kVault);
    Word<uint32_t>(kManager + 0x18) = At(kBuddies);
    Word<uint32_t>(kManager + 0x1C) = At(kStats);
    Word<int>(kBuddies + 4) = 0x4455;
    Word<uint32_t>(kScript + 0x14) = At(kScriptParams);
    std::snprintf(&Word<char>(kChallenge), 32, "challenge-xyz");
    uint32_t memory = kMisc + 0x800;
    Word<uint32_t>(memory + 0x12C) = 0x0012C12C;
    Word<uint32_t>(memory + 0x13C) = 0x0013C13C;
    Word<uint32_t>(memory + 0x140) = 0x00140140;
}
TopEntry* AddEntryAt(int i, Mem::ListNode* head, const char* name, int score, int rating)
{
    TopEntry* entry = EntryAt(i);
    entry->m_node = {reinterpret_cast<void**>(0x0064D640), entry, 0, &entry->m_node, &entry->m_node};
    std::snprintf(entry->m_name, sizeof(entry->m_name), "%s", name);
    entry->m_score = score;
    entry->m_rating = rating;
    Link(head, &entry->m_node);
    return entry;
}
LevelScores* AddLevelAt(int i, TopList* list, uint32_t level)
{
    LevelScores* scores = LevelAt(i);
    scores->m_node = {reinterpret_cast<void**>(0x0064D648), scores, 0, &scores->m_node, &scores->m_node};
    scores->m_level = level;
    InitHead(&scores->m_entries, 0x0064D640);
    Link(&list->m_levels, &scores->m_node);
    return scores;
}
// The level list: `count` entries, each with "level" (0x651533EC) and a name (0xC4745838); `missing`: an entry
// without its level.
void SetUpLevelList(int count, int missing = -1)
{
    uint32_t array = At(kLevelArray);
    Word<uint32_t>(kLevelArray + 4) = static_cast<uint32_t>(count);
    static const char* names[] = {"Level Zero", "Level One", "Level Two", "Level Three", "Level Four"};
    for (int i = 0; i < count; ++i) {
        uint32_t entry = At(kLevelEntries + 0x10 * i);
        g_elements[{Rel(array), static_cast<uint32_t>(i)}] = entry;
        if (i != missing)
            g_checksums[KeyOf(entry, 0x651533ECu)] = 0x1E000000u + i;
        g_strings[KeyOf(entry, 0xC4745838u)] = names[i % 5];
    }
}

// The stand-ins' answers.
int g_connected, g_initResult, g_persistThink;
std::vector<uint32_t> g_players;
size_t g_playerIndex;
uint32_t g_taskDestroy;
uint32_t g_bucketOps[4];

void Install(StandIns& s)
{
    InstallScriptStandIns(s);
    InstallCrtStandIns(s);
    InstallSystemStandIns(s);
    StubCdecl(s, {0x00600710}, 0x0710, 0, [](Regs&) { return static_cast<uint32_t>(g_connected); });
    StubCdecl(s, {0x00601280}, 0x1280, 0);
    StubCdecl(s, {0x00602550}, 0x2550, 1, [](Regs&) { return static_cast<uint32_t>(g_initResult); });
    StubCdecl(s, {0x00602210}, 0x2210, 0, [](Regs&) { return static_cast<uint32_t>(g_persistThink); });
    StubCdecl(s, {0x00601A70}, 0x1A70, 1, [](Regs&) { return At(kGame); });
    StubCdecl(s, {0x00600720}, 0x0720, 1);
    StubCdecl(s, {0x006012E0}, 0x12E0, 1, [](Regs&) { return At(kChallenge); });
    s.At({0x00600780}, 0, [](Regs& r) {
        Rec(0x0780, {r.args[0], r.args[1], Str(r.args[2])});
        return 0u;
    });
    StubCdecl(s, {0x00600850}, 0x0850, 2);
    StubCdecl(s, {0x00601CE0}, 0x1CE0, 3);
    s.At({0x006021E0}, 0, [](Regs& r) {
        RecN(0x21E0, {r.args[0], r.args[1], r.args[2], r.args[3], Str(r.args[4]), r.args[5], r.args[6]});
        return 0u;
    });
    s.At({0x00600640}, 0, [](Regs& r) {
        Rec(0x0640, {Str(r.args[0]), Str(r.args[1]), r.args[2]});
        return 0u;
    });
    StubCdecl(s, {0x005FF860}, 0xF860, 0);
    // The SDK's bucket operations (its function pointers): ints and strings, game and player keys.
    for (int i = 0; i < 4; ++i) {
        bool string = i == 1 || i == 3;
        g_bucketOps[i] = reinterpret_cast<uint32_t>(s.Thunk(0, [i, string](Regs& r) {
            Rec(0xB0C0 + static_cast<uint32_t>(i),
                {r.args[0], Str(r.args[1]), r.args[2], string ? Str(r.args[3]) : r.args[3], r.args[4]});
            return 0u;
        }));
    }
    // The manager.
    Stub(s, {0x00536A40}, 0, 0x6A40, [](Regs&) { return reinterpret_cast<uint32_t>("Trick Attack"); });
    Stub(s, {0x00534F40}, 0, 0x4F40, [](Regs&) { return 0xABCDu; });
    Stub(s, {0x00535B70}, 0, 0x5B70, [](Regs&) { return 0xDCBAu; });
    Stub(s, {0x005436E0}, 4, 0x36E0, [](Regs& r) { return 10000u + r.args[0]; });
    Stub(s, {0x005369A0, AddressOf(&GameNet::Manager::GetLevelName)}, 4, 0x69A0,
         [](Regs&) { return reinterpret_cast<uint32_t>("The Level"); });
    Stub(s, {0x00532C50, AddressOf(&GameNet::Manager::GetMaxPlayers)}, 0, 0x2C50, [](Regs&) { return 6u; });
    Stub(s, {0x00533240, AddressOf(&GameNet::Manager::SetLobbyId)}, 4, 0x3240);
    Stub(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, 0xDB90);
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        Rec(0x7EB0, {r.ecx, r.args[0]});
        RecordDesc(0x7EB1, r.ecx, reinterpret_cast<Net::MsgDesc*>(r.args[1]));
        return 0u;
    });
    Stub(s, {0x005434A0}, 0, 0x34A0, [](Regs& r) { return 0x700u + (Rel(r.ecx) & 0xFFF); });
    // The players.
    auto next = [](Regs& r) {
        Rec(0x72C0, {r.ecx, r.args[1]});
        return g_playerIndex < g_players.size() ? At(g_players[g_playerIndex++]) : 0u;
    };
    s.At({0x005372C0}, 8, [next](Regs& r) {
        g_playerIndex = 0;
        return next(r);
    });
    s.At({0x00537010}, 8, next);
    // The script arrays.
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return At(kLevelArray);
    });
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9090, {Str(r.args[0])});
        return At(kLevelArray + 0x20);
    });
    s.At({0x00477140, AddressOf(&Script::CleanUpArray)}, 0, [](Regs& r) {
        Rec(0x7140, {r.args[0]});
        return 0u;
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        return reinterpret_cast<uint32_t>("A Title");
    });
    void* destroy = s.Thunk(4, [](Regs& r) {
        Rec(0x7A5D, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_taskDestroy = reinterpret_cast<uint32_t>(destroy);
}

void MapOurCode()
{
    g_codeMap.clear();
    MapCode(&StatsMan::StatsTask, 0x0054D4A0);
    MapCode(&StatsMan::LogInThread, 0x0054CAC0);
    MapCode(&StatsMan::OnPersonalStats, 0x0054CCF0);
    MapCode(&StatsMan::OnTopStats, 0x0054DB20);
}

void Snapshot()
{
    for (uint32_t i = 0x12000; i < g_heapNext + 0x40 && i < kArenaSize; i += 4)
        g_trace.push_back(Norm(Word<uint32_t>(i)));
    for (uint32_t i = 0; i < 0x20; i += 4)
        g_trace.push_back(Norm(Word<uint32_t>(kManager + i)));
    g_trace.push_back(Norm(Word<uint32_t>(kManager + 0x9950)));
    g_trace.push_back(GameVariable<uint32_t>(0x007CE460));
    g_trace.push_back(Hash(&GameVariable<char>(0x007D7A78)));
    g_trace.push_back(Hash(&GameVariable<char>(0x007D7B78)));
}
using Case = std::function<void(bool original)>;
void RunCases(int run, const std::vector<Case>& cases)
{
    for (size_t i = 0; i < cases.size(); ++i) {
        std::vector<uint32_t> kept = g_trace;
        SetUpArena(run);
        g_trace = kept;
        ResetAnswers();
        GameVariable<uint32_t>(0x0068E818) = 0xB0B0;
        GameVariable<uint32_t>(0x0068E834) = g_bucketOps[0];
        GameVariable<uint32_t>(0x0068E83C) = g_bucketOps[1];
        GameVariable<uint32_t>(0x0068E84C) = g_bucketOps[2];
        GameVariable<uint32_t>(0x0068E854) = g_bucketOps[3];
        GameVariable<uint32_t>(0x007CE460) = 10000;
        GameVariable<bool>(0x007CE4FD) = false;
        std::memset(&GameVariable<char>(0x007D7A78), 0, 0x200);
        SetUpStats();
        Word<uint32_t>(kTaskVtable + 8) = g_taskDestroy;
        g_connected = 1;
        g_initResult = 0;
        g_persistThink = 1;
        g_players.clear();
        g_now = 60000;
        g_trace.push_back(0xCA5E0000u + static_cast<uint32_t>(i));
        cases[i](run == 0);
        Snapshot();
    }
}
void Compare2(const char* what, const std::vector<Case>& cases)
{
    MapOurCode();
    RunBoth(what, Install, [&](bool original) { RunCases(original ? 0 : 1, cases); },
            {{0x0068E818, 4, {}}, {0x0068E834, 4, {}}, {0x0068E83C, 4, {}}, {0x0068E84C, 4, {}}, {0x0068E854, 4, {}},
             {0x007CE460, 4, {}}, {0x007CE4FD, 1, {}}, {0x007D7A78, 0x200, {}}});
}

using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void CallCFunc(bool original, uint32_t address, CFunc ours)
{
    auto* params = &Word<Script::CStruct>(kParams);
    auto* script = &Word<Script::CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    g_trace.insert(g_trace.end(), {0xC0DE, static_cast<uint32_t>(result)});
}
template <typename P>
void Result(P value)
{
    g_trace.push_back(Rel(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value))));
}

// A top list with levels and entries (or the ratings list's entries).
void FillTopList(TopList* list, int levels, int perLevel, bool negative = false)
{
    int entry = 0;
    for (int l = 0; l < levels; ++l) {
        LevelScores* scores = AddLevelAt(l, list, 0x1E000000u + static_cast<uint32_t>(l));
        for (int e = 0; e < perLevel && entry < 60; ++e, ++entry) {
            char name[16];
            std::snprintf(name, sizeof(name), "P%d_%d", l, e);
            int score = (e * 37 + l * 11) % 23 * 100;
            int rating = negative && e == 1 ? -5 - e : 900 * e + 35 * l;
            AddEntryAt(entry, &scores->m_entries, name, score, rating);
        }
    }
}

// --- the lists, parsing, ranks ---

void TestNetStatsLists()
{
    std::vector<Case> cases;
    for (unsigned int flags : {0u, 1u}) {
        // The node / search destructors.
        for (uint32_t address : {0x0054D420u, 0x0054D460u, 0x0054D740u, 0x0054D710u, 0x0054D6E0u, 0x0054D780u}) {
            cases.push_back([flags, address](bool original) {
                TopList* list = &Stats()->m_topLists[0];
                AddEntryAt(0, &list->m_entries, "A", 1, 2);
                TopEntry* entry = AddEntryAt(1, &list->m_entries, "B", 3, 4);
                AddEntryAt(2, &list->m_entries, "C", 5, 6);
                void* node = entry;
                if (original) {
                    Result(Original<void*(__thiscall*)(void*, unsigned int)>(address)(node, flags));
                    return;
                }
                auto* ours = static_cast<GameNet::StatsNode*>(node);
                switch (address) {
                case 0x0054D420: Result(ours->DeleteEntryNode(flags)); break;
                case 0x0054D460: Result(ours->DeleteLevelNode(flags)); break;
                case 0x0054D740: Result(ours->DeleteSortNode(flags)); break;
                case 0x0054D710: Result(ours->DeleteLevelsHead(flags)); break;
                case 0x0054D6E0: Result(static_cast<TopEntry*>(node)->Destroy(flags)); break;
                default: Result(reinterpret_cast<SortEntry*>(node)->Destroy(flags)); break;
                }
            });
        }
        for (uint32_t address : {0x0054D370u, 0x0054D3A0u, 0x0054D3D0u}) {
            cases.push_back([flags, address](bool original) {
                auto* search = reinterpret_cast<GameNet::StatsSearch*>(EntryAt(5));
                search->m_vtable = reinterpret_cast<void**>(0x1111);
                search->m_node = reinterpret_cast<Mem::ListNode*>(0x2222);
                if (original)
                    Result(Original<void*(__thiscall*)(void*, unsigned int)>(address)(search, flags));
                else if (address == 0x0054D370)
                    Result(search->DeleteSortSearch(flags));
                else if (address == 0x0054D3A0)
                    Result(search->DeleteEntrySearch(flags));
                else
                    Result(search->DeleteLevelSearch(flags));
            });
        }
        for (int entries = 0; entries <= 2; ++entries) {
            cases.push_back([flags, entries](bool original) {
                TopList* list = &Stats()->m_topLists[1];
                AddLevelAt(0, list, 1);
                LevelScores* scores = AddLevelAt(1, list, 2);
                for (int i = 0; i < entries; ++i)
                    AddEntryAt(i, &scores->m_entries, "E", i, i);
                Result(original ? Original<LevelScores*(__thiscall*)(LevelScores*, unsigned int)>(0x0054EBB0)(scores,
                                                                                                          flags)
                                : scores->Destroy(flags));
            });
        }
    }
    for (uint32_t address : {0x0054D8F0u, 0x0054D400u, 0x0054EA20u}) {
        cases.push_back([address](bool original) {
            TopList* list = &Stats()->m_topLists[0];
            AddEntryAt(0, &list->m_entries, "A", 1, 2);
            TopEntry* entry = AddEntryAt(1, &list->m_entries, "B", 3, 4);
            auto* ours = reinterpret_cast<GameNet::StatsNode*>(entry);
            if (original)
                Original<void(__thiscall*)(void*)>(address)(entry);
            else if (address == 0x0054D8F0)
                ours->DestroyEntryNode();
            else if (address == 0x0054D400)
                ours->DestroyLevelNode();
            else
                ours->DestroySortNode();
        });
    }
    for (uint32_t address : {0x0054CA90u, 0x0054CAA0u, 0x0054CAB0u}) {
        cases.push_back([address](bool original) {
            auto* search = reinterpret_cast<GameNet::StatsSearch*>(EntryAt(5));
            search->m_vtable = reinterpret_cast<void**>(0x1111);
            if (original)
                Original<void(__thiscall*)(void*)>(address)(search);
            else if (address == 0x0054CA90)
                search->DestroySortSearch();
            else if (address == 0x0054CAA0)
                search->DestroyEntrySearch();
            else
                search->DestroyLevelSearch();
        });
    }
    cases.push_back([](bool original) {
        TopList* list = &Stats()->m_topLists[2];
        std::memset(list, 0xAB, sizeof(TopList));
        Result(original ? Original<TopList*(__thiscall*)(TopList*)>(0x0054D8B0)(list) : list->Construct());
    });
    cases.push_back([](bool original) {
        TopList* list = &Stats()->m_topLists[2];
        FillTopList(list, 2, 2);
        if (original)
            Original<void(__thiscall*)(TopList*)>(0x0054D5C0)(list);
        else
            list->DestroyBody();
    });
    for (int levels = 0; levels <= 2; ++levels) {
        cases.push_back([levels](bool original) {
            TopList* list = &Stats()->m_topLists[3];
            FillTopList(list, levels, 2);
            AddEntryAt(20, &list->m_entries, "R1", 7, 8);
            AddEntryAt(21, &list->m_entries, "R2", 9, 10);
            if (original)
                Original<void(__thiscall*)(TopList*)>(0x0054E920)(list);
            else
                list->Free();
        });
    }
    cases.push_back([](bool original) {
        std::memset(Stats(), 0xEE, 0x200);
        Result(original ? Original<StatsMan*(__thiscall*)(StatsMan*)>(0x0054DBD0)(Stats()) : Stats()->Construct());
    });
    for (bool task : {false, true}) {
        cases.push_back([task](bool original) {
            FillTopList(&Stats()->m_topLists[1], 1, 1);
            if (!task)
                Stats()->m_task = nullptr;
            if (original)
                Original<void(__thiscall*)(StatsMan*)>(0x0054D610)(Stats());
            else
                Stats()->DestroyBody();
        });
    }
    // Parsing.
    const char* ratings[] = {"100:5:Alice\n200:7:Bob\n", "100:5", "100", "", "1:2:A\n3:4"};
    for (const char* text : ratings) {
        cases.push_back([text](bool original) {
            AddEntryAt(0, &Stats()->m_topLists[4].m_entries, "Existing", 1, 1);
            std::snprintf(Text(), 0x1000, "%s", text);
            if (original)
                Original<void(__thiscall*)(StatsMan*, char*)>(0x0054D7B0)(Stats(), Text());
            else
                Stats()->ParseRatings(Text());
        });
    }
    struct Top {
        const char* text;
        bool titled;
    };
    const Top tops[] = {
        {"Level:1234\n50:3:Ann\n40:2:Ben\nLevel:5678\n30:1:Cat\n", false},
        {"level:1\n1:2:X\nLEVEL:2\n", false},
        {"Nope:1\n2:3:Y\n", false},
        {"Level:7\n", false},
        {"Level:7\n9", false},
        {"Level:7\n9:8", false},
        {"Level:7\n9:8:Name", false},
        {"Combos all time\nLevel:3\n1:1:A\n2:2:B\n", true},
        {"Title only", true},
        {"", false},
    };
    for (const Top& top : tops) {
        for (int list : {0, 3}) {
            cases.push_back([top, list](bool original) {
                AddLevelAt(0, &Stats()->m_topLists[list], 99);
                std::snprintf(Text(), 0x1000, "%s", top.text);
                if (original)
                    Original<void(__thiscall*)(StatsMan*, int, char*, bool)>(0x0054D910)(Stats(), list, Text(),
                                                                                       top.titled);
                else
                    Stats()->ParseTopList(list, Text(), top.titled);
            });
        }
    }
    // The download's callback for each file.
    for (int result : {0, 3}) {
        for (int file = 0; file <= 5; ++file) {
            cases.push_back([result, file](bool original) {
                Stats()->m_topFile = file;
                std::snprintf(Text(), 0x1000, "%s",
                              file == 4 ? "5:6:Rated\n" : file >= 2 ? "Title\nLevel:4\n1:2:C\n" : "Level:4\n1:2:C\n");
                using Fn = int(__cdecl*)(void*, int, char*, int64_t, void*);
                void* vault = &Word<uint8_t>(kVault);
                int value = original ? Original<Fn>(0x0054DB20)(nullptr, result, Text(), 0x30, vault)
                                     : StatsMan::OnTopStats(nullptr, result, Text(), 0x30, vault);
                g_trace.push_back(static_cast<uint32_t>(value));
            });
        }
    }
    // Level names: found, not found, an entry without a level (it matches), no levels.
    struct Name {
        int count, missing;
        uint32_t level;
    };
    for (Name n : {Name{3, -1, 0x1E000001}, Name{3, -1, 0x1E000009}, Name{3, 0, 0x1E000002}, Name{3, 1, 0x1E000001},
                   Name{3, 1, 0x1E000002}, Name{0, -1, 0x1E000000}}) {
        cases.push_back([n](bool original) {
            SetUpLevelList(n.count, n.missing);
            const char* name = original ? Original<const char*(__stdcall*)(uint32_t)>(0x0054D2F0)(n.level)
                                        : StatsMan::LevelName(n.level);
            g_trace.push_back(Hash(name));
        });
    }
    // Ranks.
    for (int rating : {0, 1000, 3496, 3497, 3500, 34965, 34966, 35000, 99999, -5000}) {
        cases.push_back([rating](bool original) {
            Stats()->m_personal.m_rating = rating;
            int rank = original ? Original<int(__thiscall*)(StatsMan*)>(0x0054D2D0)(Stats()) : Stats()->GetRank();
            g_trace.push_back(static_cast<uint32_t>(rank));
            CallCFunc(original, 0x0054D6A0, GameNet::GetRank);
        });
    }
    Compare2("stats lists: nodes, searches, top lists, constructor / destructor, parsing, the download, level names, "
             "ranks",
             cases);
}

// --- the menus ---

void TestNetStatsMenus()
{
    std::vector<Case> cases;
    for (int variant = 0; variant < 3; ++variant) {
        cases.push_back([variant](bool original) {
            SetUpLevelList(4);
            int* scores = Stats()->m_personal.m_highScores;
            for (int i = 0; i < 15; ++i)
                scores[i] = variant == 0 ? 0 : variant == 1 ? (i * 7919) % 5000 - (i == 3 ? 6000 : 0) : 100 * (i % 3);
            auto* personal = &Stats()->m_personal;
            if (original)
                Original<void(__thiscall*)(GameNet::PersonalStats*)>(0x0054DDB0)(personal);
            else
                personal->Fill();
        });
    }
    struct Fill {
        int levels, perLevel;
        bool negative;
    };
    for (Fill f : {Fill{0, 0, false}, Fill{1, 0, false}, Fill{1, 2, false}, Fill{3, 4, true}, Fill{2, 5, false}}) {
        cases.push_back([f](bool original) {
            SetUpLevelList(4);
            TopList* list = &Stats()->m_topLists[1];
            FillTopList(list, f.levels, f.perLevel, f.negative);
            if (original)
                Original<void(__thiscall*)(TopList*)>(0x0054E0D0)(list);
            else
                list->FillLevelScores();
        });
        for (bool ratings : {false, true}) {
            cases.push_back([f, ratings](bool original) {
                SetUpLevelList(4);
                TopList* list = &Stats()->m_topLists[1];
                FillTopList(list, f.levels, f.perLevel, f.negative);
                for (int i = 0; i < f.perLevel; ++i)
                    AddEntryAt(40 + i, &list->m_entries, "Rated", i == 2 ? 0 : 300 * i + 7, 1000 * i);
                if (original)
                    Original<void(__thiscall*)(TopList*, bool)>(0x0054E4A0)(list, ratings);
                else
                    list->FillSortedScores(ratings);
            });
        }
    }
    // More than 50 lines with a score.
    for (bool ratings : {false, true}) {
        cases.push_back([ratings](bool original) {
            SetUpLevelList(4);
            TopList* list = &Stats()->m_topLists[0];
            FillTopList(list, 2, 27); // (one in 23 has score 0)
            for (int i = 0; i < 6; ++i)
                AddEntryAt(54 + i, &list->m_entries, "R", 10 + i, i);
            if (original)
                Original<void(__thiscall*)(TopList*, bool)>(0x0054E4A0)(list, ratings);
            else
                list->FillSortedScores(ratings);
        });
    }
    for (uint32_t which : {0xEF20D3A1u, 0x9FE64312u, 0xCB95928Fu, 0x67F1AE8Au, 0x5299291Bu, 0xFEF84FFFu, 0x1234u}) {
        cases.push_back([which](bool original) {
            SetUpLevelList(4);
            g_checksums[KeyOf(At(kParams), 0x5EF31148u)] = which;
            for (int i = 0; i < 4; ++i)
                AddLevelAt(i, &Stats()->m_topLists[i], 0x1E000000u + i);
            AddEntryAt(0, &LevelAt(0)->m_entries, "L0", 50, 5);
            AddEntryAt(1, &LevelAt(1)->m_entries, "L1", 60, 6);
            AddEntryAt(2, &LevelAt(2)->m_entries, "L2", 70, 7);
            AddEntryAt(3, &LevelAt(3)->m_entries, "L3", 80, 8);
            AddEntryAt(4, &Stats()->m_topLists[4].m_entries, "R", 90, 9);
            CallCFunc(original, 0x0054EB00, GameNet::FillStatsArrays);
        });
    }
    for (bool need : {false, true}) {
        cases.push_back([need](bool original) {
            Stats()->m_needTopStats = need;
            FillTopList(&Stats()->m_topLists[1], 1, 2);
            AddEntryAt(30, &Stats()->m_topLists[4].m_entries, "R", 1, 1);
            CallCFunc(original, 0x0054EA40, GameNet::CleanUpTopStats);
            CallCFunc(original, 0x0054CE70, GameNet::NeedToRetrieveTopStats);
        });
    }
    Compare2("stats menus: personal stats, levels' best, sorted lists (50 at most), FillStatsArrays, clean-up", cases);
}

// --- logging in, the task, persistent storage, reporting ---

void RunTask(bool original)
{
    auto* task = &Word<Tsk::BaseTask>(kTask);
    if (original)
        Original<void(__cdecl*)(Tsk::BaseTask*)>(0x0054D4A0)(task);
    else
        StatsMan::StatsTask(task);
}

void TestNetStatsSession()
{
    std::vector<Case> cases;
    for (bool inList : {false, true}) {
        cases.push_back([inList](bool original) {
            g_taskInList = inList;
            g_now = 1234;
            if (original)
                Original<void(__thiscall*)(StatsMan*)>(0x0054CBE0)(Stats());
            else
                Stats()->LogIn();
        });
    }
    cases.push_back([](bool original) { CallCFunc(original, 0x0054CCB0, GameNet::StatsLogIn); });
    cases.push_back([](bool original) {
        if (original)
            Original<void(__thiscall*)(StatsMan*)>(0x0054C9D0)(Stats());
        else
            Stats()->LogOff();
    });
    cases.push_back([](bool original) { CallCFunc(original, 0x0054CCC0, GameNet::StatsLogOff); });
    for (int connected : {0, 1, 2}) {
        cases.push_back([connected](bool original) {
            g_connected = connected;
            Stats()->m_loggedIn = !connected;
            bool value = original ? Original<bool(__thiscall*)(StatsMan*)>(0x0054C9B0)(Stats()) : Stats()->UpdateLoggedIn();
            g_trace.push_back(value);
            CallCFunc(original, 0x0054CC90, GameNet::StatsLoggedIn);
        });
    }
    cases.push_back([](bool original) {
        Stats()->m_loggedIn = false;
        if (original)
            Original<void(__thiscall*)(StatsMan*)>(0x0054CBA0)(Stats());
        else
            Stats()->OnLoggedIn();
    });
    for (int result = 0; result <= 6; ++result) {
        cases.push_back([result](bool original) {
            g_initResult = result;
            Stats()->m_state = 1;
            unsigned long value = original ? Original<unsigned long(__stdcall*)(void*)>(0x0054CAC0)(Stats())
                                           : StatsMan::LogInThread(Stats());
            g_trace.push_back(static_cast<uint32_t>(value));
        });
    }
    struct TaskCase {
        int state;
        uint32_t now;
        int think;
    };
    const TaskCase taskCases[] = {{0, 99999, 1}, {1, 20000, 1}, {1, 20001, 1}, {2, 0, 1},     {3, 0, 1},
                                  {4, 99999, 1}, {5, 20000, 1}, {5, 20000, 0}, {5, 20001, 1}, {6, 100000, 1},
                                  {6, 100001, 1}, {7, 99999, 1}, {-1, 1, 1}};
    for (const TaskCase& c : taskCases) {
        cases.push_back([c](bool original) {
            SetUpStats(c.state);
            g_now = c.now;
            g_persistThink = c.think;
            Word<int>(kPlayers + 0x24) = 3;
            g_players = {kPlayers};
            RunTask(original);
        });
    }
    // Persistent storage's answers.
    struct Answer {
        int index, success;
        const char* data;
    };
    const Answer answers[] = {{0, 1, "\\THUG2Rating\\1234"}, {0, 0, "\\THUG2Rating\\1234"}, {0, 1, ""},
                              {3, 1, "\\THUG2HighScore\\555\\THUG2HighCombo\\66"},
                              {4, 1, "\\thug2highcombo\\77"}, {5, 1, "\\Other\\1\\THUG2HighCombo\\8"},
                              {6, 1, "\\THUG2HighScore\\9\\Else\\1"}, {14, 1, "\\THUG2HighScore\\10\\THUG2HighCombo\\11"},
                              {14, 1, ""}, {14, 2, "\\THUG2HighScore\\12\\THUG2HighCombo\\13"}};
    for (const Answer& a : answers) {
        cases.push_back([a](bool original) {
            Stats()->m_state = 5;
            std::snprintf(Text(), 0x1000, "%s", a.data);
            using Fn = void(__cdecl*)(int, int, int, int, int, uint32_t, char*, int, void*);
            if (original)
                Original<Fn>(0x0054CCF0)(0, 0x4455, 2, a.index, a.success, 0x777, Text(), 20, nullptr);
            else
                StatsMan::OnPersonalStats(0, 0x4455, 2, a.index, a.success, 0x777, Text(), 20, nullptr);
        });
    }
    cases.push_back([](bool original) { CallCFunc(original, 0x0054CE00, GameNet::RetrievePersonalStats); });
    // A game reported: its players (with a profile, without, negative).
    cases.push_back([](bool original) {
        uint32_t prefs = At(kPrefStruct);
        g_prefStructs[0x2A5CF019] = prefs;
        g_strings[KeyOf(prefs, 0x96875C0Fu)] = "Host's game";
        g_prefChecksums[{0xC51BF6E3, 0x906B67BA}] = 300;
        g_prefChecksums[{0x71CD53A2, 0xCD66C8AE}] = 20;
        int profiles[3] = {1001, 0, -4};
        for (int i = 0; i < 3; ++i) {
            uint32_t player = kPlayers + kPlayerSize * i;
            Word<int>(player + 0x28) = profiles[i];
            g_players.push_back(player);
        }
        if (original)
            Original<void(__thiscall*)(StatsMan*)>(0x0054CE80)(Stats());
        else
            Stats()->StartGame();
    });
    for (bool game : {false, true}) {
        for (bool final : {false, true}) {
            for (bool cheat : {false, true}) {
                cases.push_back([game, final, cheat](bool original) {
                    if (!game)
                        Stats()->m_game = nullptr;
                    GameVariable<bool>(0x007CE4FD) = cheat;
                    Stats()->m_state = 6;
                    for (int i = 0; i < 2; ++i) {
                        uint32_t player = kPlayers + kPlayerSize * i;
                        Word<int>(player + 0x24) = 70 + i;
                        g_players.push_back(player);
                    }
                    if (original)
                        Original<void(__thiscall*)(StatsMan*, bool)>(0x0054D170)(Stats(), final);
                    else
                        Stats()->ReportStats(final);
                });
            }
        }
    }
    for (bool final : {false, true}) {
        cases.push_back([final](bool original) {
            if (final)
                g_flags.insert(KeyOf(At(kParams), 0x132AA339u));
            g_players = {kPlayers};
            CallCFunc(original, 0x0054D670, GameNet::ReportStats);
        });
    }
    for (int state : {4, 6}) {
        cases.push_back([state](bool original) {
            Stats()->m_state = state;
            if (original)
                Original<void(__thiscall*)(StatsMan*, int)>(0x0054C9F0)(Stats(), 0x15);
            else
                Stats()->RemovePlayer(0x15);
        });
    }
    cases.push_back([](bool original) {
        if (original)
            Original<void(__thiscall*)(StatsMan*, int, const char*)>(0x0054CA30)(Stats(), 3, "auth-value");
        else
            Stats()->SetPlayerAuth(3, "auth-value");
    });
    cases.push_back([](bool original) {
        char* response = &Word<char>(kText);
        if (original)
            Original<void(__stdcall*)(const char*, const char*, char*)>(0x0054CA10)("chal", "pass", response);
        else
            StatsMan::GenerateAuth("chal", "pass", response);
    });
    cases.push_back([](bool) { Result(Stats()->GetPersonalStats()); }); // (source only: ours on both runs)
    Compare2("stats session: log in / off, the login thread, the task, persistent storage, reporting a game", cases);
}

} // namespace

SELFTEST(Early, TestNetStatsLists, GameNetKit::Cleanup);
SELFTEST(Early, TestNetStatsMenus, GameNetKit::Cleanup);
SELFTEST(Early, TestNetStatsSession, GameNetKit::Cleanup);
