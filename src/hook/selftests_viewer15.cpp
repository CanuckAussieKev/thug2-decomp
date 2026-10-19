// Self-tests for the skater's restart skip and destructor (src/game/skater/skater.cpp, 0x00583DA0 / 0x00583FB0 /
// 0x005840E0): the original and ours run on a stand-in skater in an arena; what they call outside is a recording
// stand-in, and the calls and the whole arena are compared.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/net/net_client.h"
#include "game/obj/compositemgr.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/nodes.h"
#include "game/sio/device.h"
#include "game/skater/skater.h"
#include "game/sys/game_clock.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CSkater;

constexpr uint32_t kSkater = 0x0000; // 0x324
constexpr uint32_t kCorePhys = 0x0400;
constexpr uint32_t kBlocks = 0x2000; // small blocks, 0x100 apart

uint32_t Block(int i) { return At(kBlocks + 0x100 * i); }

void* g_skaterVtable[24];
void* g_infoVtable[2];
void* g_cameraVtable[8];
bool g_net, g_node, g_trigger, g_camera, g_player;
uint32_t g_mode;
int g_clock;

void Simple(StandIns& s, std::initializer_list<uint32_t> addresses, int pop, uint32_t answer = 0)
{
    uint32_t tag = *addresses.begin() & 0xFFFFF;
    s.At(addresses, static_cast<uint16_t>(pop), [tag, pop, answer](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        for (int i = 0; i < pop / 4; ++i)
            g_trace.push_back(Rel(r.args[i]));
        return answer;
    });
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Rec(0xDB90, {r.ecx, r.args[0]});
        return Block(1) + (r.args[0] & 0x70);
    });
    s.At({0x0044E4F0}, 0, [](Regs& r) {
        Rec(0xE4F0, {r.ecx});
        return g_net ? 1u : 0u;
    });
    s.At({0x005A1050}, 0, [](Regs& r) {
        Rec(0x1050, {r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x0044ECE0, AddressOf(&Tmr::GameClockElapsed)}, 0, [](Regs&) {
        Rec(0xECE0, {});
        return static_cast<uint32_t>(g_clock += 7);
    });
    for (uint32_t address : {0x0043D550u, 0x00511420u, 0x00543340u})
        Simple(s, {address}, 0);
    Simple(s, {0x00583470, AddressOf(&CSkater::Restart)}, 8);
    Simple(s, {0x00583400, AddressOf(&CSkater::ResetComponents)}, 0);
    Simple(s, {0x0045E4C0, AddressOf(&Obj::CCompositeObject::SetTeleported)}, 4);
    Simple(s, {0x0045D920, AddressOf(&Obj::CCompositeObject::Update)}, 0);
    Simple(s, {0x0045D890, AddressOf(&Obj::CCompositeObject::DestroyBody)}, 0);
    s.At({0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, [](Regs& r) {
        Rec(0x19F0, {r.ecx, r.args[0]});
        return g_camera ? Block(10) : 0u;
    });
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Rec(0xC1B0, {r.args[0], r.args[1] & 0xFF});
        return g_node ? Block(11) : 0u;
    });
    s.At({0x00476950, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))}, 12,
         [](Regs& r) {
             Rec(0x6950, {r.ecx, r.args[0], r.args[2] & 0xFF});
             if (!g_trigger)
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = 0x7E16;
             return 1u;
         });
    s.At({0x00461480, AddressOf(&Obj::CObject::SpawnAndRunScript)}, 20, [](Regs& r) {
        Rec(0x1480, {r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
    s.At({0x005532A0}, 0, [](Regs& r) {
        Rec(0x32A0, {r.ecx});
        return g_mode;
    });
    s.At({0x00532EA0}, 4, [](Regs& r) {
        Rec(0x2EA0, {r.ecx, r.args[0]});
        return Block(12);
    });
    s.At({0x0053AEA0}, 4, [](Regs& r) {
        Rec(0xAEA0, {r.ecx, r.args[0]});
        return g_player ? Block(13) : 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        const auto* m = reinterpret_cast<const Net::MsgDesc*>(r.args[0]);
        Rec(0x9E50, {r.ecx, m->m_id, m->m_stream, m->m_length, Rel(reinterpret_cast<uint32_t>(m->m_data)),
                     static_cast<uint32_t>(m->m_priority), static_cast<uint32_t>(m->m_queue), m->m_groupId});
        return 0u;
    });
    Simple(s, {0x005BDBD0, AddressOf(&Sio::Device::UnpauseActuators)}, 0);
    Simple(s, {0x005BDC60, AddressOf(&Sio::Device::StopActuators)}, 0);
    for (int slot : {12, 16})
        g_skaterVtable[slot] = s.Thunk(slot == 16 ? 4 : 0, [slot](Regs& r) {
            Rec(0x5100 + slot, {r.ecx, slot == 16 ? r.args[0] : 0u});
            return 0u;
        });
    g_infoVtable[0] = s.Thunk(4, [](Regs& r) {
        Rec(0x1F00, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_cameraVtable[6] = s.Thunk(0, [](Regs& r) {
        Rec(0xCA18, {r.ecx});
        return 0u;
    });
}

// SkipToRestart: local and not; online (the input flag set or not); a node or none; paused or not; a camera or
// none; the node's TriggerScript or not; each server-telling game mode (a player or not) and another. The destructor:
// paused local with an input device, not; trick infos and none; viewport 1 local (its camera marked) or not; both
// destroy flags.
void TestSkaterRestart()
{
    SaveGlobals({{0x007CCE14, 4, {}}, {0x007CE470, 4, {}}, {0x006F0FB8, 4, {}}, {0x00696D60, 4, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        std::vector<uint32_t>& t = traces[run];
        g_clock = 3000;
        GameVariable<uint32_t>(0x007CCE14) = Block(20);
        GameVariable<uint32_t>(0x007CE470) = Block(21);
        GameVariable<uint32_t>(0x006F0FB8) = Block(22);
        GameVariable<int>(0x00696D60) = 100;
        *Ptr<void**>(kBlocks + 0x100 * 10) = g_cameraVtable;
        auto* skater = Ptr<CSkater>(kSkater);
        skater->m_vtable = g_skaterVtable;
        skater->m_id = 0x51;
        skater->m_corePhysics = Ptr<uint8_t>(kCorePhys);
        *Ptr<uint32_t>(kCorePhys + 0x1528) = Block(14);
        skater->m_unknown264 = Ptr<uint8_t>(kBlocks + 0x100 * 15);
        *Ptr<uint32_t>(kBlocks + 0x100 * 15 + 0x490) = Block(16);
        *Ptr<uint32_t>(kBlocks + 0x100 * 16 + 0x1C) = Block(17);
        skater->m_list28C = Ptr<void>(kBlocks + 0x100 * 18);
        auto skip = [&](uint32_t node, uint32_t walking) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, uint32_t)>(0x00583DA0)(skater, node, walking);
            else
                skater->SkipToRestart(node, walking);
        };
        struct Case {
            bool local, net, flag, list, b25c;
            uint32_t node;
            int pause;
            bool camera, nodeFound, trigger;
            uint32_t mode;
            bool player;
        };
        const Case cases[] = {
            {true, true, true, true, true, 0x40DE, 0, true, true, true, 0x6C5FF266, true},
            {true, true, false, false, false, 0x40DE, 1, false, true, false, 0x6EF8FDA0, false},
            {true, false, false, true, false, 0, 0, false, false, false, 0x5D32129C, true},
            {true, false, false, true, true, 0x40DF, 0, true, false, true, 0x12345678, false},
            {false, true, true, true, true, 0x40DE, 0, true, true, true, 0x6C5FF266, true},
        };
        for (const Case& c : cases) {
            skater->m_unknownBC = c.local ? 1 : 0;
            g_net = c.net;
            *Ptr<uint8_t>(kBlocks + 0x100 * 14 + 0x70) = c.flag ? 1 : 0;
            skater->m_list28C = c.list ? Ptr<void>(kBlocks + 0x100 * 18) : nullptr;
            skater->m_unknown25C = c.b25c ? Ptr<void>(kBlocks + 0x100 * 19) : nullptr;
            skater->m_pauseState = c.pause;
            g_camera = c.camera;
            g_node = c.nodeFound;
            g_trigger = c.trigger;
            g_mode = c.mode;
            g_player = c.player;
            skip(c.node, 0x101);
        }
        // The destructor.
        for (int k = 0; k < 4; ++k) { // (3: not paused, with its +0x264: the actuators left alone)
            auto* d = Ptr<CSkater>(0x1000 + 0x400 * k);
            std::memset(d, 0, sizeof(CSkater));
            new (&d->m_displayName) Str::String();
            d->m_unknownBC = k != 2 ? 1 : 0;
            d->m_paused = k == 0;
            d->m_viewport = k == 1 ? 1 : 0;
            d->m_unknown264 = (k == 0 || k == 3) ? Ptr<uint8_t>(kBlocks + 0x100 * 15) : nullptr;
            for (int i = 0; i < 11; ++i) {
                d->m_trickInfo[i] = (i + k) % 3 ? Ptr<uint8_t>(kBlocks + 0x100 * 23 + 0x10 * i) : nullptr;
                *Ptr<void**>(kBlocks + 0x100 * 23 + 0x10 * i) = g_infoVtable;
            }
            // Its list node in a list of three.
            auto* node = reinterpret_cast<uint32_t*>(d->m_block238);
            uint32_t prev = Block(24), next = Block(25);
            node[3] = next, node[4] = prev;
            reinterpret_cast<uint32_t*>(prev)[3] = reinterpret_cast<uint32_t>(node);
            reinterpret_cast<uint32_t*>(next)[4] = reinterpret_cast<uint32_t>(node);
            g_camera = true;
            if (k == 2) {
                if (original)
                    Original<void(__thiscall*)(void*)>(0x00583FB0)(d);
                else
                    d->DestroyBody();
            } else {
                t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, unsigned int)>(0x005840E0)(d, k)
                                                                    : d->Destroy(k))));
            }
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(d) + 0x164) = 0; // (the name's capacity)
            for (int w : {3, 4})
                t.push_back(Rel(reinterpret_cast<uint32_t*>(Block(24 + w - 3))[w]));
        }
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "skater restart");
}

} // namespace

SELFTEST(Early, TestSkaterRestart, ViewerTestKit::Cleanup);
