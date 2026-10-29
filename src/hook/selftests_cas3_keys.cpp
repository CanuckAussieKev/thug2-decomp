// Self-tests for an animation's custom keys (src/game/gfx/custom_anim_key.cpp, batch CAS3-2): each original against
// ours on keys in a stand-in heap - the ranges over a grid of frames and limits, every kind's Process, the camera key's
// construction, the script key's set-up and destruction, and the key list's reader over every kind (and allocations
// failing); scripts, structures, objects, events and the matrix maker stood in; the calls, results and memory compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/gfx/custom_anim_key.h"
#include "game/obj/cutscene.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/skate_cfunc_testkit.h"
#include "hook/standins.h"

namespace {

using namespace SkateCFuncTests;
using Gfx::CCustomAnimKey;

alignas(16) uint8_t g_camera[sizeof(Gfx::Camera)];
alignas(16) uint8_t g_objects[2][0x60];
void* g_objectVtable[8];
void* g_keyVtable[4];
Params g_globalStructure;
int g_newCalls;
uint32_t g_failNew; // (a bit per Mem::New call: fail it)

uint32_t Rel(uint32_t address)
{
    if (address == reinterpret_cast<uint32_t>(g_camera))
        return 0xCA000000u;
    for (uint32_t i = 0; i < 2; ++i) {
        if (address == reinterpret_cast<uint32_t>(g_objects[i]))
            return 0x0B000000u + i;
    }
    if (address == reinterpret_cast<uint32_t>(g_globalStructure.Struct()))
        return 0x57000000u;
    return address;
}

void StandInKeyCalls(StandIns& s)
{
    StandInStructAdds(s);
    StandInMemory(s);
    s.At({0x004015D0, AddressOf(&Mem::New)}, 0, [](Regs& r) {
        int call = g_newCalls++;
        Record({0x15D0, r.args[0], static_cast<uint32_t>(call)});
        if (g_failNew & (1u << (call & 31)))
            return 0u;
        return reinterpret_cast<uint32_t>(HeapBlock(r.args[0]));
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return 7u;
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return r.args[0] == 0x5170 ? reinterpret_cast<uint32_t>(g_globalStructure.Struct()) : 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, r.ecx});
        return r.ecx;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00460AD0, AddressOf(&Obj::FindObject)}, 0, [](Regs& r) {
        Record({0x0AD0, r.args[0]});
        return reinterpret_cast<uint32_t>(g_objects[r.args[0] & 1]);
    });
    s.At({0x004609F0, AddressOf(&Obj::CObject::SelfEvent)}, 8, [](Regs& r) {
        Record({0x09F0, Rel(r.ecx), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00463AA0, AddressOf(&Obj::QuatToMatrix)}, 0, [](Regs& r) {
        auto* q = reinterpret_cast<uint32_t*>(r.args[0]);
        auto* t = reinterpret_cast<uint32_t*>(r.args[1]);
        Record({0x3AA0, q[0], q[1], q[2], q[3], t[0], t[1], t[2], t[3], Rel(r.args[2] - 0x6C)});
        return 0u;
    });
    for (int slot = 0; slot < 8; ++slot) {
        g_objectVtable[slot] = s.Thunk(0, [slot](Regs& r) {
            Record({0x0B70u + static_cast<uint32_t>(slot), Rel(r.ecx)});
            return 0u;
        });
    }
    g_keyVtable[2] = s.Thunk(4, [](Regs& r) {
        Record({0xAE02, r.ecx, Rel(r.args[0])});
        return 0x1234501u;
    });
}

uint32_t Bits(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, 4);
    return raw;
}

void TestCustomAnimKeys()
{
    Setup();
    auto* standIns = new StandIns;
    StandInKeyCalls(*standIns);
    if (!g_globalStructure.count)
        g_globalStructure.Checksum(0xA1DC81F9, 0x41).Integer(0x1111, 3);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        ResetState();
        ResetMemory();
        g_newCalls = 0;
        g_failNew = 0;
        std::memset(g_camera, 0x5A, sizeof(g_camera));
        for (auto& object : g_objects) {
            std::memset(object, 0, sizeof(object));
            *reinterpret_cast<void***>(object) = g_objectVtable;
        }
        auto heap = [](uint32_t size) { return static_cast<uint8_t*>(HeapBlock(size)); };
        using This = void*;

        // The ranges: frames around the limits, limits either way round, equal, NaN; inclusive or not; the camera
        // key cut or not
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            const float limits[] = {0.0f, 2.5f, 3.0f, 5.0f, 5.5f, 9.0f, -1.0f, nan, 4.0f};
            auto* key = reinterpret_cast<Gfx::CCameraKey*>(heap(0x30));
            for (int frame = -2; frame <= 11; ++frame) {
                key->m_frame = frame;
                for (float start : limits) {
                    for (float end : limits) {
                        uint32_t bits = 0;
                        for (int k = 0; k < 6; ++k) {
                            bool inclusive = k & 1;
                            key->m_cut = (k & 2) != 0;
                            bool camera = k >= 4 || (k & 2);
                            uint32_t address = camera ? 0x004861D0u : 0x004860C0u;
                            bool result;
                            if (original)
                                result = Original<bool(__thiscall*)(This, float, float, bool)>(address)(key, start, end,
                                                                                                         inclusive);
                            else if (camera)
                                result = key->WithinRange(start, end, inclusive);
                            else
                                result = key->CCustomAnimKey::WithinRange(start, end, inclusive);
                            bits |= (result ? 1u : 0u) << k;
                        }
                        g_trace.push_back(bits);
                    }
                }
            }
        }
        // SetActive / ProcessIfActive (a recording slot 2)
        {
            auto* key = reinterpret_cast<CCustomAnimKey*>(heap(0xC));
            key->m_vtable = g_keyVtable;
            key->m_frame = 3;
            for (int k = 0; k < 4; ++k) {
                bool active = k == 1 || k == 3;
                if (original)
                    Original<void(__thiscall*)(This, bool)>(0x00486150)(key, active);
                else
                    key->SetActive(active);
                for (int twice = 0; twice < 2; ++twice) {
                    bool result = original ? Original<bool(__thiscall*)(This, void*)>(0x00486160)(key, g_objects[k & 1])
                                           : key->ProcessIfActive(g_objects[k & 1]);
                    Record({0xAC00, result ? 1u : 0u, key->m_armed ? 1u : 0u});
                }
            }
        }
        // Every kind's Process
        {
            auto process = [&](uint32_t address, CCustomAnimKey* key, void* object, auto ours) {
                bool result = original ? Original<bool(__thiscall*)(This, void*)>(address)(key, object) : ours(object);
                Record({0x9900, address, result ? 1u : 0u});
            };
            auto* fov = reinterpret_cast<Gfx::CCameraFovKey*>(heap(0x10));
            fov->m_frame = 4;
            fov->m_fov = 1.25f;
            process(0x004868A0, fov, g_camera, [&](void* o) { return fov->Process(o); });
            auto* script = reinterpret_cast<Gfx::CScriptKey*>(heap(0x10));
            for (uint32_t name : {0x4E917BDAu, 0x208D7A0Du, 0x12345678u, 0x69F35FA0u}) {
                script->m_script = name;
                process(0x00486180, script, g_objects[1], [&](void* o) { return script->Process(o); });
                Record({script->m_script});
            }
            auto* structure = reinterpret_cast<Gfx::CStructureScriptKey*>(heap(0x10));
            for (uint32_t name : {0x5170u, 0x5171u}) {
                structure->m_structure = name;
                process(0x00486300, structure, g_objects[0], [&](void* o) { return structure->Process(o); });
            }
            auto* kill = reinterpret_cast<Gfx::CKillObjectKey*>(heap(0x10));
            for (uint32_t name : {0x5170u, 0x5171u}) {
                kill->m_structure = name;
                process(0x00486340, kill, g_objects[0], [&](void* o) { return kill->Process(o); });
            }
            auto* camera = reinterpret_cast<Gfx::CCameraKey*>(heap(0x30));
            camera->m_rotation = {0.5f, -0.25f, 0.125f, 0.75f};
            camera->m_pos = {10.0f, 20.0f, -30.0f, 1.0f};
            for (int frame : {0, 6}) {
                camera->m_frame = frame;
                camera->m_armed = false;
                process(0x00486390, camera, g_camera, [&](void* o) { return camera->Process(o); });
                Record({camera->m_armed ? 1u : 0u});
            }
            auto* event = reinterpret_cast<Gfx::CAnimScriptKey*>(heap(0x14));
            event->m_type = 0xE7E7;
            event->mp_params = reinterpret_cast<CStruct*>(0x5150);
            process(0x00486440, event, g_objects[1], [&](void* o) { return event->Process(o); });
        }
        // The camera key's construction
        {
            const float rotations[][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.3f, -0.4f, 0.5f, 0.70710677f}, {1.0f, 2.0f, 3.0f, 4.0f}};
            const float positions[][4] = {{1.5f, -2.5f, 3.75f, 9.0f}, {-0.0f, 0.0f, 1e10f, 0.0f}};
            for (int i = 0; i < 6; ++i) {
                auto* key = reinterpret_cast<Gfx::CCameraKey*>(heap(0x30));
                std::memset(key, 0xEE, 0x30);
                auto* rotation = reinterpret_cast<const Mth::Quat*>(rotations[i % 3]);
                auto* pos = reinterpret_cast<const Mth::Vector*>(positions[i % 2]);
                bool cut = i >= 3;
                Gfx::CCameraKey* made =
                    original ? Original<Gfx::CCameraKey*(__thiscall*)(This, int, const Mth::Quat*, const Mth::Vector*,
                                                                      bool)>(0x00486490)(key, i * 7 - 3, rotation, pos, cut)
                             : key->Construct(i * 7 - 3, rotation, pos, cut);
                Record({made == key ? 1u : 0u});
            }
        }
        // The script key: set up (twice: the first structure isn't freed), destroyed, destroyed again (none)
        {
            auto* key = reinterpret_cast<Gfx::CAnimScriptKey*>(heap(0x14));
            std::memset(key, 0x33, 0x14);
            for (uint32_t params : {0x7001u, 0u}) {
                if (original)
                    Original<void(__thiscall*)(This, int, uint32_t, CStruct*)>(0x004865D0)(
                        key, 17, 0xE0E0 + params, reinterpret_cast<CStruct*>(params));
                else
                    key->Set(17, 0xE0E0 + params, reinterpret_cast<CStruct*>(params));
            }
            for (int twice = 0; twice < 2; ++twice) {
                if (original)
                    Original<void(__thiscall*)(This)>(0x00486560)(key);
                else
                    key->DestroyBody();
                Record({reinterpret_cast<uint32_t>(key->m_vtable)});
            }
            // The kinds' scalar deleting destructor, freed or not
            for (unsigned flags : {0u, 1u, 3u}) {
                auto* other = reinterpret_cast<CCustomAnimKey*>(heap(0x10));
                other->m_vtable = reinterpret_cast<void**>(0x00648818);
                CCustomAnimKey* result = original ? Original<CCustomAnimKey*(__thiscall*)(This, unsigned)>(0x00486460)(
                                                        other, flags)
                                                  : other->Destroy(flags);
                Record({result == other ? 1u : 0u, reinterpret_cast<uint32_t>(other->m_vtable)});
            }
        }
        // The key list: every kind (and two unknown ones), then each allocation failing in turn
        {
            static uint32_t list[200];
            uint32_t n = 0;
            auto add = [&](uint32_t frame, uint32_t kind, uint32_t size, std::initializer_list<uint32_t> data) {
                list[n++] = frame;
                list[n++] = kind;
                list[n++] = size;
                for (uint32_t word : data)
                    list[n++] = word;
            };
            add(5, 1, 4, {Bits(0.875f)});
            add(6, 2, 32, {Bits(1.0f), Bits(2.0f), Bits(3.0f), Bits(4.0f), Bits(0.1f), Bits(0.2f), Bits(0.3f), Bits(0.9f)});
            add(7, 3, 32, {1, 2, 3, 4, 5, 6, 7, 8});
            add(8, 4, 4, {0x4E917BDA});
            add(9, 5, 4, {0x5170});
            add(10, 6, 4, {0x5171});
            add(11, 7, 32, {Bits(-1.0f), Bits(-2.0f), Bits(-3.0f), Bits(1.0f), Bits(0.0f), Bits(0.6f), Bits(0.0f), Bits(0.8f)});
            add(12, 9, 8, {0xAAAA, 0xBBBB});
            add(13, 0, 4, {0xCCCC});
            for (int round = 0; round < 10; ++round) {
                g_failNew = round == 0 ? 0u : 1u << (round - 1);
                g_newCalls = 0;
                const uint8_t* cursor = reinterpret_cast<const uint8_t*>(list);
                for (int k = 0; k < 9; ++k) {
                    CCustomAnimKey* key = original ? Original<CCustomAnimKey*(__cdecl*)(const uint8_t**)>(0x00486680)(
                                                         &cursor)
                                                   : Gfx::CreateCustomAnimKey(&cursor);
                    Record({0x6680, static_cast<uint32_t>(cursor - reinterpret_cast<const uint8_t*>(list)),
                            key ? static_cast<uint32_t>(reinterpret_cast<uint8_t*>(key) - g_heap) : 0xFFFFFFFFu});
                }
            }
            g_failNew = 0;
        }
        for (uint32_t i = 0; i < g_heapUsed; i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_heap + i));
        for (uint32_t i = 0; i < sizeof(g_camera); i += 4)
            g_trace.push_back(*reinterpret_cast<uint32_t*>(g_camera + i));
        for (auto& word : g_trace)
            word = Rel(word);
        traces[run] = g_trace;
    }
    Restore();
    Check(traces[0].size() > 2000, "custom anim keys: the original reached its calls");
    Compare(traces[0], traces[1], "an animation's custom keys (0x004860C0-0x00486875)");
}

} // namespace

SELFTEST(Early, TestCustomAnimKeys, Restore);
