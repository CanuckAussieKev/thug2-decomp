#pragma once

// Shared pieces of the self-tests for the goal editor (src/game/ed/edited_goal.cpp, goal_editor*.cpp;
// selftests_goal_editor*.cpp): recording stand-ins for the script structures and arrays the code reads and writes
// (their values scripted per structure and name), the scripts it runs, the node lookups, the C runtime's sprintf
// and the checksum of a name. Structures are tokens (static addresses, the same in both runs); pointers into the
// goals and the stack are made comparable by g_relocate.

#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/ed/goal_editor.h"
#include "game/ed/goal_editor_util.h"
#include "game/ed/park_editor.h"
#include "game/core/pool.h"
#include "game/goals/goal.h"
#include "game/goals/goal_manager.h"
#include "game/nx/feeler.h"
#include "game/script/cscript.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/appmain_testkit.h"
#include "hook/standins.h"

namespace GoalEditorTests {

using namespace AppMainTests;

// Token structures and arrays (only their addresses matter: every method is stood in for).
inline uint8_t g_structs[16][16];
inline Script::CStruct* Token(int i) { return reinterpret_cast<Script::CStruct*>(g_structs[i]); }
// Arrays: a real header (the code reads m_size) at a token address.
inline Script::CArray g_arrays[4];

// The scripted answers: (structure, name) -> value; present = found.
inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_values;
inline std::map<std::pair<uint32_t, uint32_t>, uint32_t> g_elements; // (array, index) -> element
inline std::map<uint32_t, float> g_globalFloats;                      // Script::GetFloat
inline uint32_t g_insidePark = 1;
inline uint32_t g_typeStructure = 1; // GoalTypeStructure answers (0: none)
inline const char* const kModels[] = {"m0", "m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "m9", "m10", "m11"};
inline const char* const kStrings[] = {"", "first string", "a much longer second string", "trick", "name1"};

inline uint32_t Rel(uint32_t word) { return Relocate(word); }

inline bool Lookup(uint32_t structure, uint32_t name, uint32_t& value)
{
    auto it = g_values.find({structure, name});
    if (it == g_values.end())
        return false;
    value = it->second;
    return true;
}

// Script::GetFloat (a float in ST0: a plain function a jump is written to).
inline float __cdecl FakeGetFloat(uint32_t name)
{
    Record({0x8F00, name});
    auto it = g_globalFloats.find(name);
    return it == g_globalFloats.end() ? 0.0f : it->second;
}

// Every stand-in the goal editor's code needs.
inline void StandInGoalWorld(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, Rel(r.ecx)});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 8);
        return r.ecx;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) { Record({0x7D40, Rel(r.ecx), r.args[0], r.args[1]}); return 0u; });
    s.At({0x00477DF0, AddressOf(static_cast<void (CStruct::*)(const char*, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0x7DF0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
             return 0u;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Record({0x79D0, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8,
         [](Regs& r) { Record({0x7B80, Rel(r.ecx), r.args[0], r.args[1]}); return 0u; });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8,
         [](Regs& r) { Record({0x7C60, Rel(r.ecx), r.args[0], r.args[1]}); return 0u; });
    s.At({0x00477E20, AddressOf(static_cast<void (CStruct::*)(uint32_t, float, float, float)>(&CStruct::AddVector))},
         16, [](Regs& r) { Record({0x7E20, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]}); return 0u; });
    s.At({0x00477F40,
          AddressOf(static_cast<void (CStruct::*)(const char*, float, float, float)>(&CStruct::AddVector))},
         16, [](Regs& r) {
             Record({0x7F40, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1], r.args[2],
                     r.args[3]});
             return 0u;
         });
    s.At({0x004780D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArrayPointer))},
         8, [](Regs& r) { Record({0x80D0, Rel(r.ecx), r.args[0], Rel(r.args[1])}); return 0u; });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             Record({0x6630, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, r.args[0], value))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = value;
             return 1u;
         });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x66D0, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, r.args[0], value))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = value;
             return 1u;
         });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6950, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, r.args[0], value))
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = value;
             return 1u;
         });
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))},
         12, [](Regs& r) {
             Record({0x6590, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, r.args[0], value))
                 return 0u;
             *reinterpret_cast<const char**>(r.args[1]) = kStrings[value];
             return 1u;
         });
    s.At({0x00476860,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [](Regs& r) {
             Record({0x6860, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, r.args[0], value))
                 return 0u;
             *reinterpret_cast<Script::CArray**>(r.args[1]) = &g_arrays[value];
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        uint32_t value;
        return Lookup(r.ecx, r.args[0], value) ? 1u : 0u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0xCC30, Rel(r.ecx), r.args[0]});
        auto it = g_elements.find({r.ecx, r.args[0]});
        return it == g_elements.end() ? 0xEEEE0000u + r.args[0] : it->second;
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Record({0x5790, r.args[0], Rel(r.args[1]), Rel(r.args[2]), r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x006234FE}, 0, [](Regs& r) {
        uint32_t result = StandInSprintf(r);
        Record({0x34FE, Rel(r.args[0]), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return result;
    });
    s.At({0x00472E60, AddressOf(Crc::GenerateCRCFromString)}, 0, [](Regs& r) {
        uint32_t hash = Hash(reinterpret_cast<const char*>(r.args[0]));
        Record({0x2E60, hash});
        return hash;
    });
    s.At({0x00401C30, AddressOf(Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.Bytes(0x00478F00, Branch(0xE9, 0x00478F00, reinterpret_cast<const void*>(&FakeGetFloat)));
    {
        uint32_t ours = AddressOf(static_cast<float (*)(uint32_t)>(Script::GetFloat));
        s.Bytes(ours, Branch(0xE9, ours, reinterpret_cast<const void*>(&FakeGetFloat)));
    }
    s.At({0x0045C1B0, AddressOf(SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
        return reinterpret_cast<uint32_t>(Token(8 + (r.args[0] & 3)));
    });
    s.At({0x0058DEC0, AddressOf(Ed::CParkEditor::IsInsidePark)}, 0, [](Regs& r) {
        Record({0xDEC0, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return g_insidePark;
    });
    s.At({0x004E67A0, AddressOf(Ed::GoalTypeStructure)}, 0, [](Regs& r) {
        Record({0x67A0, r.ecx});
        return g_typeStructure && r.ecx != 0x7FE2238A ? reinterpret_cast<uint32_t>(Token(12)) : 0u;
    });
    s.At({0x004E6600, AddressOf(Ed::GoalObjectModel)}, 0, [](Regs& r) {
        Record({0x6600, r.ecx, r.edx});
        return r.edx < 12 ? reinterpret_cast<uint32_t>(kModels[r.edx]) : 0u;
    });
    s.At({0x004E66A0, AddressOf(Ed::GoalObjectName)}, 0, [](Regs& r) {
        Record({0x66A0, r.ecx, r.edx});
        return r.ecx ^ (r.edx * 0x01000193u);
    });
    s.At({0x004E6AE0, AddressOf(Ed::RunMarkerObjectScript)}, 4, [](Regs& r) {
        Record({0x6AE0, r.args[0]});
        return 0u;
    });
    s.At({0x004E69D0, AddressOf(Ed::InitEditedGoal)}, 0, [](Regs& r) {
        Record({0x69D0, Rel(r.ecx)});
        // (a visible effect: the goal marked)
        // (and a goal of bytes 01 becomes a park editor goal - the component's constructor's clearing seen)
        uint32_t& level = reinterpret_cast<Ed::CEditedGoal*>(r.ecx)->m_level;
        level = level == 0x01010101 ? 0xE8B4B836 : 0x1A17;
        return 0u;
    });
}

// Allocations (the structure and array pools' items): static blocks handed out in order, the same in both runs.
inline uint8_t g_blocks[512][16];
inline uint32_t g_nextBlock = 0;
inline uint8_t g_fakePool[16];
inline uint32_t g_hit = 1;                   // the feeler's answers, a bit per call
inline uint32_t g_feelerCalls = 0;
inline uint8_t g_goalObject[0x70];           // a stand-in Game::CGoal (its vtable below)
inline void* g_goalVtable[40];
inline uint8_t g_goalManager[16];

// The stand-ins for the goals' structure / array building, the feeler and the goal manager.
inline void StandInGoalIo(StandIns& s)
{
    using Script::CArray;
    using Script::CStruct;
    s.At({0x005BA3D0, AddressOf(&Mem::Pool::Allocate)}, 0, [](Regs& r) {
        Record({0xA3D0, r.ecx == reinterpret_cast<uint32_t>(g_fakePool) ? 1u : r.ecx});
        uint8_t* block = g_blocks[g_nextBlock++ % 512];
        std::memset(block, 0xCD, 16);
        return reinterpret_cast<uint32_t>(block);
    });
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        Record({0xA3B0, r.args[0]});
        return 1u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, r.args[0]});
        return 0u;
    });
    s.At({0x0046CAA0, AddressOf(&CArray::Init)}, 0, [](Regs& r) {
        Record({0xCAA0, r.ecx});
        std::memset(reinterpret_cast<void*>(r.ecx), 0, 12);
        return r.ecx;
    });
    s.At({0x0046CC50, AddressOf(&CArray::SetSizeAndType)}, 8, [](Regs& r) {
        Record({0xCC50, r.ecx, r.args[0], r.args[1] & 0xFF});
        reinterpret_cast<CArray*>(r.ecx)->m_size = r.args[0];
        return 0u;
    });
    s.At({0x0046CB10, AddressOf(&CArray::SetElement)}, 8, [](Regs& r) {
        Record({0xCB10, r.ecx, r.args[0], Rel(r.args[1])});
        return 0u;
    });
    s.At({0x00476FB0,
          AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::VectorValue*, int)>(&CStruct::GetVector))},
         12, [](Regs& r) {
             Record({0x6FB0, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             uint32_t value;
             if (!Lookup(r.ecx, r.args[0], value))
                 return 0u;
             auto* out = reinterpret_cast<float*>(r.args[1]);
             out[0] = static_cast<float>(value);
             out[1] = 2.5f;
             out[2] = -static_cast<float>(value);
             return 1u;
         });
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return reinterpret_cast<uint32_t>(&g_arrays[r.args[0] == 0x8070C110 ? 3 : 0]);
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(Script::GetString))}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        return reinterpret_cast<uint32_t>(r.args[0] == 0x3693BFE4 ? "combo %d!" : "score %d points");
    });
    s.At({0x00478A60, AddressOf(Script::CopyArray)}, 0, [](Regs& r) {
        Record({0x8A60, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004E6890}, 0, [](Regs& r) {
        Record({0x6890, r.eax});
        return reinterpret_cast<uint32_t>(Token(13));
    });
    s.At({AddressOf(Ed::GoalTypeDefaults)}, 0, [](Regs& r) {
        Record({0x6890, r.args[0]});
        return reinterpret_cast<uint32_t>(Token(13));
    });
    s.At({0x004E6730, AddressOf(Ed::EditedGoalNodeName)}, 0, [](Regs& r) {
        Record({0x6730, r.ecx, r.edx});
        return r.ecx * 0x100 + r.edx;
    });
    s.At({0x00417760, AddressOf(static_cast<Nx::CFeeler* (Nx::CFeeler::*)()>(&Nx::CFeeler::Construct))}, 0,
         [](Regs& r) {
             Record({0x7760, Rel(r.ecx)});
             std::memset(reinterpret_cast<void*>(r.ecx), 0, 0xB0);
             return r.ecx;
         });
    s.At({0x00417190, AddressOf(&Nx::CFeeler::SetStart)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x7190, Rel(r.ecx), v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x004171B0, AddressOf(&Nx::CFeeler::SetEnd)}, 4, [](Regs& r) {
        const auto* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x71B0, Rel(r.ecx), v[0], v[1], v[2], v[3]});
        return 0u;
    });
    s.At({0x004172F0, AddressOf(&Nx::CFeeler::GetCollision)}, 8, [](Regs& r) {
        Record({0x72F0, Rel(r.ecx), r.args[0] & 0xFF, r.args[1] & 0xFF});
        uint32_t hit = (g_hit >> (g_feelerCalls++ % 32)) & 1;
        if (hit) {
            auto* point = reinterpret_cast<float*>(r.ecx + 0x78);
            point[0] = 1.5f * g_feelerCalls;
            point[1] = -3.0f;
            point[2] = 7.25f;
            point[3] = 1.0f;
        }
        return hit;
    });
    s.At({0x005591B0, AddressOf(Game::GetGoalManager)}, 0, [](Regs&) {
        Record({0x91B0});
        return reinterpret_cast<uint32_t>(g_goalManager);
    });
    s.At({0x0055BA10, AddressOf(&Game::CGoalManager::AddGoal)}, 8, [](Regs& r) {
        Record({0xBA10, r.ecx, r.args[0], Rel(r.args[1])});
        return 1u;
    });
    s.At({0x00557F90, AddressOf(&Game::CGoalManager::GetGoal)}, 8, [](Regs& r) {
        Record({0x7F90, r.ecx, r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>(g_goalObject);
    });
    s.At({0x00554430, AddressOf(&Game::CGoal::SetHasSeen)}, 0, [](Regs& r) {
        Record({0x4430, r.ecx});
        return 0u;
    });
    void* markWon = s.Thunk(0, [](Regs& r) {
        Record({0x55E0, r.ecx});
        return 0u;
    });
    for (void*& slot : g_goalVtable)
        slot = markWon;
    *reinterpret_cast<void***>(g_goalObject) = g_goalVtable;
}

// The pools' current items come from the stand-in allocator (their pools are a token).
inline void UseFakePools()
{
    g_nextBlock = 0;
    g_feelerCalls = 0;
    GameVariable<int>(0x006F3614) = 0;
    GameVariable<void*>(0x006F361C) = g_fakePool;
    GameVariable<int>(0x006F22B4) = 0;
    GameVariable<void*>(0x006F22BC) = g_fakePool;
}

// A deterministic fill of a goal: every byte from the seed, then the counts kept in range.
inline void FillGoal(Ed::CEditedGoal& goal, uint32_t seed)
{
    auto* bytes = reinterpret_cast<uint8_t*>(&goal);
    for (size_t i = 0; i < sizeof(goal); ++i) {
        seed = seed * 1103515245u + 12345u;
        bytes[i] = static_cast<uint8_t>(seed >> 16);
    }
    goal.m_numGaps = static_cast<uint8_t>(seed % 11);
    goal.m_numKeyComboSets = static_cast<int>((seed >> 4) % 11);
    goal.m_numPoints = static_cast<uint8_t>((seed >> 8) % 8);
    goal.m_currentPoint = static_cast<uint8_t>((seed >> 12) % 8);
    for (Ed::EditedGoalPoint& point : goal.m_points) {
        point.pos = {static_cast<float>(seed % 97), static_cast<float>((seed >> 3) % 89) * 0.5f,
                     static_cast<float>((seed >> 5) % 83) - 40.0f, 1.0f};
        point.height = static_cast<float>((seed >> 7) % 13);
        point.angle = static_cast<float>((seed >> 9) % 7) * 0.25f;
        point.kind = static_cast<int>(1 + (seed >> 11) % 4);
        seed = seed * 1103515245u + 12345u;
    }
}

} // namespace GoalEditorTests
