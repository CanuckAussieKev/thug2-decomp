// Self-tests for the shadow component (game/obj/shadow_component.cpp): the originals against ours on stand-in objects
// - a model component, a model with a hierarchy and a fader, the renderer's shadow objects (recording vtables) -
// every call into other code recorded by stand-ins (hook/standins.h) and the component compared afterwards.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/obj/shadow_component.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CShadowComponent;

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
};

template <typename T>
T& At(void* base, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
}

struct World {
    alignas(16) uint8_t component[0x60];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t modelComponent[0x220];
    alignas(16) uint8_t model[0x60];
    alignas(16) uint8_t hierarchy[0x60];
    alignas(16) uint8_t fader[0x10];
    alignas(16) uint8_t faderVector[0x10];
    alignas(16) uint8_t shadow[0x80];    // what the shadow constructors are given
    alignas(16) uint8_t existing[0x20];  // a shadow the component already has
    alignas(16) uint8_t params[0x10];
    alignas(16) uint8_t asset[0x10];
    alignas(16) uint8_t manager[0x10];
    alignas(16) uint8_t vector[0x10];
    void* shadowVtable[8];
    void* faderVtable[8];
    void* componentVtable[16];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    return word;
}
uint32_t HashFloats(const void* p, size_t count) { return HashBytes(static_cast<const uint8_t*>(p), count * 4); }

struct Answers {
    bool callocFails, newFails, haveModelComponent, haveHierarchy, typeFound, flag, stringFound, floatFound;
    uint32_t type;
    float value;
};
Answers g_a;

void StandIn(StandIns& s)
{
    using Script::CStruct;
    auto allocate = [](Regs&, uint32_t tag, uint32_t size, bool fails) {
        Record({tag, size});
        if (fails)
            return 0u;
        std::memset(g_world.shadow, 0xC5, sizeof(g_world.shadow));
        return reinterpret_cast<uint32_t>(g_world.shadow);
    };
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [allocate](Regs& r) {
        return allocate(r, 0x1CC0, r.args[0], g_a.callocFails);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [allocate](Regs& r) {
        Record({r.args[1], r.args[2]});
        return allocate(r, 0xAFC0, r.args[0], g_a.newFails);
    });
    s.At({0x0045D730, AddressOf(&Obj::CBaseComponent::Construct)}, 0, [](Regs& r) {
        Record({0xD730, Rel(r.ecx)});
        reinterpret_cast<uint32_t*>(r.ecx)[0] = 0xBA5E0000;
        reinterpret_cast<uint32_t*>(r.ecx)[2] = 0;
        return r.ecx;
    });
    s.At({0x0045D740, AddressOf(&Obj::CBaseComponent::DestroyBody)}, 0, [](Regs& r) {
        Record({0xD740, Rel(r.ecx), Rel(*reinterpret_cast<uint32_t*>(r.ecx))});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, Rel(r.ecx), r.args[0]});
        return g_a.haveModelComponent ? reinterpret_cast<uint32_t>(g_world.modelComponent) : 0u;
    });
    s.At({0x00407C00, AddressOf(&Ass::CAssMan::GetAssetData)}, 8, [](Regs& r) {
        Record({0x7C00, r.ecx, r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>(g_world.asset);
    });
    s.At({0x004949B0}, 4, [](Regs& r) {
        Record({0x49B0, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00493040}, 0, [](Regs& r) {
        Record({0x3040, Rel(r.ecx)});
        return g_a.haveHierarchy ? reinterpret_cast<uint32_t>(g_world.hierarchy) : 0u;
    });
    // The shadow constructors.
    s.At({0x004A19C0}, 0x18, [](Regs& r) {
        Record({0x19C0, Rel(r.ecx), Rel(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        At<void**>(reinterpret_cast<void*>(r.ecx), 0) = g_world.shadowVtable;
        At<int>(reinterpret_cast<void*>(r.ecx), 4) = 1;
        return r.ecx;
    });
    s.At({0x004A1750}, 0, [](Regs& r) {
        Record({0x1750, Rel(r.ecx)});
        At<void**>(reinterpret_cast<void*>(r.ecx), 0) = g_world.shadowVtable;
        At<int>(reinterpret_cast<void*>(r.ecx), 4) = 2;
        return r.ecx;
    });
    s.At({0x004A17E0}, 4, [](Regs& r) {
        Record({0x17E0, Rel(r.ecx), Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    s.At({0x004A1F80}, 0x18, [](Regs& r) {
        Record({0x1F80, Rel(r.ecx), Rel(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5]});
        At<void**>(reinterpret_cast<void*>(r.ecx), 0) = g_world.shadowVtable;
        At<int>(reinterpret_cast<void*>(r.ecx), 4) = 3;
        return r.ecx;
    });
    // The shadow object's slots.
    g_world.shadowVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0x5400, Rel(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_world.shadowVtable[1] = s.Thunk(0x18, [](Regs& r) {
        Record({0x5401, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1]), r.args[2], r.args[3], r.args[4], r.args[5],
                HashFloats(reinterpret_cast<void*>(r.args[0]), 4), HashFloats(reinterpret_cast<void*>(r.args[1]), 16)});
        return 0u;
    });
    g_world.shadowVtable[2] = s.Thunk(0x10, [](Regs& r) {
        Record({0x5402, Rel(r.ecx), r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    g_world.shadowVtable[3] = s.Thunk(4, [](Regs& r) {
        Record({0x5403, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    g_world.shadowVtable[4] = s.Thunk(4, [](Regs& r) {
        Record({0x5404, Rel(r.ecx)});
        const uint32_t* m = reinterpret_cast<const uint32_t*>(r.args[0]);
        g_trace.insert(g_trace.end(), m, m + 16);
        return 0u;
    });
    g_world.shadowVtable[5] = s.Thunk(0, [](Regs& r) {
        Record({0x5405, Rel(r.ecx)});
        return 0u;
    });
    g_world.shadowVtable[6] = s.Thunk(0, [](Regs& r) {
        Record({0x5406, Rel(r.ecx)});
        return 0u;
    });
    g_world.shadowVtable[7] = s.Thunk(8, [](Regs& r) {
        Record({0x5407, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1])});
        return 0u;
    });
    g_world.faderVtable[5] = s.Thunk(4, [](Regs& r) {
        Record({0xFAD5, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.faderVector);
    });
    g_world.componentVtable[2] = s.Thunk(4, [](Regs& r) {
        Record({0xC002, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    // The script structure.
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Record({0x6950, Rel(r.ecx), r.args[0], r.args[2]});
             if (!g_a.typeFound)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = g_a.type;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        return static_cast<uint32_t>(g_a.flag);
    });
    s.At({0x00476590, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))}, 12,
         [](Regs& r) {
             auto** out = reinterpret_cast<const char**>(r.args[1]);
             Record({0x6590, Rel(r.ecx), r.args[0], Hash(*out), r.args[2]});
             if (!g_a.stringFound)
                 return 0u;
             *out = "Some_Shadow";
             return 1u;
         });
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         [](Regs& r) {
             auto* out = reinterpret_cast<uint32_t*>(r.args[1]);
             Record({0x66D0, Rel(r.ecx), r.args[0], *out, r.args[2]});
             if (!g_a.floatFound)
                 return 0u;
             std::memcpy(out, &g_a.value, 4);
             return 1u;
         });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0x7D40, Rel(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
}

void Fill(Random& r, uint8_t* p, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        float f = static_cast<float>(static_cast<int>(r.Below(4001)) - 2000) / 16.0f;
        std::memcpy(p + i, &f, 4);
    }
}

void Build(Random& r)
{
    Fill(r, g_world.component, sizeof(g_world.component));
    Fill(r, g_world.object, sizeof(g_world.object));
    Fill(r, g_world.model, sizeof(g_world.model));
    Fill(r, g_world.hierarchy, sizeof(g_world.hierarchy));
    Fill(r, g_world.faderVector, sizeof(g_world.faderVector));
    std::memset(g_world.shadow, 0xC5, sizeof(g_world.shadow));
    std::memset(g_world.existing, 0, sizeof(g_world.existing));
    At<void**>(g_world.existing, 0) = g_world.shadowVtable;
    At<int>(g_world.existing, 4) = static_cast<int>(r.Below(5));
    At<void**>(g_world.fader, 0) = g_world.faderVtable;
    std::memset(g_world.modelComponent, 0, sizeof(g_world.modelComponent));
    At<uint8_t*>(g_world.modelComponent, 0x208) = r.OneIn(6) ? nullptr : g_world.model;
    At<uint8_t>(g_world.model, 0x44) = static_cast<uint8_t>(r.Below(8));
    At<uint32_t>(g_world.model, 0x30) = 0x0DE10000 + r.Below(100);
    At<uint8_t*>(g_world.model, 0x2C) = r.Bit() ? g_world.fader : nullptr;
    auto* c = reinterpret_cast<CShadowComponent*>(g_world.component);
    c->m_vtable = g_world.componentVtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    c->mp_shadow = r.OneIn(3) ? nullptr : g_world.existing;
    c->mp_model = r.OneIn(4) ? nullptr : reinterpret_cast<Obj::CBaseComponent*>(g_world.modelComponent);
    c->m_followObject = r.Bit();
    static const uint32_t kTypes[] = {0x76A54CD1, 0x3E84C2FD, 0x46653221, 0x806FFF30, 0x12345678};
    c->m_shadowType = kTypes[r.Below(5)];
    g_a = {};
    g_a.callocFails = r.OneIn(6);
    g_a.newFails = r.OneIn(8);
    g_a.haveModelComponent = !r.OneIn(5);
    g_a.haveHierarchy = r.Bit();
    g_a.typeFound = !r.OneIn(4);
    g_a.type = kTypes[r.Below(5)];
    g_a.flag = r.OneIn(3);
    g_a.stringFound = r.Bit();
    g_a.floatFound = r.Bit();
    g_a.value = static_cast<float>(r.Below(1000)) / 64.0f;
}

void Trace(std::vector<uint32_t>& trace)
{
    for (const uint8_t* area : {g_world.component, g_world.shadow, g_world.existing})
        for (size_t i = 0; i < 0x48; i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(area + i)));
}

void TestShadowComponent()
{
    std::vector<Saved> saved = {{0x007CC979, 1, {}}, {0x007D1800, 4, {}}, {0x006918DC, 4, {}}};
    SaveAll(saved);
    GameVariable<uint8_t>(0x007CC979) = 1;
    GameVariable<void*>(0x007D1800) = g_world.manager;
    GameVariable<uint32_t>(0x006918DC) = 0xA55E0000;
    auto* s = new StandIns;
    StandIn(*s);
    std::vector<uint32_t> traces[2];
    int updates = 0, made = 0;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x5AD0};
        for (int n = 0; n < 4000; ++n) {
            Build(r);
            auto* c = reinterpret_cast<CShadowComponent*>(g_world.component);
            uint32_t which = r.Below(13);
            Record({0xCA5E, which});
            size_t before = g_trace.size();
            switch (which) {
            case 0: { // Create / Construct, Destroy
                void* result = original ? Original<void*(__cdecl*)()>(0x00431800)() : CShadowComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                unsigned flags = r.Below(4);
                result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x004313D0)(c, flags) : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                break;
            }
            case 1: // Finalize (no model: a crash in both)
                g_a.haveModelComponent = true;
                At<uint8_t*>(g_world.modelComponent, 0x208) = g_world.model;
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00431400)(c);
                else
                    c->Finalize();
                break;
            case 2: // RefreshShadow
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00431450)(c);
                else
                    c->RefreshShadow();
                break;
            case 3: { // Hide
                bool hide = r.Bit();
                if (original)
                    Original<void(__thiscall*)(void*, bool)>(0x00431570)(c, hide);
                else
                    c->Hide(hide);
                break;
            }
            case 4: { // CreateShadow
                int type = static_cast<int>(r.Below(5));
                if (type == 2 && g_a.callocFails)
                    g_a.callocFails = false; // (a crash in both: the scale written through no shadow)
                if (type == 3 && g_a.newFails)
                    g_a.newFails = false; // (a crash in both: no shadow updated)
                const char* name = r.Bit() ? "Ped_Shadow" : "Other";
                if (type == 2 && !At<uint8_t*>(g_world.modelComponent, 0x208))
                    At<uint8_t*>(g_world.modelComponent, 0x208) = g_world.model; // (no model: a crash in both)
                if (type == 2)
                    g_a.haveModelComponent = true;
                if (original)
                    Original<void(__thiscall*)(void*, int, const char*)>(0x004315A0)(c, type, name);
                else
                    c->CreateShadow(type, name);
                break;
            }
            case 5: // DestroyShadow
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00431780)(c);
                else
                    c->DestroyShadow();
                break;
            case 6: { // the setters
                const auto* v = reinterpret_cast<const Mth::Vector*>(g_world.hierarchy + 0x10 * r.Below(4));
                if (original) {
                    Original<void(__thiscall*)(void*, const void*)>(0x004317C0)(c, v);
                    Original<void(__thiscall*)(void*, const void*)>(0x004317E0)(c, v + 1);
                    Original<void(__thiscall*)(void*, const void*)>(0x00431360)(c, g_world.vector);
                } else {
                    c->SetPosition(v);
                    c->SetNormal(v + 1);
                    c->SetLightDirection(reinterpret_cast<const Mth::Vector*>(g_world.vector));
                }
                break;
            }
            case 7:
            case 8: { // InitFromStructure
                // (making a simple shadow that fails is a crash in both; a detailed / Volume one needs the model too)
                g_a.callocFails = false;
                g_a.newFails = false;
                g_a.haveModelComponent = true;
                At<uint8_t*>(g_world.modelComponent, 0x208) = g_world.model;
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00431860)(c, g_world.params);
                else
                    c->InitFromStructure(reinterpret_cast<Script::CStruct*>(g_world.params));
                break;
            }
            case 9:
            case 10: // Update
                if (original)
                    Original<void(__thiscall*)(void*)>(0x004319D0)(c);
                else
                    c->Update();
                break;
            default: { // CallMemberFunction
                static const uint32_t kNames[] = {0xAAF6E513, 0xF272C43A, 0x12345678};
                uint32_t name = kNames[r.Below(3)];
                if (name == 0xF272C43A && r.Bit())
                    At<int>(g_world.existing, 4) = 2;
                int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x00431B10)(
                                            c, name, g_world.params, nullptr)
                                      : c->CallMemberFunction(name, reinterpret_cast<Script::CStruct*>(g_world.params),
                                                              nullptr);
                Record({static_cast<uint32_t>(result)});
                break;
            }
            }
            if (original)
                for (size_t k = before; k < g_trace.size(); ++k) {
                    updates += g_trace[k] == 0x5401 || g_trace[k] == 0x5404;
                    made += g_trace[k] == 0x19C0 || g_trace[k] == 0x1750 || g_trace[k] == 0x1F80;
                }
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    RestoreAll(saved);
    Log("  shadow component: %d shadow updates, %d shadows made in the original's run", updates, made);
    Check(updates > 500 && made > 300, "shadow component: the cases reach the shadows");
    Compare(traces[0], traces[1], "shadow component (create, init, update, refresh, member functions)");
}

} // namespace

SELFTEST(Early, TestShadowComponent, RemoveStandIns);
