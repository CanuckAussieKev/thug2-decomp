// Self-tests for the CAS option lists (src/game/skate/cas_options.cpp) and the models' and animations' helpers
// (src/game/gfx/model_utils.cpp) - batch CAS3-3: each original against ours on hand-built option lists and parameter
// structures, the global array lookup, the random numbers, the appearance's part lookup, the composite objects'
// components, the models' Hide, the debug output and the memory stood in; the calls, results and memory compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/string.h"
#include "game/gfx/model.h"
#include "game/gfx/model_utils.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skate/cas_options.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Script::CArray;

constexpr uint32_t kDescId = 0x4BB2084E, kAllowedToPick = 0x355F9467, kFilter = 0x0D7260FD, kNoRandom = 0xF6F8E158,
                   kAllowRandom = 0xF1E3CD22, kAlreadySelected = 0x92BDDFD9, kRandomIndex = 0x4B833E64;

// The option list (a global array 0x0A110000 of up to 12 structures).
Params g_options[12];
uint32_t g_optionPointers[12];
CArray g_list;
uint32_t g_rnd; // (Rnd's answer: n - 1 - this, kept in range)
Params g_parts[2];

// The composite objects: components of the model type in a chain, their objects and models.
struct FakeComponent {
    uint8_t bytes[0x20C];
};
alignas(16) FakeComponent g_components[4];
alignas(16) uint8_t g_compObjects[4][0x20];
alignas(16) uint8_t g_models[4][0x48];
uint8_t g_manager[0x10];
alignas(16) uint32_t g_fakeVtable[8];
void* g_slotVtable[8];

uint32_t Rel(uint32_t address)
{
    for (uint32_t i = 0; i < 12; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_options[i].Struct()))
            return 0x0F000000u + i;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_models[i]))
            return 0x0D000000u + i;
        if (address == reinterpret_cast<uint32_t>(&g_components[i]))
            return 0x0C000000u + i;
    }
    return address;
}

void SetList(int count, std::initializer_list<int> order = {})
{
    std::memset(&g_list, 0, sizeof(g_list));
    g_list.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    g_list.m_size = static_cast<uint32_t>(count);
    for (int i = 0; i < count; ++i)
        g_optionPointers[i] = reinterpret_cast<uint32_t>(g_options[i].Struct());
    if (order.size()) {
        g_list.m_size = static_cast<uint32_t>(order.size());
        int i = 0;
        for (int which : order)
            g_optionPointers[i++] = reinterpret_cast<uint32_t>(g_options[which].Struct());
    }
    g_list.m_elements = g_optionPointers;
}

void StandInOptionCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    s.At({0x00479070, AddressOf(static_cast<CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x9070, r.args[0]});
        return r.args[0] == 0x0A110000 ? reinterpret_cast<uint32_t>(&g_list) : 0u;
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        Record({0x3DE0, r.args[0]});
        int n = static_cast<int>(r.args[0]);
        return n <= 0 ? 0u : static_cast<uint32_t>((n - 1 - static_cast<int>(g_rnd % static_cast<uint32_t>(n))));
    });
    s.At({0x004890B0}, 4, [](Regs& r) {
        Record({0x90B0, r.ecx, r.args[0]});
        return r.args[0] < 2 ? reinterpret_cast<uint32_t>(g_parts[r.args[0]].Struct()) : 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf); // (the game's sprintf, for the original)
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Record({0xF2D0, r.ecx == reinterpret_cast<uint32_t>(g_manager) ? 1u : r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(&g_components[0]);
    });
    s.At({0x00494B70, AddressOf(&Nx::CModel::Hide)}, 4, [](Regs& r) {
        Record({0x4B70, Rel(r.ecx), r.args[0] & 0xFF});
        return 1u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        uint32_t i = r.args[0] - 0x0B0;
        return i < 4 && i != 2 ? reinterpret_cast<uint32_t>(g_compObjects[i]) : 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        for (uint32_t i = 0; i < 4; ++i) {
            if (r.ecx == reinterpret_cast<uint32_t>(g_compObjects[i])) {
                Record({0xDB90, i, r.args[0]});
                return reinterpret_cast<uint32_t>(&g_components[i]);
            }
        }
        return 0u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
        return 0u;
    });
    g_slotVtable[4] = s.Thunk(8, [](Regs& r) {
        Record({0x5104, r.ecx, r.args[0], r.args[1]});
        return 0x4444u;
    });
}

// The components: object ids 0xB0..0xB3 (the second one's id 0), the third's model hidden already, chained in order.
void SetComponents()
{
    std::memset(g_components, 0, sizeof(g_components));
    for (uint32_t i = 0; i < 4; ++i) {
        std::memset(g_compObjects[i], 0, sizeof(g_compObjects[i]));
        std::memset(g_models[i], 0, sizeof(g_models[i]));
        uint8_t* c = g_components[i].bytes;
        *reinterpret_cast<uint8_t**>(c + 0xC) = g_compObjects[i];
        *reinterpret_cast<FakeComponent**>(c + 0x14) = i < 3 ? &g_components[i + 1] : nullptr;
        *reinterpret_cast<uint8_t**>(c + 0x208) = g_models[i];
        *reinterpret_cast<uint32_t*>(g_compObjects[i] + 0x14) = i == 1 ? 0u : 0xB0u + i;
        g_models[i][0x44] = i == 2 ? 0x05 : 0x02;
    }
}

uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

void CallSlot4(void* code, void* object, uint32_t a, uint32_t b)
{
    __asm {
        push b
        push a
        mov ecx, object
        call code
    }
}

void TestCasOptionsAndModelUtils()
{
    Setup();
    g_saved.push_back({0x006F0FB8, 4, {}});
    g_saved.push_back({0x006FA2F0, 8, {}});
    g_saved.push_back({0x007CC979, 1, {}});
    for (size_t i = g_saved.size() - 3; i < g_saved.size(); ++i) {
        g_saved[i].bytes.resize(g_saved[i].size);
        std::memcpy(g_saved[i].bytes.data(), reinterpret_cast<void*>(g_saved[i].address), g_saved[i].size);
    }
    auto* standIns = new StandIns;
    StandInOptionCalls(*standIns);
    GameVariable<uint8_t*>(0x006F0FB8) = g_manager;
    GameVariable<bool>(0x007CC979) = true;

    // The options (asymmetric): desc_ids (one missing), allowed_to_pick, filters, no_random / allow_random,
    // already_selected, random_index
    for (int i = 0; i < 12; ++i) {
        Params& p = g_options[i];
        if (p.count)
            continue;
        if (i != 4)
            p.Checksum(kDescId, 0xDE50 + i * 3);
        if (i % 3 != 1)
            p.Integer(kAllowedToPick, i % 4 == 0 ? 2 : 1);
        if (i % 4 != 3)
            p.Checksum(kFilter, i % 2 ? 0xF1 : 0xF2);
        if (i == 2 || i == 7)
            p.Flag(kNoRandom);
        if (i == 7 || i == 9)
            p.Integer(kAllowRandom, i == 7 ? 1 : 0);
        p.Integer(kAlreadySelected, i % 5 == 0 ? 1 : 0);
        if (i != 6)
            p.Integer(kRandomIndex, 11 - i);
    }
    if (!g_parts[0].count) {
        g_parts[0].Checksum(kDescId, 0x79DAA2F9);
        g_parts[1].Checksum(kDescId, 0x79DAA2F8);
    }

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        auto rel = [](uint32_t value) { return Rel(value); };

        // The CAS options
        {
            for (uint32_t part : {0u, 1u, 2u}) {
                bool r = original ? Original<bool(__cdecl*)(void*, uint32_t)>(0x00485CB0)(g_manager, part)
                                  : Obj::PartIsCreatedGraphic(g_manager, part);
                Record({0xCB0, r ? 1u : 0u});
            }
            SetList(12);
            for (uint32_t desc : {0u, 0xDE50u, 0xDE50u + 9, 0xDE50u + 12, 0xDE50u + 15, 0xDE50u + 99}) {
                Script::CStruct* r = original
                                         ? Original<Script::CStruct*(__cdecl*)(uint32_t, uint32_t, bool)>(0x00485CF0)(
                                               0x0A110000, desc, true)
                                         : Obj::FindCasOption(0x0A110000, desc, true);
                Record({0xCF0, rel(reinterpret_cast<uint32_t>(r))});
            }
            SetList(0, {4, 5, 6}); // (the first without a desc_id: it has the one searched for)
            for (uint32_t desc : {0xDE50u + 18, 0xDE50u + 99}) {
                Script::CStruct* r = original
                                         ? Original<Script::CStruct*(__cdecl*)(uint32_t, uint32_t, bool)>(0x00485CF0)(
                                               0x0A110000, desc, true)
                                         : Obj::FindCasOption(0x0A110000, desc, true);
                Record({0xCF1, rel(reinterpret_cast<uint32_t>(r))});
            }
            SetList(12);
            for (uint32_t filter : {0u, 0xF1u, 0xF2u, 0xF3u}) {
                Script::CStruct* out[16];
                std::memset(out, 0, sizeof(out));
                int count = -5;
                if (original)
                    Original<void(__cdecl*)(uint32_t, uint32_t, Script::CStruct**, int*)>(0x00485D70)(0x0A110000, filter,
                                                                                                      out, &count);
                else
                    Obj::GetMatchingCasOptions(0x0A110000, filter, out, &count);
                Record({0xD70, static_cast<uint32_t>(count)});
                for (auto* o : out)
                    Record({rel(reinterpret_cast<uint32_t>(o))});
            }
            for (int round = 0; round < 9; ++round) {
                g_rnd = static_cast<uint32_t>(round * 3);
                SetList(round == 7 ? 3 : 12);
                if (round == 8)
                    SetList(0, {0, 5, 10}); // (every one already selected: all of them again)
                uint32_t filter = round % 3 == 0 ? 0u : round % 3 == 1 ? 0xF1u : 0xF2u;
                Script::CStruct* r = original ? Original<Script::CStruct*(__cdecl*)(uint32_t, uint32_t)>(0x00485E20)(
                                                    0x0A110000, filter)
                                              : Obj::PickRandomCasOption(0x0A110000, filter);
                Record({0xE20, rel(reinterpret_cast<uint32_t>(r))});
            }
            SetList(12);
            for (int index : {0, 3, 11, 12, -1, 6}) {
                for (uint32_t filter : {0u, 0xF1u, 0xF2u}) {
                    Script::CStruct* r = original
                                             ? Original<Script::CStruct*(__cdecl*)(uint32_t, int, uint32_t)>(0x00485FC0)(
                                                   0x0A110000, index, filter)
                                             : Obj::GetCasOptionByIndex(0x0A110000, index, filter);
                    Record({0xFC0, rel(reinterpret_cast<uint32_t>(r))});
                }
            }
        }
        // The animation helpers
        {
            const uint32_t flags[] = {0, 0x4F792E6C, 0x3153E314, 0x6D941203, 0x12345};
            for (int k = 0; k < 6; ++k) {
                Params p;
                if (k < 5)
                    p.Flag(flags[k]);
                if (k == 5)
                    p.Flag(flags[3]).Flag(flags[2]);
                int loop = -9;
                bool r = original ? Original<bool(__cdecl*)(int*, Script::CStruct*)>(0x00487770)(&loop, p.Struct())
                                  : Gfx::GetAnimLoopType(&loop, p.Struct());
                Record({0x7770, r ? 1u : 0u, static_cast<uint32_t>(loop)});
            }
            const uint32_t names[] = {0x6086AA70, 0x617FE530, 0x230CCBF4, 0xFF03CC4E, 0x5555};
            for (int k = 0; k < 14; ++k) {
                Params p;
                if (k < 5)
                    p.Checksum(0x46E55E8F, names[k]).Checksum(0x28782D3B, names[4 - k]);
                if (k == 5)
                    p.Float(0x46E55E8F, 37.9f).Float(0x28782D3B, -2.5f);
                if (k == 6)
                    p.Float(0x46E55E8F, 1.25f).Integer(0x28782D3B, 3).Flag(0xD029F619);
                if (k == 7)
                    p.Checksum(0x46E55E8F, names[1]).Integer(0x46E55E8F, 90).Flag(0xF8CFD515);
                if (k == 8)
                    p.Checksum(0x28782D3B, names[2]).Flag(0xF8CFD515);
                if (k == 9)
                    p.Float(0x28782D3B, 1e10f);
                if (k >= 10)
                    p.Float(0x46E55E8F, k * 7.77f).Checksum(0x28782D3B, names[k - 10]);
                float start = -1.0f, end = -1.0f;
                float current = 0.3f + k, duration = 9.75f - k;
                bool r = original ? Original<bool(__cdecl*)(float*, float*, float, float, Script::CStruct*, int)>(
                                        0x004877E0)(&start, &end, current, duration, p.Struct(), 0)
                                  : Gfx::GetAnimTimes(&start, &end, current, duration, p.Struct());
                Record({0x77E0, r ? 1u : 0u, Bits(start), Bits(end)});
            }
        }
        // The model paths
        for (const char* name : {"skater", "peds/ped_1", "veh\\car", "a"}) {
            char out[300];
            std::memset(out, 0x11, sizeof(out));
            if (original)
                Original<void(__cdecl*)(const char*, const char*, char*)>(0x00487A70)(name, ".mdl", out);
            else
                Gfx::MakeModelFileName(name, ".mdl", out);
            Record({0x7A70, Hash(out)});
            alignas(4) uint8_t string[sizeof(Str::String)];
            std::memset(string, 0x22, sizeof(string));
            auto* path = reinterpret_cast<Str::String*>(string);
            Str::String* r = original
                                 ? Original<Str::String*(__cdecl*)(Str::String*, const char*, const char*)>(0x004879D0)(
                                       path, name, ".skin")
                                 : Gfx::MakeModelPath(path, name, ".skin");
            Record({0x79D0, r == path ? 1u : 0u, Hash(path->m_buffer), static_cast<uint32_t>(path->m_serial)});
        }
        // LeastOf (NaN in each place)
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float values[] = {1.0f, 2.0f, -3.0f, 2.0f, nan};
            for (float a : values) {
                for (float b : values) {
                    for (float c : values) {
                        float r = original ? Original<float(__cdecl*)(float, float, float)>(0x00487AD0)(a, b, c)
                                           : Gfx::LeastOf(a, b, c);
                        Record({Bits(r)});
                    }
                }
            }
        }
        // The scale
        {
            static Script::CVector vector;
            vector.unknown0 = 0;
            vector.x = 1.5f;
            vector.y = -2.0f;
            vector.z = 0.25f;
            for (int k = 0; k < 7; ++k) {
                Params p;
                if (k == 0)
                    p.Float(0x13B9DA7B, 2.5f);
                if (k == 1)
                    p.Integer(0x13B9DA7B, 3);
                if (k == 2)
                    p.Vector(0x13B9DA7B, &vector).Float(0x7323E97C, 9.0f);
                if (k == 3)
                    p.Checksum(0x13B9DA7B, 0x1).Float(0x0424D9EA, 4.0f);
                if (k == 4)
                    p.Float(0x7323E97C, -1.0f).Float(0x9D2D8850, 7.0f);
                if (k == 5)
                    p.Integer(0x9D2D8850, 5);
                float scale[4] = {11.0f, 12.0f, 13.0f, 14.0f};
                bool r = original ? Original<bool(__cdecl*)(float*, Script::CStruct*)>(0x00487B00)(scale, p.Struct())
                                  : Gfx::GetScaleFromParams(reinterpret_cast<Mth::Vector*>(scale), p.Struct());
                Record({0x7B00, r ? 1u : 0u, Bits(scale[0]), Bits(scale[1]), Bits(scale[2]), Bits(scale[3])});
            }
        }
        // The models hidden for a cutscene (and an empty list shown)
        {
            auto* list = reinterpret_cast<uint32_t*>(0x006FA2F0);
            for (int k = 0; k < 2; ++k) {
                SetComponents();
                list[0] = 0;
                list[1] = 7;
                if (k == 1) {
                    for (auto& c : g_components)
                        *reinterpret_cast<uint32_t*>(c.bytes + 0xC) = reinterpret_cast<uint32_t>(g_compObjects[1]);
                }
                if (original)
                    Original<void(__cdecl*)()>(0x00487D20)();
                else
                    Gfx::HideModelsForCutscene();
                Record({0xD20, list[1], list[0] ? 1u : 0u});
                if (list[0])
                    Record({reinterpret_cast<uint32_t*>(list[0])[0], reinterpret_cast<uint32_t*>(list[0])[1]});
                if (original)
                    Original<void(__cdecl*)()>(0x00487D30)();
                else
                    Gfx::ShowModelsAfterCutscene();
                Record({0xD30, list[0], list[1]});
            }
            // (a list of its own through the members)
            uint32_t own[2] = {0, 0};
            SetComponents();
            if (original)
                Original<void(__thiscall*)(void*)>(0x00487BD0)(own);
            else
                reinterpret_cast<Gfx::SHiddenModels*>(own)->Hide();
            if (original)
                Original<void(__thiscall*)(void*)>(0x00487CB0)(own);
            else
                reinterpret_cast<Gfx::SHiddenModels*>(own)->Show();
            Record({own[0], own[1]});
        }
        // The slot-4 jump
        {
            void* object[2] = {g_slotVtable, nullptr};
            CallSlot4(original ? reinterpret_cast<void*>(0x00487D50) : reinterpret_cast<void*>(&Gfx::VirtualSlot4Jump),
                      object, 0x1234, 0x5678);
        }
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        for (auto& word : g_trace)
            word = Rel(word);
        traces[run] = g_trace;
    }
    Restore();

    Check(traces[0].size() > 600, "CAS options and model helpers: the original reached its calls");
    Compare(traces[0], traces[1], "the CAS option lists and the models' helpers (0x00485CB0-0x00487D50)");
}

} // namespace

SELFTEST(Early, TestCasOptionsAndModelUtils, Restore);
