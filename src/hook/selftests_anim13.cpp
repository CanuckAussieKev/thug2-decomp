// Self-tests for AN-15: the animation blender component (src/game/obj/anim_blender.cpp) - the original and ours on
// identical memory with stand-in channels (a fake vtable), observers, script structures and channel methods; the
// calls out (arguments, the blend's buffers), the results and the arena compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/anim_blend.h"
#include "game/net/buddy_status.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Obj::CAnimBlenderComponent;

struct BlenderArena {
    alignas(16) uint8_t heap[0x1800];
    uint8_t object[0x40];
    uint8_t model[0x210];
    uint8_t thing[0x50];
    uint8_t timelines[2][0x40];
    uint8_t observers[3][0x10];
    uint8_t observerNodes[3][0xC];
    uint8_t links[3][0x14];
    float weights[8];
    uint32_t sets[8];
    uint8_t pose[0x40];
    uint8_t params[3][0x10];
    uint8_t script[0x40];
    Mth::Vector vector[2];
    uint8_t manager[0x200];
};
BlenderArena g_bl;
uint32_t g_blUsed;
bool g_blOriginal;
uint8_t g_channelUpdate[16]; // slot 1's answer for each channel, by creation order
int g_channelsMade;
float g_value;               // 0x0047D620's answer
uint32_t g_flagAnswer;       // ContainsFlag's
// the script structures' values: name -> checksum / float, per structure
struct ParamValue {
    uint32_t name;
    uint32_t value;
};
std::vector<ParamValue> g_paramValues[3];

template <typename F>
F BlFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_blOriginal, address));
}
uint32_t P(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
int ParamsIndex(uint32_t params)
{
    for (int i = 0; i < 3; ++i)
        if (params == P(g_bl.params[i]))
            return i;
    return -1;
}
bool Lookup(uint32_t params, uint32_t name, uint32_t* out)
{
    int i = ParamsIndex(params);
    if (i < 0)
        return false;
    for (const ParamValue& v : g_paramValues[i]) {
        if (v.name == name) {
            *out = v.value;
            return true;
        }
    }
    return false;
}
uint32_t BlBump(uint32_t size)
{
    uint32_t at = P(g_bl.heap) + g_blUsed;
    g_blUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0, size);
    return at;
}
uint32_t ChannelIndex(void* channel)
{
    return (P(channel) - P(g_bl.heap)) / 16;
}

// The fake channels' vtable (thiscall emulated with fastcall: ECX the channel, stack arguments popped).
void* __fastcall ChannelDestroy(void* self, void*, uint32_t flags)
{
    Record({0xC0DE0000u, ChannelIndex(self), flags});
    auto* link = static_cast<Mem::ListNode*>(self);
    link->prev->next = link->next;
    link->next->prev = link->prev;
    return self;
}
bool __fastcall ChannelUpdate(void* self, void*, int index, uint32_t flag)
{
    Record({0xC0DE0001u, ChannelIndex(self), static_cast<uint32_t>(index), flag & 0xFF});
    return g_channelUpdate[*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(self) + 0x70) & 15] != 0;
}
void __fastcall ChannelSlot2(void* self, void*) { Record({0xC0DE0002u, ChannelIndex(self)}); }
void __fastcall ChannelSlot3(void* self, void*, uint32_t params) { Record({0xC0DE0003u, ChannelIndex(self), params}); }
int __fastcall ChannelSlot4(void* self, void*, uint32_t name, uint32_t params, uint32_t script)
{
    Record({0xC0DE0004u, ChannelIndex(self), name, params, script});
    return 0x40 + static_cast<int>(ChannelIndex(self));
}
void __fastcall ChannelSlot5(void* self, void*) { Record({0xC0DE0005u, ChannelIndex(self)}); }
void __fastcall ChannelSlot6(void* self, void*, uint32_t value) { Record({0xC0DE0006u, ChannelIndex(self), value}); }
void* g_channelVtable[8] = {reinterpret_cast<void*>(&ChannelDestroy), reinterpret_cast<void*>(&ChannelUpdate),
                            reinterpret_cast<void*>(&ChannelSlot2),   reinterpret_cast<void*>(&ChannelSlot3),
                            reinterpret_cast<void*>(&ChannelSlot4),   reinterpret_cast<void*>(&ChannelSlot5),
                            reinterpret_cast<void*>(&ChannelSlot6),   nullptr};

void* __fastcall ObserverDestroy(void* self, void*, uint32_t flags)
{
    Record({0x0B5E0000u, P(self) - P(g_bl.observers), flags});
    return self;
}
void __fastcall ObserverNotify(void* self, void*, uint32_t pose) { Record({0x0B5E0001u, P(self) - P(g_bl.observers), pose}); }
void* g_observerVtable[2] = {reinterpret_cast<void*>(&ObserverDestroy), reinterpret_cast<void*>(&ObserverNotify)};

// 0x0047D620 (returns a float in ST0)
float __fastcall FakeGetValue(void* self, void*, uint32_t anim)
{
    Record({0x0047D620u, ChannelIndex(self), anim});
    return g_value;
}

// Every function: made, finalized, set up from a structure (the first channel), channels added at the front and the
// back, each first-channel call with and without a channel, the walks, the member functions with ids found and
// not, the update with no change, one channel and several (weights below the threshold after the first, bone sets),
// the observers, the links' destructors, the larger of two floats, destroyed.
void TestAnimBlenderComponent()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x006F8A58, 12}});
    StandIns& s = *new StandIns();
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA110C00u, r.args[0]});
        return BlBump(r.args[0]);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        uint32_t at = BlBump(r.args[0]);
        std::memset(reinterpret_cast<void*>(at), 0xCD, r.args[0]);
        return at;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xC7A00000u, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xC7A10000u});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0x45DB9000u, r.ecx, r.args[0]});
        return r.args[0] == 0x286A8D26 ? P(g_bl.model) : (r.args[0] & 0xFFF0u);
    });
    s.At({0x004739A0, AddressOf(&Script::CScript::SetWaitOnComponent)}, 12, [](Regs& r) {
        Record({0x4739A000u, r.ecx, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
    s.At({0x004ECC30}, 12, [](Regs& r) {
        Record({0x4ECC3000u, r.ecx, r.args[1], r.args[2]});
        auto* head = *reinterpret_cast<uint32_t**>(r.ecx);
        head[1] = P(head);
        head[2] = P(head);
        *reinterpret_cast<uint32_t*>(r.args[0]) = r.args[2];
        return r.args[0];
    });
    // the script structures
    using Script::CStruct;
    // (the original calls the bool-assert getters, ours the plain ones)
    s.At({0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
        Record({0x476EC000u, r.ecx, r.args[0], r.args[2] & 0xFF});
        return static_cast<uint32_t>(Lookup(r.ecx, r.args[0], reinterpret_cast<uint32_t*>(r.args[1])));
    });
    for (uint32_t address : {0x004766D0u, 0x00476D50u}) {
        s.At({address, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))}, 12,
             [](Regs& r) {
            Record({0x47660000u, r.ecx, r.args[0], r.args[2] & 0xFF});
            return static_cast<uint32_t>(Lookup(r.ecx, r.args[0], reinterpret_cast<uint32_t*>(r.args[1])));
        });
    }
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8, [](Regs& r) {
        Record({0x477D4000u, r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x476B4000u, r.ecx, r.args[0]});
        return g_flagAnswer;
    });
    // the channels' methods
    struct Method {
        uint32_t address;
        uint16_t pop;
        uint32_t answer;
        uint32_t ours = 0; // our rewrite the source calls instead, if any
    };
    const Method methods[] = {{0x0047EFF0, 0, 0},  {0x0047E010, 4, 0},    {0x0047EF20, 0, 0},  {0x004803D0, 8, 0},
                              {0x0047D640, 4, 1, AddressOf(&Gfx::CBlendChannel::HasValue)},  {0x0047D420, 4, 0, AddressOf(&Gfx::CBlendChannel::SetFlag4C)},    {0x0047F260, 32, 0}, {0x0047E850, 16, 0},
                              {0x0047E810, 4, 0},  {0x0047E280, 8, 0},    {0x0047E450, 8, 0},  {0x0047DC40, 16, 0},
                              {0x0047E210, 0, 1},  {0x0047EA80, 4, 0},    {0x0047E030, 0, 0},  {0x0047E1B0, 0, 0},
                              {0x0047D430, 8, 0},  {0x00433EA0, 4, 0x101}, {0x004A44E0, 4, 0}};
    for (const Method& m : methods) {
        uint32_t address = m.address, pop = m.pop, answer = m.answer;
        void* thunk = s.At({address}, m.pop, [address, pop, answer](Regs& r) {
            g_trace.push_back(address);
            g_trace.push_back(r.ecx >= P(g_bl.heap) && r.ecx < P(g_bl.heap) + sizeof(g_bl.heap) ? ChannelIndex(reinterpret_cast<void*>(r.ecx)) : r.ecx);
            for (uint32_t k = 0; k < pop / 4u; ++k)
                g_trace.push_back(r.args[k]);
            // 0x0047DC40: true for channel 2 only
            if (address == 0x0047DC40)
                return static_cast<uint32_t>(ChannelIndex(reinterpret_cast<void*>(r.ecx)) % 3 == 2);
            return answer;
        });
        if (m.ours)
            s.Bytes(m.ours, Branch(0xE9, m.ours, thunk));
    }
    s.At({0x004813B0}, 12, [](Regs& r) {
        Record({0x4813B000u, r.ecx});
        reinterpret_cast<float*>(r.args[0])[0] = 1.25f;
        reinterpret_cast<float*>(r.args[1])[0] = 2.5f;
        reinterpret_cast<float*>(r.args[2])[0] = 3.75f;
        return 0u;
    });
    s.At({0x0047F150}, 12, [](Regs& r) {
        Record({0x47F15000u, ChannelIndex(reinterpret_cast<void*>(r.ecx)), r.args[0], r.args[1], r.args[2]});
        auto* channel = reinterpret_cast<uint8_t*>(r.ecx);
        *reinterpret_cast<void***>(channel) = g_channelVtable;
        *reinterpret_cast<uint32_t*>(channel + 4) = r.ecx;
        *reinterpret_cast<uint32_t*>(channel + 0x70) = static_cast<uint32_t>(g_channelsMade++);
        *reinterpret_cast<float*>(channel + 0x30) = g_channelsMade % 3 == 2 ? 0.000001f : 0.25f * g_channelsMade;
        *reinterpret_cast<uint32_t*>(channel + 0x2C) = g_channelsMade % 3 == 2 ? (g_channelsMade == 2 ? 5u : 4u) : static_cast<uint32_t>(g_channelsMade % 3);
        *reinterpret_cast<uint32_t*>(channel + 0x50) = 0xA5E70000u + g_channelsMade;
        *reinterpret_cast<uint32_t*>(channel + 0x54) = g_channelsMade == 3 ? 0 : 0x20000000u;
        *reinterpret_cast<uint32_t*>(channel + 0x58) = 0x58580000u + g_channelsMade;
        *reinterpret_cast<uint32_t*>(channel + 0x20) = g_channelsMade == 1 ? P(g_bl.timelines[0]) : P(g_bl.timelines[1]);
        return r.ecx;
    });
    s.At({0x005480F0, AddressOf(&GameNet::FoldedGetters::Word18)}, 0, [](Regs& r) {
        Record({0x5480F000u, r.ecx});
        return 0x5E1E0000u;
    });
    s.At({0x004A2BA0}, 0, [](Regs& r) {
        Record({0x4A2BA000u, r.ecx});
        return 24u;
    });
    s.At({0x0047D8F0, AddressOf(Gfx::BlendPosesById)}, 0, [](Regs& r) {
        Record({0x47D8F000u, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6]});
        const auto* w = reinterpret_cast<const uint32_t*>(r.args[1]);
        const auto* ids = reinterpret_cast<const uint32_t*>(r.args[2]);
        for (uint32_t k = 0; k < r.args[5]; ++k)
            Record({w[k], ids[k]});
        return 0u;
    });
    s.Bytes(0x0047D620, Branch(0xE9, 0x0047D620, reinterpret_cast<void*>(&FakeGetValue)));
    const uint32_t ourGetValue = AddressOf(&Gfx::CBlendChannel::GetValue);
    s.Bytes(ourGetValue, Branch(0xE9, ourGetValue, reinterpret_cast<void*>(&FakeGetValue)));

    auto runOnce = [&](bool original) {
        g_blOriginal = original;
        g_trace.clear();
        std::memset(&g_bl, 0xB7, sizeof(g_bl));
        std::memset(g_channelUpdate, 0, sizeof(g_channelUpdate));
        g_blUsed = 0;
        g_channelsMade = 0;
        g_value = 7.5f;
        g_flagAnswer = 0;
        GameVariable<void*>(0x007D1800) = g_bl.manager;
        *reinterpret_cast<uint32_t*>(g_bl.manager + 0x110) = 0x70D0110;
        *reinterpret_cast<uint32_t*>(g_bl.manager + 0x114) = 0x80770114;
        GameVariable<bool>(0x007CC979) = true;
        GameVariable<float*>(0x006F8A5C) = g_bl.weights;
        GameVariable<uint32_t*>(0x006F8A58) = g_bl.sets;
        GameVariable<void*>(0x006F8A60) = g_bl.pose;
        for (auto& v : g_paramValues)
            v.clear();
        g_paramValues[0] = {{0x5CFCCF35, 0}, {0x4D786F62, Bits(0.5f)}, {0xBF237659, Bits(1.5f)}};
        g_paramValues[1] = {{0x5CFCCF35, 0x1D2}, {0x4D786F62, Bits(2.0f)}};
        g_paramValues[2] = {{0x5CFCCF35, 0x5555}};
        // the model's flags, the timelines
        *reinterpret_cast<uint32_t*>(g_bl.model + 0x208) = P(g_bl.thing);
        g_bl.thing[0x44] = 2;
        for (int t = 0; t < 2; ++t) {
            *reinterpret_cast<uint32_t*>(g_bl.timelines[t] + 4) = t == 0 ? 0x11u : 0xFFFFFFFFu;
            *reinterpret_cast<float*>(g_bl.timelines[t] + 0x18) = 4.5f + t;
            *reinterpret_cast<uint32_t*>(g_bl.timelines[t] + 0x30) = 0x30300000u + t;
        }
        void* script = g_bl.script;
        *reinterpret_cast<uint32_t*>(g_bl.script + 0x14) = P(g_bl.params[2]);
        auto words = [](const void* p, int n) {
            const auto* w = static_cast<const uint32_t*>(p);
            g_trace.insert(g_trace.end(), w, w + n);
        };
        using Self = CAnimBlenderComponent;
        auto* blender = BlFn<Self*(__cdecl*)()>(0x0041A5B0)();
        Record({P(blender)});
        words(blender->mp_observers, 3);
        {
            uint8_t dirty[0x44];
            std::memset(dirty, 0xE3, sizeof(dirty));
            Record({BlFn<void*(__thiscall*)(void*)>(0x0041A410)(dirty) == dirty});
            words(dirty + 4, 0x40 / 4);
        }
        blender->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_bl.object);
        auto call0 = [&](uint32_t address) { return BlFn<uint32_t(__thiscall*)(void*)>(address)(blender); };
        // with no channel
        BlFn<void(__thiscall*)(void*)>(0x004196D0)(blender);
        call0(0x00419730);
        call0(0x004199C0);
        for (uint32_t address : {0x00419C70u, 0x00419AB0u, 0x00419AD0u, 0x00419CD0u})
            Record({call0(address) & (address == 0x00419C70u || address == 0x00419CD0u ? 0xFFu : 0xFFFFFFFFu)});
        Record({Bits(BlFn<float(__thiscall*)(void*, uint32_t)>(0x00419AF0)(blender, 9)),
                Bits(BlFn<float(__thiscall*)(void*)>(0x00419F80)(blender)), call0(0x0041A0A0),
                BlFn<bool(__thiscall*)(void*, uint32_t)>(0x00419B10)(blender, 9)});
        Record({P(BlFn<void*(__thiscall*)(void*, uint32_t)>(0x00419D40)(blender, 0x1D2))});
        // the first channel, from a structure
        BlFn<void(__thiscall*)(void*, void*)>(0x00419DD0)(blender, g_bl.params[0]);
        BlFn<void(__thiscall*)(void*, void*)>(0x00419DD0)(blender, g_bl.params[0]); // (only with none)
        auto add = BlFn<void*(__thiscall*)(void*, uint32_t, bool)>(0x00419FD0);
        void* second = add(blender, 0x1D2, false);
        void* third = add(blender, 0x3D3, true);
        void* fourth = add(blender, 0x1D2, false);
        Record({ChannelIndex(second), ChannelIndex(third), ChannelIndex(fourth)});
        BlFn<void(__thiscall*)(void*)>(0x004196D0)(blender);
        words(blender, 0x44 / 4);
        // the first channel's calls
        call0(0x00419730);
        BlFn<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00419A90)(blender, 0xA1, 0xA2);
        BlFn<void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00419B50)(
            blender, 1, 2, 3, 4, 5, 6, 7);
        BlFn<void(__thiscall*)(void*, float, float, float, float)>(0x00419B90)(blender, 1.0f, 2.0f, 3.0f, 4.0f);
        BlFn<void(__thiscall*)(void*, uint32_t)>(0x00419BB0)(blender, 0xBB);
        BlFn<void(__thiscall*)(void*, float, uint32_t)>(0x00419BD0)(blender, 0.75f, 0xBD);
        float floats[5] = {1, 2, 3, 4, 5};
        BlFn<void(__thiscall*)(void*, float*, uint32_t)>(0x00419BF0)(blender, floats, 0xBF);
        BlFn<void(__thiscall*)(void*, bool)>(0x00419B30)(blender, true);
        Record({call0(0x00419AB0), call0(0x00419AD0), call0(0x00419CD0) & 0xFF,
                Bits(BlFn<float(__thiscall*)(void*, uint32_t)>(0x00419AF0)(blender, 0x77)),
                BlFn<bool(__thiscall*)(void*, uint32_t)>(0x00419B10)(blender, 0x78),
                Bits(BlFn<float(__thiscall*)(void*)>(0x00419F80)(blender)), call0(0x0041A0A0)});
        float times[3] = {-1, -1, -1};
        BlFn<void(__thiscall*)(void*, float*, float*, float*)>(0x0041A0E0)(blender, &times[0], &times[1], &times[2]);
        words(times, 3);
        *reinterpret_cast<uint32_t*>(g_bl.timelines[1] + 4) = 0x12u;
        BlFn<void(__thiscall*)(void*, float*, float*, float*)>(0x0041A0E0)(blender, &times[0], &times[1], &times[2]);
        words(times, 3);
        *reinterpret_cast<uint32_t*>(g_bl.timelines[1] + 4) = 0xFFFFFFFFu;
        times[0] = times[1] = times[2] = -2.0f;
        BlFn<void(__thiscall*)(void*, float*, float*, float*)>(0x00419CF0)(second, &times[0], &times[1], &times[2]);
        words(times, 3);
        // the walks
        Record({P(BlFn<void*(__thiscall*)(void*, uint32_t)>(0x00419D40)(blender, 0x1D2)),
                P(BlFn<void*(__thiscall*)(void*, uint32_t)>(0x00419D40)(blender, 0x999))});
        BlFn<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00419980)(blender, 0x66, 0x1D2);
        call0(0x004199C0);
        BlFn<void(__thiscall*)(void*, uint32_t)>(0x004199F0)(blender, 0xF0);
        Record({BlFn<bool(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x00419C10)(blender, 1, 2, 3, 4),
                call0(0x00419C70) & 0xFF, call0(0x00419CA0) & 0xFF});
        g_bl.thing[0x44] = 6;
        Record({call0(0x00419CA0) & 0xFF});
        // the member functions
        using Member = int(__thiscall*)(void*, uint32_t, void*, void*);
        for (int p = 0; p < 3; ++p) {
            Record({static_cast<uint32_t>(BlFn<Member>(0x004198D0)(blender, 0x9A, g_bl.params[p], script)),
                    static_cast<uint32_t>(BlFn<Member>(0x00419F10)(blender, 0x9B, g_bl.params[p], script)),
                    static_cast<uint32_t>(BlFn<Member>(0x00419E90)(blender, 0x9C, g_bl.params[p], script)),
                    static_cast<uint32_t>(BlFn<Member>(0x004197C0)(blender, 0x9D, g_bl.params[p], script))});
        }
        Record({static_cast<uint32_t>(BlFn<Member>(0x004198D0)(blender, 0x9A, nullptr, script)),
                static_cast<uint32_t>(BlFn<Member>(0x00419E90)(blender, 0x9C, nullptr, script))});
        for (uint32_t name : {0x0B1E7291u, 0xEA6D0EFDu, 0x12345678u})
            Record({static_cast<uint32_t>(BlFn<Member>(0x00419950)(blender, name, g_bl.params[1], script))});
        g_flagAnswer = 1;
        BlFn<Member>(0x0041A1A0)(blender, 0x9E, g_bl.params[1], script);
        g_flagAnswer = 0;
        BlFn<Member>(0x0041A1A0)(blender, 0x9E, g_bl.params[2], script);
        BlFn<Member>(0x00419750)(blender, 0x9F, g_bl.params[2], script); // (one with 0x5555)
        BlFn<Member>(0x00419750)(blender, 0x9F, g_bl.params[2], script); // (none left)
        // the observers: three nodes on the list
        auto** sentinel = static_cast<uint32_t**>(blender->mp_observers);
        uint32_t* previous = reinterpret_cast<uint32_t*>(sentinel);
        for (int o = 0; o < 3; ++o) {
            *reinterpret_cast<void***>(g_bl.observers[o]) = g_observerVtable;
            auto* node = reinterpret_cast<uint32_t*>(g_bl.observerNodes[o]);
            node[0] = o == 1 ? 0 : P(g_bl.observers[o]);
            node[2] = P(previous);
            previous[1] = P(node);
            previous = node;
        }
        previous[1] = P(sentinel);
        reinterpret_cast<uint32_t*>(sentinel)[2] = P(previous);
        reinterpret_cast<uint32_t*>(g_bl.observerNodes[1])[0] = P(g_bl.observers[1]);
        BlFn<void(__thiscall*)(void*, void*)>(0x0041A220)(blender, g_bl.pose);
        // the update: no change; changes from channel 0 and 2 (some below the weight threshold)
        auto update = [&]() {
            std::memset(g_bl.weights, 0x5A, sizeof(g_bl.weights));
            std::memset(g_bl.sets, 0x5B, sizeof(g_bl.sets));
            call0(0x0041A260);
            words(g_bl.weights, 8);
            words(g_bl.sets, 8);
        };
        update();
        g_channelUpdate[0] = 1;
        update();
        g_channelUpdate[0] = 0;
        g_channelUpdate[4] = 1;
        update();
        // the links' destructors on spare nodes
        for (int l = 0; l < 3; ++l) {
            auto* link = reinterpret_cast<Mem::ListNode*>(g_bl.links[l]);
            link->next = link;
            link->prev = link;
        }
        auto* a = reinterpret_cast<Mem::ListNode*>(g_bl.links[0]);
        auto* b = reinterpret_cast<Mem::ListNode*>(g_bl.links[1]);
        a->next = b;
        a->prev = b;
        b->next = a;
        b->prev = a;
        BlFn<void(__thiscall*)(void*)>(0x00419D70)(a);
        Record({P(BlFn<void*(__thiscall*)(void*, unsigned)>(0x00419D90)(b, 1))});
        Record({P(BlFn<void*(__thiscall*)(void*, unsigned)>(0x0041A170)(g_bl.links[2], 0))});
        // the larger of two floats, the vector at +0x1C
        auto larger = BlFn<float(__cdecl*)(float, float)>(0x004196B0);
        const float pairs[][2] = {{1, 2}, {2, 1}, {3, 3}, {-0.0f, 0.0f}, {std::numeric_limits<float>::quiet_NaN(), 1},
                                  {1, std::numeric_limits<float>::quiet_NaN()}};
        for (const auto& pr : pairs)
            Record({Bits(larger(pr[0], pr[1]))});
        g_bl.vector[0] = {1, 2, 3, 4};
        Mth::Vector got{};
        BlFn<Mth::Vector*(__thiscall*)(const void*, Mth::Vector*)>(0x0041A680)(reinterpret_cast<uint8_t*>(&g_bl.vector[0]) - 0x1C, &got);
        words(&got, 4);
        // reset (all but the first deleted), then destroyed
        call0(0x00419A20);
        words(blender, 0x44 / 4);
        Record({call0(0x00419C70) & 0xFF});
        // one channel left: the single-channel update
        std::memset(g_channelUpdate, 1, sizeof(g_channelUpdate));
        update();
        Record({P(BlFn<void*(__thiscall*)(void*, unsigned)>(0x0041A610)(blender, 1))});
        EndRunPatches();
        std::vector<uint32_t> trace = g_trace;
        const auto* all = reinterpret_cast<const uint32_t*>(&g_bl);
        trace.insert(trace.end(), all, all + (sizeof(g_bl) - sizeof(g_bl.manager)) / 4);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    Log("blender: %u trace words", static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Obj animation blender component");
}
SELFTEST(Early, TestAnimBlenderComponent, TestStandIns::RemoveStandIns);

} // namespace
