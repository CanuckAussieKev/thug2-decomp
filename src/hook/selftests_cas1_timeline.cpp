// Self-tests for the animation timeline, the blend channel's timeline, the base controller and event, and
// Mth::PointInBox (src/game/gfx/anim_timeline.cpp, src/game/mth/geometry.cpp): original and ours on timelines in an
// arena - random plays, steps at random slow-motion, added time, list unlinking, controllers with a recording vtable,
// the script structure and component lookups stood in; the results and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/alloc.h"
#include "game/core/manager.h"
#include "game/gfx/anim_track.h"
#include "game/mth/geometry.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Gfx::CAnimController;
using Gfx::CAnimTimeline;
using Gfx::CAnimTrack;

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kTimes[] = {0.0f, 1.0f, 2.5f, -3.0f, 10.0f, 30.5f, 100.0f, 0.25f, 7.0f, 7.0f, -0.0f, kNaN};
const float kSpeeds[] = {0.0f, 1.0f, 0.5f, 2.0f, -1.0f, 3.3f, 10.0f, 0.1f, kNaN};
const float kSlomos[] = {1.0f, 1.0f, 0.5f, 2.0f, 0.0f, 0.3f};

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

void TraceTimeline(std::vector<uint32_t>& trace, const CAnimTimeline* t)
{
    auto* words = reinterpret_cast<const uint32_t*>(t);
    for (int i = 0; i < 0x34 / 4; ++i)
        trace.push_back(ArenaRelocate(words[i]));
}

// The recording vtable of the stand-in controllers: 0 (the scalar deleting destructor) and 1 (the pose).
void* g_controllerVtable[2];
uint32_t g_checksumAnswer;
bool g_checksumFound;
uint32_t g_component; // GetComponent's answer

void StandInTimelineCalls(StandIns& s)
{
    StandInMemory(s);
    g_controllerVtable[0] = s.Thunk(4, [](Regs& r) {
        Record({0xC0, ArenaRelocate(r.ecx), r.args[0]});
        return r.ecx;
    });
    g_controllerVtable[1] = s.Thunk(4, [](Regs& r) {
        Record({0xC1, ArenaRelocate(r.ecx), ArenaRelocate(r.args[0])});
        return 0x33u;
    });
    s.At({0x00476950, AddressOf(static_cast<bool (Script::CStruct::*)(uint32_t, uint32_t*, int)>(
                          &Script::CStruct::GetChecksum))},
         12, [](Regs& r) {
             Record({0x6950, ArenaRelocate(r.ecx), r.args[0], ArenaRelocate(r.args[1]), r.args[2]});
             if (g_checksumFound)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = g_checksumAnswer;
             return g_checksumFound ? 0x01u : 0x7700u;
         });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, ArenaRelocate(r.ecx), r.args[0]});
        return g_component;
    });
}

// A head node and nodes linked after it (as the channel's list), for the unlinking destructors.
void LinkAfter(Mem::ListNode* head, Mem::ListNode* node)
{
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

void TestCas1Timeline()
{
    GameMemory& memory = *new GameMemory({{0x00684FC8, 4}, {0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInTimelineCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("timeline", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x7100 + round);
            std::vector<uint32_t> trace;
            auto* head = reinterpret_cast<Mem::ListNode*>(Bump(0x14));
            head->next = head->prev = head;
            CAnimTimeline* timelines[4];
            for (CAnimTimeline*& t : timelines) {
                t = reinterpret_cast<CAnimTimeline*>(Bump(sizeof(CAnimTimeline)));
                trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481650)(t)));
            }
            for (int step = 0; step < 40; ++step) {
                g_trace.clear();
                CAnimTimeline* t = timelines[random.Below(4)];
                GameVariable<float>(0x00684FC8) = random.Pick(kSlomos);
                uint32_t op = random.Below(11);
                trace.push_back(op);
                switch (op) {
                case 0:
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481650)(t)));
                    break;
                case 1:
                    Fn<void(__thiscall*)(void*)>(original, 0x00481380)(t);
                    break;
                case 2:
                case 3: {
                    float from = random.Pick(kTimes), to = random.Pick(kTimes);
                    if (std::isnan(from) && random.Below(2))
                        from = 4.0f;
                    int loopType = static_cast<int>(random.Below(5));
                    if (random.Below(3) == 0) { // (cycling backwards over a short range: wrapped many times)
                        loopType = CAnimTimeline::kCycle;
                        from = 1.0f;
                        to = 0.25f;
                    }
                    Fn<void(__thiscall*)(void*, uint32_t, float, float, int, float, float)>(original, 0x00481430)(
                        t, random.Next(), from, to, loopType, 1.5f, random.Pick(kSpeeds));
                    break;
                }
                case 4:
                case 5: {
                    double time = Fn<double(__thiscall*)(void*)>(original, 0x004814B0)(t);
                    uint64_t bits = std::bit_cast<uint64_t>(time);
                    trace.push_back(static_cast<uint32_t>(bits));
                    trace.push_back(static_cast<uint32_t>(bits >> 32));
                    break;
                }
                case 6:
                    Fn<void(__thiscall*)(void*)>(original, 0x00481640)(t);
                    break;
                case 7: {
                    const float frames[] = {0.0f, 1.0f, 5.5f, -2.0f, 40.0f, 0.333f, kNaN};
                    Fn<void(__thiscall*)(void*, float)>(original, 0x004816B0)(t, random.Pick(frames));
                    break;
                }
                case 8: {
                    float a = 9.0f, b = 9.0f, c = 9.0f;
                    if (random.Below(4) == 0)
                        t->m_direction = static_cast<int>(random.Below(3)) - 1; // (a direction no play sets: 1)
                    Fn<void(__thiscall*)(void*, float*, float*, float*)>(original, 0x004813B0)(t, &a, &b, &c);
                    trace.insert(trace.end(), {Bits(a), Bits(b), Bits(c)});
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004813F0)(t) & 0xFF);
                    break;
                }
                case 9: { // the current time at an end, or the loop type changed
                    if (random.Below(2))
                        t->m_current = random.Below(2) ? t->m_end : t->m_start;
                    else
                        t->m_loopType = static_cast<int>(random.Below(5));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004813F0)(t) & 0xFF);
                    break;
                }
                default: { // unlinked from the list (its body or freed), then made again
                    auto* node = reinterpret_cast<Mem::ListNode*>(t);
                    LinkAfter(head, node);
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x00481690)(t);
                    else
                        trace.push_back(ArenaRelocate(
                            Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004816E0)(t, random.Below(4))));
                    trace.push_back(reinterpret_cast<uint32_t>(t->m_vtable));
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t>(head->next)));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481650)(t)));
                    break;
                }
                }
                TraceTimeline(trace, t);
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "timeline: made, reset, played, stepped (every direction / loop type, NaN), added time, times, complete, "
              "unlinked / freed");
}

void TestCas1BlendTimeline()
{
    GameMemory& memory = *new GameMemory({{0x007D1800, 4}});
    StandIns& s = *new StandIns();
    StandInTimelineCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 80; ++round) {
        ok &= CompareTwoRuns("blend timeline", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x7300 + round);
            std::vector<uint32_t> trace;
            auto* t = reinterpret_cast<CAnimTrack*>(Bump(sizeof(CAnimTrack)));
            uint32_t object = Bump(0x40);
            uint32_t component = Bump(0x20);
            std::memset(reinterpret_cast<void*>(component), 0x11, 0x20);
            reinterpret_cast<uint32_t*>(component)[6] = 0x5E1E7000 + round; // its skeleton (+0x18)
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00481830)(t, object)));
            auto* events = reinterpret_cast<uint8_t*>(Bump(0x14 * 4));
            std::vector<CAnimController*> controllers;
            for (int i = 0; i < 8; ++i) {
                auto* c = reinterpret_cast<CAnimController*>(Bump(sizeof(CAnimController)));
                controllers.push_back(c);
            }
            for (int step = 0; step < 30; ++step) {
                g_trace.clear();
                uint32_t op = random.Below(10);
                trace.push_back(op);
                switch (op) {
                case 0: { // controllers set up by hand (some null)
                    int count = static_cast<int>(random.Below(7));
                    t->m_controllerCount = count;
                    for (int i = 0; i < 6; ++i) {
                        CAnimController* c = controllers[random.Below(8)];
                        c->m_vtable = g_controllerVtable;
                        t->mp_controllers[i] = random.Below(5) ? c : nullptr;
                    }
                    break;
                }
                case 1:
                    Fn<void(__thiscall*)(void*)>(original, 0x00481870)(t);
                    break;
                case 2:
                case 3: {
                    CAnimController* c = random.Below(4) ? controllers[random.Below(8)] : nullptr;
                    Fn<void(__thiscall*)(void*, void*)>(original, 0x004818B0)(t, c);
                    break;
                }
                case 4: {
                    bool any = false;
                    for (int i = 0; i < t->m_controllerCount; ++i)
                        any |= !t->mp_controllers[i];
                    if (!any) // (a null controller isn't checked: the original would crash)
                        trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x00481920)(
                                            t, reinterpret_cast<void*>(0x9A5E0000)) &
                                        0xFF);
                    break;
                }
                case 5:
                    t->m_state = static_cast<int>(random.Below(4));
                    t->m_fadeLeft = 2.0f;
                    t->m_fadeScale = 3.0f;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481980)(t));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481990)(t));
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x00481960)(t);
                    break;
                case 6:
                    t->mp_events = reinterpret_cast<Gfx::CAnimEvent*>(events);
                    t->m_eventCount = random.Below(5);
                    for (int i = 0; i < 4; ++i)
                        events[0x14 * i + 8] = static_cast<uint8_t>(random.Below(2));
                    Fn<void(__thiscall*)(void*)>(original, 0x004819A0)(t);
                    break;
                case 7: { // the base controller: made on this timeline, its id, its object and skeleton
                    CAnimController* c = controllers[random.Below(8)];
                    trace.push_back(
                        ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x00481710)(c, t)));
                    trace.push_back(reinterpret_cast<uint32_t>(c->m_vtable));
                    g_checksumFound = random.Below(2) != 0;
                    g_checksumAnswer = random.Next();
                    trace.push_back(
                        Fn<uint32_t(__thiscall*)(void*, void*)>(original, 0x00481740)(c, reinterpret_cast<void*>(object)) &
                        0xFF);
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481760)(c)));
                    g_component = random.Below(3) ? component : 0;
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x00481770)(c)));
                    trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*)>(original, 0x004819D0)(t)));
                    c->m_vtable = g_controllerVtable;
                    break;
                }
                case 8: { // the base controller's and event's destructors
                    CAnimController* c = controllers[random.Below(8)];
                    c->m_vtable = g_controllerVtable;
                    if (random.Below(2))
                        Fn<void(__thiscall*)(void*)>(original, 0x00481730)(c);
                    else
                        trace.push_back(ArenaRelocate(
                            Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x004817A0)(c, random.Below(4))));
                    trace.push_back(reinterpret_cast<uint32_t>(c->m_vtable));
                    c->m_vtable = g_controllerVtable;
                    auto* event = reinterpret_cast<Gfx::CAnimEventBase*>(events + 0x14 * random.Below(4));
                    event->m_vtable = nullptr;
                    Fn<void(__thiscall*)(void*)>(original, 0x004819E0)(event);
                    break;
                }
                default: // made again
                    trace.push_back(
                        ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00481830)(t, object + 4)));
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            }
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "blend timeline: made, reset, controllers deleted / removed / posed, states, events armed; the base "
              "controller (made, id, object, skeleton, destroyed), the event's destructor");
}

void TestCas1PointInBox()
{
    const float values[] = {0.0f, 1.0f, -1.0f, 2.0f, 0.5f, -0.0f, kNaN, 1.0f};
    std::vector<uint32_t> expected, actual;
    Lcg random(0x7400);
    for (int i = 0; i < 4000; ++i) {
        Mth::Vector v[3];
        for (Mth::Vector& vector : v)
            vector = {random.Pick(values), random.Pick(values), random.Pick(values), random.Pick(values)};
        if (i % 3 == 0) { // (inside more often)
            v[1] = {2.0f, 2.0f, 2.0f, 0.0f};
            v[2] = {-1.0f, -1.0f, -1.0f, 0.0f};
        }
        auto original = Original<bool(__cdecl*)(const Mth::Vector*, const Mth::Vector*, const Mth::Vector*)>(0x004817D0);
        expected.push_back(original(&v[0], &v[1], &v[2]));
        actual.push_back(Mth::PointInBox(&v[0], &v[1], &v[2]));
    }
    Compare(expected, actual, "PointInBox: every comparison, edges and NaNs");
}

} // namespace

SELFTEST(Early, TestCas1Timeline, RemoveStandIns);
SELFTEST(Early, TestCas1BlendTimeline, RemoveStandIns);
SELFTEST(Early, TestCas1PointInBox);
