// Self-tests for the goal editor component (src/game/ed/goal_editor.cpp): the original and ours on the same
// component (50 goals of several levels, used or not, complete or not) with the edited goals' own code, the
// structures, scripts, goal manager, objects and memory stood in for; the calls and the component compared.

#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/goal_editor.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/script/names.h"
#include "game/script/struct_buffer.h"
#include "hook/goal_editor_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

#pragma warning(disable : 4996) // (strcpy: the goals' names)

namespace {

using namespace GoalEditorTests;
using Ed::CEditedGoal;
using Ed::CGoalEditorComponent;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

alignas(16) uint8_t g_editorBytes[2][sizeof(CGoalEditorComponent)];
uint8_t g_camera[0x100];
uint8_t g_skateObject[0x380];
uint8_t g_script[0x40];
uint8_t g_manager[16];
CGoalEditorComponent* Editor() { return reinterpret_cast<CGoalEditorComponent*>(g_editorBytes[0]); }

constexpr uint32_t kLevels[] = {0xE8B4B836, 0x11223344, 0xB664035D, 0x55667788};
const char* const kNames[] = {"Name0", "name1", "NAME1", "Other"};

void UseEditorRelocation()
{
    g_relocate = [](uint32_t word) {
        for (uint32_t i = 0; i < 2; ++i) {
            uint32_t base = reinterpret_cast<uint32_t>(g_editorBytes[i]);
            if (word >= base && word < base + sizeof(CGoalEditorComponent))
                return 0xED000000u + i * 0x100000u + (word - base);
        }
        return OnStack(word) ? 0x57AC0000u : word;
    };
}

void FillEditor(uint32_t seed)
{
    CGoalEditorComponent* editor = Editor();
    std::memset(editor, 0x5A, 0x18);
    for (uint32_t i = 0; i < 50; ++i) {
        CEditedGoal& goal = editor->m_goals[i];
        FillGoal(goal, seed * 77 + i);
        uint32_t r = (seed * 2654435761u) ^ (i * 40503u);
        goal.m_used = (r >> 3) % 4 != 0;
        goal.m_level = kLevels[(r >> 5) % 4];
        goal.m_goalIndex = static_cast<int>((r >> 7) % 6);
        goal.m_minPoints = static_cast<uint8_t>(2 + (r >> 9) % 4);
        goal.m_numPoints = static_cast<uint8_t>((r >> 11) % 7);
        goal.m_type = (r >> 13) % 3 ? 0x61C5D092 : 0x54166ACD;
        std::strcpy(goal.m_name, kNames[(r >> 15) % 4]);
        goal.m_points[1].pos.x = static_cast<float>((r >> 17) % 200) - 100.0f;
        goal.m_points[1].pos.z = static_cast<float>((r >> 19) % 200) - 100.0f;
    }
    editor->mp_current = seed % 5 ? &editor->m_goals[seed % 50] : nullptr;
    editor->mp_input = reinterpret_cast<Obj::CBaseComponent*>(Token(14));
    editor->mp_camera = reinterpret_cast<Obj::CBaseComponent*>(g_camera);
    *reinterpret_cast<uint32_t*>(g_editorBytes[0] + 0xC) = reinterpret_cast<uint32_t>(Token(15)); // the object
    for (uint32_t i = 0; i < sizeof(g_camera); ++i)
        g_camera[i] = static_cast<uint8_t>(seed * 13 + i);
}

void StandInComponentWorld(StandIns& s)
{
    s.At({0x00626EFB}, 0, [](Regs& r) {
        const char* a = reinterpret_cast<const char*>(r.args[0]);
        const char* b = reinterpret_cast<const char*>(r.args[1]);
        Record({0x6EFB, Hash(a), Hash(b)});
        for (;; ++a, ++b) {
            int x = std::tolower(static_cast<unsigned char>(*a)), y = std::tolower(static_cast<unsigned char>(*b));
            if (x != y || !x)
                return static_cast<uint32_t>(x - y);
        }
    });
    s.At({0x00558E30, AddressOf(&Game::CGoalManager::GoalExists)}, 4, [](Regs& r) {
        Record({0x8E30, r.ecx, r.args[0]});
        return r.args[0] & 1;
    });
    s.At({0x0055BEA0, AddressOf(&Game::CGoalManager::RemoveGoal)}, 4, [](Regs& r) {
        Record({0xBEA0, r.ecx, r.args[0]});
        return 1u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, r.ecx, r.args[0]});
        return r.args[0] ^ 0x5A5A0000u;
    });
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Record({0x19F0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(Token(5));
    });
    s.At({0x0047A0C0, AddressOf(Script::ReadFromBuffer)}, 0, [](Regs& r) {
        Record({0xA0C0, r.args[0], r.args[1]});
        return r.args[1] + 0x40;
    });
    s.At({0x00479E20, AddressOf(Script::WriteToBuffer)}, 0, [](Regs& r) {
        Record({0x9E20, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0x123u;
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x11, 0x18);
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, r.ecx, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_editorBytes[1]);
    });
    s.At({0x0046F060, AddressOf(Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The getters by name (answered by the name's hash).
    using Script::CStruct;
    s.At({0x004765F0, AddressOf(static_cast<bool (CStruct::*)(const char*, const char**, int)>(&CStruct::GetString))},
         12, [](Regs& r) {
             uint32_t name = Hash(reinterpret_cast<const char*>(r.args[0]));
             Record({0x65F0, Rel(r.ecx), name, Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, name, value))
                 return 0u;
             *reinterpret_cast<const char**>(r.args[1]) = kStrings[value];
             return 1u;
         });
    s.At({0x00476A00, AddressOf(static_cast<bool (CStruct::*)(const char*, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) {
             uint32_t name = Hash(reinterpret_cast<const char*>(r.args[0]));
             Record({0x6A00, Rel(r.ecx), name, Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, name, value))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = value;
             return 1u;
         });
    s.At({0x00477770, AddressOf(static_cast<void (CStruct::*)(const char*)>(&CStruct::RemoveComponent))}, 4,
         [](Regs& r) {
             Record({0x7770, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
             return 0u;
         });
    s.At({0x00477C30, AddressOf(static_cast<void (CStruct::*)(const char*, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) {
             Record({0x7C30, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
             return 0u;
         });
    // The edited goals' own code (tested by selftests_goal_editor.cpp).
    s.At({0x004E96D0, AddressOf(&CEditedGoal::ReadFromStructure)}, 4, [](Regs& r) {
        Record({0x96D0, Rel(r.ecx), Rel(r.args[0])});
        reinterpret_cast<CEditedGoal*>(r.ecx)->m_used = true;
        return 0u;
    });
    s.At({0x004EB260, AddressOf(&CEditedGoal::WriteIntoStructure)}, 4, [](Regs& r) {
        Record({0xB260, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004E9590, AddressOf(&CEditedGoal::PlacePointsOnGround)}, 0, [](Regs& r) {
        Record({0x9590, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x004E6BF0, AddressOf(&CEditedGoal::RemoveGapAndRenumber)}, 4, [](Regs& r) {
        Record({0x6BF0, Rel(r.ecx), r.args[0]});
        return 0u;
    });
}

void DumpEditor(std::vector<uint32_t>& trace)
{
    for (uint32_t e = 0; e < 2; ++e) {
        const auto* words = reinterpret_cast<const uint32_t*>(g_editorBytes[e]);
        for (size_t i = 0; i < sizeof(CGoalEditorComponent) / 4; ++i)
            trace.push_back(Relocate(words[i]));
    }
    trace.push_back(HashBytes(g_camera, sizeof(g_camera)));
}

// Every function of the component, round by round on differently filled components and levels.
void TestGoalEditorComponent()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x007CE478, 4}, {0x007CCC58, 0x40}, {0x007CCC40, 0x18},
                                                       {0x006F3614, 16}, {0x006F22B4, 16}, {0x006F0FB8, 4},
                                                       {0x007D1800, 4}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    StandInGoalIo(s);
    StandInComponentWorld(s);
    UseEditorRelocation();
    auto run = [&](bool original) {
        g_trace.clear();
        g_values.clear();
        g_elements.clear();
        UseFakePools();
        g_globalFloats = {{0x71B20276, 30.0f}};
        GameVariable<uint8_t*>(0x007CE478) = g_skateObject;
        GameVariable<uint8_t*>(0x006F0FB8) = g_manager;
        GameVariable<uint8_t*>(0x007D1800) = g_manager;
        *reinterpret_cast<uint32_t*>(g_script + 0x14) = reinterpret_cast<uint32_t>(Token(7));
        std::memset(g_editorBytes[1], 1, sizeof(g_editorBytes[1]));
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004E7BE0, 0x004E7C60, 0x004E7D00, 0x004E7E00, 0x004E7E50, 0x004E8130,
                                   0x004E8240, 0x004E8280, 0x004E82C0, 0x004E8300, 0x004E8390, 0x004E8420,
                                   0x004E8090, 0x004E7B50, 0x004E7F30, 0x004E7FC0, 0x004EA580, 0x004EA6C0,
                                   0x004EA780, 0x004EA470, 0x004EA2C0, 0x004EB850, 0x004EBA10, 0x004EBB70,
                                   0x004EA7C0, 0x004E8210, 0x004E8470, 0x004EA230, 0x004E6FD0, 0x004E7B20,
                                   0x004EB7F0, 0x004E6FB0});
        using Self = CGoalEditorComponent*;
        uint32_t params = reinterpret_cast<uint32_t>(Token(0));
        for (uint32_t round = 0; round < 24; ++round) {
            FillEditor(round + 1);
            Self editor = Editor();
            *reinterpret_cast<uint32_t*>(g_skateObject + 0x378) = kLevels[round % 4];
            auto ret = [&](void* p) { trace.push_back(Rel(reinterpret_cast<uint32_t>(p))); };
            ret(Fn<CEditedGoal*(__thiscall*)(Self, const char*)>(original, 0x004E7BE0)(editor, kNames[round % 4]));
            ret(Fn<CEditedGoal*(__thiscall*)(Self, uint32_t)>(original, 0x004E7C60)(
                editor, Hash(round % 3 ? "CreatedGoal2" : "CreatedGoal9")));
            g_values.clear();
            switch (round % 4) {
            case 0: g_values[{params, 0x9982E501}] = Hash("CreatedGoal1"); break;
            case 1: g_values[{params, 0xBFECC45B}] = 2; break;
            case 2: g_values[{params, 0x474A6A7F}] = round % 8 == 2 ? 99 : round % 6; break;
            default: break;
            }
            ret(Fn<CEditedGoal*(__thiscall*)(Self, uint32_t)>(original, 0x004E7D00)(editor, params));
            trace.push_back(Fn<int(__thiscall*)(Self)>(original, 0x004E7E00)(editor));
            trace.push_back(Fn<int(__thiscall*)(Self, uint32_t)>(original, 0x004E7E50)(
                editor, round % 5 ? kLevels[round % 4] : 0));
            Mth::Vector at = editor->m_goals[round].m_points[round % 3].pos;
            at.x += static_cast<float>(round % 7) * 6.0f;
            trace.push_back(Fn<bool(__thiscall*)(Self, const Mth::Vector&, float, uint32_t, int)>(original, 0x004E8130)(
                editor, at, 1.5f, kLevels[round % 4], static_cast<int>(round % 3)));
            trace.push_back(Fn<bool(__thiscall*)(Self, const Mth::Vector&, float, uint32_t, int)>(original, 0x004E8130)(
                editor, editor->m_goals[round].m_points[0].pos, 25.0f, editor->m_goals[round].m_level, 0));
            if (round % 6 == 5) {
                for (CEditedGoal& goal : editor->m_goals)
                    goal.m_numPoints = static_cast<uint8_t>(goal.m_numPoints % 2);
            }
            trace.push_back(Fn<bool(__thiscall*)(Self, float, float, float, float)>(original, 0x004E8300)(
                editor, -90.0f + round, -95.0f, 90.0f - round, 95.0f - 2.0f * round));
            if (round % 3 == 0)
                Fn<void(__thiscall*)(Self, float, float, float, float)>(original, 0x004E8390)(
                    editor, -80.0f, -70.0f + round, 85.0f, 75.0f);
            Fn<void(__thiscall*)(Self, int)>(original, 0x004E8420)(editor, static_cast<int>(round % 9));
            if (editor->mp_current) {
                Fn<void(__thiscall*)(Self)>(original, 0x004E7B50)(editor);
                Fn<void(__thiscall*)(Self)>(original, 0x004E7F30)(editor);
                Fn<void(__thiscall*)(Self)>(original, 0x004EA580)(editor);
                editor->mp_current->m_numPoints = static_cast<uint8_t>(round % 2);
                editor->mp_current->m_points[editor->mp_current->m_currentPoint].kind = static_cast<int>(round % 5);
                Fn<void(__thiscall*)(Self)>(original, 0x004E7B50)(editor);
                Fn<void(__thiscall*)(Self)>(original, 0x004EA580)(editor);
            }
            Fn<void(__thiscall*)(Self)>(original, 0x004E7FC0)(editor);
            Fn<void(__thiscall*)(Self)>(original, 0x004EA780)(editor);
            DumpEditor(trace);
            // Removing, new goals, clearing.
            if (round % 3 == 1)
                editor->mp_current = &editor->m_goals[(round * 7) % 50];
            Fn<void(__thiscall*)(Self, CEditedGoal*)>(original, 0x004E8090)(editor, &editor->m_goals[(round * 7) % 50]);
            if (round % 4 == 3) {
                for (CEditedGoal& goal : editor->m_goals)
                    goal.m_used = true;
            }
            if (round % 2 && round % 4 != 3) // (NewEditorGoal without a free slot reads a null goal)
                Fn<bool(__thiscall*)(Self, uint32_t, void*)>(original, 0x004EA7C0)(editor, params, g_script);
            else
                Fn<void(__thiscall*)(Self)>(original, 0x004EA6C0)(editor);
            Fn<void(__thiscall*)(Self)>(original, 0x004E82C0)(editor);
            DumpEditor(trace);
            // Reading and writing.
            g_values.clear();
            g_elements.clear();
            uint32_t entries = reinterpret_cast<uint32_t>(&g_arrays[1]);
            g_arrays[1].m_size = round % 6;
            if (round % 5)
                g_values[{params, 0x38DBE1D0}] = 1;
            for (uint32_t i = 0; i < 6; ++i) {
                uint32_t entry = reinterpret_cast<uint32_t>(Token(1 + i % 4)) + i;
                g_elements[{entries, i}] = entry;
                if ((i + round) % 4)
                    g_values[{entry, 0x651533EC}] = kLevels[(i + round) % 4];
            }
            editor->mp_current = &editor->m_goals[round];
            if (round % 7 == 6) {
                for (CEditedGoal& goal : editor->m_goals)
                    goal.m_level = 0xE8B4B836;
            }
            if (round % 3 == 0) {
                // (ReadFromBuffer's structure: the next pool block)
                uint32_t structure = reinterpret_cast<uint32_t>(g_blocks[g_nextBlock % 512]);
                g_values[{structure, 0x38DBE1D0}] = 1;
                trace.push_back(Rel(reinterpret_cast<uint32_t>(
                    Fn<const uint8_t*(__thiscall*)(Self, uint32_t, const uint8_t*)>(original, 0x004EA2C0)(
                        editor, kLevels[round % 4], reinterpret_cast<const uint8_t*>(0x00ABC000)))));
            } else {
                Fn<void(__thiscall*)(Self, uint32_t, int)>(original, 0x004EA470)(editor, params,
                                                                                  static_cast<int>(round % 2));
            }
            ret(editor->mp_current);
            Fn<void(__thiscall*)(Self, uint32_t)>(original, 0x004EB850)(editor, params);
            Fn<void(__thiscall*)(Self, uint32_t, uint32_t)>(original, 0x004EBA10)(editor, kLevels[round % 4], params);
            trace.push_back(Fn<uint32_t(__thiscall*)(Self, uint32_t, void*, uint32_t)>(original, 0x004EBB70)(
                editor, kLevels[round % 4], reinterpret_cast<void*>(0x00DEF000), 0x400 + round));
            editor->mp_current = &editor->m_goals[round + 1];
            if (round % 2)
                Fn<void(__thiscall*)(Self)>(original, 0x004E8240)(editor);
            else
                Fn<void(__thiscall*)(Self)>(original, 0x004E8280)(editor);
            DumpEditor(trace);
        }
        // Made, registered, found, finalised, destroyed.
        Self made = reinterpret_cast<Self>(Fn<Obj::CBaseComponent*(__cdecl*)()>(original, 0x004EB7F0)());
        trace.push_back(Rel(reinterpret_cast<uint32_t>(made)));
        Fn<void(__cdecl*)()>(original, 0x004E6FB0)();
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<Self(__cdecl*)()>(original, 0x004E8470)())));
        Fn<void(__thiscall*)(Self)>(original, 0x004E8210)(made);
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<Self(__thiscall*)(Self, unsigned)>(original, 0x004E7B20)(made, 1))));
        Fn<void(__thiscall*)(Self)>(original, 0x004E6FD0)(made);
        DumpEditor(trace);
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor: the component");
    g_relocate = nullptr;
    g_globalFloats.clear();
    g_values.clear();
    g_elements.clear();
    delete &s;
}

// The edited goals' methods the member functions call: recorded (tested by selftests_goal_editor.cpp).
void StandInGoalMethods(StandIns& s)
{
    struct Method {
        uint32_t address;
        uint32_t ours;
        uint16_t pop;
    };
    const Method methods[] = {
        {0x004E7390, AddressOf(&CEditedGoal::AddGap), 4},
        {0x004E6B80, AddressOf(&CEditedGoal::RemoveGap), 4},
        {0x004E6C40, AddressOf(&CEditedGoal::AddKeyComboSet), 16},
        {0x004E6CD0, AddressOf(&CEditedGoal::RemoveKeyComboSet), 4},
        {0x004E7590, AddressOf(&CEditedGoal::ReadSpecificParams), 4},
        {0x004E6DA0, AddressOf(&CEditedGoal::SetType), 4},
        {0x004E6F70, AddressOf(&CEditedGoal::SetTimeLimit), 4},
        {0x004E7A80, AddressOf(&CEditedGoal::WriteNodePositions), 0},
        {0x004E7780, AddressOf(&CEditedGoal::CreateMarkers), 0},
        {0x004EB6B0, AddressOf(&CEditedGoal::AddToGoalManager), 4},
    };
    for (const Method& m : methods) {
        uint32_t id = m.address, count = m.pop / 4u;
        s.At({m.address, m.ours}, m.pop, [id, count](Regs& r) {
            Record({id, Rel(r.ecx)});
            for (uint32_t i = 0; i < count; ++i)
                Record({i == 0 || i == 1 ? r.args[i] & 0xFF : Rel(r.args[i])});
            return 0u;
        });
    }
    s.At({0x004E6D50, AddressOf(&CEditedGoal::FindKeyComboSet)}, 4, [](Regs& r) {
        Record({0x6D50, Rel(r.ecx), r.args[0]});
        auto* goal = reinterpret_cast<CEditedGoal*>(r.ecx);
        return r.args[0] < 6 ? reinterpret_cast<uint32_t>(&goal->m_keyComboSets[r.args[0] % 3]) : 0u;
    });
    s.At({0x004E7960, AddressOf(&CEditedGoal::GetCurrentPoint)}, 12, [](Regs& r) {
        Record({0x7960, Rel(r.ecx)});
        auto* pos = reinterpret_cast<float*>(r.args[0]);
        pos[0] = 1.0f, pos[1] = 2.25f, pos[2] = -3.0f, pos[3] = 9.0f;
        *reinterpret_cast<float*>(r.args[1]) = 4.5f;
        *reinterpret_cast<float*>(r.args[2]) = 0.75f;
        return 1u;
    });
}

// The member functions, round by round, with each value there or not and the goal found or not.
void TestGoalEditorMembers()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x007CE478, 4}, {0x007CCC58, 0x40}, {0x007CCC40, 0x18},
                                                       {0x006F3614, 16}, {0x006F22B4, 16}, {0x006F0FB8, 4},
                                                       {0x007D1800, 4}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    StandInGoalIo(s);
    StandInComponentWorld(s);
    StandInGoalMethods(s);
    UseEditorRelocation();
    static const uint32_t kMembers[] = {0x004E8490, 0x004E84E0, 0x004E8530, 0x004E8590, 0x004E8630, 0x004E8680,
                                        0x004E8720, 0x004E8780, 0x004E87A0, 0x004E87C0, 0x004E8900, 0x004E8920,
                                        0x004E8950, 0x004E8980, 0x004E89A0, 0x004E89F0, 0x004E8A40, 0x004E8AA0,
                                        0x004E8B30, 0x004E8B90, 0x004E8BB0, 0x004E8C20, 0x004E8C90, 0x004E8CF0,
                                        0x004E8D30, 0x004E8D50, 0x004E8DB0, 0x004E8E00, 0x004E8E60, 0x004EA810,
                                        0x004EBFF0, 0x004EC1D0, 0x004EC200};
    auto run = [&](bool original) {
        g_trace.clear();
        g_elements.clear();
        UseFakePools();
        GameVariable<uint8_t*>(0x007CE478) = g_skateObject;
        GameVariable<uint8_t*>(0x006F0FB8) = g_manager;
        GameVariable<uint8_t*>(0x007D1800) = g_manager;
        *reinterpret_cast<uint32_t*>(g_script + 0x14) = reinterpret_cast<uint32_t>(Token(7));
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004E8490, 0x004E84E0, 0x004E8530, 0x004E8590, 0x004E8630, 0x004E8680,
                                   0x004E8720, 0x004E8780, 0x004E87A0, 0x004E87C0, 0x004E8900, 0x004E8920,
                                   0x004E8950, 0x004E8980, 0x004E89A0, 0x004E89F0, 0x004E8A40, 0x004E8AA0,
                                   0x004E8B30, 0x004E8B90, 0x004E8BB0, 0x004E8C20, 0x004E8C90, 0x004E8CF0,
                                   0x004E8D30, 0x004E8D50, 0x004E8DB0, 0x004E8E00, 0x004E8E60, 0x004EA810,
                                   0x004EBFF0, 0x004EC1D0, 0x004EC200});
        uint32_t params = reinterpret_cast<uint32_t>(Token(0));
        for (uint32_t round = 0; round < 12; ++round) {
            for (size_t m = 0; m < sizeof(kMembers) / 4; ++m) {
                FillEditor(round * 40 + static_cast<uint32_t>(m) + 1);
                CGoalEditorComponent* editor = Editor();
                *reinterpret_cast<uint32_t*>(g_skateObject + 0x378) = kLevels[(round + m) % 4];
                if ((round + m) % 6 == 5) {
                    for (CEditedGoal& goal : editor->m_goals)
                        goal.m_used = true;
                }
                if (kMembers[m] == 0x004E8C90 && round % 6 < 3) {
                    // (exactly 9, 10 or 11 complete goals of the level)
                    for (int i = 0; i < 50; ++i) {
                        CEditedGoal& goal = editor->m_goals[i];
                        goal.m_level = kLevels[round % 4] == 0xB664035D ? 0xE8B4B836 : kLevels[round % 4];
                        goal.m_used = i < 9 + static_cast<int>(round % 3);
                        goal.m_numPoints = goal.m_minPoints;
                    }
                }
                if (kMembers[m] == 0x004E8C90 && round % 6 == 3) {
                    for (int i = 0; i < 50; ++i) { // (49 used, none of the level)
                        editor->m_goals[i].m_used = i != 7;
                        editor->m_goals[i].m_level = 0x0BADBAD0;
                    }
                }
                g_values.clear();
                uint32_t which = (round + static_cast<uint32_t>(m)) % 5;
                // How the goal is named: by id (found or not), by number, not at all (the current one).
                if (which == 0)
                    g_values[{params, 0x9982E501}] = Hash(round % 2 ? "CreatedGoal1" : "CreatedGoal77");
                else if (which == 1)
                    g_values[{params, 0x474A6A7F}] = round % 6;
                if (round % 3) {
                    g_values[{params, 0x3AC9AB5E}] = (round * 3) % 8 + 0x100;
                    g_values[{params, 0xEB7D6498}] = round % 8;
                    g_values[{params, 0xEDF5DB70}] = 540;
                    g_values[{params, 0xA4BEE6A1}] = 3;
                    g_values[{params, 0x687E2C25}] = 1;
                    g_values[{params, 0}] = round % 4 == 1 ? 0xDF47B144 : 77 + round;
                    g_values[{params, 0x651533EC}] = round % 4 == 2 ? 0xB664035D : kLevels[round % 4];
                    g_values[{params, 0xC6322A25}] = 1;
                    g_values[{params, 0x98439808}] = 1;
                    g_values[{params, Hash("type")}] = round % 4 ? 0x0FC6F698 : 0x54166ACD;
                    g_values[{params, Hash("text")}] = 1 + round % 3;
                }
                if (round % 4 != 3)
                    g_values[{params, 0xB99F6330}] = 1;
                // The goals' types for the flag and the unfinished goal.
                for (CEditedGoal& goal : editor->m_goals)
                    goal.m_type = (round / 2) % 2 ? 0x0FC6F698 : goal.m_type;
                for (int i = 0; i < 50; ++i) {
                    editor->m_goals[i].m_gaps[0] = static_cast<uint8_t>(i % 8);
                    editor->m_goals[i].m_numGaps = static_cast<uint8_t>(i % 3);
                }
                if (kMembers[m] == 0x004E8BB0 || kMembers[m] == 0x004E8C20) {
                    // (named goals only: a missing one is a null write)
                    g_values.erase({params, 0x9982E501});
                    g_values.erase({params, 0x474A6A7F});
                    if (round % 4 == 1)
                        g_values[{params, 0xBFECC45B}] = 4; // (a goal named "name1": usually there)
                    if (round % 2)
                        g_values[{params, Hash("Name")}] = 2;
                }
                uint32_t result = Fn<uint32_t(__thiscall*)(CGoalEditorComponent*, uint32_t, void*)>(
                                      original, kMembers[m])(editor, params, g_script) & 0xFF;
                trace.push_back(result);
                DumpEditor(trace);
            }
        }
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor: the member functions");
    g_relocate = nullptr;
    g_values.clear();
    g_elements.clear();
    delete &s;
}

} // namespace

SELFTEST(Early, TestGoalEditorMembers, RemoveStandIns);
SELFTEST(Early, TestGoalEditorComponent, RemoveStandIns);
