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
#include "game/obj/object_proximity.h"
#include "game/obj/pager_message.h"
#include "game/obj/objman.h"
#include "game/script/names.h"
#include "game/script/struct_buffer.h"
#include "game/skate/skate_module.h"
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
uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

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
                if (kMembers[m] == 0x004E8C90)
                    trace.push_back(0xC0DE0000u + round * 0x100u +
                                    static_cast<uint32_t>(Fn<int(__thiscall*)(CGoalEditorComponent*)>(original, 0x004E7E00)(editor)));
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

// The object proximity component: made, destroyed, its radii set (given or not, the attention radius's flag with
// none / one set / not a number), the local and the nearest skater's squared distance (no skater, the object
// itself, nearer / farther / equal / not a number).
void TestObjectProximity()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x007CE478, 4}, {0x007D1800, 4}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    StandInComponentWorld(s);
    static uint8_t object[0x60];
    static uint8_t skaters[4][0x60];
    static uint32_t nodes[5][4];
    static uint32_t local = 0;
    s.At({0x00566520, AddressOf(&Mdl::Skate::GetLocalSkater)}, 0, [](Regs& r) {
        Record({0x6520, r.ecx});
        return local;
    });
    s.At({0x005665C0, AddressOf(&Mdl::Skate::GetSkater)}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx, r.args[0]});
        return r.args[0] == 1 ? 0u : r.args[0] == 2 ? reinterpret_cast<uint32_t>(object)
                                                    : reinterpret_cast<uint32_t>(skaters[r.args[0] % 4]);
    });
    auto run = [&](bool original) {
        g_trace.clear();
        g_values.clear();
        GameVariable<uint8_t*>(0x007CE478) = g_skateObject;
        GameVariable<uint8_t*>(0x007D1800) = g_manager;
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004EC420, 0x004EC220, 0x004EC370, 0x004EC240, 0x004EC390, 0x004EC3C0,
                                   0x004EC4B0, 0x004EC250, 0x004EC280, 0x004EC2B0, 0x004EC2E0, 0x004EC310,
                                   0x004EC480});
        auto* component = reinterpret_cast<uint8_t*>(g_editorBytes[1]);
        uint32_t params = reinterpret_cast<uint32_t>(Token(0));
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__cdecl*)()>(original, 0x004EC420)())));
        Fn<void(__cdecl*)()>(original, 0x004EC220)();
        static const float kValues[] = {0.0f, 1.5f, -2.0f, 100.0f, 3.3333f};
        for (uint32_t round = 0; round < 40; ++round) {
            for (uint32_t i = 0; i < 0x30; ++i)
                component[i] = static_cast<uint8_t>(round * 7 + i);
            *reinterpret_cast<uint8_t**>(component + 0xC) = object;
            if (round % 5 == 1)
                *reinterpret_cast<float*>(component + 0x28) = 0.0f;
            if (round % 9 == 4)
                *reinterpret_cast<uint32_t*>(component + 0x28) = 0x7FC00000; // (not a number)
            g_values.clear();
            if (round % 4)
                g_values[{params, 0}] = Bits(kValues[round % 5]);
            if (round % 3)
                g_values[{params, 0xFD2EACAD}] = 1;
            uint32_t setter = 0x004EC250 + 0x30 * (round % 5);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, setter)(component, params,
                                                                                               g_script) & 0xFF);
            // The skaters: positions round the object, ids.
            for (uint32_t k = 0; k < 4; ++k) {
                float* pos = reinterpret_cast<float*>(skaters[k] + 0x4C);
                pos[0] = static_cast<float>((round * 13 + k * 31) % 50) - 25.0f;
                pos[1] = static_cast<float>((round + k) % 7) * 0.5f;
                pos[2] = static_cast<float>((round * 5 + k * 11) % 40) - 20.0f + (round % 2 ? 0.1f * k : 0.0f);
                *reinterpret_cast<uint32_t*>(skaters[k] + 0x14) = 0x500 + k + round;
            }
            if (round % 8 == 3) // (two equally near)
                std::memcpy(skaters[3] + 0x4C, skaters[0] + 0x4C, 12);
            if (round % 11 == 5)
                *reinterpret_cast<uint32_t*>(skaters[0] + 0x50) = 0x7FC00000;
            float* at = reinterpret_cast<float*>(object + 0x4C);
            at[0] = 1.0f + round, at[1] = 0.25f, at[2] = -3.0f;
            local = round % 4 == 0 ? 0u : round % 4 == 1 ? reinterpret_cast<uint32_t>(object)
                                                          : reinterpret_cast<uint32_t>(skaters[round % 4]);
            // The skater list: round % 5 entries.
            uint32_t count = round % 5;
            *reinterpret_cast<uint32_t*>(g_skateObject + 0x34) = reinterpret_cast<uint32_t>(nodes[0]);
            for (uint32_t k = 0; k < 5; ++k) {
                nodes[k][1] = k < count ? k : 0xFFFFFFFF;
                nodes[k][3] = k < 4 ? reinterpret_cast<uint32_t>(nodes[k + 1]) : 0;
            }
            if (round % 3 == 2 && local) {
                // (the attention radius at the local skater's distance: equal, or just under it once rounded)
                const float* p = reinterpret_cast<const float*>(reinterpret_cast<uint8_t*>(local) + 0x4C);
                double dx = static_cast<double>(p[0]) - at[0], dy = static_cast<double>(p[1]) - at[1],
                       dz = static_cast<double>(p[2]) - at[2];
                *reinterpret_cast<float*>(component + 0x28) = static_cast<float>((dx * dx + dz * dz) + dy * dy);
            }
            uint32_t id = 0xAAAA;
            double distance = Fn<double(__thiscall*)(void*, uint32_t*)>(original, 0x004EC3C0)(component, &id);
            uint32_t words[2];
            std::memcpy(words, &distance, 8);
            trace.insert(trace.end(), {words[0], words[1], id});
            id = 0xBBBB;
            trace.push_back(Bits(Fn<float(__thiscall*)(void*, uint32_t*)>(original, 0x004EC4B0)(component, &id)));
            trace.push_back(id);
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, 0x004EC480)(component, params,
                                                                                                  g_script) & 0xFF);
            for (uint32_t i = 0; i < 0x30; i += 4)
                trace.push_back(Rel(*reinterpret_cast<uint32_t*>(component + i)));
        }
        Fn<void(__thiscall*)(void*)>(original, 0x004EC240)(component);
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*, unsigned)>(original, 0x004EC390)(
            component, 1))));
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x004EC370)(component))));
        for (uint32_t i = 0; i < 0x30; i += 4)
            trace.push_back(Rel(*reinterpret_cast<uint32_t*>(component + i)));
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    UseEditorRelocation();
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor range: the object proximity component");
    g_relocate = nullptr;
    g_values.clear();
    delete &s;
}

// The proximity update's distances: scripted (the local one unrounded, as the original returns it).
double g_localDistance = 0.0;
float g_nearestDistance = 0.0f;
double __fastcall FakeLocalDistance(void* self, void*, uint32_t* id)
{
    Record({0xC3C0, Rel(reinterpret_cast<uint32_t>(self))});
    *id = 0x1D01;
    return g_localDistance;
}
float __fastcall FakeNearestDistance(void* self, void*, uint32_t* id)
{
    Record({0xC4B0, Rel(reinterpret_cast<uint32_t>(self))});
    *id = 0x1D02;
    return g_nearestDistance;
}

// The proximity component's update: every radius zero or not (not a number too), the distances inside / on /
// outside each radius, the object dead or not.
void TestObjectProximityUpdate()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x006F3614, 16}, {0x006F22B4, 16}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    StandInGoalIo(s);
    for (uint32_t at : {0x004EC3C0u, AddressOf(&Obj::CObjectProximityComponent::LocalSkaterDistanceSquared)})
        s.Bytes(at, Branch(0xE9, at, reinterpret_cast<const void*>(&FakeLocalDistance)));
    for (uint32_t at : {0x004EC4B0u, AddressOf(&Obj::CObjectProximityComponent::NearestSkaterDistanceSquared)})
        s.Bytes(at, Branch(0xE9, at, reinterpret_cast<const void*>(&FakeNearestDistance)));
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x09F0, r.ecx, r.args[0], Rel(r.args[1])});
        return 0u;
    });
    static uint8_t object[0x60];
    static const float kRadii[] = {0.0f, 100.0f, 400.0f, 25.5f, 0.0f};
    static const double kLocal[] = {50.0, 100.0, 100.00000001, 400.0, 3000.0, 25.5, 0.0};
    static const float kNearest[] = {50.0f, 100.0f, 400.0f, 1e9f, 0.0f, 25.5f};
    auto run = [&](bool original) {
        g_trace.clear();
        UseFakePools();
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004EC560});
        auto* component = reinterpret_cast<uint8_t*>(g_editorBytes[1]);
        Lcg lcg(0x9A6E);
        for (uint32_t round = 0; round < 300; ++round) {
            *reinterpret_cast<uint8_t**>(component + 0xC) = object;
            *reinterpret_cast<uint32_t*>(object + 0x08) = lcg.Below(4) == 0 ? 1u : 0x20u;
            for (uint32_t k = 0; k < 5; ++k)
                *reinterpret_cast<float*>(component + 0x18 + 4 * k) = lcg.Pick(kRadii);
            if (round % 37 == 5)
                *reinterpret_cast<uint32_t*>(component + 0x18 + 4 * (round % 5)) = 0x7FC00000;
            g_localDistance = lcg.Pick(kLocal);
            g_nearestDistance = lcg.Pick(kNearest);
            Fn<void(__thiscall*)(void*)>(original, 0x004EC560)(component);
        }
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    UseEditorRelocation();
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor range: the object proximity component's update");
    g_relocate = nullptr;
    delete &s;
}

// The pager: made (its list), messages sent (flagged or not), read by number (in range, past the end), counted,
// the first unread found (none, some, all read), cleared down to a count or all, destroyed; the list's erase.
void TestPagerMessages()
{
    std::unique_ptr<GameMemory> memory(new GameMemory({{0x006F3614, 16}, {0x006F22B4, 16}, {0x007D1800, 4},
                                                       {0x007CC979, 1}}));
    StandIns& s = *new StandIns();
    StandInGoalWorld(s);
    StandInGoalIo(s);
    StandInComponentWorld(s);
    using Script::CStruct;
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        uint8_t* block = g_blocks[g_nextBlock++ % 512];
        std::memset(block, 0xCD, 16);
        return reinterpret_cast<uint32_t>(block);
    });
    s.At({0x004964A0}, 8, [](Regs& r) {
        Record({0x64A0, Rel(r.ecx), r.args[0], *reinterpret_cast<uint32_t*>(r.args[1])});
        auto* list = reinterpret_cast<Obj::PointerList*>(r.ecx);
        auto* where = reinterpret_cast<Obj::PointerListNode*>(r.args[0]);
        auto* node = reinterpret_cast<Obj::PointerListNode*>(g_blocks[g_nextBlock++ % 512]);
        node->data = *reinterpret_cast<void**>(r.args[1]);
        node->next = where;
        node->prev = where->prev;
        where->prev->next = node;
        where->prev = node;
        ++list->size;
        return 0u;
    });
    s.At({0x00474190, AddressOf(Script::KillSpawnedScriptsWithObjectAndName)}, 0, [](Regs& r) {
        Record({0x4190, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00461410, AddressOf(&Obj::CObject::SpawnScript)}, 16, [](Regs& r) {
        Record({0x1410, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
    s.At({0x00478480, AddressOf(&CStruct::AppendStructure)}, 4, [](Regs& r) {
        Record({0x8480, r.ecx, r.args[0]});
        return r.ecx;
    });
    s.At({0x00478670, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructure))}, 8,
         [](Regs& r) {
             Record({0x8670, r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    auto run = [&](bool original) {
        g_trace.clear();
        g_values.clear();
        UseFakePools();
        GameVariable<uint8_t*>(0x007D1800) = g_manager;
        GameVariable<bool>(0x007CC979) = true;
        *reinterpret_cast<uint32_t*>(g_script + 0x14) = reinterpret_cast<uint32_t>(Token(7));
        std::vector<uint32_t> trace;
        Originals guard(original, {0x004ECC30, 0x004ECF20, 0x004EC960, 0x004ECE10, 0x004ECE90, 0x004ECF80,
                                   0x004ECC80, 0x004EC9A0, 0x004ECB50, 0x004EC980, 0x004ECBD0, 0x004ECDF0});
        uint32_t params = reinterpret_cast<uint32_t>(Token(0));
        auto* pager = reinterpret_cast<uint8_t*>(g_editorBytes[1]);
        Fn<void(__cdecl*)()>(original, 0x004EC960)();
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__cdecl*)()>(original, 0x004ECF20)())));
        *reinterpret_cast<uint32_t*>(pager + 0xC) = reinterpret_cast<uint32_t>(Token(15)); // the object
        auto member = [&](uint32_t address) {
            trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, void*)>(original, address)(pager, params,
                                                                                               g_script) & 0xFF);
        };
        auto messages = [&]() {
            auto* list = reinterpret_cast<Obj::PointerList*>(pager + 0x18);
            trace.push_back(static_cast<uint32_t>(list->size));
            Obj::PointerListNode* node = list->head->next;
            for (int i = 0; i < list->size && node != list->head; ++i, node = node->next) {
                auto* message = static_cast<Obj::PagerMessage*>(node->data);
                trace.insert(trace.end(), {reinterpret_cast<uint32_t>(message), message ? message->read : 0xFFu});
            }
        };
        for (uint32_t round = 0; round < 30; ++round) {
            g_values.clear();
            if (round % 3 == 0) // (the message's parameters: the block after the message's)
                g_values[{reinterpret_cast<uint32_t>(g_blocks[(g_nextBlock + 1) % 512]), 0x8E6DFA5D}] = 1;
            member(0x004EC9A0); // send
            // (the message's own parameters: the block after the message; flagged in some rounds)
            member(0x004EC980);
            // (a number past the end reads the sentinel: only numbers up to the count, 0 too)
            int count = reinterpret_cast<Obj::PointerList*>(pager + 0x18)->size;
            g_values[{params, 0x0A89DA7D}] = static_cast<uint32_t>(round % 5 == 4 ? count : static_cast<int>(round % 4) % (count + 1));
            if (round % 6 == 1)
                g_values.erase({params, 0x0A89DA7D});
            if (count)
                member(0x004ECB50);
            member(0x004ECBD0);
            if (round % 7 == 6) {
                g_values[{params, 0x340FB7F4}] = round % 2 ? 2u : 0u;
                if (round % 14 == 13)
                    g_values.erase({params, 0x340FB7F4});
                member(0x004ECDF0);
            }
            messages();
        }
        // Every message read, then the first unread; erase of part of the list; destroyed.
        auto* list = reinterpret_cast<Obj::PointerList*>(pager + 0x18);
        for (Obj::PointerListNode* node = list->head->next; node != list->head; node = node->next)
            static_cast<Obj::PagerMessage*>(node->data)->read = true;
        member(0x004ECBD0);
        for (int k = 0; k < 6; ++k)
            member(0x004EC9A0);
        messages();
        Obj::PointerListNode* out = nullptr;
        Obj::PointerListNode* first = list->head->next->next;
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*, void*, void*, void*)>(
            original, 0x004ECC30)(list, &out, first, list->head->prev))));
        trace.push_back(reinterpret_cast<uint32_t>(out));
        messages();
        // (a range across the sentinel: it stays)
        Fn<void*(__thiscall*)(void*, void*, void*, void*)>(original, 0x004ECC30)(list, &out, list->head->prev,
                                                                                 list->head->next);
        trace.push_back(reinterpret_cast<uint32_t>(out));
        trace.push_back(reinterpret_cast<uint32_t>(list->head->next));
        trace.push_back(reinterpret_cast<uint32_t>(list->head->prev));
        messages();
        Fn<void(__thiscall*)(void*)>(original, 0x004ECE90)(pager);
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*)>(original, 0x004ECE10)(pager))));
        trace.push_back(Rel(reinterpret_cast<uint32_t>(Fn<void*(__thiscall*)(void*, unsigned)>(original, 0x004ECF80)(pager, 1))));
        trace.insert(trace.end(), g_trace.begin(), g_trace.end());
        return trace;
    };
    UseEditorRelocation();
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, "goal editor range: the pager message component");
    g_relocate = nullptr;
    g_values.clear();
    delete &s;
}

} // namespace

SELFTEST(Early, TestPagerMessages, RemoveStandIns);
SELFTEST(Early, TestObjectProximityUpdate, RemoveStandIns);
SELFTEST(Early, TestObjectProximity, RemoveStandIns);
SELFTEST(Early, TestGoalEditorMembers, RemoveStandIns);
SELFTEST(Early, TestGoalEditorComponent, RemoveStandIns);
