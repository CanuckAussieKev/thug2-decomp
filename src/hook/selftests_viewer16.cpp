// Self-tests for the skater's career (src/game/skate/career.cpp, 0x00584110-0x00584CF3): the original and ours run the
// same calls on an identical career in an arena (its words filled with a pattern); the gap checklists, the info
// structure and the script values are recording stand-ins, and the calls, results and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkaterCareer;

constexpr uint32_t kCareer = 0x0000; // 0x6AC
constexpr uint32_t kBlocks = 0x1000;  // gap checklists, 0x20 apart; the info at +0x400; arrays at +0x500

uint32_t g_allGot; // a bit per level

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x00578D90, AddressOf(&Game::CGapChecklist::Reset)}, 0, [](Regs& r) {
        Rec(0x8D90, {r.ecx});
        return 0u;
    });
    s.At({0x00578D50, AddressOf(&Game::CGapChecklist::AllGot)}, 0, [](Regs& r) {
        Rec(0x8D50, {r.ecx});
        uint32_t level = (r.ecx - At(kBlocks)) / 0x20;
        return (g_allGot >> level) & 1;
    });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Rec(0x6630, {r.ecx, r.args[0], r.args[2] & 0xFF});
             *reinterpret_cast<int*>(r.args[1]) = static_cast<int>(r.args[0] & 0xFF);
             return 1u;
         });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        return At(kBlocks + 0x480);
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return At(kBlocks + 0x500);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return 0x1E0u + r.args[0];
    });
}

// Every bit function (both levels: given and current; words 0-7; set / clear / ask / just got), the counts (levels
// left out or not), the global flags and cheats, the info integers, cash / amounts / counters, +0x634, starting a
// level, every reset part (and an unknown one), and the gaps check (listed levels visited or not, all got or not).
void TestCareer()
{
    SaveGlobals({{0x007CE4E8, 0x18, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        auto* c = Ptr<CSkaterCareer>(kCareer);
        auto* words = reinterpret_cast<uint32_t*>(c);
        for (uint32_t i = 0; i < sizeof(CSkaterCareer) / 4; ++i)
            words[i] = (i * 0x9E3779B9u) ^ (i << 7);
        c->m_level = 3;
        for (int i = 0; i < CSkaterCareer::kLevels; ++i)
            c->m_gaps[i] = Ptr<Game::CGapChecklist>(kBlocks + 0x20 * i);
        c->m_info = Ptr<CStruct>(kBlocks + 0x400);
        for (int i = 0; i < 0x18; ++i)
            GameVariable<uint8_t>(0x007CE4E8 + i) = static_cast<uint8_t>(i % 3 == 0);
        auto call1 = [&](uint32_t address, int a, void (CSkaterCareer::*ours)(int)) {
            if (original)
                Original<void(__thiscall*)(void*, int)>(address)(c, a);
            else
                (c->*ours)(a);
        };
        auto call2 = [&](uint32_t address, int a, int b, void (CSkaterCareer::*ours)(int, int)) {
            if (original)
                Original<void(__thiscall*)(void*, int, int)>(address)(c, a, b);
            else
                (c->*ours)(a, b);
        };
        auto ask2 = [&](uint32_t address, int a, int b, bool (CSkaterCareer::*ours)(int, int)) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, int, int)>(address)(c, a, b) : (c->*ours)(a, b));
        };
        auto ask1 = [&](uint32_t address, int a, bool (CSkaterCareer::*ours)(int)) {
            t.push_back(original ? Original<bool(__thiscall*)(void*, int)>(address)(c, a) : (c->*ours)(a));
        };
        const int bits[] = {0, 1, 31, 32, 63, 64, 100, 199, 200, 255};
        for (int level : {-1, 0, 7, 20}) {
            for (int bit : bits) {
                call2(0x00584140, bit, level, &CSkaterCareer::SetGoal);
                ask2(0x005841D0, bit, level, &CSkaterCareer::GetGoal);
                call2(0x00584430, bit + 1 < 256 ? bit + 1 : bit, level, &CSkaterCareer::SetFlag);
                ask2(0x005844C0, bit, level, &CSkaterCareer::GetFlag);
                if (bit % 3 == 0) {
                    call2(0x00584180, bit, level, &CSkaterCareer::UnSetGoal);
                    call2(0x00584470, bit, level, &CSkaterCareer::UnSetFlag);
                }
                ask1(0x00584210, bit, &CSkaterCareer::JustGotGoal);
                ask1(0x00584500, bit, &CSkaterCareer::JustGotFlag);
            }
            t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, int)>(0x00584250)(c, level) : c->CountGoals(level)));
            t.push_back(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int)>(0x00584110)(c, level)
                                                            : c->GetGapChecklist(level)) - g_base);
        }
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x00584130)(c) : c->GetLevel()));
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x005842A0)(c) : c->CountTotalGoals()));
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*)>(0x00584320)(c) : c->CountLevelsWithGoals()));
        for (int flag : {0, 5, 37, 255}) {
            call1(0x005843A0, flag, &CSkaterCareer::SetGlobalFlag);
            ask1(0x00584400, flag + 1, &CSkaterCareer::GetGlobalFlag);
            if (flag & 1)
                call1(0x005843D0, flag, &CSkaterCareer::UnSetGlobalFlag);
        }
        for (uint32_t cheat : {0x00000025u, 0x00000026u, 0x000000FFu}) {
            g_integers[cheat] = static_cast<int>(cheat);
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x00584CC0)(c, cheat) : c->GetCheat(cheat));
            t.push_back(GameVariable<uint8_t>(0x007CE4FD));
        }
        t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, uint32_t)>(0x00584540)(c, 0xABCD12)
                                                   : c->GetInfoInteger(0xABCD12)));
        t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t, int)>(0x00584560)(c, 0xABCD13, 77) : c->SetInfoInteger(0xABCD13, 77));
        call1(0x00584580, 9, &CSkaterCareer::AddToInfo471FC4DD);
        for (int amount : {5, -3, 40000}) {
            t.push_back(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, int)>(0x005845C0)(c, amount)
                                                            : c->AddCash(amount)) - g_base);
            call1(0x005845F0, amount * 2, &CSkaterCareer::SetLevelAmount);
            t.push_back(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00584620)(c)
                                                            : c->IncrementCounter()) - g_base);
        }
        for (int level : {0, 3, 17})
            t.push_back(original ? Original<uint8_t(__thiscall*)(void*, int)>(0x00584640)(c, level) : c->GetCounter(level));
        if (original)
            Original<void(__thiscall*)(void*, uint8_t)>(0x00584650)(c, 0x5A);
        else
            c->SetFlag5EF(0x5A);
        for (int value : {10, 4, 30})
            call1(0x00584660, value, &CSkaterCareer::SetValue634);
        for (int level : {-1, 5, -1, 12})
            call1(0x00584C60, level, &CSkaterCareer::StartLevel);
        // The gaps check: listed levels 2, 5, 12 (the array's elements name the integers 0x1E0+i).
        *Ptr<uint32_t>(kBlocks + 0x504) = 3;
        g_integers[0x1E0] = 2, g_integers[0x1E1] = 5, g_integers[0x1E2] = 12;
        for (uint32_t got : {0u, 0x1024u, 0xFFFFFFFFu}) {
            g_allGot = got;
            for (int visit : {0, 1}) {
                c->m_visited[2] = c->m_visited[5] = visit != 0;
                t.push_back(original ? Original<bool(__thiscall*)(void*)>(0x00584BC0)(c) : c->AllGapsGot());
            }
        }
        // Resets.
        for (uint32_t part : {0x12345u, 0x4C10DE52u, 0x4DA4937Bu})
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x00584BA0)(c, part) : c->ResetPart(part));
        for (uint32_t i = 0; i < sizeof(CSkaterCareer) / 4; ++i)
            words[i] ^= i * 0x01000193u; // (dirty again)
        c->m_level = 4;
        for (int i = 0; i < CSkaterCareer::kLevels; ++i)
            c->m_gaps[i] = Ptr<Game::CGapChecklist>(kBlocks + 0x20 * i);
        c->m_info = Ptr<CStruct>(kBlocks + 0x400);
        t.push_back(original ? Original<bool(__thiscall*)(void*, bool, uint32_t)>(0x00584680)(c, true, 0) : c->Reset(true, 0));
        for (int i = 0; i < 0x18; ++i)
            t.push_back(GameVariable<uint8_t>(0x007CE4E8 + i));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "career");
}

} // namespace

SELFTEST(Early, TestCareer, ViewerTestKit::Cleanup);
