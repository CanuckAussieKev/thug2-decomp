// Self-tests for the sound component (game/obj/sound_component.cpp): the originals against ours on stand-in objects,
// every call into other code recorded by stand-ins (hook/standins.h) - the sound effects manager, the cameras, the
// emitter and proxim nodes, the clock, the script structures - and the component compared afterwards.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/emitter.h"
#include "game/obj/object.h"
#include "game/obj/proxim.h"
#include "game/obj/sound_component.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sfx/sfx.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CSoundComponent;

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
    alignas(16) uint8_t component[0xC0 + 0x40];
    alignas(16) uint8_t object[0x120];
    alignas(16) uint8_t manager[0x20];
    alignas(16) uint8_t playing[0x30]; // the props the manager has
    alignas(16) uint8_t camera[0x10];
    alignas(16) uint8_t cameraPos[0x10];
    alignas(16) uint8_t emitter[0x40];
    alignas(16) uint8_t proxim[0x60];
    alignas(16) uint8_t closest[0x10];
    alignas(16) uint8_t params[0x10];
    alignas(16) uint8_t props[0x30];
    alignas(16) uint8_t block[0xC0];
    void* vtable[16];
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
    bool callocFails, havePlaying, haveCamera;
    uint32_t playId;
    uint32_t found;
    uint32_t seed;
    uint32_t time;
    float distance;
    float floats[4];
};
Answers g_a;

uint32_t NameBit(uint32_t name) { return 1u << ((name * 2654435761u) >> 27); }
bool Found(uint32_t name) { return (g_a.found & NameBit(name)) != 0; }

void StandIn(StandIns& s)
{
    using Script::CStruct;
    using Sfx::CSfxManager;
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
    // The sound effects manager.
    s.At({0x0047BB50, AddressOf(&CSfxManager::StopObjectSounds)}, 4, [](Regs& r) {
        Record({0xBB50, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x0047BC30, AddressOf(&CSfxManager::StopObjectSound)}, 8, [](Regs& r) {
        Record({0xBC30, Rel(r.ecx), Rel(r.args[0]), r.args[1]});
        return 0u;
    });
    s.At({0x0047BA00, AddressOf(&CSfxManager::GetPositionalProps)}, 12, [](Regs& r) {
        Record({0xBA00, Rel(r.ecx), Rel(r.args[0]), r.args[1], r.args[2] & 0xFF});
        return g_a.havePlaying ? reinterpret_cast<uint32_t>(g_world.playing) : 0u;
    });
    s.At({0x0047C140, AddressOf(&CSfxManager::PlayObjectSound)}, 20, [](Regs& r) {
        const uint32_t* p = reinterpret_cast<const uint32_t*>(r.args[1]);
        Record({0xC140, Rel(r.ecx), r.args[0], p[0], p[1], p[2], p[3], Rel(r.args[2]), r.args[3] & 0xFF,
                r.args[4] & 0xFF});
        return g_a.playId;
    });
    s.At({0x0047AC10, AddressOf(Sfx::FalloffFromChecksum)}, 0, [](Regs& r) {
        Record({0xAC10, r.args[0]});
        return r.args[0] & 3;
    });
    // Nodes and cameras.
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
    s.At({0x00485A00}, 0, [](Regs& r) {
        Record({0x5A00, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_world.cameraPos);
    });
    s.At({0x004A09E0}, 0, [](Regs& r) {
        Record({0x09E0, Rel(r.args[0])});
        *reinterpret_cast<float*>(r.args[1]) = g_a.distance;
        return g_a.haveCamera ? reinterpret_cast<uint32_t>(g_world.camera) : 0u;
    });
    s.At({0x005BE970, AddressOf(Tmr::GetTime)}, 0, [](Regs&) {
        Record({0xE970});
        return g_a.time;
    });
    g_world.vtable[13] = s.Thunk(12, [](Regs& r) {
        Record({0xC013, Rel(r.ecx), r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    // The script structures.
    auto getter = [](uint32_t tag) {
        return [tag](Regs& r) {
            uint32_t name = tag == 0x6680 ? Hash(reinterpret_cast<const char*>(r.args[0])) : r.args[0];
            // (the original's dropoff_function slot is left as it was - Percent's, or garbage)
            bool asserted = r.args[2] != 0 || name == 0x0C6AC50A;
            Record({tag, Rel(r.ecx), name, asserted ? 0xDEADu : Rel(*reinterpret_cast<uint32_t*>(r.args[1])), r.args[2]});
            if (!Found(name))
                return 0u;
            uint32_t* out = reinterpret_cast<uint32_t*>(r.args[1]);
            if (tag == 0x66D0) {
                float f = g_a.floats[(name * 7u) >> 30];
                std::memcpy(out, &f, 4);
            } else if (tag == 0x6630 || tag == 0x6680) {
                *out = static_cast<uint32_t>(static_cast<int>(g_a.seed % 12) - 2);
            } else {
                *out = name == 0 ? 0x50000000 + (g_a.seed & 3) : name ^ g_a.seed;
            }
            return 1u;
        };
    };
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         getter(0x6950));
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6630));
    s.At({0x00476680, AddressOf(static_cast<bool (CStruct::*)(const char*, int*, int)>(&CStruct::GetInteger))}, 12,
         getter(0x6680));
    s.At({0x004766D0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
         getter(0x66D0));
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [](Regs& r) {
             Record({0x6AF0, Rel(r.ecx), r.args[0]});
             return static_cast<uint32_t>(Found(r.args[0] ^ 0x1111));
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, Rel(r.ecx), r.args[0]});
        return static_cast<uint32_t>(Found(r.args[0] ^ 0x2222));
    });
}

// GetSlomoFrameSeconds returns its double in ST0: this stand-in (written over both) loads it.
double g_frameSeconds;
__declspec(naked) void FrameSecondsStandIn()
{
    __asm {
        fld qword ptr [g_frameSeconds]
        ret
    }
}

void FillProps(Random& r, uint8_t* p, uint32_t checksum)
{
    auto* props = reinterpret_cast<Sfx::PlaybackProps*>(p);
    props->checksum = checksum;
    props->dropoff = r.OneIn(4) ? 0.0f : r.Value();
    props->pitch = r.Value();
    props->volume = r.Value();
    props->pitch2 = r.Value();
    props->volume2 = r.Value();
    props->pitch3 = r.OneIn(3) ? props->pitch : r.Value();
    props->volume3 = r.OneIn(3) ? props->volume : r.Value();
    float step1 = r.Value() / 100.0f, step2 = r.Value() / 100.0f;
    std::memcpy(&props->unknown20, &step1, 4);
    std::memcpy(&props->unknown24, &step2, 4);
    props->falloff = r.Below(4);
    At<uint32_t>(p, 0x2C) = r.Below(4000);
}

void Build(Random& r)
{
    std::memset(g_world.component, 0, sizeof(g_world.component));
    std::memset(g_world.props, 0xC7, sizeof(g_world.props));
    std::memset(g_world.block, 0xB1, sizeof(g_world.block));
    auto* c = reinterpret_cast<CSoundComponent*>(g_world.component);
    c->m_vtable = g_world.vtable;
    c->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_world.object);
    for (uint32_t& s : c->m_sounds)
        s = r.OneIn(3) ? 0u : 0x5D000000 + r.Below(5);
    for (int i = 0; i < 2; ++i)
        FillProps(r, g_world.component + 0x38 + 0x30 * i, r.OneIn(3) ? 0u : 0x50000000 + r.Below(4));
    for (int i = 0x98; i < 0xB8; i += 4)
        At<float>(g_world.component, i) = r.Value();
    c->mp_emitter = r.Bit() ? reinterpret_cast<Obj::EmitterNode*>(g_world.emitter) : nullptr;
    c->mp_proxim = r.Bit() ? reinterpret_cast<Obj::ProximNode*>(g_world.proxim) : nullptr;
    for (int i = 0x4C; i < 0x5C; i += 4)
        At<float>(g_world.object, i) = r.Value();
    FillProps(r, g_world.playing, 0x50000001);
    for (int i = 0; i < 16; i += 4) {
        At<float>(g_world.closest, i) = r.Value();
        At<float>(g_world.cameraPos, i) = r.Value();
    }
    g_a = {};
    g_a.callocFails = r.OneIn(6);
    g_a.havePlaying = r.Bit();
    g_a.haveCamera = !r.OneIn(5);
    g_a.playId = r.OneIn(4) ? 0u : 0x1D000000 + r.Below(100);
    g_a.found = r.Next() ^ (r.Next() << 12);
    g_a.seed = r.Next();
    g_a.time = r.Below(5000);
    g_a.distance = r.Value() * 3.0f;
    g_frameSeconds = r.OneIn(3) ? 1.0 / 60.0 : r.Value() / 1000.0;
    for (float& f : g_a.floats)
        f = r.OneIn(5) ? 0.0f : r.Value();
    // Edges: a waiting sound due exactly now; the camera exactly at the edge of hearing (240 past the dropoff) - in
    // the float sum, which may round below the exact one.
    auto* first = reinterpret_cast<Sfx::PlaybackProps*>(g_world.component + 0x38);
    if (r.OneIn(4))
        At<uint32_t>(g_world.component, 0x38 + 0x2C) = g_a.time;
    if (r.OneIn(3)) {
        if (r.Bit())
            first->dropoff = static_cast<float>(r.Below(1000)) + 1.0f / static_cast<float>(1 + r.Below(5000));
        float d = first->dropoff == 0.0f ? 1020.0f : first->dropoff;
        g_a.distance = 240.0f + d;
    }
}

void Trace(std::vector<uint32_t>& trace)
{
    for (const uint8_t* area : {g_world.component, g_world.playing, g_world.props, g_world.block})
        for (size_t i = 0; i < (area == g_world.component || area == g_world.block ? 0xC0u : 0x30u); i += 4)
            trace.push_back(Rel(*reinterpret_cast<const uint32_t*>(area + i)));
}

void TestSoundComponent()
{
    std::vector<Saved> saved = {{0x006F85A8, 4, {}}};
    SaveAll(saved);
    GameVariable<void*>(0x006F85A8) = g_world.manager;
    auto* s = new StandIns;
    StandIn(*s);
    for (uint32_t address : {0x005BEAA0u, AddressOf(Tmr::GetSlomoFrameSeconds)})
        s->Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&FrameSecondsStandIn)));
    std::vector<uint32_t> traces[2];
    int played = 0, waited = 0;
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        Random r{0x50D0};
        for (uint32_t n = 0; n < 5000; ++n) {
            Build(r);
            auto* c = reinterpret_cast<CSoundComponent*>(g_world.component);
            auto* params = reinterpret_cast<Script::CStruct*>(g_world.params);
            uint32_t which = r.Below(14);
            Record({0xCA5E, which});
            size_t before = g_trace.size();
            switch (which) {
            case 0: { // Create / Construct, RegisterMemberFunctions, Destroy
                void* made = original ? Original<void*(__cdecl*)()>(0x00433100)() : CSoundComponent::Create();
                Record({Rel(reinterpret_cast<uint32_t>(made))});
                if (original)
                    Original<void(__cdecl*)()>(0x00432C80)();
                else
                    CSoundComponent::RegisterMemberFunctions();
                unsigned flags = r.Below(4);
                void* result = original ? Original<void*(__thiscall*)(void*, unsigned)>(0x00433160)(c, flags) : c->Destroy(flags);
                Record({Rel(reinterpret_cast<uint32_t>(result))});
                break;
            }
            case 1: // InitFromStructure
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00432CA0)(c, params);
                else
                    c->InitFromStructure(params);
                break;
            case 2:
            case 3: // Update (the waiting sounds)
                if (original)
                    Original<void(__thiscall*)(void*)>(0x004335D0)(c);
                else
                    c->Update();
                break;
            case 4: { // CallMemberFunction
                static const uint32_t kNames[] = {0x3E5E8023, 0x4E4132D6, 0xE8347171, 0x12345678};
                uint32_t name = kNames[r.Below(4)];
                int result = original ? Original<int(__thiscall*)(void*, uint32_t, void*, void*)>(0x00433190)(
                                            c, name, params, nullptr)
                                      : c->CallMemberFunction(name, params, nullptr);
                Record({static_cast<uint32_t>(result)});
                break;
            }
            case 5: { // KeepLoopingSound
                FillProps(r, g_world.props, 0x5E000000 + n);
                uint32_t time = r.Below(9000);
                if (original)
                    Original<void(__thiscall*)(void*, const void*, uint32_t)>(0x00432D40)(c, g_world.props, time);
                else
                    c->KeepLoopingSound(reinterpret_cast<const Sfx::PlaybackProps*>(g_world.props), time);
                break;
            }
            case 6: { // the positions and the extent
                Mth::Vector out = {1.0f, 2.0f, 3.0f, 4.0f};
                void* camera = r.OneIn(4) ? nullptr : g_world.camera;
                uint32_t a, b;
                bool extent;
                if (original) {
                    a = Rel(reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, void*, void*)>(0x00432E50)(c, &out, camera)));
                    Record({Bits(out.x), Bits(out.w)});
                    b = Rel(reinterpret_cast<uint32_t>(Original<void*(__thiscall*)(void*, void*, void*)>(0x00432EB0)(c, &out, camera)));
                    Record({Bits(out.x), Bits(out.w)});
                    extent = Original<bool(__thiscall*)(void*, void*, void*)>(0x00432F10)(c, camera, &out);
                } else {
                    a = Rel(reinterpret_cast<uint32_t>(c->GetSoundPosition(&out, camera)));
                    Record({Bits(out.x), Bits(out.w)});
                    b = Rel(reinterpret_cast<uint32_t>(c->GetPreviousSoundPosition(&out, camera)));
                    Record({Bits(out.x), Bits(out.w)});
                    extent = c->GetSoundExtent(camera, &out);
                }
                Record({a, b, extent, Bits(out.x), Bits(out.y), Bits(out.z), Bits(out.w)});
                break;
            }
            case 7: // UpdateProps
                FillProps(r, g_world.props, 0x5E000000 + n);
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00432F70)(c, g_world.props);
                else
                    c->UpdateProps(reinterpret_cast<Sfx::PlaybackProps*>(g_world.props));
                break;
            case 8:
            case 9:
            case 10: { // PlaySound / Obj_PlaySound
                g_a.found |= NameBit(0x9E497FC6); // (Percent, asserted)
                bool result = true;
                if (which == 10) {
                    result = original ? Original<bool(__thiscall*)(void*, void*, void*)>(0x00433630)(c, params, nullptr)
                                      : c->Obj_PlaySound(params, nullptr);
                } else if (original) {
                    Original<void(__thiscall*)(void*, void*)>(0x00433240)(c, params);
                } else {
                    c->PlaySound(params);
                }
                Record({result});
                break;
            }
            case 11: // AdjustSound
                if (original)
                    Original<void(__thiscall*)(void*, void*, void*)>(0x00432FF0)(c, params, nullptr);
                else
                    c->AdjustSound(params, nullptr);
                break;
            default: // SetSound
                if (original)
                    Original<void(__thiscall*)(void*, void*)>(0x00432CE0)(c, params);
                else
                    c->SetSound(params);
                break;
            }
            if (original)
                for (size_t k = before; k < g_trace.size(); ++k) {
                    played += g_trace[k] == 0xC140;
                    waited += g_trace[k] == 0x09E0;
                }
            Trace(g_trace);
        }
        traces[run] = g_trace;
    }
    delete s;
    RestoreAll(saved);
    Log("  sound component: %d sounds played, %d waiting sounds looked at in the original's run", played, waited);
    Check(played > 500 && waited > 400, "sound component: the cases reach the sounds");
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
    Compare(traces[0], traces[1], "sound component (create, update, waiting sounds, play, adjust, member functions)");
}

} // namespace

SELFTEST(Early, TestSoundComponent, RemoveStandIns);
