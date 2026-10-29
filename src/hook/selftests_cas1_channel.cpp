// Self-tests for the animation blender's channel (src/game/gfx/anim_channel.cpp): original and ours on a channel in an
// arena with timelines of a recording vtable - made, reset, destroyed; updated (the blend value, the time waited for,
// timelines deleted and pushed); flip, wobble, loop type and speed with the network messages; the playing animation
// looked for in a structure; anim tags; poses blended; the blend buffers made. The network, scripts, structures,
// components, the skeleton and the memory manager stood in; the calls, results and memory compared.

#include <windows.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/anim_blend.h"
#include "game/gfx/anim_track.h"
#include "game/gfx/frame_source.h"
#include "game/gfx/model_utils.h"
#include "game/gfx/skeleton.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/anim_blender.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rfx_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RfxTests;
using Gfx::CBlendChannel;
using Gfx::CAnimTrack;
using Script::CStruct;

const float kNaN = std::numeric_limits<float>::quiet_NaN();

// What the stand-ins answer.
uint32_t g_player, g_app, g_skeletonComponent, g_tagsAll, g_tags;
bool g_isLocal, g_blocked, g_modelReady, g_hasTags;
std::vector<Script::CComponent*> g_components;
Script::CSymbolTableEntry* g_symbol;
void* g_timelineVtable[7];
void* g_controllerVtable[2];
uint32_t g_slot5Answer = 1;

void StandInChannelCalls(StandIns& s)
{
    StandInMemory(s);
    s.At({0x0053AEA0, AddressOf(&GameNet::Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xAEA0, r.args[0] & 0xFFFF});
        return g_player;
    });
    s.At({0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, [](Regs& r) {
        Record({0x3300, ArenaRelocate(r.ecx)});
        return g_isLocal ? 1u : 0u;
    });
    s.At({0x005434B0, AddressOf(&GameNet::PlayerInfo::GetSkaterValue2BC)}, 0, [](Regs& r) {
        Record({0x34B0, ArenaRelocate(r.ecx)});
        return 3u;
    });
    s.At({0x00532EA0, AddressOf(&GameNet::Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.args[0]});
        return g_app;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        auto* desc = reinterpret_cast<Net::MsgDesc*>(r.args[0]);
        auto* data = static_cast<uint8_t*>(desc->m_data);
        Record({0x9E50, ArenaRelocate(r.ecx), desc->m_id, desc->m_length, desc->m_singular, static_cast<uint32_t>(desc->m_queue),
                static_cast<uint32_t>(desc->m_priority)});
        for (int i = 0; i < (desc->m_length == 8 ? 5 : desc->m_length); ++i)
            Record({data[i]});
        return 0u;
    });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xDB90, ArenaRelocate(r.ecx), r.args[0]});
        return g_skeletonComponent;
    });
    s.At({0x004616F0, AddressOf(&Obj::CObject::SetTagsFromStruct)}, 4, [](Regs& r) {
        Record({0x16F0, ArenaRelocate(r.ecx), ArenaRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x004738E0, AddressOf(&Script::CScript::Construct)}, 0, [](Regs& r) {
        Record({0x38E0, ArenaRelocate(r.ecx)});
        return r.ecx;
    });
    s.At({0x00472900, AddressOf(&Script::CScript::IsBlocked)}, 4, [](Regs& r) {
        Record({0x2900, ArenaRelocate(r.ecx), r.args[0] & 0xFF});
        return g_blocked ? 1u : 0u;
    });
    s.At({0x004728F0, AddressOf(&Script::CScript::StopWaiting)}, 0, [](Regs& r) {
        Record({0x28F0, ArenaRelocate(r.ecx)});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x6410, ArenaRelocate(r.ecx)});
        return r.ecx;
    });
    s.At({0x00477C60, AddressOf(static_cast<void (CStruct::*)(uint32_t, float)>(&CStruct::AddFloat))}, 8,
         [](Regs& r) {
             Record({0x7C60, ArenaRelocate(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477090, 0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Record({0x7090, ArenaRelocate(r.ecx)});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Record({0x9070, ArenaRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Record({0x8540, ArenaRelocate(r.ecx), ArenaRelocate(r.args[0])});
        return 0u;
    });
    s.At({0x004781B0, AddressOf(static_cast<void (CStruct::*)(uint32_t, CStruct*)>(&CStruct::AddStructurePointer))}, 8,
         [](Regs& r) {
             Record({0x81B0, ArenaRelocate(r.ecx), r.args[0], ArenaRelocate(r.args[1])});
             return 0u;
         });
    s.At({0x00479020, AddressOf(static_cast<CStruct* (*)(uint32_t)>(&Script::GetStructure))}, 0, [](Regs& r) {
        Record({0x9020, r.args[0]});
        return g_tagsAll;
    });
    s.At({0x004767C0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, CStruct**, int)>(&CStruct::GetStructure))},
         12, [](Regs& r) {
             Record({0x67C0, ArenaRelocate(r.ecx), r.args[0], r.args[2]});
             if (g_hasTags)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = g_tags;
             return g_hasTags ? 1u : 0u;
         });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        auto* previous = reinterpret_cast<Script::CComponent*>(r.args[0]);
        size_t i = 0;
        if (previous) {
            while (i < g_components.size() && g_components[i] != previous)
                ++i;
            ++i;
        }
        Record({0x6490, static_cast<uint32_t>(i)});
        return i < g_components.size() ? reinterpret_cast<uint32_t>(g_components[i]) : 0u;
    });
    s.At({0x00478CF0, AddressOf(&Script::LookUpSymbol)}, 0, [](Regs& r) {
        Record({0x8CF0, r.args[0]});
        return (r.args[0] & 1) ? reinterpret_cast<uint32_t>(g_symbol) : 0u;
    });
    s.At({0x0046CC30, AddressOf(&Script::CArray::GetElement)}, 4, [](Regs& r) {
        auto* array = reinterpret_cast<Script::CArray*>(r.ecx);
        Record({0xCC30, r.args[0]});
        return array->m_elements[r.args[0]];
    });
    s.At({0x00419CA0, AddressOf(&Obj::CAnimBlenderComponent::ModelReady)}, 0, [](Regs& r) {
        Record({0x9CA0, ArenaRelocate(r.ecx)});
        return g_modelReady ? 1u : 0u;
    });
    s.At({0x0047D680, AddressOf(&Gfx::BlendPoses)}, 0, [](Regs& r) {
        Record({0xD680, ArenaRelocate(r.args[0]), ArenaRelocate(r.args[1]), r.args[2], ArenaRelocate(r.args[3]),
                r.args[4]});
        return 0u;
    });
    s.At({0x004A3090, AddressOf(&Gfx::CSkeleton::SetLOD)}, 4, [](Regs& r) {
        Record({0x3090, ArenaRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB9E0, AddressOf(&Mem::Manager::InitNamedHeap)}, 16, [](Regs& r) {
        Record({0xB9E0, r.args[0], r.args[1], Hash(reinterpret_cast<const char*>(r.args[2])), r.args[3]});
        return 0u;
    });
    s.At({0x005BAF50, AddressOf(&Mem::Manager::GetNamedHeap)}, 8, [](Regs& r) {
        Record({0xAF50, r.args[0], r.args[1]});
        return 0x4EA90000u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1])),
                r.args[2], r.args[3]});
        return 0u;
    });
    // The timelines' recording vtable: 0 the scalar deleting destructor, 2 Update, 5 CallMemberFunction.
    for (int slot = 0; slot < 7; ++slot) {
        uint16_t pop = slot == 0 ? 4 : slot == 5 ? 12 : slot == 6 ? 32 : 0;
        g_timelineVtable[slot] = s.Thunk(pop, [slot](Regs& r) {
            Record({0x7100u + static_cast<uint32_t>(slot), ArenaRelocate(r.ecx)});
            if (slot == 0)
                Record({r.args[0]});
            if (slot == 5)
                Record({r.args[0], ArenaRelocate(r.args[1]), ArenaRelocate(r.args[2])});
            if (slot == 6) {
                for (int i = 0; i < 8; ++i)
                    Record({ArenaRelocate(r.args[i])});
            }
            return slot == 0 ? r.ecx : slot == 5 ? g_slot5Answer : 1u;
        });
    }
    g_controllerVtable[0] = s.Thunk(4, [](Regs& r) { return r.ecx; });
    g_controllerVtable[1] = s.Thunk(4, [](Regs& r) {
        Record({0xC1, ArenaRelocate(r.ecx), ArenaRelocate(r.args[0])});
        return 1u;
    });
}

// The fake vtable on every timeline of the channel (new ones come with the real one).
void UseFakeTimelines(CBlendChannel* channel)
{
    for (Mem::ListNode* node = channel->m_timelines.next; node != &channel->m_timelines; node = node->next) {
        if (node->vtable == reinterpret_cast<void**>(0x00648794))
            node->vtable = g_timelineVtable;
    }
}

CAnimTrack* NewTimeline(Lcg& random, uint32_t object, uint32_t controller)
{
    auto* t = reinterpret_cast<CAnimTrack*>(Bump(sizeof(CAnimTrack)));
    const float times[] = {0.0f, 1.0f, 2.0f, 5.0f, 5.0f, 10.0f, kNaN};
    t->m_vtable = g_timelineVtable;
    auto* node = reinterpret_cast<Mem::ListNode*>(t);
    node->data = t;
    node->priority = 0;
    t->m_start = random.Pick(times);
    t->m_current = random.Pick(times);
    t->m_end = random.Pick(times);
    if (t->m_start > t->m_end) // (a timeline's start is never past its end: the wrap would never end)
        std::swap(t->m_start, t->m_end);
    t->m_speed = random.Pick({0.0f, 0.5f / 60, 1.0f / 60, 2.0f / 60});
    t->m_direction = random.Below(3) ? 0 : -1;
    t->m_loopType = static_cast<int>(random.Below(4));
    t->m_complete = false;
    t->m_anim = 0xA0 + random.Below(4);
    t->m_state = static_cast<int>(random.Below(3));
    t->m_controllerCount = static_cast<int>(random.Below(3));
    for (auto& c : t->mp_controllers)
        c = reinterpret_cast<Gfx::CAnimController*>(controller);
    t->m_fadeRate = 0.1f;
    t->m_fadeLeft = random.Pick({0.0f, 0.25f, 0.5f, 1.0f});
    t->m_fadeScale = random.Pick({0.0f, 0.5f, 1.0f, 2.0f, -1.0f});
    t->mp_object = reinterpret_cast<Obj::CCompositeObject*>(object);
    t->mp_events = nullptr;
    t->m_eventCount = 0;
    t->m_eventsOn = random.Below(2) != 0;
    return t;
}

void TestCas1Channel()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007CCE14, 4}, {0x0067DC10, 4},
                                          {0x006F8A58, 0x18}, {0x00684FC8, 4}, {0x007D1EC0, 1}, {0x006F3614, 16},
                                          {0x006F3548, 16}, {0x006F92C8, 16}});
    StandIns& s = *new StandIns();
    StandInChannelCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 120; ++round) {
        ok &= CompareTwoRuns("channel", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            GameVariable<uint32_t>(0x007CCE14) = 0x3E700000;
            GameVariable<uint32_t>(0x0067DC10) = 0x00FFFFFF;
            Lcg random(0x7700 + round);
            std::vector<uint32_t> trace;
            // The object, its blender, skeleton component, skeleton and bind translations, the poses.
            uint32_t object = Bump(0x40);
            uint32_t blender = Bump(0x44);
            *reinterpret_cast<float*>(blender + 0x24) = 123.5f;
            uint32_t component = Bump(0x20);
            uint32_t skeleton = Bump(0x20);
            uint32_t skeletonData = Bump(0x5A0);
            uint32_t binds = Bump(64 * 16);
            for (uint32_t i = 0; i < 64 * 4; ++i)
                reinterpret_cast<float*>(binds)[i] = static_cast<float>(i) * 0.5f;
            *reinterpret_cast<uint32_t*>(skeletonData + 0x590) = binds;
            *reinterpret_cast<uint32_t*>(skeleton + 8) = skeletonData;
            *reinterpret_cast<int*>(skeleton + 0x14) = static_cast<int>(1 + random.Below(64));
            *reinterpret_cast<uint32_t*>(component + 0x18) = skeleton;
            // The blend buffers (as InitBuffers makes them): 16 poses and weights, two blended poses.
            GameVariable<uint32_t>(0x006F8A64) = Bump(16 * 0x840);
            GameVariable<uint32_t>(0x006F8A5C) = Bump(16 * 4);
            GameVariable<uint32_t>(0x006F8A60) = Bump(2 * 0x840);
            uint32_t controller = Bump(0x14);
            *reinterpret_cast<void***>(controller) = g_controllerVtable;
            uint32_t script = Bump(0x100);
            g_app = Bump(0x40);
            g_player = Bump(0x20);
            g_skeletonComponent = component;
            auto* objectBytes = reinterpret_cast<uint8_t*>(object);
            *reinterpret_cast<uint32_t*>(objectBytes + 0x10) = random.Below(2) ? script : 0; // its script
            *reinterpret_cast<uint32_t*>(objectBytes + 0x14) = 0x10 + random.Below(3);     // its id
            *reinterpret_cast<int*>(script + 0xA4) = random.Pick({6, 2, 5});
            auto* channel = reinterpret_cast<CBlendChannel*>(Bump(sizeof(CBlendChannel)));
            channel->m_waiting = random.Below(2) != 0; // (Construct reads it before setting it)
            trace.push_back(ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, int)>(
                original, 0x0047F150)(channel, object, blender, random.Below(2))));
            channel->m_flags = random.Next() << 8 | random.Below(256);
            channel->m_key = random.Below(3) ? 0x123 : 0;
            Fn<void(__thiscall*)(void*)>(original, 0x0047DCF0)(channel);
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            uint32_t noSkeleton = Bump(0x20); // (a skeleton component without a skeleton)
            *reinterpret_cast<uint32_t*>(noSkeleton + 0x18) = 0;
            // Script structures for IsAnimOneOf / SetAnimTags.
            auto* array = reinterpret_cast<Script::CArray*>(Bump(0x10));
            auto* elements = reinterpret_cast<uint32_t*>(Bump(16));
            elements[0] = 0xB0;
            elements[1] = 0xA2;
            elements[2] = 0xA3;
            array->m_size = 1 + random.Below(3);
            array->m_elements = elements;
            g_symbol = reinterpret_cast<Script::CSymbolTableEntry*>(Bump(0x14));
            g_symbol->type = random.Below(2) ? Script::ESYMBOLTYPE_ARRAY : Script::ESYMBOLTYPE_INTEGER;
            g_symbol->raw = reinterpret_cast<uint32_t>(array);
            std::vector<Script::CComponent*> components;
            for (int i = 0; i < 6; ++i) {
                auto* c = reinterpret_cast<Script::CComponent*>(Bump(0x10));
                c->name = random.Below(3) ? 0 : 0x77;
                c->typeAndFlag = static_cast<uint8_t>(random.Pick({Script::ESYMBOLTYPE_NAME, Script::ESYMBOLTYPE_ARRAY,
                                                                   Script::ESYMBOLTYPE_INTEGER}) << 1);
                c->raw = c->Type() == Script::ESYMBOLTYPE_ARRAY ? (random.Below(4) ? reinterpret_cast<uint32_t>(array) : 0)
                                                                : 0xA0 + random.Below(6);
                components.push_back(c);
            }
            for (int step = 0; step < 30; ++step) {
                g_trace.clear();
                UseFakeTimelines(channel);
                GameVariable<float>(0x00684FC8) = random.Pick({1.0f, 0.5f, 2.0f, 0.0f, kNaN});
                g_isLocal = random.Below(3) != 0;
                g_player = random.Below(5) ? g_player : 0;
                if (!g_player)
                    g_player = Bump(0x20);
                uint32_t app = g_app;
                bool send = random.Below(3) != 0;
                if (random.Below(4) == 0)
                    channel->m_id = random.Below(2);
                uint32_t op = random.Below(24);
                trace.push_back(op);
                switch (op) {
                case 0:
                case 1: { // a timeline added by hand
                    if (reinterpret_cast<Gfx::AnimListHead*>(&channel->m_timelines)->CountItems() >= 15)
                        break;
                    CAnimTrack* t = NewTimeline(random, object, controller);
                    auto* node = reinterpret_cast<Mem::ListNode*>(t);
                    Mem::ListNode* after = random.Below(2) ? &channel->m_timelines : channel->m_timelines.prev;
                    node->next = after->next;
                    node->prev = after;
                    after->next->prev = node;
                    after->next = node;
                    break;
                }
                case 2: { // pushed
                    GameVariable<int>(0x006F8A68) = static_cast<int>(1 + random.Below(4));
                    Fn<void(__thiscall*)(void*, float)>(original, 0x0047DE20)(channel,
                                                                              random.Pick({0.0f, 0.3f, 1.0f, kNaN}));
                    break;
                }
                case 3: // updated
                    channel->m_waiting = random.Below(2) != 0;
                    channel->m_waitTime = random.Pick({0.0f, 1.5f, 5.0f, 10.0f, kNaN});
                    channel->m_periodLeft = random.Pick({0.0f, 0.01f, 0.2f, -1.0f, kNaN});
                    channel->m_period = random.Pick({0.5f, 0.3f, 0.0f});
                    channel->m_target = std::bit_cast<uint32_t>(random.Pick({0.0f, 1.0f, 0.25f}));
                    g_blocked = random.Below(4) != 0;
                    g_modelReady = random.Below(3) != 0;
                    GameVariable<bool>(0x007D1EC0) = random.Below(6) == 0;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, int, bool)>(original, 0x0047EFB0)(
                                        channel, static_cast<int>(random.Below(2)), random.Below(3) != 0) &
                                    0xFF);
                    break;
                case 4:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, bool, uint32_t, bool)>(original, 0x0047DC40)(
                                        channel, 0x12, random.Below(2) != 0, 7, send) &
                                    0xFF);
                    break;
                case 5:
                    Fn<void(__thiscall*)(void*)>(original, 0x0047DD80)(channel);
                    break;
                case 6:
                    Fn<void(__thiscall*)(void*, float)>(original, 0x0047E010)(channel, random.Pick({1.0f, -2.0f, 0.5f}));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0047E210)(channel) & 0xFF);
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0047E240)(channel) & 0xFF);
                    break;
                case 7: {
                    g_components.assign(components.begin(), components.begin() + random.Below(7));
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, void*, void*)>(original, 0x0047DF50)(
                                        channel, reinterpret_cast<void*>(0x5770000), nullptr) &
                                    0xFF);
                    break;
                }
                case 8:
                    Fn<void(__thiscall*)(void*, float, bool)>(original, 0x0047E280)(
                        channel, random.Pick({-0.5f, 0.0f, 0.3f, 0.5f, 1.0f, 1.7f, kNaN}), send);
                    break;
                case 9: {
                    const float a[] = {0.05f, 0.1f, 10.1f, 3.0f};
                    const float b[] = {0.04f, 10.1f, 3.0f};
                    const float c[] = {0.0022f, 20.0f, 3.0f};
                    const float d[] = {0.0017f, 10.0f, 3.0f};
                    const float e[] = {1.5f, 1.0f, kNaN};
                    float details[5] = {random.Pick(a), random.Pick(b), random.Pick(c), random.Pick(d), random.Pick(e)};
                    Fn<void(__thiscall*)(void*, const float*, bool)>(original, 0x0047E450)(channel, details, send);
                    break;
                }
                case 10: { // (twice: a loop type past a byte is never the one last sent)
                    int loopType = static_cast<int>(random.Below(4)) - (random.Below(4) == 0 ? 200 : 0);
                    for (int i = 0; i < 2; ++i)
                        Fn<void(__thiscall*)(void*, int, bool)>(original, 0x0047E740)(channel, loopType, send);
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0047E810)(channel, 0);
                    break;
                }
                case 11:
                case 12: {
                    if (random.Below(6) == 0)
                        g_app = 0;
                    Fn<void(__thiscall*)(void*, float, bool, bool, bool)>(original, 0x0047E850)(
                        channel, random.Pick({0.0f, 1.0f, 1.02f, 2.0f, -1.0f, kNaN}), send, random.Below(2) != 0,
                        random.Below(2) != 0);
                    double speed = Fn<double(__thiscall*)(void*, void*, void*)>(original, 0x0047EA40)(channel, nullptr,
                                                                                                     nullptr);
                    uint64_t bits = std::bit_cast<uint64_t>(speed);
                    trace.push_back(static_cast<uint32_t>(bits));
                    trace.push_back(static_cast<uint32_t>(bits >> 32));
                    break;
                }
                case 13:
                    Fn<void(__thiscall*)(void*, bool)>(original, 0x0047EA80)(channel, random.Below(2) != 0);
                    break;
                case 14:
                    g_tagsAll = random.Below(4) ? 0x7A600000 : 0;
                    g_hasTags = random.Below(3) != 0;
                    g_tags = 0x7A610000;
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x0047F000)(channel, 0xA1);
                    break;
                case 15: // the waiting script
                    Fn<void(__thiscall*)(void*, void*)>(original, 0x0047EF60)(channel, reinterpret_cast<void*>(script));
                    break;
                case 16:
                    channel->m_waiting = random.Below(2) != 0;
                    Fn<void(__thiscall*)(void*)>(original, 0x0047EF20)(channel);
                    break;
                case 17: { // a pose (the timeline there must be: none crashes)
                    int count = Fn<int(__thiscall*)(void*)>(original, 0x0047DD10)(&channel->m_timelines);
                    trace.push_back(static_cast<uint32_t>(count));
                    if (count) {
                        float weight = 9.0f;
                        uint32_t pose = Bump(0x840);
                        Fn<void(__thiscall*)(void*, int, void*, float*)>(original, 0x0047EAA0)(
                            channel, static_cast<int>(random.Below(static_cast<uint32_t>(count))),
                            reinterpret_cast<void*>(pose), &weight);
                        trace.push_back(std::bit_cast<uint32_t>(weight));
                    }
                    break;
                }
                case 18:
                    GameVariable<bool>(0x007D1EC0) = random.Below(6) == 0;
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*)>(original, 0x0047EFF0)(channel) & 0xFF);
                    break;
                case 19:
                    Fn<void(__thiscall*)(void*)>(original, 0x0047E030)(channel);
                    Fn<void(__thiscall*)(void*)>(original, 0x0047E1B0)(channel);
                    Fn<void(__thiscall*)(void*)>(original, 0x0047DDC0)(channel);
                    break;
                case 20: // another skeleton component (or one without a skeleton: no blending)
                    g_skeletonComponent = random.Below(3) ? component : noSkeleton;
                    Fn<void(__thiscall*)(void*)>(original, 0x0047DCF0)(channel);
                    g_skeletonComponent = component;
                    break;
                case 21: { // a timeline node of the list type destroyed
                    auto* node = reinterpret_cast<Mem::ListNode*>(Bump(0x14));
                    node->next = channel->m_timelines.next;
                    node->prev = &channel->m_timelines;
                    channel->m_timelines.next->prev = node;
                    channel->m_timelines.next = node;
                    node->data = node;
                    trace.push_back(ArenaRelocate(
                        Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047DD40)(node, random.Below(4))));
                    break;
                }
                default: // the channel's flags and blend settings stirred
                    channel->m_flags ^= 0x20000000;
                    channel->m_flag4C = random.Below(2) != 0;
                    break;
                }
                g_app = app;
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                memory.Dump(trace);
            }
            g_trace.clear();
            UseFakeTimelines(channel);
            // Destroyed: its body (on the blender's list), or freed.
            Mem::ListNode* blenderHead = reinterpret_cast<Mem::ListNode*>(Bump(0x14));
            blenderHead->next = blenderHead->prev = &channel->m_node;
            channel->m_node.next = channel->m_node.prev = blenderHead;
            if (random.Below(2))
                Fn<void(__thiscall*)(void*)>(original, 0x0047EE90)(channel);
            else
                trace.push_back(
                    ArenaRelocate(Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0047F230)(channel, random.Below(4))));
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "channel: made, finalised, updated (blend value, waits, timelines updated / deleted / pushed), flips, "
              "wobble, loop type, speed and their messages, the animation looked for, anim tags, poses, destroyed");
}

void TestCas1ChannelBuffers()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x006F8A58, 0x18}});
    StandIns& s = *new StandIns();
    StandInChannelCalls(s);
    g_relocate = ArenaRelocate;
    bool ok = true, reported = false;
    for (int round = 0; round < 12; ++round) {
        ok &= CompareTwoRuns("channel buffers", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            Lcg random(0x7800 + round);
            std::vector<uint32_t> trace;
            if (random.Below(4) == 0)
                g_failAllocations = {random.Below(2) == 0, random.Below(2) == 0};
            Fn<void(__cdecl*)(int, int)>(original, 0x0047EB30)(static_cast<int>(random.Below(5)),
                                                                static_cast<int>(random.Below(4)));
            g_failAllocations.clear();
            trace.insert(trace.end(), g_trace.begin(), g_trace.end());
            memory.Dump(trace);
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "channel: the blend buffers (each pose's rotations the identity; allocations failing)");
}

// --- playing and the member functions ---

// The parameters the getters answer from (for the structure g_params; others have nothing).
struct ParamModel {
    std::vector<std::pair<uint32_t, uint32_t>> checksums;
    std::vector<std::pair<uint32_t, float>> floats;
    std::vector<std::pair<uint32_t, int>> integers;
    std::vector<uint32_t> flags;
};
ParamModel g_model;
uint32_t g_params = 0x5A000000;
int g_loopAnswer;
const float g_lengths[7] = {1.0f, 30.0f, 0.5f, 60.0f, 0.0f, 12.25f, -2.0f};
Lcg g_made(0); // (the new timelines' times)

template <typename T>
const T* Find(const std::vector<std::pair<uint32_t, T>>& list, uint32_t name)
{
    for (auto& [n, v] : list) {
        if (n == name)
            return &v;
    }
    return nullptr;
}

void StandInPlayCalls(StandIns& s)
{
    StandInChannelCalls(s);
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) {
             Record({0x6950, ArenaRelocate(r.ecx), r.args[0], r.args[2] & 0xFF});
             const uint32_t* v = r.ecx == g_params ? Find(g_model.checksums, r.args[0]) : nullptr;
             if (v)
                 *reinterpret_cast<uint32_t*>(r.args[1]) = *v;
             return v ? 1u : 0u;
         });
    s.At({0x004766D0, 0x00476D50, AddressOf(static_cast<bool (CStruct::*)(uint32_t, float*, int)>(&CStruct::GetFloat))},
         12, [](Regs& r) {
             Record({0x66D0, ArenaRelocate(r.ecx), r.args[0], r.args[2] & 0xFF});
             const float* v = r.ecx == g_params ? Find(g_model.floats, r.args[0]) : nullptr;
             if (v)
                 *reinterpret_cast<float*>(r.args[1]) = *v;
             return v ? 1u : 0u;
         });
    s.At({0x00476630, 0x00476CB0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, int*, int)>(&CStruct::GetInteger))},
         12, [](Regs& r) {
             Record({0x6630, ArenaRelocate(r.ecx), r.args[0], r.args[2] & 0xFF});
             const int* v = r.ecx == g_params ? Find(g_model.integers, r.args[0]) : nullptr;
             if (v)
                 *reinterpret_cast<int*>(r.args[1]) = *v;
             return v ? 1u : 0u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4, [](Regs& r) {
        Record({0x6B40, ArenaRelocate(r.ecx), r.args[0]});
        bool found = false;
        for (uint32_t flag : g_model.flags)
            found |= r.ecx == g_params && flag == r.args[0];
        return found ? 1u : 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0x7D40, ArenaRelocate(r.ecx), r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x004776B0, AddressOf(static_cast<void (CStruct::*)(uint32_t)>(&CStruct::RemoveComponent))}, 4, [](Regs& r) {
        Record({0x76B0, ArenaRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00409030, AddressOf(&Script::NewStruct)}, 0, [](Regs&) {
        Record({0x9030});
        return Bump(8);
    });
    s.At({0x00472FD0, AddressOf(&Script::CScript::Block)}, 4, [](Regs& r) {
        Record({0x2FD0, ArenaRelocate(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x0047D580, AddressOf(&Gfx::AddTimelineInfo)}, 8, [](Regs& r) {
        Record({0xD580, ArenaRelocate(r.args[0]), ArenaRelocate(r.args[1])});
        return 0u;
    });
    s.At({0x0048CF50, AddressOf(&Nx::GetAnimData)}, 0, [](Regs& r) {
        Record({0xCF50, r.args[0], r.args[1]});
        uint32_t key = r.args[0];
        return (key & 0xFF) == 0xA7 ? 0u : reinterpret_cast<uint32_t>(&g_lengths[key % 7]); // (0xA7: not in the set)
    });
    s.At({0x004877E0, AddressOf(&Gfx::GetAnimTimes)}, 0, [](Regs& r) { // (the original passes the script too)
        Record({0x77E0, ArenaRelocate(r.args[0]), ArenaRelocate(r.args[1]), r.args[2], r.args[3], ArenaRelocate(r.args[4])});
        *reinterpret_cast<float*>(r.args[0]) = std::bit_cast<float>(r.args[2]) * 0.5f;
        if (r.args[2] & 0x100)
            *reinterpret_cast<float*>(r.args[1]) = std::bit_cast<float>(r.args[3]) + 1.0f;
        return 0u;
    });
    s.At({0x00487770, AddressOf(&Gfx::GetAnimLoopType)}, 0, [](Regs& r) {
        Record({0x7770, ArenaRelocate(r.args[0]), ArenaRelocate(r.args[1])});
        *reinterpret_cast<int*>(r.args[0]) = g_loopAnswer;
        return 1u;
    });
    s.At({0x00481FB0, AddressOf(&CAnimTrack::AddController)}, 4, [](Regs& r) {
        Record({0x1FB0, ArenaRelocate(r.ecx), ArenaRelocate(r.args[0])});
        return r.ecx;
    });
    // New timelines (0x00481EE0's memory) made with the recording vtable.
    s.At({0x00481830, AddressOf(&CAnimTrack::Construct)}, 4, [](Regs& r) {
        Record({0x1830, ArenaRelocate(r.ecx), ArenaRelocate(r.args[0])});
        auto* t = reinterpret_cast<CAnimTrack*>(r.ecx);
        std::memset(t, 0, sizeof(CAnimTrack));
        t->m_vtable = g_timelineVtable;
        auto* node = reinterpret_cast<Mem::ListNode*>(t);
        node->data = t;
        node->next = node->prev = node;
        const float times[] = {0.0f, 1.0f, 2.0f, 4.0f, -1.0f};
        t->m_start = g_made.Pick(times);
        t->m_current = g_made.Pick(times);
        t->m_end = g_made.Pick(times);
        if (t->m_start > t->m_end)
            std::swap(t->m_start, t->m_end);
        t->m_direction = g_made.Below(3) ? 0 : -1;
        t->m_speed = 1.0f / 60;
        t->m_loopType = 1;
        t->m_anim = 0xA1;
        t->m_state = 2;
        t->mp_object = reinterpret_cast<Obj::CCompositeObject*>(r.args[0]);
        t->m_eventsOn = true;
        return r.ecx;
    });
}

void* g_objectVtable[4];

// A random set of parameters for the plays and member functions.
void RandomModel(Lcg& random)
{
    g_model = {};
    const uint32_t anims[] = {0xA0, 0xA1, 0xA2, 0xA7, 0x230CCBF4, 0};
    if (random.Below(4))
        g_model.checksums.push_back({0x98549BA4, random.Pick(anims)}); // anim
    if (random.Below(3))
        g_model.floats.push_back({0xF0D90109, random.Pick({1.0f, 2.0f, 0.5f})}); // Speed
    if (random.Below(4) == 0)
        g_model.floats.push_back({0x4F3ABD6C, random.Pick({50.0f, 10.0f, 0.0f})}); // the blend percentage
    if (random.Below(2))
        g_model.floats.push_back({0x8F0D24ED, random.Pick({0.0f, 0.2f, 0.5f})}); // BlendPeriod
    if (random.Below(4) == 0)
        g_model.floats.push_back({0x4B4A153A, random.Pick({2.0f, 0.0f, 8.0f})});
    if (random.Below(4) == 0)
        g_model.floats.push_back({0x23272E4D, random.Pick({2.0f, 0.5f, 0.0f})});
    if (random.Below(3))
        g_model.floats.push_back({0, random.Pick({0.0f, 1.0f, 3.0f, 30.0f, 50.0f, 1500.0f, -2.0f, kNaN})});
    if (random.Below(4) == 0)
        g_model.checksums.push_back({0x6C565F57, 0xB7}); // PartialAnimOverlay
    if (random.Below(3) == 0)
        g_model.checksums.push_back({0xF0CE9E0F, 0x1D}); // (its id)
    if (random.Below(3) == 0)
        g_model.checksums.push_back({0, random.Pick(anims)});
    if (random.Below(3) == 0)
        g_model.integers.push_back({0xD2D6A582, static_cast<int>(random.Below(4))}); // Index
    if (random.Below(2))
        g_model.integers.push_back({0xAF06447B, static_cast<int>(random.Below(2))}); // enabled
    for (uint32_t flag : {0x099371AEu, 0xFF5AC79Eu, 0xF8CFD515u, 0x84F13067u, 0xFE09FE09u, 0xD443A2BCu, 0x4A07C332u,
                          0x019176C5u, 0x9E497FC6u, 0x49E0EE96u, 0xD029F619u, 0x91A4C826u, 0xE18CD075u, 0xCF466AA2u,
                          0xE1B3F630u}) {
        if (random.Below(5) == 0)
            g_model.flags.push_back(flag);
    }
    // InitFromStructure's
    if (random.Below(2))
        g_model.checksums.push_back({0x5CFCCF35, random.Below(3)}); // SubsetId
    g_model.floats.push_back({0x4D786F62, 0.75f});                     // BlendWeight
    if (random.Below(2))
        g_model.integers.push_back({0x3D836C96, 7});
    if (random.Below(2))
        g_model.checksums.push_back({0x5B8C6DC2, 0xE7});
    g_model.checksums.push_back({random.Below(2) ? 0x52A864B0u : 0x09794932u, 0x100u * (1 + random.Below(3))});
    if (random.Below(2))
        g_model.checksums.push_back({0xEAE64B47, random.Pick({0xA0u, 0xA1u, 0xA2u})});
}

void TestCas1ChannelPlay()
{
    GameMemory& memory = *new GameMemory({{0x007CC979, 1}, {0x007D1800, 4}, {0x007CCE14, 4}, {0x0067DC10, 4},
                                          {0x006F8A58, 0x18}, {0x00684FC8, 4}, {0x006F3614, 16}, {0x006F3548, 16},
                                          {0x006F92C8, 16}});
    StandIns& s = *new StandIns();
    StandInPlayCalls(s);
    g_objectVtable[3] = s.Thunk(12, [](Regs& r) {
        Record({0x0B3, ArenaRelocate(r.ecx), r.args[0], ArenaRelocate(r.args[1]), ArenaRelocate(r.args[2])});
        return 1u;
    });
    g_relocate = ArenaRelocate;
    const uint32_t names[] = {0x1DF59A54, 0x27A98022, 0x2889C6C9, 0x76CC99D5, 0x0B1E7291, 0x2CB9AD09, 0x42FFC3CE,
                              0x4FAF61A9, 0x58C52F64, 0x5195BCBB, 0x68C86AEC, 0x6ECEB234, 0x74CF5C94, 0x80FDBDF2,
                              0x9069F357, 0x922E7D14, 0x986D274E, 0x98A32669, 0x1A0F9646, 0xB5FB7EB6, 0xC68365EB,
                              0xEA6D0EFD, 0x5E3F0F17, 0x6AAEB76F, 0x83654874, 0xBD4EDD44, 0xD63A1B81, 0xF5E2B871,
                              0x12345678, 0x74CF5C95};
    bool ok = true, reported = false;
    for (int round = 0; round < 150; ++round) {
        ok &= CompareTwoRuns("channel play", reported, [&](bool original) {
            g_trace.clear();
            ResetArena(0xEE);
            memory.Zero();
            GameVariable<uint8_t>(0x007CC979) = 1;
            GameVariable<uint32_t>(0x007D1800) = 0x3A9A0000;
            GameVariable<uint32_t>(0x007CCE14) = 0x3E700000;
            GameVariable<uint32_t>(0x0067DC10) = 0x00FFFFFF;
            GameVariable<float>(0x00684FC8) = 1.0f;
            Lcg random(0x7900 + round);
            g_made = Lcg(0x7A00 + round);
            std::vector<uint32_t> trace;
            uint32_t object = Bump(0x40);
            *reinterpret_cast<void***>(object) = g_objectVtable;
            uint32_t script = Bump(0x100);
            *reinterpret_cast<uint32_t*>(script + 0x14) = 0x5C000000; // its parameters
            *reinterpret_cast<uint32_t*>(object + 0x10) = random.Below(2) ? script : 0;
            *reinterpret_cast<uint32_t*>(object + 0x14) = 0x10 + random.Below(3);
            uint32_t blender = Bump(0x44);
            uint32_t controller = Bump(0x14);
            *reinterpret_cast<void***>(controller) = g_controllerVtable;
            g_app = Bump(0x40);
            g_player = Bump(0x20);
            g_skeletonComponent = 0;
            auto* channel = reinterpret_cast<CBlendChannel*>(Bump(sizeof(CBlendChannel)));
            channel->m_waiting = false;
            Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, int)>(original, 0x0047F150)(channel, object, blender, 1);
            channel->m_flags = random.Next() << 8 | random.Below(256);
            channel->m_key = 0x100 * random.Below(4);
            GameVariable<int>(0x006F8A68) = static_cast<int>(1 + random.Below(4));
            for (int step = 0; step < 25; ++step) {
                g_trace.clear();
                RandomModel(random);
                g_isLocal = random.Below(3) != 0;
                g_slot5Answer = random.Below(3);
                g_loopAnswer = static_cast<int>(random.Below(5));
                g_tagsAll = 0;
                *reinterpret_cast<uint32_t*>(object + 0x20) = random.Below(2) ? 0x7A620000u : 0; // its tags
                channel->m_flag4C = random.Below(2) != 0;
                channel->m_flag4E = random.Below(4) == 0;
                channel->m_hasNextBlend = random.Below(5) == 0;
                channel->m_nextBlend = 0.4f;
                channel->m_firstTime = random.Pick({0.0f, 0.5f, 1.0f, 1.5f});
                if (random.Below(6) == 0)
                    channel->m_id = random.Below(2);
                if (random.Below(8) == 0)
                    channel->m_key = random.Below(2) ? 0 : 0x200;
                uint32_t op = random.Below(12);
                trace.push_back(op);
                switch (op) {
                case 0: { // a timeline by hand
                    if (reinterpret_cast<Gfx::AnimListHead*>(&channel->m_timelines)->CountItems() >= 6)
                        break;
                    CAnimTrack* t = NewTimeline(random, object, controller);
                    t->m_controllerCount = 0;
                    auto* node = reinterpret_cast<Mem::ListNode*>(t);
                    node->next = channel->m_timelines.next;
                    node->prev = &channel->m_timelines;
                    channel->m_timelines.next->prev = node;
                    channel->m_timelines.next = node;
                    break;
                }
                case 1:
                    Fn<void(__thiscall*)(void*, uint32_t, bool, float, float, int, float, float, uint32_t)>(
                        original, 0x0047F260)(channel, random.Pick({0u, 0xA2u, 0xA3u}), random.Below(2) != 0,
                                              random.Pick({0.0f, 0.5f, -1.0f, 9.0f}), random.Pick({1.0f, 30.0f, 0.25f}),
                                              static_cast<int>(random.Below(4)), random.Pick({0.0f, 0.3f, kNaN}),
                                              random.Pick({1.0f, 2.5f, -3.0f}), g_params);
                    break;
                case 2:
                    Fn<void(__thiscall*)(void*, uint32_t, float)>(original, 0x004803D0)(channel, random.Pick({0xA0u, 0xA5u}),
                                                                                         random.Pick({0.0f, 0.3f}));
                    break;
                case 3:
                    channel->m_waiting = random.Below(2) != 0;
                    Fn<void(__thiscall*)(void*, uint32_t)>(original, 0x00480410)(channel, g_params);
                    break;
                case 4:
                case 5:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, float)>(original, 0x0047F470)(
                        channel, g_params, script, random.Pick({0.3f, 0.0f})));
                    break;
                case 10: // (WaitAnim and the time-passed test more often)
                case 11:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x0047FA00)(
                        channel, op == 10 ? 0x74CF5C94u : 0x922E7D14u, g_params, script));
                    break;
                default:
                    trace.push_back(Fn<uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t)>(original, 0x0047FA00)(
                        channel, random.Pick(names), g_params, script));
                    break;
                }
                trace.insert(trace.end(), g_trace.begin(), g_trace.end());
                memory.Dump(trace);
                for (int i = 0; i < 0x74 / 4; ++i) // (the channel each step: the next step sets some of it again)
                    trace.push_back(ArenaRelocate(reinterpret_cast<uint32_t*>(channel)[i]));
            }
            DumpArena(trace);
            return trace;
        });
    }
    delete &s;
    delete &memory;
    g_relocate = nullptr;
    Check(ok, "channel: PlayAnim and its message, PlayAnimation, InitFromStructure, the script play (every parameter), "
              "every member function");
}

} // namespace

SELFTEST(Early, TestCas1Channel, RemoveStandIns);
SELFTEST(Early, TestCas1ChannelPlay, RemoveStandIns);
SELFTEST(Early, TestCas1ChannelBuffers, RemoveStandIns);
