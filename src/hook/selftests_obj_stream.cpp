// Self-tests for the stream component (game/obj/stream_component.cpp): the originals against ours on stand-in
// objects, every call into other code recorded by stand-ins (hook/standins.h) - the streams (Pcm), the emitter and
// proxim nodes, the camera, the script structures - and the component compared afterwards.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/obj/stream_component.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CStreamComponent;

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

struct World {
    alignas(16) uint8_t component[0x40];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t block[0x40];
    alignas(16) uint8_t camera[0x10];
    alignas(16) uint8_t cameraPos[0x10];
    alignas(16) uint8_t emitter[0x40];
    alignas(16) uint8_t proxim[0x60];
    alignas(16) uint8_t closest[0x10];
    alignas(16) uint8_t params[0x10];
};
World g_world;

uint32_t Rel(uint32_t word)
{
    auto base = reinterpret_cast<uint32_t>(&g_world);
    if (word >= base && word < base + sizeof(World))
        return 0x77000000 + (word - base);
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (word >= reinterpret_cast<uint32_t>(tib->StackLimit) && word < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x57AC0000;
    return word;
}

struct Answers {
    bool callocFails;
    uint32_t result;
    uint32_t found;
    uint32_t seed;
    float floats[4];
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
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0xF060, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The streams.
    s.At({0x004506D0, AddressOf(Pcm::StopStream)}, 0, [](Regs& r) {
        Record({0x06D0, r.args[0]});
        return 0u;
    });
    s.At({0x00451E20, AddressOf(Pcm::PlayStreamFromObject)}, 0, [](Regs& r) {
        Record({0x1E20, Rel(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7],
                r.args[8], r.args[9]});
        return g_a.result;
    });
    s.At({0x00451B80, AddressOf(Pcm::AddStream)}, 0, [](Regs& r) {
        Record({0x1B80, r.args[0], r.args[1], Rel(r.args[2])});
        return g_a.result & 1;
    });
    s.At({0x00450AE0, AddressOf(Pcm::RemoveStream)}, 0, [](Regs& r) {
        Record({0x0AE0, r.args[0], r.args[1], Rel(r.args[2])});
        return (g_a.result >> 1) & 1;
    });
    s.At({0x0047AC10, AddressOf(Sfx::FalloffFromChecksum)}, 0, [](Regs& r) {
        Record({0xAC10, r.args[0]});
        return r.args[0] & 3;
    });
    // Nodes and the camera.
    s.At({0x0046AB90, AddressOf(Obj::Proxim_GetNode)}, 0, [](Regs& r) {
        Record({0xAB90, r.args[0]});
        return reinterpret_cast<uint32_t>(g_world.proxim);
    });
    s.At({0x004680E0, AddressOf(Obj::Emitter_GetNode)}, 0, [](Regs& r) {
        Record({0x80E0, r.args[0]});
        return (r.args[0] & 1) ? reinterpret_cast<uint32_t>(g_world.emitter) : 0u;
    });
    s.At({0x004681A0, AddressOf(&Obj::EmitterNode::GetClosestPoint)}, 8, [](Regs& r) {
        Record({0x81A0, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1])});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_world.closest, 16);
        return r.args[0];
    });
    s.At({0x0046AEC0, AddressOf(&Obj::ProximNode::GetClosestPoint)}, 8, [](Regs& r) {
        Record({0xAEC0, Rel(r.ecx), Rel(r.args[1])});
        std::memcpy(reinterpret_cast<void*>(r.args[0]), g_world.closest, 16);
        return r.args[0];
    });
    s.At({0x00485A00, AddressOf(&Gfx::Camera::GetPos)}, 0, [](Regs& r) {
        Record({0x5A00, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.cameraPos);
    });
    // The script structures.
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            uint32_t name = r.args[0];
            // (the original leaves these slots uninitialised)
            bool uninitialised = name == 0x0C6AC50A || name == 0x8A7132CE || (name == 0 && tag == 0x6950);
            Record({tag, Rel(r.ecx), name, uninitialised ? 0xDEADu : Rel(*reinterpret_cast<uint32_t*>(r.args[1])),
                    r.args[2]});
            if (!Found(name))
                return 0u;
            uint32_t* out = reinterpret_cast<uint32_t*>(r.args[1]);
            if (tag == 0x66D0) {
                float f = g_a.floats[(name * 7u) >> 30];
                std::memcpy(out, &f, 4);
            } else if (tag == 0x6630) {
                *out = static_cast<uint32_t>(static_cast<int>(g_a.seed % 12) - 2);
            } else {
                *out = name ^ g_a.seed;
            }
            return 1u;
        };
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         getter(0x6950));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
}

void Build(Random& r)
{
    std::memset(g_world.component, 0, sizeof(g_world.component));
    std::memset(g_world.block, 0xB1, sizeof(g_world.block));
    auto* c = reinterpret_cast<CStreamComponent*>(g_world.component);
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    for (uint32_t& s : c->m_streams)
        s = r.Bit() ? 0u : 0x57000000 + r.Below(9);
    c->mp_emitter = r.Bit() ? reinterpret_cast<Obj::EmitterNode*>(g_world.emitter) : nullptr;
    c->mp_proxim = r.Bit() ? reinterpret_cast<Obj::ProximNode*>(g_world.proxim) : nullptr;
    for (int i = 0x4C; i < 0x5C; i += 4)
        At<float>(g_world.object, i) = r.Value();
    for (int i = 0; i < 16; i += 4)
        At<float>(g_world.closest, i) = r.Value();
    g_a = {};
    g_a.callocFails = r.OneIn(6);
    g_a.result = r.OneIn(4) ? 0u : r.Next();
    g_a.found = r.Next() ^ (r.Next() << 12);
    g_a.seed = r.Next();
    for (float& f : g_a.floats)
        f = r.OneIn(5) ? 0.0f : r.Value();
}

void Trace(std::vector<uint32_t>& trace)
{
    for (const uint8_t* area : {g_world.component, g_world.block})
        for (size_t i = 0; i < 0x2C; i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(area + i)));
}

void TestStreamComponent()
{
    auto* s = new StandIns;
    StandIn(*s);
    std::vector<uint32_t> traces[2];
    int played = 0;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x57E4};
        for (uint32_t n = 0; n < 4000; ++n) {
            Build(r);
            auto* c = reinterpret_cast<CStreamComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(g_world.params);
            uint32_t which = r.Below(11);
            Record({0xCA5E, which});
            size_t before = g_trace.size();
            using Member = bool(__thiscall*)(void*, void*, void*);
            switch (which) {
            case 0: { // Create, Construct, RegisterMemberFunctions, Destroy
                void* made = original ? Original<void*(__cdecl*)()>(0x00433D90)() : CStreamComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                Trace(g_trace);
                void* built = original ? Original<void*(__thiscall*)(void*)>(0x00433C10)(g_world.block)
                                       : reinterpret_cast<CStreamComponent*>(g_world.block)->Construct();
                Record({Rel(reinterpret_cast<uint32_t>(built))});
                if (original)
                    Original<void(__cdecl*)()>(0x00433870)();
                else
                    CStreamComponent::RegisterMemberFunctions();
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00433C40)(c, flags) : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                break;
            }
            case 1: // InitFromStructure
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00433900)(c, params);
                else
                    c->InitFromStructure(params);
                break;
            case 2: { // the position and the extent
                Mth::Vector out = {1.0f, 2.0f, 3.0f, 4.0f};
                void* camera = r.OneIn(4) ? nullptr : g_world.camera;
                uint32_t a;
                bool extent;
                if (original) {
                    a = Rel(reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, void*, void*)>(0x00433CD0)(c, &out, camera)));
                    Record({Bits(out.x), Bits(out.w)});
                    extent = Original<bool(__thiscall*)(void*, void*, void*)>(0x00433D30)(c, camera, &out);
                } else {
                    a = Rel(reinterpret_cast<uint32_t>(c->GetStreamPosition(&out, camera)));
                    Record({Bits(out.x), Bits(out.w)});
                    extent = c->GetStreamExtent(camera, &out);
                }
                Record({a, extent, Bits(out.x), Bits(out.y), Bits(out.z), Bits(out.w)});
                break;
            }
            case 3:
            case 4: { // PlayStream
                bool result = original ? Original<Member>(0x00433970)(c, params, nullptr) : c->PlayStream(params, nullptr);
                Record({result});
                break;
            }
            case 5:
            case 6: { // AddStream / RemoveStream
                bool result = original ? Original<Member>(which == 5 ? 0x00433B30 : 0x00433BA0)(c, params, nullptr)
                              : which == 5 ? c->AddStream(params, nullptr)
                                           : c->RemoveStream(params, nullptr);
                Record({result});
                break;
            }
            default: { // the member functions
                static const uint32_t kOriginals[] = {0x00433C70, 0x00433C90, 0x00433CB0, 0x00433930};
                int k = static_cast<int>(which - 7);
                bool result;
                if (original)
                    result = Original<Member>(kOriginals[k])(c, params, nullptr);
                else if (k == 0)
                    result = c->Obj_PlayStream(params, nullptr);
                else if (k == 1)
                    result = c->Obj_AddStream(params, nullptr);
                else if (k == 2)
                    result = c->Obj_RemoveStream(params, nullptr);
                else
                    result = c->Obj_StopStream(params, nullptr);
                Record({result});
                break;
            }
            }
            if (original)
                for (size_t k = before; k < g_trace.size(); ++k)
                    played += g_trace[k] == 0x1E20;
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    Log("  stream component: %d streams played in the original's run", played);
    Check(played > 200, "stream component: the cases reach the streams");
    Compare(traces[0], traces[1], "stream component (create, positions, play, add, remove, stop)");
}

} // namespace

SELFTEST(Early, TestStreamComponent, RemoveStandIns);
