// Self-tests for the suspend component and its task (game/obj/suspend_component.cpp), the texture projection
// component (game/obj/texture_projection_component.cpp) and Mth::Vector::MultiplyBy (game/mth/vector_transform.cpp):
// the originals against ours on stand-in objects, every call into other code recorded by stand-ins
// (hook/standins.h), the components compared afterwards.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/movie_manager.h"
#include "game/obj/object.h"
#include "game/obj/suspend_component.h"
#include "game/obj/texture_projection_component.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/engine_texture_projection.h"
#include "game/gfx/model.h"
#include "game/gfx/pc_texture_projection.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CSuspendComponent;
using Obj::CTextureProjectionComponent;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Bit() { return (Next() & 1) != 0; }
    bool OneIn(uint32_t n) { return Below(n) == 0; }
    float Value() { return static_cast<float>(static_cast<int>(Below(30001)) - 3000) / 7.0f; }
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}
uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

constexpr int kComponents = 4;
struct World {
    alignas(16) uint8_t components[kComponents][0x50];
    alignas(16) uint8_t objects[kComponents][0x120];
    alignas(16) uint8_t models[2][0x240];   // model components (+0x208 the model)
    alignas(16) uint8_t model[0x60];
    alignas(16) uint8_t block[0x60];
    alignas(16) uint8_t skate[0x400];
    alignas(16) uint8_t mlp[0x40];
    alignas(16) uint8_t params[0x10];
    alignas(16) uint8_t array[0x10];
    uint32_t elements[8];
    alignas(16) uint8_t projections[6][0xD0];

    void* modelVtable[16];
};
World g_world;
int g_nextProjection;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= reinterpret_cast<uint32_t>(tib->StackLimit) && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x57AC0000;
    if (word == reinterpret_cast<uint32_t>(Original<void*>(0x00434420)) || word == AddressOf(Obj::SuspendTask) ||
        word == reinterpret_cast<uint32_t>(&Obj::SuspendTask))
        return 0x7A5C0000; // the task's callback
    return word;
}

struct Answers {
    bool callocFails, culled, sphereNaN;
    uint32_t found;
    uint32_t flags;
    uint32_t seed;
    int noLod;
    int viewports;
    float floats[4];
    float scriptFloats[4];
};
Answers g_a;

uint32_t NameBit(uint32_t name) { return 1u << ((name * 2654435761u) >> 27); }
bool Found(uint32_t name) { return (g_a.found & NameBit(name)) != 0; }

void StandIn(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0x1CC0, r.args[0]});
        if (g_a.callocFails)
            return 0u;
        if (r.args[0] == 0xC8) {
            uint8_t* p = g_world.projections[g_nextProjection++ % 6];
            std::memset(p, 0, 0xD0);
            return reinterpret_cast<uint32_t>(p);
        }
        std::memset(g_world.block, 0, sizeof(g_world.block));
        return reinterpret_cast<uint32_t>(g_world.block);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x0045D760, AddressOf(&Obj::CBaseComponent::Suspend)}, 4, [](Regs& r) {
        Record({0xD760, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x00406890, AddressOf(&Tsk::BaseTask::Construct)}, 4, [](Regs& r) {
        Record({0x6890, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    s.At({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, [](Regs& r) {
        const uint32_t* task = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0x6C90, Rel(r.ecx), Rel(r.args[0])});
        if (task)
            Record({task[0], Rel(task[6]), Rel(task[7])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.models[r.args[0] & 1]);
    });
    s.At({0x0045D9E0, AddressOf(&Obj::CCompositeObject::Suspend)}, 4, [](Regs& r) {
        Record({0xD9E0, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Record({0xF2D0, r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.components[0]);
    });
    s.At({0x00493C60, AddressOf(&Nx::CModel::SetActive)}, 4, [](Regs& r) {
        Record({0x3C60, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00492F30, AddressOf(&Nx::CModel::GetBoundingSphere)}, 4, [](Regs& r) {
        Record({0x2F30, Rel(r.ecx)});
        float* s4 = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 4; ++i)
            s4[i] = g_a.floats[i] * (i + 1);
        return r.args[0];
    });
    s.At({0x0048C490, AddressOf(&Nx::Engine::SphereVisibleInOneViewport)}, 0, [](Regs& r) {
        const uint32_t* v = reinterpret_cast<const uint32_t*>(r.args[0]);
        Record({0xC490, v[0], v[1], v[2], v[3], r.args[1]});
        return static_cast<uint32_t>(g_a.culled);
    });
    g_world.modelVtable[1] = s.Thunk(0, [](Regs& r) {
        Record({0x0DE1, Rel(r.ecx)});
        return 0u;
    });
    // The texture projections.
    s.At({0x0057A2F0, AddressOf(&Obj::ListHead::DeleteAllNodes)}, 0, [](Regs& r) {
        Record({0xA2F0, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x004C7E80, AddressOf(&Nx::CPcTextureProjection::Construct)}, 0, [](Regs& r) {
        Record({0x7E80, Rel(r.ecx)});
        At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0) = 0x0064A764;
        return r.ecx;
    });
    s.At({0x004A04C0, AddressOf(&Nx::CTextureProjection::InitFromStructure)}, 4, [](Regs& r) {
        Record({0x04C0, Rel(r.ecx), Rel(r.args[0]), At<uint32_t>(reinterpret_cast<void*>(r.ecx), 0x14)});
        return 0u;
    });
    s.At({0x0049FFE0, AddressOf(&Nx::CTextureProjection::Update)}, 4, [](Regs& r) {
        Record({0xFFE0, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // Script values.
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return static_cast<uint32_t>(g_a.noLod);
    });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             Record({0x66D0, Rel(r.ecx), r.args[0], Rel(r.args[1]), r.args[2]});
             if (!Found(r.args[0]))
                 return 0u;
             *reinterpret_cast<float*>(r.args[1]) = g_a.floats[(r.args[0] * 7u) >> 30];
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        return static_cast<uint32_t>((g_a.flags & NameBit(r.args[0])) != 0);
    });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6950, Rel(r.ecx), r.args[0], *reinterpret_cast<uint32_t*>(r.args[1]), r.args[2]});
             *reinterpret_cast<uint32_t*>(r.args[1]) = r.ecx ^ g_a.seed;
             return 1u;
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [](Regs& r) {
             Record({0x6860, Rel(r.ecx), r.args[0], r.args[2]});
             *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_world.array);
             return 1u;
         });
}

// Script::GetFloat's float in ST0 (recorded first).
float g_scriptFloat;
void __cdecl RecordScriptFloat(uint32_t name)
{
    Record({0x8F00, name});
    g_scriptFloat = g_a.scriptFloats[(name * 5u) >> 30];
}
__declspec(naked) void ScriptFloatStandIn()
{
    __asm {
        mov eax, [esp + 4]
        push eax
        call RecordScriptFloat
        add esp, 4
        fld dword ptr [g_scriptFloat]
        ret
    }
}

void Build(Random& r)
{
    std::memset(g_world.components, 0, sizeof(g_world.components));
    std::memset(g_world.block, 0xB1, sizeof(g_world.block));
    for (int i = 0; i < kComponents; ++i) {
        auto* c = reinterpret_cast<CSuspendComponent*>(g_world.components[i]);
        c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.objects[i]);
        c->mp_nextOfType = i + 1 < kComponents && !r.OneIn(6)
                               ? reinterpret_cast<Obj::CBaseComponent*>(g_world.components[i + 1])
                               : nullptr;
        c->m_neverSuspend = r.OneIn(4);
        c->m_suspendDistance = r.Value();
        c->m_suspendDistanceSquared = r.Value() * r.Value();
        c->m_distanceSquared = r.OneIn(3) ? 0.0f : r.Value() * r.Value();
        c->m_framesBeforeCheck = r.OneIn(3) ? static_cast<int>(r.Below(4)) : 0;
        c->mp_model = r.OneIn(4) ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_world.models[r.Below(2)]);
        c->m_far = r.Bit();
        for (float& d : c->m_lodDistances)
            d = r.Value() * 3.0f;
        if (r.OneIn(5))
            reinterpret_cast<uint32_t*>(c->m_lodDistances)[2] = 0x49742420;
        c->m_framesBeforeLod = r.OneIn(3) ? static_cast<int>(r.Below(3)) : 0;
        for (int k = 0x4C; k < 0x5C; k += 4)
            At<float>(g_world.objects[i], k) = r.Value();
        for (int k = 0x6C; k < 0xAC; k += 4)
            At<float>(g_world.objects[i], k) = r.Value() / 300.0f;
        At<uint32_t>(g_world.objects[i], 0x08) = r.OneIn(5) ? 4u : 0u;
        At<uint32_t>(g_world.objects[i], 0x104) = r.Bit() ? 2u : 0u;
    }
    for (auto& m : g_world.models) {
        std::memset(m, 0, sizeof(m));
        At<void**>(m, 0) = g_world.modelVtable;
        At<uint8_t*>(m, 0x208) = r.OneIn(5) ? nullptr : g_world.model;
        m[0x234] = r.OneIn(6);
    }
    g_world.model[0x44] = static_cast<uint8_t>(r.Below(8));
    std::memset(g_world.skate, 0, sizeof(g_world.skate));
    GameVariable<uint8_t*>(0x007CE478) = g_world.skate;
    g_world.skate[0x3DC] = r.OneIn(5);
    for (int k = 0; k < 3; ++k)
        At<float>(g_world.skate, 0x3E0 + 4 * k) = r.Value();
    g_a = {};
    g_a.callocFails = r.OneIn(6);
    g_a.culled = r.Bit();
    g_a.found = r.Next() ^ (r.Next() << 12);
    g_a.flags = r.Next() ^ (r.Next() << 12);
    g_a.seed = r.Next();
    g_a.noLod = r.OneIn(4) ? 1 : 0;
    for (float& f : g_a.floats)
        f = r.OneIn(5) ? 0.0f : r.Value();
    for (float& f : g_a.scriptFloats)
        f = r.OneIn(6) ? 0.0f : r.Value() / 10.0f;
    GameVariable<int>(0x00701378) = static_cast<int>(r.Below(3));
    // Edges: distances exactly on the LOD / interleave / suspend limits (whole numbers: exact squares), and one just
    // over AnimLODInterleave2's exact square but at its float one.
    for (auto& comp : g_world.components) {
        auto* c = reinterpret_cast<CSuspendComponent*>(comp);
        if (r.OneIn(4)) {
            int which = static_cast<int>(r.Below(2));
            c->m_lodDistances[which] = static_cast<float>(1 + r.Below(300));
            c->m_distanceSquared = c->m_lodDistances[which] * c->m_lodDistances[which];
        }
        if (r.OneIn(4)) {
            float* pos = reinterpret_cast<float*>(c->m_object) + 0x4C / 4;
            float* camera = reinterpret_cast<float*>(g_world.skate + 0x3E0);
            for (int k = 0; k < 3; ++k) {
                pos[k] = static_cast<float>(static_cast<int>(r.Below(200)) - 100);
                camera[k] = static_cast<float>(static_cast<int>(r.Below(200)) - 100);
            }
            double dx = pos[0] - camera[0], dy = pos[1] - camera[1], dz = pos[2] - camera[2];
            c->m_suspendDistanceSquared = static_cast<float>(dx * dx + dy * dy + dz * dz);
        }
    }
    if (r.OneIn(3)) {
        float i2 = g_a.scriptFloats[(0xDB120FB5u * 5u) >> 30];
        auto* c = reinterpret_cast<CSuspendComponent*>(g_world.components[r.Below(kComponents)]);
        c->m_distanceSquared = static_cast<float>(static_cast<double>(i2) * i2);
        c->m_lodDistances[0] = 1e6f;
        g_a.scriptFloats[(0x3271AA80u * 5u) >> 30] = 1e4f; // (AnimLODInterleave4: far beyond)
    }
    GameVariable<int>(0x006951B0) = static_cast<int>(r.Next());
    GameVariable<int>(0x006951B4) = static_cast<int>(r.Next());
    GameVariable<int>(0x006951B8) = static_cast<int>(r.Next());
    GameVariable<uint32_t>(0x006951BC) = 0;
    // The projections' array of structures, the list.
    auto* array = reinterpret_cast<Script::CArray*>(g_world.array);
    std::memset(array, 0, sizeof(*array));
    array->m_type = Script::ESYMBOLTYPE_STRUCTURE;
    array->m_size = 1 + r.Below(4);
    for (int i = 0; i < 8; ++i)
        g_world.elements[i] = 0x5E000000 + i;
    if (array->m_size == 1)
        array->m_single = g_world.elements[0];
    else
        array->m_elements = g_world.elements;
    g_nextProjection = 0;
    std::memset(g_world.projections, 0, sizeof(g_world.projections));
}

void Trace(std::vector<uint32_t>& trace)
{
    for (auto& c : g_world.components)
        for (size_t i = 0; i < 0x4C; i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(c + i)));
    for (size_t i = 0; i < 0x4C; i += 4)
        trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(g_world.block + i)));
    for (auto& p : g_world.projections)
        for (size_t i = 0; i < 0x18; i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(p + i)));
    trace.push_back(GameVariable<uint32_t>(0x006951B0));
    trace.push_back(GameVariable<uint32_t>(0x006951B4));
    trace.push_back(GameVariable<uint32_t>(0x006951B8));
    trace.push_back(Rel(GameVariable<uint32_t>(0x006951BC)));
}

void TestSuspendComponent()
{
    std::vector<Saved> saved = {{0x007CE478, 4, {}}, {0x00701378, 4, {}}, {0x006951B0, 0x10, {}}, {0x00696D68, 4, {}},
                                {0x006F0FB8, 4, {}}};
    SaveAll(saved);
    GameVariable<uint8_t*>(0x00696D68) = g_world.mlp;
    GameVariable<uint32_t>(0x006F0FB8) = 0xC0B70000;
    auto* s = new StandIns;
    StandIn(*s);
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s->Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&ScriptFloatStandIn)));
    std::vector<uint32_t> traces[2];
    int suspended = 0, drawn = 0;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x5059};
        for (uint32_t n = 0; n < 5000; ++n) {
            Build(r);
            auto* c = reinterpret_cast<CSuspendComponent*>(g_world.components[r.Below(kComponents)]);
            auto* params = reinterpret_cast<Script::CStruct*>(g_world.params);
            uint32_t which = r.Below(14);
            Record({0xCA5E, which});
            size_t before = g_trace.size();
            switch (which) {
            case 0: { // Create / Construct, Destroy, Update, Finalize
                void* made = original ? Original<void*(__cdecl*)()>(0x00434290)() : CSuspendComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00433FD0)(c, flags) : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                if (original) {
                    Original<void(__thiscall*)(void*)>(0x00433E50)(c);
                    Original<void(__thiscall*)(void*)>(0x004341C0)(c);
                } else {
                    c->Update();
                    c->Finalize();
                }
                break;
            }
            case 1: // RegisterMemberFunctions (the task made)
                if (original)
                    Original<void(__cdecl*)()>(0x00434610)();
                else
                    CSuspendComponent::RegisterMemberFunctions();
                break;
            case 2:
            case 3: // InitFromStructure
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00434000)(c, params);
                else
                    c->InitFromStructure(params);
                break;
            case 4: { // the small ones
                bool isFar;
                if (original) {
                    Original<void(__thiscall*)(void*)>(r.Bit() ? 0x00433E60 : 0x00433E70)(c);
                    isFar = Original<bool(__thiscall*)(void*)>(0x00433E90)(c);
                    Original<void(__cdecl*)()>(0x00433E20)();
                } else {
                    if (r.Bit())
                        c->ResetCounts();
                    else
                        c->DisableLod();
                    isFar = c->IsFar();
                    Obj::NextSuspendFrame();
                }
                Record({isFar});
                break;
            }
            case 5:
            case 6: { // ShouldAnimate
                float d = 1.5f;
                float* out = r.Bit() ? &d : nullptr;
                bool result = original ? Original<bool(__thiscall*)(void*, float*)>(0x00433EA0)(c, out) : c->ShouldAnimate(out);
                Record({result, Bits(d)});
                break;
            }
            case 7:
            case 8: // UpdateVisibility
                if (original)
                    Original<void(__thiscall*)(void*)>(0x004342F0)(c);
                else
                    c->UpdateVisibility();
                break;
            case 9:
            case 10: // the task
                if (original)
                    Original<void(__cdecl*)(void*)>(0x00434420)(nullptr);
                else
                    Obj::SuspendTask(nullptr);
                break;
            case 11: { // Obj_ResetNoSuspendCount, MultiplyBy
                bool result = original ? Original<bool(__thiscall*)(void*, void*, void*)>(0x004341E0)(c, params, nullptr)
                                       : c->Obj_ResetNoSuspendCount(params, nullptr);
                Record({result});
                for (int k = 0; k < 40; ++k) {
                    Mth::Vector v = {r.Value(), r.Value(), r.Value(), r.OneIn(3) ? 1.0f : r.Value()};
                    const auto* m = reinterpret_cast<const Mth::Matrix*>(g_world.objects[k & 3] + 0x6C);
                    // A sum's order shows only at a float tie: w's term exactly halfway between two floats, the
                    // x and y terms half a double's step down and a little more than that up.
                    static Mth::Matrix tie;
                    if (k == 0) {
                        const float one = 1.0f + 1.0f / 4096.0f, step = 1.0f / 67108864.0f; // 1 + 2^-12, 2^-26
                        for (int col = 0; col < 4; ++col) {
                            reinterpret_cast<float*>(&tie.row[0])[col] = step;
                            reinterpret_cast<float*>(&tie.row[1])[col] = step;
                            reinterpret_cast<float*>(&tie.row[2])[col] = 0.0f;
                            reinterpret_cast<float*>(&tie.row[3])[col] = one;
                        }
                        float x = 1.0f / 134217728.0f; // 2^-27
                        v = {(r.Bit() ? -x : x * (1.0f + 1.0f / 128.0f)), 0.0f, 0.0f, one};
                        v.y = v.x < 0.0f ? x * (1.0f + 1.0f / 128.0f) : -x;
                        m = &tie;
                    }
                    Mth::Vector* back = original ? Original<Mth::Vector*(__thiscall*)(void*, const void*)>(0x00434200)(&v, m)
                                                 : &v.MultiplyBy(*m);
                    Record({back == &v, Bits(v.x), Bits(v.y), Bits(v.z), Bits(v.w)});
                }
                break;
            }
            default: { // the texture projection component
                auto* p = reinterpret_cast<CTextureProjectionComponent*>(g_world.components[0]);
                if (which == 12) {
                    void* made = original ? Original<void*(__cdecl*)()>(0x00434870)() : CTextureProjectionComponent::Create();
                    Record({Rel(reinterpret_cast<uint32_t>(made))});
                    if (!made)
                        break;
                    auto* q = static_cast<CTextureProjectionComponent*>(made);
                    q->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.objects[1]);
                    g_a.callocFails = false; // (a projection that fails is written through: a crash in both)
                    if (original) {
                        Original<void(__thiscall*)(void*, void*)>(0x00434730)(q, params);
                        Original<void(__thiscall*)(void*)>(0x00434630)(q);
                        Original<void(__thiscall*)(void*)>(0x00434810)(q);
                    } else {
                        q->InitFromStructure(params);
                        q->Finalize();
                        q->Update();
                    }
                    Trace(g_trace);
                    unsigned flags = r.Below(4);
                    void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00434840)(q, flags) : q->Destroy(flags);
                    Record({Rel(reinterpret_cast<uint32_t>(result))});
                } else {
                    // The list head's own destructor, on a list of one projection.
                    Mem::ListNode& head = p->m_projections.m_node;
                    auto* node = reinterpret_cast<Mem::ListNode*>(g_world.projections[0]);
                    head.next = head.prev = node;
                    node->next = node->prev = &head;
                    unsigned flags = r.Below(2);
                    void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00434670)(&p->m_projections, flags)
                                            : p->m_projections.Destroy(flags);
                    Record({Rel(reinterpret_cast<uint32_t>(result)), Rel(reinterpret_cast<uint32_t>(node->next))});
                }
                break;
            }
            }
            if (original)
                for (size_t k = before; k < g_trace.size(); ++k) {
                    suspended += g_trace[k] == 0xD9E0;
                    drawn += g_trace[k] == 0x3C60;
                }
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    RestoreAll(saved);
    Log("  suspend component: %d objects suspended / woken, %d models told in the original's run", suspended, drawn);
    Check(suspended > 300 && drawn > 300, "suspend component: the cases reach the objects and models");
    for (size_t i = 0; i < traces[0].size() && i < traces[1].size(); ++i) {
        if (traces[0][i] != traces[1][i]) {
            size_t k = i;
            while (k > 0 && traces[0][k] != 0xCA5E)
                --k;
            Log("  first difference at %u: the case at %u (kind %u)", static_cast<unsigned>(i), static_cast<unsigned>(k),
                traces[0][k + 1]);
            break;
        }
    }
    Compare(traces[0], traces[1], "suspend component and task, texture projections, MultiplyBy");
}

} // namespace

SELFTEST(Early, TestSuspendComponent, RemoveStandIns);
