// Self-tests for the career's save half (src/game/skate/career.cpp: the constructor, ReadFromStructure, WriteToStructure,
// GetCareerSaveInfo, the destructors): the original and ours run on a career in an arena; the gap checklists, arrays
// and structures are recording stand-ins (arrays read back by address, structures answering by structure and name),
// and the calls, results and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/game_mode.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkaterCareer;

std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_answers; // (structure, name) -> value

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x00579690, AddressOf(&Game::CGapChecklist::Construct)}, 0, [](Regs& r) {
        Rec(0x9690, {r.ecx});
        *reinterpret_cast<uint8_t*>(r.ecx) = static_cast<uint8_t>((r.ecx >> 4) & 1);
        return r.ecx;
    });
    s.At({0x00578D90, AddressOf(&Game::CGapChecklist::Reset)}, 0, [](Regs& r) {
        Rec(0x8D90, {r.ecx});
        return 0u;
    });
    s.At({0x00578840, AddressOf(&Game::CGapChecklist::Clear)}, 0, [](Regs& r) {
        Rec(0x8840, {r.ecx});
        return 0u;
    });
    s.At({0x00585520, AddressOf(&Game::CGapChecklist::DestroyBody)}, 0, [](Regs& r) {
        Rec(0x5520, {r.ecx});
        return 0u;
    });
    s.At({0x00578880, AddressOf(&Game::CGapChecklist::AddGap)}, 12, [](Regs& r) {
        Rec(0x8880, {r.ecx, Text(r.args[0]), r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00578BC0, AddressOf(&Game::CGapChecklist::SetGapPositions)}, 36, [](Regs& r) {
        Rec(0x8BC0, {r.ecx, Text(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7], r.args[8]});
        return 0u;
    });
    s.At({0x00578D20, AddressOf(&Game::CGapChecklist::Count)}, 0, [](Regs& r) {
        Rec(0x8D20, {r.ecx});
        return ((r.ecx >> 5) & 3) + 1;
    });
    s.At({0x00579420, AddressOf(&Game::CGapChecklist::GetGapName)}, 4, [](Regs& r) {
        Rec(0x9420, {r.ecx, r.args[0]});
        return At(0x3F00);
    });
    s.At({0x00579440, AddressOf(&Game::CGapChecklist::GetGapCount)}, 4, [](Regs& r) {
        Rec(0x9440, {r.ecx, r.args[0]});
        return r.args[0] & 1;
    });
    s.At({0x00579450, AddressOf(&Game::CGapChecklist::GetGapScore)}, 4, [](Regs& r) {
        Rec(0x9450, {r.ecx, r.args[0]});
        return 100u + r.args[0];
    });
    s.At({0x00579460, AddressOf(&Game::CGapChecklist::GetGapPositions)}, 12, [](Regs& r) {
        Rec(0x9460, {r.ecx, r.args[0]});
        auto* p = reinterpret_cast<float*>(r.args[1]);
        auto* d = reinterpret_cast<float*>(r.args[2]);
        p[0] = 1.5f, p[1] = 2.5f, p[2] = 3.5f;
        d[0] = -1.0f, d[2] = 0.5f;
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
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Rec(0xCC30, {r.ecx, r.args[0]});
        return r.ecx + 0x100 * r.args[0] + 0x7;
    });
    auto answer = [](Regs& r, uint32_t tag) {
        Rec(tag, {r.ecx, r.args[0], r.args[2] & 0xFF});
        auto it = g_answers.find({r.ecx, r.args[0]});
        if (it == g_answers.end())
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
        return 1u;
    };
    s.At({0x004767C0, 0x00476E70, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, [answer](Regs& r) { return answer(r, 0x67C0); });
    s.At({0x00476860, 0x00476F10, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [answer](Regs& r) { return answer(r, 0x6860); });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [answer](Regs& r) { return answer(r, 0x6630); });
    s.At({0x00476590, 0x00476C60, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [answer](Regs& r) { return answer(r, 0x6590); });
    s.At({0x00476FB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))}, 12,
         [](Regs& r) {
             Rec(0x6FB0, {r.ecx, r.args[0]});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 4.0f, v[1] = static_cast<float>(r.args[0] & 0xF), v[2] = -2.0f, v[3] = 1.0f;
             return 1u;
         });
    s.At({0x00477E20, AddressOf(static_cast<void (CStruct::*)(uint32_t, float, float, float)>(&CStruct::AddVector))}, 16,
         [](Regs& r) {
             Rec(0x7E20, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
             return 0u;
         });
    s.At({0x004780D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArrayPointer))}, 8,
         [](Regs& r) {
             Rec(0x80D0, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8, [](Regs& r) {
        Rec(0x8670, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Rec(0x9020, {r.args[0]});
        return At(0x3E00);
    });
    s.At({0x005532A0, AddressOf(&Game::CGameMode::GetNameChecksum)}, 0, [](Regs& r) {
        Rec(0x32A0, {r.ecx});
        return 0x6A3Eu;
    });
}

// The constructor; writing (levels built or not, gaps got or not) directly and through the command; reading (a full
// save; an older one without the counter total, the gaps and the visited levels); the destructors.
void TestCareerSave()
{
    SaveGlobals({{0x007CE4E8, 0x18, {}}, {0x007CE478, 4, {}}, {0x007CE470, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_answers.clear();
        std::vector<uint32_t>& t = traces[run];
        std::memcpy(Ptr<char>(0x3F00), "gap", 4);
        GameVariable<uint32_t>(0x007CE478) = At(0x3C00);
        GameVariable<uint32_t>(0x007CE470) = At(0x3D00);
        *Ptr<uint32_t>(0x3C00 + 0x378) = 0x378378;
        auto* c = Ptr<CSkaterCareer>(0);
        std::memset(c, 0xCD, sizeof(CSkaterCareer));
        t.push_back(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x00585270)(c) : c->Construct()) - g_base);
        *Ptr<uint32_t>(0x3C00 + 0x20) = At(0);
        for (int i = 0; i < CSkaterCareer::kLevels; ++i)
            t.push_back(c->m_visited[i]); // (as constructed)
        for (int i = 0; i < CSkaterCareer::kLevels; ++i) {
            c->m_visited[i] = i % 4 == 1;
            if (c->m_gaps[i])
                c->m_gaps[i]->m_built = (i & 1) != 0; // (some checklists built: their gaps written)
        }
        auto* words = reinterpret_cast<uint32_t*>(c);
        for (uint32_t i = 1; i < 0x630 / 4; ++i)
            words[i] ^= i * 0x2545F491u;
        if (original)
            Original<void(__thiscall*)(void*, CStruct*)>(0x00585550)(c, Ptr<CStruct>(0x3A00));
        else
            c->WriteToStructure(Ptr<CStruct>(0x3A00));
        *Ptr<uint32_t>(0x3B00 + 0x14) = At(0x3A80);
        t.push_back(original ? Original<bool(__cdecl*)(CStruct*, void*)>(0x00585E20)(nullptr, Ptr<void>(0x3B00))
                             : Obj::CFunc_GetCareerSaveInfo(nullptr, Ptr<Script::CScript>(0x3B00)));
        // A save: its career structure, the arrays (read back by GetElement's formula), each level's gaps.
        uint32_t save = At(0x3800), career = At(0x4000);
        g_answers[{save, 0x4DA4937B}] = career;
        for (uint32_t name : {0xB7EF8BFCu, 0xC0FA354Fu, 0xA888FE32u, 0x9DD7E4A6u, 0x7106C6BFu, 0x414FD20Fu, 0x9CC5BBF6u, 0xE888DD02u})
            g_answers[{career, name}] = At(0x5000) + (name & 0xF0);
        g_answers[{career, 0x0444419A}] = 1234;
        g_answers[{career, 0x95947B61}] = 5678;
        g_answers[{career, 0x6C1A5E39}] = 9;
        g_answers[{career, 0x7A6BF912}] = 0x86;
        g_answers[{career, 0xF884773C}] = 44;
        g_answers[{career, 0x1C6CF075}] = 55;
        g_answers[{career, 0x55D761CF}] = At(0x3E40);
        uint32_t gapsArray = At(0x5000) + (0x9DD7E4A6 & 0xF0);
        for (int level = 0; level < CSkaterCareer::kLevels; ++level) {
            uint32_t entry = gapsArray + 0x100 * level + 7;
            g_answers[{entry, 0x651533EC}] = static_cast<uint32_t>(level);
            if (level & 1) {
                g_answers[{entry, 0x73F8CA00}] = 1;
                uint32_t list = At(0xA000) + 0x10 * level;
                *reinterpret_cast<uint32_t*>(list + 4) = 3;
                g_answers[{entry, 0xD76C173E}] = list;
                for (int g = 0; g < 3; ++g) {
                    uint32_t gap = list + 0x100 * g + 7;
                    g_answers[{gap, 0x699FCFC0}] = At(0x3F00);
                    if (g != 1)
                        g_answers[{gap, 0x25EB70DB}] = static_cast<uint32_t>(g + level);
                    g_answers[{gap, 0x92AB03E8}] = 50;
                }
            }
        }
        auto read = [&]() {
            if (original)
                Original<void(__thiscall*)(void*, CStruct*)>(0x00584D00)(c, reinterpret_cast<CStruct*>(save));
            else
                c->ReadFromStructure(reinterpret_cast<CStruct*>(save));
        };
        read();
        g_answers.erase({career, 0x6C1A5E39});
        g_answers.erase({career, 0x9DD7E4A6});
        g_answers.erase({career, 0x7106C6BF});
        read();
        if (original)
            Original<void(__thiscall*)(void*)>(0x00585E40)(c);
        else
            c->DestroyBody();
        auto* other = Ptr<CSkaterCareer>(0x1800);
        std::memset(other, 0, sizeof(CSkaterCareer));
        other->m_gaps[3] = Ptr<Game::CGapChecklist>(0x1F00);
        other->m_info = Ptr<CStruct>(0x1F80);
        for (unsigned int flags : {0u, 1u})
            t.push_back(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x00585EA0)(other, flags)
                                                            : other->Destroy(flags)) - g_base);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "career save");
}

} // namespace

SELFTEST(Early, TestCareerSave, ViewerTestKit::Cleanup);
