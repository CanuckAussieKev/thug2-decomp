// Self-tests for the gap checklist (src/game/skate/gap_checklist.cpp): the original and ours build the same
// checklists in an arena (gaps added in every order case, found again, built from stand-in node scripts) and run every
// query, update and script command on them; what they call outside (structures, arrays, scripts, the goal manager,
// the skate module and career, memory, debug output) is stood in for by recording code (viewer_testkit.h), and the
// calls and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/string.h"
#include "game/mth/vector.h"
#include "game/script/nodes.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/goals/goal_manager.h"
#include "game/skate/skate_module.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Game::CGapChecklist;

// Arena layout.
constexpr uint32_t kChecklist = 0x0100; // 0x18
constexpr uint32_t kOther = 0x0140;     // a second checklist (the career's)
constexpr uint32_t kSkate = 0x0200;     // the skate module (+0x20 the career)
constexpr uint32_t kCareer = 0x0280;
constexpr uint32_t kScript = 0x0300;    // a CScript (+0x14 its parameters)
constexpr uint32_t kResults = 0x0400;   // its parameters
constexpr uint32_t kParams = 0x0500;    // parameter structures, 0x10 apart
constexpr uint32_t kNodes = 0x0600;     // node parameters for Build, 0x10 apart
constexpr uint32_t kGoals = 0x0700;     // the goal manager
constexpr uint32_t kNodeObjects = 0x0800; // CGapCheck / node objects tested alone, 0x80 apart

bool g_goalsListening;
int g_currentLevel;
uint32_t g_levelChecklist;
int g_nodeCalls;

void Install(StandIns& s)
{
    InstallCommon(s, false);
    auto getter = [](Regs& r, uint32_t name, uint32_t tag) {
        Rec(tag, {r.ecx, name, r.args[2] & 0xFF});
        auto it = g_params.find({r.ecx, name});
        if (it == g_params.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [getter](Regs& r) { return getter(r, r.args[0], 0x6630); });
    s.At({0x00476C60, 0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [getter](Regs& r) { return getter(r, r.args[0], 0x6C60); });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [getter](Regs& r) { return getter(r, r.args[0], 0x6950); });
    s.At({0x004790C0, AddressOf(static_cast<int (*)(const char*)>(&Script::GetInteger))}, 0, [](Regs& r) {
        uint32_t name = Text(r.args[0]);
        Rec(0x90C0, {name});
        auto it = g_integers.find(name);
        return it == g_integers.end() ? 0u : static_cast<uint32_t>(it->second);
    });
    s.At({0x00477C30, AddressOf(static_cast<void (CStruct::*)(const char*, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Rec(0x7C30, {r.ecx, Text(r.args[0]), r.args[1]});
             return 0u;
         });
    s.At({0x00477A90, AddressOf(static_cast<void (CStruct::*)(const char*, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Rec(0x7A90, {r.ecx, Text(r.args[0]), Text(r.args[1])});
             return 0u;
         });
    s.At({0x00477E20, AddressOf(static_cast<void (CStruct::*)(uint32_t, float, float, float)>(&CStruct::AddVector))},
         16, [](Regs& r) {
             Rec(0x7E20, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
             return 0u;
         });
    s.At({0x004780D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArrayPointer))},
         8, [](Regs& r) {
             Rec(0x80D0, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x0046CAA0, AddressOf(&Script::CArray::Init)}, 0, [](Regs& r) {
        Rec(0xCAA0, {r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 12);
        return r.ecx;
    });
    s.At({0x0046CC50, AddressOf(&Script::CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Rec(0xCC50, {r.ecx, r.args[0], r.args[1] & 0xFF});
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&Script::CArray::SetElement)}, 8, [](Regs& r) {
        Rec(0xCB10, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0045CCA0, AddressOf(&SkateScript::ForEachNodeScriptCall)}, 0, [](Regs& r) {
        Rec(0xCCA0, {r.args[0], r.args[1], r.args[2]});
        auto callback = reinterpret_cast<SkateScript::ScriptCallCallback>(r.args[2]);
        for (int i = 0; i < g_nodeCalls; ++i)
            callback(Ptr<CStruct>(kNodes + 0x10 * i), Ptr<uint8_t>(0x0F00 + i));
        return 0u;
    });
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) {
        Rec(0x91B0, {});
        return At(kGoals);
    });
    s.At({0x0055DEA0, AddressOf(&Game::CGoalManager::HasActiveGapGoal)}, 0, [](Regs& r) {
        Rec(0xDEA0, {r.ecx});
        return g_goalsListening ? 1u : 0u;
    });
    s.At({0x005586E0, AddressOf(&Game::CGoalManager::GotGap)}, 4, [](Regs& r) {
        Rec(0x86E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005656C0, AddressOf(&Mdl::Skate::GetGapChecklist)}, 0, [](Regs& r) {
        Rec(0x56C0, {r.ecx});
        return At(kChecklist);
    });
    s.At({0x00584110, AddressOf(&Obj::CSkaterCareer::GetGapChecklist)}, 4, [](Regs& r) {
        Rec(0x4110, {r.ecx, r.args[0]});
        return g_levelChecklist;
    });
    s.At({0x00584130, AddressOf(&Obj::CSkaterCareer::GetLevel)}, 0, [](Regs& r) {
        Rec(0x4130, {r.ecx});
        return static_cast<uint32_t>(g_currentLevel);
    });
}

void SaveGapGlobals() { SaveGlobals({{0x007CE4B0, 4, {}}, {0x007CE478, 4, {}}, {0x00690FA0, 4, {}}}); }

// Gaps for a node / a parameter structure.
void SetNode(uint32_t offset, int score, const char* text)
{
    if (score >= 0)
        g_params[{At(offset), 0xCD66C8AE}] = static_cast<uint32_t>(score);
    if (text)
        g_params[{At(offset), 0xC4745838}] = reinterpret_cast<uint32_t>(text);
}

void PushChecklist(std::vector<uint32_t>& t, uint32_t offset)
{
    auto* list = Ptr<CGapChecklist>(offset);
    int guard = 0;
    for (Mem::ListNode* node = list->m_gaps.next; node != &list->m_gaps && guard < 64; node = node->next, ++guard) {
        auto* gap = reinterpret_cast<Game::CGapCheck*>(node);
        t.insert(t.end(), {Rel(reinterpret_cast<uint32_t>(node)), Hash(gap->m_name.c_str()),
                           static_cast<uint32_t>(gap->m_score), static_cast<uint32_t>(gap->m_count),
                           static_cast<uint32_t>(gap->m_pending), gap->m_inLevel});
    }
}

// Adding gaps (new ones in every position, the same name and score again, the same name with another score, names
// differing only in case), building from node scripts (gaps, a zero score, an empty text, with and without
// PrintGaps), every query, SetGapPositions, GotGap, ClearPending, AwardPendingGaps (first-time gaps one and several,
// the goal manager listening or not), Reset, AllGot, the totals, the menu and list commands (no gaps, one, many;
// choose_script or not), the four script commands, Clear, and the gap's and its node's constructors and destructors.
void TestGapChecklist()
{
    SaveGapGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    Alias(&Game::AddGapFromNodeCallback, 0x00578AA0);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_integers.clear();
        GameVariable<int>(0x00690FA0) = 3;
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CE478) = At(kSkate);
        *Ptr<uint32_t>(kSkate + 0x20) = At(kCareer);
        *Ptr<uint32_t>(0x7000 + 0x124) = 0x5C819700; // the script heap
        auto* script = Ptr<Script::CScript>(kScript);
        *Ptr<uint32_t>(kScript + 0x14) = At(kResults);
        auto* list = Ptr<CGapChecklist>(kChecklist);
        auto* other = Ptr<CGapChecklist>(kOther);
        for (CGapChecklist* l : {list, other})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00579690)(l)
                                                               : l->Construct())));
        auto call0 = [&](uint32_t address, void (CGapChecklist::*ours)(), CGapChecklist* l) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(l);
            else
                (l->*ours)();
        };
        auto add = [&](const char* name, int count, int score) {
            if (original)
                Original<void(__thiscall*)(void*, const char*, int, int)>(0x00578880)(list, name, count, score);
            else
                list->AddGap(name, count, score);
        };
        // The commands with no gaps.
        auto commands = [&]() {
            for (uint32_t p : {kParams, kParams + 0x10}) {
                CStruct* params = Ptr<CStruct>(p);
                t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*, Script::CScript*)>(0x00578DD0)(list, params, script)
                                     : list->GetGapTotals(params, script));
                t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*, Script::CScript*)>(0x00578E40)(list, params, script)
                                     : list->AddGapsToMenu(params, script));
                t.push_back(original ? Original<bool(__thiscall*)(void*, CStruct*, Script::CScript*)>(0x00579080)(list, params, script)
                                     : list->CreateGapList(params, script));
                using Command = bool(__cdecl*)(CStruct*, Script::CScript*);
                const struct {
                    uint32_t address;
                    Command ours;
                } cfuncs[] = {{0x005792E0, &Game::CFunc_AddGapsToMenu},
                              {0x00579300, &Game::CFunc_CreateGapList},
                              {0x00579320, &Game::CFunc_GetLevelGapTotals}};
                for (const auto& c : cfuncs)
                    t.push_back((original ? Original<Command>(c.address)(params, script) : c.ours(params, script)));
            }
        };
        g_params[{At(kParams + 0x10), 0xA7D7C610}] = 0xC4005E11; // choose_script (the second parameters)
        g_params[{At(kParams + 0x10), 0x651533EC}] = 4;          // level
        g_params[{At(kParams), 0x651533EC}] = 2;
        g_levelChecklist = At(kChecklist);
        commands();
        add("Lonely", 0, 40);
        commands();
        // Adding.
        add("Kicker Gap", 0, 100);
        add("Rail Gap", 2, 50);
        add("Big Drop", 1, 250);
        add("kicker gap", 0, 100);
        add("apple", 3, 100);
        add("Zed", 0, 100);
        add("Low", 0, 10);
        add("Rail Gap", 0, 50);   // found again
        add("Rail Gap", 4, 75);   // another score
        add("KICKER GAP", 0, 100);
        add("Kicker Gap", 9, 100); // found again
        t.push_back(list->m_built);
        PushChecklist(t, kChecklist);
        // Queries.
        for (const char* name : {"Big Drop", "Low", "Nowhere", "kicker gap", "Rail Gap"})
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, const char*)>(0x00578B30)(list, name)
                                                       : list->FindGap(name)));
        for (int index = -1; index < 13; ++index)
            t.push_back(Rel(reinterpret_cast<uint32_t>(
                original ? Original<void*(__thiscall*)(void*, int)>(0x005793D0)(list, index) : list->GetGap(index))));
        for (int index = 0; index < 10; ++index) {
            t.push_back(Hash(original ? Original<const char*(__thiscall*)(void*, int)>(0x00579420)(list, index)
                                      : list->GetGapName(index)));
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, int)>(0x00579440)(list, index)
                                                       : list->GetGapCount(index)));
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, int)>(0x00579450)(list, index)
                                                       : list->GetGapScore(index)));
        }
        for (const char* name : {"Low", "Nowhere", "Zed"}) {
            Mth::Vector pos = {1.5f, -2.0f, static_cast<float>(std::strlen(name)), 1.0f};
            Mth::Vector dir = {0.0f, 1.0f, 0.25f, 0.0f};
            if (original)
                Original<void(__thiscall*)(void*, const char*, Mth::Vector, Mth::Vector)>(0x00578BC0)(list, name, pos, dir);
            else
                list->SetGapPositions(name, pos, dir);
        }
        for (int index : {0, 1, 5}) {
            Mth::Vector pos = {}, dir = {};
            if (original)
                Original<void(__thiscall*)(void*, int, Mth::Vector*, Mth::Vector*)>(0x00579460)(list, index, &pos, &dir);
            else
                list->GetGapPositions(index, &pos, &dir);
            const auto* words = reinterpret_cast<const uint32_t*>(&pos);
            t.insert(t.end(), words, words + 4);
            words = reinterpret_cast<const uint32_t*>(&dir);
            t.insert(t.end(), words, words + 4);
        }
        // Landing gaps.
        auto got = [&](const char* name) {
            if (original)
                Original<void(__thiscall*)(void*, const char*)>(0x00578C70)(list, name);
            else
                list->GotGap(name);
        };
        auto counts = [&]() {
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x00578D20)(list) : list->Count()));
            t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00578D50)(list) : list->AllGot());
        };
        counts();
        got("Low");
        got("Low");
        got("Zed");
        got("Nowhere");
        call0(0x00578CF0, &CGapChecklist::ClearPending, list);
        PushChecklist(t, kChecklist);
        for (bool listening : {true, false}) {
            g_goalsListening = listening;
            got("Low");
            got("Zed");
            got("apple");
            got("Big Drop");
            got("kicker gap");
            got("Lonely");
            call0(0x005796C0, &CGapChecklist::AwardPendingGaps, list);
            PushChecklist(t, kChecklist);
        }
        got("Rail Gap");
        got("Zed");
        got("Zed"); // (landed twice: counted twice)
        call0(0x005796C0, &CGapChecklist::AwardPendingGaps, list); // a single first-time gap
        counts();
        commands();
        PushChecklist(t, kChecklist);
        // GiveAllGaps: the level named, then the current level.
        g_currentLevel = 6;
        for (uint32_t p : {kParams + 0x10, kParams + 0x20}) {
            t.push_back((original ? Original<bool(__cdecl*)(CStruct*, Script::CScript*)>(0x00579360)(Ptr<CStruct>(p), script)
                                  : Game::CFunc_GiveAllGaps(Ptr<CStruct>(p), script)));
        }
        counts();
        PushChecklist(t, kChecklist);
        reinterpret_cast<Game::CGapCheck*>(list->m_gaps.next->next)->m_inLevel = false;
        call0(0x00578D90, &CGapChecklist::Reset, list);
        counts();
        PushChecklist(t, kChecklist);
        // Building from the node scripts (the second checklist), and AddGapFromNode directly.
        SetNode(kNodes, 100, "Kicker Gap");
        SetNode(kNodes + 0x10, 0, "Zero");
        SetNode(kNodes + 0x20, 30, "");
        SetNode(kNodes + 0x30, -1, "No score");
        SetNode(kNodes + 0x40, 55, "New Gap");
        SetNode(kNodes + 0x50, 70, nullptr);
        g_nodeCalls = 6;
        for (int print : {0, 1}) {
            g_integers[Hash("PrintGaps")] = print;
            g_integers[0x2E75A8A7] = print;
            g_printfArgs = 2;
            CGapChecklist* l = print ? other : list;
            call0(0x00578AC0, &CGapChecklist::Build, l);
            t.push_back(Rel(GameVariable<uint32_t>(0x007CE4B0)));
            PushChecklist(t, print ? kOther : kChecklist);
            for (int i = 0; i < 6; ++i) {
                CStruct* params = Ptr<CStruct>(kNodes + 0x10 * i);
                if (original)
                    Original<void(__thiscall*)(void*, CStruct*, const uint8_t*)>(0x00578A00)(l, params, nullptr);
                else
                    l->AddGapFromNode(params, nullptr);
            }
            g_printfArgs = 0;
        }
        // Building a fresh checklist.
        auto* fresh = Ptr<CGapChecklist>(kOther + 0x20);
        if (original)
            Original<void*(__thiscall*)(void*)>(0x00579690)(fresh);
        else
            fresh->Construct();
        g_nodeCalls = 0; // (no gaps: only Build's own flag)
        call0(0x00578AC0, &CGapChecklist::Build, fresh);
        t.push_back(fresh->m_built);
        PushChecklist(t, kOther + 0x20);
        // Clear.
        call0(0x00578840, &CGapChecklist::Clear, other);
        t.push_back(other->m_built);
        commands();
        call0(0x00578840, &CGapChecklist::Clear, list);
        t.push_back(list->m_built);
        // A gap and its node's destructors alone.
        for (int i = 0; i < 4; ++i) {
            auto* gap = Ptr<Game::CGapCheck>(kNodeObjects + 0x80 * i);
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x005786E0)(gap)
                                                               : gap->Construct())));
            gap->m_name = "name";
            auto* neighbour = Ptr<Game::CGapCheck>(kNodeObjects + 0x40 + 0x80 * i);
            neighbour->m_node.next = neighbour->m_node.prev = &gap->m_node;
            gap->m_node.next = gap->m_node.prev = &neighbour->m_node;
        }
        auto* first = Ptr<Game::CGapCheck>(kNodeObjects);
        if (original)
            Original<void(__thiscall*)(void*)>(0x00579510)(first);
        else
            first->DestroyBody();
        t.push_back(Rel(reinterpret_cast<uint32_t>(
            original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005794E0)(Ptr<void>(kNodeObjects + 0x80), 1)
                     : Ptr<Game::CGapCheck>(kNodeObjects + 0x80)->Destroy(1))));
        auto* node2 = Ptr<Game::CGapCheckNode>(kNodeObjects + 0x100);
        auto* node3 = Ptr<Game::CGapCheckNode>(kNodeObjects + 0x180);
        if (original)
            Original<void(__thiscall*)(void*)>(0x00578680)(node2);
        else
            node2->DestroyBody();
        for (unsigned int flags : {0u, 1u}) {
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005786A0)(node3, flags)
                                                                : node3->Destroy(flags))));
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005794B0)(node2, flags)
                                                                : node2->Destroy2(flags))));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
        t.push_back(GameVariable<uint32_t>(0x00690FA0));
    }
    Cleanup();
    Compare(traces[0], traces[1], "gap checklist");
}

} // namespace

SELFTEST(Early, TestGapChecklist, ViewerTestKit::Cleanup);
