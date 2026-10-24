// Self-tests for the script commands 0x005B1B60-0x005B2420 (interpolating parameters, a texture's name, power-ups,
// the hunt, entering vehicles, the lobby keyboard): each original against ours on identical stand-in state; the
// network manager, its players and apps, the skate module, the object manager and the other callees are stood in for
// by recording code (the game's functions and ours alike). Structure getters run for real on hand-built structures.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/hunt_cfuncs.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/player_info.h"
#include "game/obj/compositemgr.h"
#include "game/obj/tracker.h"
#include "game/script/game_util_cfuncs.h"
#include "game/sio/device.h"
#include "game/skater/skater_state.h"
#include "hook/gs4_testkit.h"

namespace {

using namespace Gs4Tests;

alignas(16) uint8_t g_hSkate[0x100];
alignas(16) uint8_t g_hObjectManager[0x40];
alignas(16) uint8_t g_hNetManager[0x40];
alignas(16) uint8_t g_hPlayers[4][0x200];
alignas(16) uint8_t g_hServer[0x40];
alignas(16) uint8_t g_hClients[3][0x40];
alignas(16) uint8_t g_hObject[0x40];
bool g_hSkaterExists[8];
bool g_hLocal[4];
bool g_hObserver[4];
int g_hPlayerCount;
bool g_hInNetGame;
bool g_hObjectExists;
bool g_hHaveClient;
const char* g_hTyped;

uint32_t PlayerIndex(uint32_t address)
{
    for (uint32_t i = 0; i < 4; ++i)
        if (address == reinterpret_cast<uint32_t>(g_hPlayers[i]))
            return 0x9100 + i;
    return address;
}
// The players in order (observers skipped when asked); the search remembers the next index.
uint32_t PlayerFrom(uint32_t search, int start, bool observers)
{
    for (int i = start; i < g_hPlayerCount; ++i) {
        if (!observers && g_hObserver[i])
            continue;
        reinterpret_cast<uint32_t*>(search)[1] = static_cast<uint32_t>(i + 1);
        return reinterpret_cast<uint32_t>(g_hPlayers[i]);
    }
    return 0;
}
void RecordDesc(uint32_t address)
{
    auto* desc = reinterpret_cast<Net::MsgDesc*>(address);
    Record({desc->m_id, desc->m_stream, desc->m_length, static_cast<uint32_t>(desc->m_priority),
            static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_singular, static_cast<uint32_t>(desc->m_delay),
            desc->m_forcedSequenceId, desc->m_compression});
    auto* data = static_cast<const uint8_t*>(desc->m_data);
    if (data && desc->m_id == 0x89 && desc->m_length == 8) // (a byte and a dword: the padding is stack garbage)
        Record({data[0], *reinterpret_cast<const uint32_t*>(data + 4)});
    else if (data)
        Record({HashBytes(data, desc->m_length)});
}

void StandInHuntCalls(StandIns& s)
{
    using GameNet::Manager;
    s.At({0x005665C0}, 4, [](Regs& r) {
        Record({0x65C0, r.ecx == reinterpret_cast<uint32_t>(g_hSkate), r.args[0]});
        return r.args[0] < 8 && g_hSkaterExists[r.args[0]] ? 0x5C000000u + r.args[0] : 0u;
    });
    s.At({0x0045F2D0, AddressOf(&Obj::CCompositeObjectManager::GetFirstComponentByType)}, 4, [](Regs& r) {
        Record({0xF2D0, r.ecx == reinterpret_cast<uint32_t>(g_hObjectManager), r.args[0]});
        return 0x57A70000u;
    });
    s.At({0x00519470, AddressOf(&Obj::CSkaterStateComponent::ClearPowerups)}, 0, [](Regs& r) {
        Record({0x9470, r.ecx});
        return 0u;
    });
    s.At({0x0053AEA0, AddressOf(&Manager::PlayerByObjectId)}, 4, [](Regs& r) {
        Record({0xAEA0, r.ecx == reinterpret_cast<uint32_t>(g_hNetManager), r.args[0] & 0xFFFF});
        uint32_t id = r.args[0] & 0xFFFF;
        return id < 4 ? reinterpret_cast<uint32_t>(g_hPlayers[id]) : 0u;
    });
    s.At({0x005372C0, AddressOf(&Manager::FirstPlayerInfo)}, 8, [](Regs& r) {
        Record({0x72C0, r.ecx == reinterpret_cast<uint32_t>(g_hNetManager), r.args[1] & 0xFF,
                *reinterpret_cast<uint32_t*>(r.args[0])});
        return PlayerFrom(r.args[0], 0, (r.args[1] & 0xFF) != 0);
    });
    s.At({0x00537010, AddressOf(&Manager::NextPlayerInfo)}, 8, [](Regs& r) {
        Record({0x7010, r.args[1] & 0xFF});
        return PlayerFrom(r.args[0], static_cast<int>(reinterpret_cast<uint32_t*>(r.args[0])[1]), (r.args[1] & 0xFF) != 0);
    });
    s.At({0x00543300, AddressOf(&GameNet::PlayerInfo::IsLocalPlayer)}, 0, [](Regs& r) {
        uint32_t index = PlayerIndex(r.ecx);
        Record({0x3300, index});
        return g_hLocal[index & 3] ? 1u : 0u;
    });
    s.At({0x005434A0, AddressOf(&GameNet::PlayerInfo::GetConnHandle)}, 0, [](Regs& r) {
        Record({0x34A0, PlayerIndex(r.ecx)});
        return 0x4A00u + (PlayerIndex(r.ecx) & 0xFF);
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        Record({0x7EB0, r.ecx == reinterpret_cast<uint32_t>(g_hServer), r.args[0]});
        RecordDesc(r.args[1]);
        return 0u;
    });
    s.At({0x00532EA0, AddressOf(&Manager::GetClient)}, 4, [](Regs& r) {
        Record({0x2EA0, r.args[0]});
        return g_hHaveClient ? reinterpret_cast<uint32_t>(g_hClients[r.args[0] % 3]) : 0u;
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        Record({0x9E50, r.ecx - reinterpret_cast<uint32_t>(g_hClients)});
        RecordDesc(r.args[0]);
        return 0u;
    });
    s.At({0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, [](Regs& r) {
        Record({0xE4F0, r.ecx == reinterpret_cast<uint32_t>(g_hNetManager)});
        return g_hInNetGame ? 1u : 0u;
    });
    s.At({0x005BCC40, AddressOf(&Sio::GetKeyboardChars)}, 0, [](Regs& r) {
        Record({0xCC40});
        std::memcpy(reinterpret_cast<char*>(r.args[0]), g_hTyped, std::strlen(g_hTyped) + 1);
        return static_cast<uint32_t>(std::strlen(g_hTyped));
    });
    s.At({0x005BCC50, AddressOf(&Sio::ClearKeyboardChars)}, 0, [](Regs&) {
        Record({0xCC50});
        return 0u;
    });
    s.At({0x00462340, AddressOf(&Obj::CFunc_ObjectExists)}, 0, [](Regs& r) {
        Record({0x2340, ArrayId(r.args[0]), r.args[1]});
        return g_hObjectExists ? 1u : 0u;
    });
    using RunScriptFn = void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*);
    s.At({0x00475790, AddressOf(static_cast<RunScriptFn>(&Script::RunScript))}, 0, [](Regs& r) {
        Record({0x5790, r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
        return 0u;
    });
}

void TestHuntCFuncs()
{
    SaveGlobals({{0x007CE478, 4, {}}, {0x006F0FB8, 4, {}}, {0x007CCE14, 4, {}}});
    auto* standIns = new StandIns;
    StandInStructCalls(*standIns);
    StandInHuntCalls(*standIns);

    constexpr uint32_t kA = 0x174841BC, kB = 0x8E411006, kT = 0x404E690D, kIgnore = 0xF277291D, kAngles = 0x9D2D0915;
    static TestParams a, b, a2, b2;
    static TestArray ignore, angles;
    a.Reset();
    a.Integer(0x0A000001, 10).Float(0x0A000002, 1.5f).Integer(0x0A000003, -7).Float(0x0A000004, 3.0f)
        .Checksum(0x0A000005, 0x1234).Integer(0x0A000006, 100).Float(0x0A000007, 0.1f).Integer(0, 9)
        .Float(0x0A000008, 3.0f).Float(0x0A000009, -3.0f).Integer(0x0A00000A, 3).Float(0x0A00000B, 0.25f)
        .Integer(0x0A00000C, 2000000001);
    b.Reset();
    b.Integer(0x0A000001, 20).Float(0x0A000002, -2.25f).Float(0x0A000003, 2.5f).Integer(0x0A000004, 5)
        .Integer(0x0A000005, 1).Integer(0x0A000006, 7).Float(0x0A000008, -3.0f).Float(0x0A000009, 3.0f)
        .Integer(0x0A00000A, -3).Float(0x0A00000B, 6.1f).Integer(0x0A00000C, -2000000001).Integer(0, 4);
    ignore.Set(Script::ESYMBOLTYPE_NAME, {0x0A000006, 0x0BADBAD0});
    angles.Set(Script::ESYMBOLTYPE_NAME, {0x0A000008, 0x0A000009, 0x0A00000A, 0x0A00000B});
    a2.Reset();
    b2.Reset();
    a2.Float(0x0A000001, 1.0f);
    b2.Float(0x0A000001, 2.0f);

    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        UseStandInArrayPool();
        UseStandInStructPool();
        std::memset(g_hSkate, 0, sizeof(g_hSkate));
        std::memset(g_hNetManager, 0, sizeof(g_hNetManager));
        reinterpret_cast<void**>(g_hNetManager)[1] = g_hServer; // m_server
        std::memset(g_hPlayers, 0, sizeof(g_hPlayers));
        GameVariable<void*>(0x007CE478) = g_hSkate;
        GameVariable<void*>(0x006F0FB8) = g_hObjectManager;
        GameVariable<void*>(0x007CCE14) = g_hNetManager;
        for (int i = 0; i < 8; ++i)
            g_hSkaterExists[i] = i == 0 || i == 3 || i == 7;
        g_hPlayerCount = 4;
        for (int i = 0; i < 4; ++i) {
            g_hLocal[i] = i == 1;
            g_hObserver[i] = i == 2;
        }
        g_hInNetGame = false;
        g_hObjectExists = false;
        g_hHaveClient = true;
        g_own.Reset();
        auto call = [&](uint32_t address, CFunc ours, TestParams& params, void* object = nullptr) {
            Call(original, address, ours, params, object);
        };
        using namespace CFuncs;
        TestParams none;

        // InterpolateParameters
        for (float t : {0.0f, 0.3f, 1.0f, -0.5f, 2.75f}) {
            TestParams p; p.Structure(kA, a.Struct()).Structure(kB, b.Struct()).Float(kT, t).Array(kIgnore, ignore.Get())
                .Array(kAngles, angles.Get());
            UseStandInArrayPool();
            call(0x005B1B60, CFunc_InterpolateParameters, p);
        }
        {
            TestParams p; p.Structure(kA, a.Struct()).Structure(kB, b.Struct()).Float(kT, 0.6f);
            UseStandInArrayPool();
            call(0x005B1B60, CFunc_InterpolateParameters, p);
            TestParams q; q.Structure(kA, a2.Struct()).Structure(kB, b2.Struct());
            UseStandInArrayPool();
            call(0x005B1B60, CFunc_InterpolateParameters, q);
        }
        // GetTextureFromPath
        for (const char* path : {"textures/skater/face_01", "a\\b/c\\name", "plain", "", "end/", "/x"}) {
            TestParams p; p.String(0xF4AB74F0, path);
            call(0x005B1E00, CFunc_GetTextureFromPath, p);
        }
        // ClearPowerups
        call(0x005B1EA0, CFunc_ClearPowerups, none);
        // PlaceObject, PlacedLessThan
        {
            constexpr uint32_t kPos = 0x7F261953;
            alignas(16) static float pos[4][4];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    pos[i][j] = static_cast<float>(i * 10 + j) + 0.5f;
            call(0x005B1EE0, CFunc_PlaceObject, none); // no object
            reinterpret_cast<uint32_t*>(g_hObject)[0x14 / 4] = 2;
            for (int i = 0; i < 3; ++i) {
                TestParams p; p.Vector(kPos, pos[i]);
                call(0x005B1EE0, CFunc_PlaceObject, p, g_hObject);
            }
            for (int n : {1, 3, 4}) {
                TestParams p; p.Integer(0, n);
                call(0x005B2210, CFunc_PlacedLessThan, p, g_hObject);
            }
            reinterpret_cast<uint32_t*>(g_hObject)[0x14 / 4] = 7; // no player
            TestParams p; p.Vector(kPos, pos[3]);
            call(0x005B1EE0, CFunc_PlaceObject, p, g_hObject);
            TestParams q; q.Integer(0, 9);
            call(0x005B2210, CFunc_PlacedLessThan, q, g_hObject);
            call(0x005B2210, CFunc_PlacedLessThan, q);
            Record({HashBytes(g_hPlayers[2] + 0x30, 0x1A0)});
        }
        // AnnounceHuntStarted, AnnounceNeedToPlaceObjects, AtLeastOneObjectPlaced
        {
            call(0x005B1F70, CFunc_AnnounceHuntStarted, none);
            call(0x005B2030, CFunc_AnnounceNeedToPlaceObjects, none);
            call(0x005B20E0, CFunc_AtLeastOneObjectPlaced, none); // player 2 (an observer) has placed some
            reinterpret_cast<int*>(g_hPlayers[3] + 0x34)[0] = 1;
            call(0x005B20E0, CFunc_AtLeastOneObjectPlaced, none);
            reinterpret_cast<int*>(g_hPlayers[3] + 0x34)[0] = -1;
            call(0x005B20E0, CFunc_AtLeastOneObjectPlaced, none); // (a negative count isn't one)
            reinterpret_cast<int*>(g_hPlayers[0] + 0x34)[0] = 2;
            call(0x005B20E0, CFunc_AtLeastOneObjectPlaced, none);
            g_hPlayerCount = 0;
            call(0x005B1F70, CFunc_AnnounceHuntStarted, none);
            call(0x005B20E0, CFunc_AtLeastOneObjectPlaced, none);
            g_hPlayerCount = 4;
        }
        // ReportObjectPickup, BroadcastEnterVehicle
        {
            TestParams p; p.Checksum(0xF79C90AE, 2).Checksum(0x40C698AF, 0x0B1EC700);
            call(0x005B2170, CFunc_ReportObjectPickup, p);
            g_hInNetGame = true;
            call(0x005B2170, CFunc_ReportObjectPickup, p);
            g_hInNetGame = false;
            TestParams q; q.Checksum(0x5B24FAAA, 0x1234567F).Checksum(0x81CFF663, 0xC0C0C0C0);
            call(0x005B2260, CFunc_BroadcastEnterVehicle, q);
            g_hHaveClient = false;
            call(0x005B2260, CFunc_BroadcastEnterVehicle, q);
            g_hHaveClient = true;
        }
        // LobbyCheckKeyboard
        for (const char* typed : {"", " ", " hello", "x ", "  "}) {
            for (bool exists : {false, true}) {
                g_hTyped = typed;
                g_hObjectExists = exists;
                UseStandInArrayPool();
                call(0x005B2300, CFunc_LobbyCheckKeyboard, none);
            }
        }
        traces[run] = g_trace;
    }
    RestoreGlobals();
    DumpTraces(traces[0], traces[1]);
    Compare(traces[0], traces[1], "interpolation, texture name, power-ups, the hunt, vehicles, lobby keyboard (0x005B1B60-0x005B2420)");
}

} // namespace

SELFTEST(Early, TestHuntCFuncs, Gs4Tests::RestoreGlobals);
