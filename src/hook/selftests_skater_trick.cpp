// Self-tests for the trick component's button events and checks (game/skater/trick_component.cpp): the originals
// against ours over random event rings, every call recorded by stand-ins (hook/standins.h).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/dbg.h"
#include "game/obj/object.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skater/trick_component.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Obj::CTrickComponent;
using Obj::TrickEvent;

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

constexpr uint32_t kSize = 0x1C00;
alignas(16) uint8_t g_trick[kSize];
alignas(16) uint8_t g_object[0x200];
alignas(16) uint8_t g_parts[6][0x20];
alignas(16) uint8_t g_balance[0x100];
alignas(16) uint8_t g_input[0x500];
alignas(16) uint8_t g_structs[16][0x10];
alignas(16) uint8_t g_array[0xC];
alignas(16) uint8_t g_nodes[8][0x10];   // a trigger's components (+0 type, +8 value, +0xC next)

uint32_t g_now, g_clockStep;
bool g_found[8];
uint32_t g_checkResults;   // the sequence check's answers, a bit each call
const char* g_names[] = {"Kickflip", "kickFLIP", "Heelflip", "", "Kickflip2"};

// The buttons the events use: the eight trick buttons, the d-pad, others.
const uint32_t kButtons[] = {0xB7231A95, 0x85981897, 0x4B358AEB, 0x786B8B68, 0xA50950C5, 0xBC6B118F, 0xD8847EFA,
                             0xE3006FC4, 0x20689278, 0x2B489A86, 0x321C9756, 0x26B0C991, 0x7323E97C, 0x12345678,
                             0x03749393, 0x7473A305, 0x9A7DC229, 0};

uint32_t Id(uint32_t p)
{
    auto in = [&](const void* base, size_t size, size_t each, uint32_t tag) -> uint32_t {
        auto b = reinterpret_cast<uint32_t>(base);
        if (p >= b && p < b + size)
            return tag + static_cast<uint32_t>((p - b) / each) * 0x1000 + static_cast<uint32_t>((p - b) % each);
        return 0;
    };
    if (!p)
        return 0;
    for (auto r : {in(g_trick, sizeof(g_trick), sizeof(g_trick), 0x91000000),
                   in(g_structs, sizeof(g_structs), sizeof(g_structs[0]), 0x92000000),
                   in(g_parts, sizeof(g_parts), sizeof(g_parts[0]), 0x93000000),
                   in(g_array, sizeof(g_array), sizeof(g_array), 0x94000000),
                   in(g_nodes, sizeof(g_nodes), sizeof(g_nodes[0]), 0x95000000)})
        if (r)
            return r;
    return 0xBAD00000;
}

void StandInAll(StandIns& s, bool standInCheck)
{
    using Script::CStruct;
    s.At({0x005BE970, AddressOf(&Tmr::GetTime)}, 0, [](Regs&) {
        g_now += g_clockStep;
        Record({0x713E, g_now});
        return g_now;
    });
    s.At({0x0044ECE0}, 0, [](Regs&) { Record({0xC10C}); return 777u; });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             bool found = g_found[(r.args[0] ^ (r.args[0] >> 7) ^ Id(r.ecx)) & 7];
             Record({0x6FC5, Id(r.ecx), r.args[0], r.args[2], found});
             if (found)
                 *reinterpret_cast<uint32_t*>(r.args[1]) =
                     r.args[0] == 0 ? 0xBA11 : ((r.args[0] & 0xF0F0F0F0) | 1) ^ ((Id(r.ecx) >> 8) & 0x30);
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [](Regs& r) {
             bool found = g_found[(r.args[0] >> 3) & 7];
             Record({0x6FA7, Id(r.ecx), r.args[0], r.args[2], found});
             if (found)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_array);
             return static_cast<uint32_t>(found);
         });
    s.At({0x00476630, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))}, 12,
         [](Regs& r) {
             bool found = g_found[(r.args[0] >> 11) & 7];
             Record({0x6F17, Id(r.ecx), r.args[0], r.args[2], found});
             if (found)
                 *reinterpret_cast<int*>(r.args[1]) = 42;
             return static_cast<uint32_t>(found);
         });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))}, 12,
         [](Regs& r) {
             bool found = g_found[(r.args[0] >> 13) & 7];
             Record({0x6F57, Id(r.ecx), r.args[0], r.args[2], found});
             if (found)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = reinterpret_cast<uint32_t>(g_structs[5 + (r.args[0] & 3)]);
             return static_cast<uint32_t>(found);
         });
    for (uint32_t address : {0x00476C60u, AddressOf(static_cast<bool (CStruct::*)(uint32_t, const char**, int)>(&CStruct::GetString))})
        s.At({address}, 12, [](Regs& r) {
            bool found = g_found[(r.args[0] >> 2) & 7];
            Record({0x6F5A, Id(r.ecx), r.args[0], r.args[2] & 0xFF, found});
            if (found)
                *reinterpret_cast<const char**>(r.args[1]) = g_names[Id(r.ecx) / 0x1000 % 5];
            return static_cast<uint32_t>(found);
        });
    s.At({0x00626EFB}, 0, [](Regs& r) { // _stricmp (the C runtime isn't started offline)
        const char* a = reinterpret_cast<const char*>(r.args[0]);
        const char* b = reinterpret_cast<const char*>(r.args[1]);
        return static_cast<uint32_t>(_stricmp(a, b));
    });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x6E57, r.args[0]});
        return (r.args[0] & 0x10) ? 0u : reinterpret_cast<uint32_t>(g_structs[10]);
    });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&Script::GetInteger))}, 0, [](Regs& r) {
        Record({0x6E71, r.args[0]});
        return r.args[0] ^ 0x5555;
    });
    s.At({0x00479070, AddressOf(static_cast<Script::CArray* (*)(uint32_t)>(&Script::GetArray))}, 0, [](Regs& r) {
        Record({0x6EA4, r.args[0]});
        return (r.args[0] & 1) ? 0u : reinterpret_cast<uint32_t>(g_array);
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        Record({0x6E1E, Id(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(g_structs[r.args[0] % 5]);
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0xDB6, r.args[0]});
        if (r.args[0] == 0x0064C4D8)
            Record({r.args[1], r.args[2]});
        return 0u;
    });
    if (standInCheck)
    s.At({0x0051FDA0, AddressOf(&CTrickComponent::CheckTrigger)}, 0x10, [](Regs& r) {
        bool yes = (g_checkResults & 1) != 0;
        g_checkResults = (g_checkResults >> 1) | (g_checkResults << 31);
        Record({0x51FD, Id(r.args[0]), r.args[1], r.args[2], r.args[3] & 0xFF, yes});
        return static_cast<uint32_t>(yes);
    });
    s.At({0x0046F060, AddressOf(&Script::RegisterMemberFunctionTable)}, 0, [](Regs& r) {
        Record({0x4E6, r.args[0], r.args[1], r.args[2]});
        return 0u;
    });
}

void Build(Random& r)
{
    for (size_t i = 0; i < sizeof(g_trick); ++i)
        g_trick[i] = static_cast<uint8_t>(r.Next());
    auto* t = reinterpret_cast<CTrickComponent*>(g_trick);
    t->m_object = reinterpret_cast<Obj::CCompositeObject*>(g_object);
    t->m_trickLists = reinterpret_cast<Script::CStruct*>(g_structs[15]);
    t->m_balance1C = g_balance;
    g_balance[0x74] = r.Bit();
    t->m_eventCount = static_cast<uint16_t>(r.OneIn(6) ? 100 : r.Below(30));
    t->m_eventHead = static_cast<uint16_t>(r.OneIn(4) ? r.Below(3) : r.Below(100));
    g_now = 100000 + r.Below(1000);
    g_clockStep = r.OneIn(3) ? 0 : r.Below(r.Bit() ? 5 : 50);
    uint32_t time = g_now;
    int index = t->m_eventHead;
    for (int n = 0; n < 100; ++n) {
        TrickEvent& e = t->m_events[index];
        time -= r.Below(r.Bit() ? 40 : 150);
        e.time = time;
        e.type = r.OneIn(8) ? 0 : 1 + static_cast<int>(r.Below(2));
        e.button = kButtons[r.OneIn(3) ? r.Below(18) : r.Below(4)];
        e.used = r.OneIn(4);
        e.flags = r.OneIn(2) ? 0 : (r.OneIn(2) ? 0x100 : r.Next() & 0x10F);
        if (--index < 0)
            index += 100;
    }
    for (uint32_t& b : t->m_buttonMap)
        b = kButtons[r.Below(18)];
    for (int i = 0; i < CTrickComponent::kButtons; ++i) {
        t->m_down[i] = r.Bit();
        static const float holds[] = {0.0f, 0.0f, 0.0f, 50.0f, 100500.0f, 3e9f, 0.0f};
        t->m_heldOffUntil[i] = holds[r.Below(7)];
        if (t->m_heldOffUntil[i] == 0.0f && r.OneIn(4))
            t->m_heldOffUntil[i] = static_cast<float>(g_now + g_clockStep); // (exactly now)
    }
    t->m_ignoredCount = static_cast<int>(r.Below(3));
    t->m_ignored[0] = kButtons[r.Below(18)];
    t->m_ignored[1] = kButtons[r.Below(18)];
    t->m_queueFirst = r.OneIn(3) ? -1 : static_cast<int>(r.Below(100));
    t->m_queueLast = r.OneIn(3) ? -1 : static_cast<int>(r.Below(100));
    if (r.OneIn(4))
        t->m_queueLast = (t->m_queueFirst + 99) % 100; // full
    for (auto& q : t->m_queue)
        q.list = r.OneIn(3) ? 0xAAAA : (r.Bit() ? 0x100 + r.Below(8) : r.Below(4));
    t->m_extraListCount = static_cast<int>(r.Below(11));
    for (uint32_t& l : t->m_extraLists)
        l = r.OneIn(3) ? 0xAAAA : 0x100 + 2 * r.Below(4);
    t->m_manualList = r.OneIn(3) ? 0 : 0x200 + 2 * r.Below(4);
    for (bool& f : g_found)
        f = r.Bit();
    g_checkResults = r.Next();
    reinterpret_cast<Script::CArray*>(g_array)->m_size = r.Below(6);
    // A trigger's components.
    for (int i = 0; i < 8; ++i) {
        g_nodes[i][0] = static_cast<uint8_t>(r.Below(6));
        At<uint32_t>(g_nodes[i], 8) = kButtons[r.Below(18)];
        At<uint8_t*>(g_nodes[i], 0xC) = g_nodes[(i + 1) % 8];
    }
    // The object's components for Finalize.
    static const uint32_t types[] = {0x27D7CD28, 0x589E5FB3, 0x5BD63B29, 0x488AF07A, 0x43686585, 0x4FF8DAC6};
    uint8_t* next = nullptr;
    for (int i = 0; i < 6; ++i) {
        At<uint32_t>(g_parts[i], 4) = r.OneIn(6) ? 0x1u : types[i];
        At<uint8_t*>(g_parts[i], 0x10) = next;
        next = g_parts[i];
    }
    At<uint8_t*>(g_object, 0x100) = next;
}

void Trace()
{
    const uint32_t* w = reinterpret_cast<const uint32_t*>(g_trick);
    for (size_t i = 0x18 / 4; i < kSize / 4; ++i)
        g_trace.push_back(Id(w[i]) == 0xBAD00000 ? w[i] : Id(w[i]));
}

uint32_t CallButtonStub(void* code, uint32_t button, uint32_t& ecx, uint32_t& edx)
{
    uint32_t result, c = ecx, d = edx;
    __asm {
        mov eax, button
        mov ecx, c
        mov edx, d
        call code
        mov result, eax
        mov c, ecx
        mov d, edx
    }
    ecx = c;
    edx = d;
    return result & 0xFF;
}
void CallMarkUsedStub(void* code, void* trick, uint32_t flags, uint32_t& ecx, uint32_t& edx)
{
    uint32_t c = reinterpret_cast<uint32_t>(trick), d = edx;
    __asm {
        mov ecx, c
        mov edx, d
        push flags
        call code
        mov c, ecx
        mov d, edx
    }
    ecx = c;
    edx = d;
}

void TestTrickEvents()
{
    std::vector<uint32_t> traces[2];
    int matches = 0;
    {
        auto* s = new StandIns;
        StandInAll(*s, true);
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x270F56E1};
            for (int n = 0; n < 6000; ++n) {
                Build(r);
                auto* t = reinterpret_cast<CTrickComponent*>(g_trick);
                int which = static_cast<int>(r.Below(14));
                Record({0xCA5E, static_cast<uint32_t>(which)});
                uint32_t b1 = kButtons[r.Below(r.Bit() ? 4 : 18)], b2 = kButtons[r.Below(r.Bit() ? 4 : 18)];
                uint32_t ms = r.OneIn(2) ? 1000 : (r.OneIn(4) ? r.Below(200) : 300 + r.Below(700));
                uint32_t mask = r.OneIn(2) ? 0 : (r.Bit() ? 0x100 : r.Next() & 0x10F);
                if (g_clockStep == 0 && t->m_eventCount && r.Bit()) // (exactly an event's age)
                    ms = g_now - t->m_events[(t->m_eventHead + 100 - r.Below(t->m_eventCount)) % 100].time;
                size_t before = g_trace.size();
                switch (which) {
                case 0: {
                    bool result = original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(0x0051F6B0)(t, b1, ms, mask)
                                           : t->FindPress(b1, ms, mask);
                    Record({0xA, result});
                    break;
                }
                case 1: {
                    bool result = original ? Original<bool(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0051F7A0)(t, b1, b2, ms, mask)
                                           : t->FindPressBoth(b1, b2, ms, mask);
                    Record({0xB, result});
                    break;
                }
                case 2: {
                    uint32_t buttons[3] = {b1, b2, kButtons[r.Below(r.Bit() ? 4 : 18)]};
                    bool any = r.OneIn(4);
                    bool result = original ? Original<bool(__thiscall*)(void*, const uint32_t*, bool)>(0x0051F9B0)(t, buttons, any)
                                           : t->CheckSpinSequence(buttons, any);
                    Record({0xC, result});
                    break;
                }
                case 3: {
                    bool twice = r.Bit(), setMap = r.Bit();
                    static const uint32_t pad[] = {0x20689278, 0x2B489A86, 0x321C9756};
                    if (r.Bit())
                        b1 = pad[r.Below(3)];
                    bool result = original ? Original<bool(__thiscall*)(void*, uint32_t, bool, bool)>(0x0051FB90)(t, b1, twice, setMap)
                                           : t->CheckReleasePress(b1, twice, setMap);
                    Record({0xD, result});
                    break;
                }
                case 4: {
                    int index = static_cast<int>(r.Below(CTrickComponent::kButtons));
                    bool pressed = r.Bit();
                    if (original)
                        Original<void(__thiscall*)(void*, int, bool)>(0x0051F8F0)(t, index, pressed);
                    else
                        t->UpdateButton(index, pressed);
                    break;
                }
                case 5: {
                    int type = static_cast<int>(r.Below(3));
                    void* e = original ? Original<void*(__thiscall*)(void*, int)>(0x0051F570)(t, type) : t->AddEvent(type);
                    Record({Id(reinterpret_cast<uint32_t>(e))});
                    if (original)
                        Original<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x0051F610)(t, b1, ms);
                    else
                        t->ExpireButton(b1, ms);
                    break;
                }
                case 6: {
                    uint32_t ecx = 0x11111111, edx = 0x22222222;
                    if (original)
                        CallMarkUsedStub(OriginalFunction(0x0051EBD0), t, mask | 0x200, ecx, edx);
                    else
                        CallMarkUsedStub(reinterpret_cast<void*>(&Obj::MarkUsedStub), t, mask | 0x200, ecx, edx);
                    Record({Id(ecx)}); // (EDX: the original leaves its loop pointer there)
                    if (original) {
                        Original<void(__thiscall*)(void*)>(0x0051EB80)(t);
                    } else {
                        t->ClearUsed();
                    }
                    for (uint32_t button : kButtons) {
                        uint32_t c = 0x33333333, d = 0x44444444;
                        uint32_t yes = CallButtonStub(original ? OriginalFunction(0x0051EA40) : reinterpret_cast<void*>(&Obj::IsDirectionStub),
                                                      button, c, d);
                        Record({yes, c, d});
                    }
                    break;
                }
                case 7: {
                    int count = static_cast<int>(r.Below(4));
                    uint32_t out[4] = {9, 9, 9, 9};
                    uint32_t result = original ? Original<uint32_t(__thiscall*)(void*, const uint8_t*, int, uint32_t*)>(0x0051EC40)(t, g_nodes[r.Below(8)], count, out)
                                               : t->ReadTrigger(g_nodes[r.Below(8)], count, out);
                    Record({result, out[0], out[1], out[2], out[3]});
                    uint32_t map[3] = {b1, b2, 0x5A5A};
                    bool down;
                    if (original) {
                        Original<void(__thiscall*)(void*, const uint32_t*)>(0x0051ECC0)(t, map);
                        down = Original<bool(__thiscall*)(void*, uint32_t)>(0x0051ECF0)(t, kButtons[r.Below(8)]);
                    } else {
                        t->SetButtonMap(map);
                        down = t->IsButtonDown(kButtons[r.Below(8)]);
                    }
                    Record({down});
                    break;
                }
                case 8: {
                    uint32_t list = r.Below(4);
                    int index = static_cast<int>(r.Below(30));
                    bool flag = r.Bit();
                    if (original) {
                        if (r.OneIn(4))
                            Original<void(__thiscall*)(void*)>(0x0051ED10)(t);
                        Original<void(__thiscall*)(void*, uint32_t, int, bool)>(0x0051ED20)(t, list, index, flag);
                        Original<void(__thiscall*)(void*, uint32_t)>(0x0051ED90)(t, 0x100 + 2 * r.Below(4));
                    } else {
                        if (r.OneIn(4))
                            t->ClearQueue();
                        t->QueueTrick(list, index, flag);
                        t->RemoveTrickList(0x100 + 2 * r.Below(4));
                    }
                    break;
                }
                case 9: {
                    auto* trick = reinterpret_cast<Script::CStruct*>(g_structs[r.Below(5)]);
                    const char* name = g_names[r.Below(5)];
                    bool a, b;
                    if (original) {
                        a = Original<bool(__thiscall*)(void*, Script::CStruct*)>(0x0051EE10)(t, trick);
                        b = Original<bool(__thiscall*)(void*, Script::CStruct*, const char*)>(0x0051EF50)(t, trick, name);
                    } else {
                        a = t->IsTrickEntry(trick);
                        b = t->TrickNamed(trick, name);
                    }
                    Record({a, b});
                    break;
                }
                case 10: {
                    uint32_t list = 0x400 + 2 * r.Below(3) + (r.OneIn(5) ? 1 : 0);
                    bool flag = r.Bit();
                    uint32_t used = r.Next();
                    if (original) {
                        Original<void(__thiscall*)(void*, uint32_t, bool, uint32_t)>(0x00521930)(t, list, flag, used);
                        Original<void(__thiscall*)(void*)>(0x00521A10)(t);
                        Original<void(__thiscall*)(void*, uint32_t, uint32_t, bool)>(0x00521A80)(t, list, mask, flag);
                    } else {
                        t->QueueDoneTricks(list, flag, used);
                        t->QueueAllDoneTricks();
                        t->SetPendingTrick(list, mask, flag);
                    }
                    break;
                }
                case 11: {
                    if (original) {
                        Original<void(__thiscall*)(void*)>(0x0051F010)(t);
                        Original<void(__thiscall*)(void*)>(0x0051F2B0)(t);
                    } else {
                        t->DumpEventBuffer();
                        t->Finalize();
                    }
                    break;
                }
                case 12: {
                    int index = static_cast<int>(r.Below(CTrickComponent::kButtons));
                    float until = static_cast<float>(r.Below(5000));
                    bool result;
                    if (original) {
                        Original<void(__thiscall*)(void*, int, float)>(0x0051F290)(t, index, until);
                        result = Original<bool(__thiscall*)(void*, void*, void*)>(0x0051F260)(t, g_structs[11], nullptr);
                        Original<void(__cdecl*)()>(0x0051EA90)();
                    } else {
                        t->HoldOffButton(index, until);
                        result = t->SetCurrentBailEndpose(reinterpret_cast<Script::CStruct*>(g_structs[11]), nullptr);
                        CTrickComponent::RegisterMemberFunctions();
                    }
                    Record({result});
                    break;
                }
                default: {
                    static const uint32_t resets[] = {0x0051F320, 0x0051EAB0, 0x0051EB30, 0x0051EF00, 0x0051EB00};
                    int k = static_cast<int>(r.Below(5));
                    if (original) {
                        Original<void(__thiscall*)(void*)>(resets[k])(t);
                    } else {
                        switch (k) {
                        case 0: t->Reset(); break;
                        case 1: t->ClearExtraTricks(); break;
                        case 2: t->ClearBlock1238(); break;
                        case 3: t->ClearBlock1280(); break;
                        default: t->ClearEventBuffer(); break;
                        }
                    }
                    break;
                }
                }
                if (original)
                    for (size_t k = before; k + 1 < g_trace.size(); ++k)
                        matches += (g_trace[k] >= 0xA && g_trace[k] <= 0xD) && g_trace[k + 1] == 1;
                Trace();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    Log("  trick events: %d checks matched by the original", matches);
    Check(matches > 100, "trick events: the checks match now and then");
    Compare(traces[0], traces[1], "trick events: presses, sequences, buttons, queue, lists, resets");
}

// The trigger check (0x0051FDA0): every kind over random event rings (often of just the trigger's buttons).
alignas(16) uint8_t g_physics[0x300];
alignas(16) uint8_t g_kind[0x10];
alignas(16) uint8_t g_triggerNodes[6][0x10];
bool g_noKind;

const uint32_t kKinds[] = {0x823B8342, 0x7FA5CDBB, 0x61B8FCE2, 0x7D482318, 0xA2E042D7, 0xC1AD35C0, 0x3FE7C311,
                           0x3C3028F4, 0x395EC2D4, 0x14FE9B7D, 0x2C434C90, 0xA8123ECF, 0xFFB718BD, 0x696E5E66,
                           0x3F369070, 0xF630DAE5, 0xAC2B1445, 0xB6258557, 0x12345678};
const int kKindButtons[] = {1, 1, 1, 2, 3, 3, 3, 2, 2, 2, 2, 3, 2, 2, 2, 3, 3, 1, 2};
constexpr int kKindCount = sizeof(kKinds) / sizeof(kKinds[0]);

void TestTrickTriggers()
{
    std::vector<uint32_t> traces[2];
    int found[2][kKindCount] = {};
    {
        auto* s = new StandIns;
        StandInAll(*s, false);
        s->At({0x00476490, AddressOf(&Script::CStruct::GetNextComponent)}, 4, [](Regs& r) {
            Record({0x6C0, Id(r.ecx), r.args[0]});
            return g_noKind ? 0u : reinterpret_cast<uint32_t>(g_kind);
        });
        for (int run = 0; run < 2; ++run) {
            bool original = run == 0;
            g_trace.clear();
            Random r{0x0051FDA0};
            for (int n = 0; n < 12000; ++n) {
                Build(r);
                auto* t = reinterpret_cast<CTrickComponent*>(g_trick);
                t->m_input = g_input;
                g_input[0x498] = r.OneIn(20);
                g_input[0x499] = r.OneIn(20);
                t->m_corePhysics = g_physics;
                static const float rates[] = {1.0f, 0.5f, 0.3f, 0.25f, 2.0f, 0.0f, -0.5f, 0.999f};
                At<float>(g_physics, 0x2E8) = rates[r.Below(8)];
                int kind = static_cast<int>(r.Below(kKindCount));
                g_noKind = r.OneIn(40);
                At<uint32_t>(g_kind, 8) = kKinds[kind];
                At<uint8_t*>(g_kind, 0xC) = g_triggerNodes[0];
                // The trigger's buttons: the common four, the map's names, the d-pad, others.
                static const uint32_t names[] = {0x9A7DC229, 0x03749393, 0x7473A305};
                uint32_t pool[3];
                for (int i = 0; i < 3; ++i) {
                    uint32_t b = r.OneIn(8) ? names[r.Below(3)] : kButtons[r.OneIn(5) ? r.Below(18) : r.Below(4)];
                    pool[i] = b;
                    g_triggerNodes[i][0] = static_cast<uint8_t>(r.Below(6) << 1);
                    At<uint32_t>(g_triggerNodes[i], 8) = b;
                    At<uint8_t*>(g_triggerNodes[i], 0xC) = g_triggerNodes[i + 1];
                }
                for (int i = 3; i < 6; ++i) {
                    g_triggerNodes[i][0] = 0;
                    At<uint32_t>(g_triggerNodes[i], 8) = kButtons[r.Below(4)];
                    At<uint8_t*>(g_triggerNodes[i], 0xC) = g_triggerNodes[(i + 1) % 6];
                }
                // Often events of just the trigger's buttons (as mapped), mostly without flags.
                uint32_t mapped[3];
                for (int i = 0; i < 3; ++i)
                    mapped[i] = pool[i] == names[0]   ? t->m_buttonMap[0]
                                : pool[i] == names[1] ? t->m_buttonMap[1]
                                : pool[i] == names[2] ? t->m_buttonMap[2]
                                                      : pool[i];
                if (r.Bit()) {
                    int spread = 1 + static_cast<int>(r.Below(3));
                    for (TrickEvent& e : t->m_events) {
                        e.button = r.OneIn(8) ? kButtons[r.Below(18)] : mapped[r.Below(spread)];
                        e.type = r.OneIn(16) ? 0 : 1 + static_cast<int>(r.Below(2));
                        if (!r.OneIn(4))
                            e.flags = 0;
                    }
                    if (r.Bit())
                        t->m_eventCount = static_cast<uint16_t>(20 + r.Below(81));
                }
                if (kKinds[kind] == 0x696E5E66 && r.Bit()) { // (ReleaseAndTap's three, newest first)
                    static const int types[] = {2, 1, 2}, which[] = {1, 1, 0};
                    for (int i = 0; i < 3; ++i) {
                        TrickEvent& e = t->m_events[(t->m_eventHead + 100 - i) % 100];
                        e.type = types[i];
                        e.button = mapped[which[i]];
                        e.flags = 0;
                    }
                }
                // The time after the buttons: a number or a global's name.
                uint32_t ms = r.OneIn(2) ? 1000 : (r.OneIn(4) ? r.Below(200) : 300 + r.Below(1500));
                if (g_clockStep == 0 && t->m_eventCount && r.OneIn(3)) // (exactly an event's age)
                    ms = g_now - t->m_events[(t->m_eventHead + 100 - r.Below(t->m_eventCount)) % 100].time;
                int count = r.OneIn(20) ? static_cast<int>(r.Below(4)) : kKindButtons[kind];
                bool number = r.Bit();
                g_triggerNodes[count][0] = number ? 2 : 0x1A;
                At<uint32_t>(g_triggerNodes[count], 8) = number ? ms : ms ^ 0x5555;
                auto* trigger = r.OneIn(40) ? nullptr : reinterpret_cast<Script::CStruct*>(g_structs[r.Below(4)]);
                uint32_t markFlags = r.OneIn(4) ? 1 : (r.Bit() ? 4 : r.Next() & 0x30F);
                uint32_t mask = r.OneIn(2) ? 0 : (r.Bit() ? 0x100 : r.Next() & 0x10F);
                bool scaled = r.Bit();
                Record({0xC4EC, static_cast<uint32_t>(kind), markFlags, mask, scaled});
                bool result = original
                                  ? Original<bool(__thiscall*)(void*, Script::CStruct*, uint32_t, uint32_t, bool)>(0x0051FDA0)(
                                        t, trigger, markFlags, mask, scaled)
                                  : t->CheckTrigger(trigger, markFlags, mask, scaled);
                Record({0xE, result});
                found[run][kind] += result;
                Trace();
            }
            traces[run] = g_trace;
        }
        delete s;
    }
    int total = 0;
    for (int k = 0; k < kKindCount; ++k) {
        Log("  trick triggers: %08X found %d times by the original", kKinds[k], found[0][k]);
        total += found[0][k];
        if (k + 1 < kKindCount)
            Check(found[0][k] > 5, "trick triggers: every kind is found now and then");
    }
    Check(total > 1000, "trick triggers: found often");
    Compare(traces[0], traces[1], "trick triggers: every kind over random events");
}

} // namespace

SELFTEST(Early, TestTrickEvents, RemoveStandIns);
SELFTEST(Early, TestTrickTriggers, RemoveStandIns);
