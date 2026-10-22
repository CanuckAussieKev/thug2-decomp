// Self-tests for the network game's join (src/game/net/gamenet_join.cpp): every function the original against ours on
// a stand-in world - the GameNet manager, its apps and connections, the net library's and the script system's calls,
// GameSpy's CD-key SDK, the memory manager and the main loop's tasks all stood in by recorders - for each branch; the
// calls (with the messages' defined bytes), the handlers added and the manager's state compared. The THUGPRO peer
// mode's changes are tested on ours alone in selftests_thugpro_join.cpp.

#include <windows.h>

#pragma warning(disable : 4996) // (strcpy into the test's own buffers)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/chat.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "game/net/net_manager.h"
#include "game/net/thugpro_join.h"
#include "game/net/thugpro_peer.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace JoinTests {

using GameNet::Manager;
using TestStandIns::AddressOf;
using TestStandIns::g_trace;
using TestStandIns::Hash;
using TestStandIns::HashBytes;
using TestStandIns::Record;
using TestStandIns::Regs;
using TestStandIns::Resolve;
using TestStandIns::StandIns;

// --- The stand-in world: blocks at fixed places in one allocation (the same in both runs) ---

struct Block {
    uint32_t offset;
    uint32_t size;
};
enum BlockId : uint32_t {
    kManager, kNetManager, kMainLoop, kMemManager, kServerApp, kClient0, kClient1, kNewApp, kConn0, kConn1, kConn2,
    kConn3, kLobby, kBuddy, kStats, kGameMode, kSkate, kProfileLocal, kProfileNew, kPlayerHeap, kMessage,
    kPrefStruct, kTasks, kBlockCount
};
constexpr uint32_t kConnSize = (sizeof(Net::Conn) + 0xF) & ~0xFu;
constexpr uint32_t kAppSize = (sizeof(Net::Client) + 0xF) & ~0xFu;
const Block kBlocks[kBlockCount] = {
    {0x00000, 0x400},              // GameNet::Manager
    {0x00400, 0x400},              // Net::Manager
    {0x00800, 0x100},              // Mlp::Manager
    {0x00900, 0x700},              // Mem::Manager
    {0x01000, kAppSize},           // the server
    {0x01000 + kAppSize, kAppSize},         // client 0
    {0x01000 + 2 * kAppSize, kAppSize},     // client 1
    {0x01000 + 3 * kAppSize, kAppSize},     // a new app (CreateNewApp...)
    {0x01000 + 4 * kAppSize, kConnSize},    // connections
    {0x01000 + 4 * kAppSize + kConnSize, kConnSize},
    {0x01000 + 4 * kAppSize + 2 * kConnSize, kConnSize},
    {0x01000 + 4 * kAppSize + 3 * kConnSize, kConnSize},
    {0x01000 + 4 * kAppSize + 4 * kConnSize, 0x200},   // lobby
    {0x01200 + 4 * kAppSize + 4 * kConnSize, 0x40},    // presence
    {0x01240 + 4 * kAppSize + 4 * kConnSize, 0x40},    // stats
    {0x01280 + 4 * kAppSize + 4 * kConnSize, 0x40},    // game mode
    {0x012C0 + 4 * kAppSize + 4 * kConnSize, 0x40},    // skate
    {0x01300 + 4 * kAppSize + 4 * kConnSize, 0x80},    // the local skater's profile
    {0x01380 + 4 * kAppSize + 4 * kConnSize, 0x80},    // the new player's profile
    {0x01400 + 4 * kAppSize + 4 * kConnSize, 0x4000},  // allocations
    {0x05400 + 4 * kAppSize + 4 * kConnSize, 0x2000},  // the received message
    {0x07400 + 4 * kAppSize + 4 * kConnSize, 0x40},    // the preference structure
    {0x07440 + 4 * kAppSize + 4 * kConnSize, 0x40},    // tasks (4 x 0x10)
};
constexpr uint32_t kWorldSize = 0x07480 + 4 * kAppSize + 4 * kConnSize;
uint8_t* g_world;
uint32_t g_heapUsed;

uint8_t* At(BlockId id) { return g_world + kBlocks[id].offset; }
template <typename T>
T* As(BlockId id)
{
    return reinterpret_cast<T*>(At(id));
}
// A pointer into the world as (block + 1) << 24 | offset; the game's own addresses and others as they are.
uint32_t Rel(uint32_t p)
{
    auto base = reinterpret_cast<uint32_t>(g_world);
    if (p >= base && p < base + kWorldSize) {
        uint32_t offset = p - base;
        for (uint32_t i = 0; i < kBlockCount; ++i)
            if (offset >= kBlocks[i].offset && offset < kBlocks[i].offset + kBlocks[i].size)
                return ((i + 1) << 24) | (offset - kBlocks[i].offset);
    }
    return p;
}
uint32_t Rel(const void* p) { return Rel(reinterpret_cast<uint32_t>(p)); }

// The script answers.
struct Scripted {
    int banned = 0;
    int numObservers = 0, maxObservers = 2, numPlayers = 1, maxPlayers = 8;
    int rankLimit = 3, rankTest = 0;
    const char* password = "";
    uint32_t gameMode = 0x1C471C60; // netlobby
    int rank = 4;
    bool createFails = false;
    uint32_t firstConn = kConn1;
    int writeSize = 300;
    // The bytes of a message's fields left undefined by the original (its stack): not compared.
    bool rankDefined = true, cdKeyDefined = false, proceedIps = false, proceedAll = true, challengeDefined = true;
};
Scripted g_script;
uint32_t g_nextConn;

// The handlers' codes: ours mapped to the addresses they replace.
uint32_t HandlerAddress(uint32_t code)
{
    struct Pair {
        Net::MsgHandlerCode ours;
        uint32_t address;
    };
    const Pair pairs[] = {
        {&Manager::HandleJoinRefused, 0x0052E1E0}, {&Manager::HandleJoinProceed, 0x0052E490},
        {&GameNet::HandleChat, 0x00532050},       {&Manager::HandleConnection, 0x0052F790},
        {&Manager::HandleJoinRequest, 0x0052FA10},
    };
    for (const Pair& pair : pairs)
        if (Resolve(reinterpret_cast<const void*>(pair.ours)) == Resolve(reinterpret_cast<const void*>(code)))
            return pair.address;
    return code;
}

// A message's defined bytes, by id.
void RecordMessage(uint8_t id, uint32_t length, const uint8_t* data)
{
    Record({0x3E55, id, length});
    if (!data)
        return;
    if (id == 3 || id == 80) {
        auto* info = reinterpret_cast<const GameNet::ConnectInfo*>(data);
        Record({static_cast<uint32_t>(info->version), static_cast<uint32_t>(info->observer) | static_cast<uint32_t>(info->willingToWait) << 8 |
                    static_cast<uint32_t>(info->broadband) << 16,
                Hash(std::string(info->password, strnlen(info->password, sizeof(info->password))).c_str())});
        if (g_script.rankDefined)
            Record({static_cast<uint32_t>(info->rank)});
        if (g_script.cdKeyDefined)
            Record({Hash(std::string(info->cdKeyResponse, strnlen(info->cdKeyResponse, 74)).c_str())});
        if (id == 80) {
            auto* join = reinterpret_cast<const GameNet::JoinInfo*>(data);
            Record({static_cast<uint32_t>(join->profile), static_cast<uint32_t>(join->rating),
                    HashBytes(reinterpret_cast<const uint8_t*>(join->name), sizeof(join->name)),
                    HashBytes(join->appearance, length > GameNet::kJoinInfoHeaderSize ? length - GameNet::kJoinInfoHeaderSize : 0)});
        }
    } else if (id == 79) {
        auto* proceed = reinterpret_cast<const GameNet::ProceedInfo*>(data);
        Record({static_cast<uint32_t>(proceed->maxPlayers)});
        if (g_script.proceedAll)
            Record({static_cast<uint32_t>(proceed->broadband),
                    HashBytes(reinterpret_cast<const uint8_t*>(proceed->serverName), sizeof(proceed->serverName))});
        if (g_script.proceedIps)
            Record({proceed->publicIp, proceed->privateIp, static_cast<uint32_t>(proceed->port)});
    } else if (id == 71) {
        Record({data[0]});
        if (data[0] == 8 && g_script.challengeDefined)
            Record({Hash(std::string(reinterpret_cast<const char*>(data + 1), strnlen(reinterpret_cast<const char*>(data + 1), 9)).c_str())});
    } else {
        Record({HashBytes(data, length)});
    }
}
// Every message sent (for the peer mode's checks): id, length, compression, the bytes.
struct Sent {
    uint8_t id;
    uint32_t length;
    uint8_t compression;
    std::vector<uint8_t> bytes;
};
std::vector<Sent> g_sent;
void Capture(uint8_t id, uint32_t length, uint8_t compression, const void* data)
{
    const auto* p = static_cast<const uint8_t*>(data);
    g_sent.push_back({id, length, compression, p ? std::vector<uint8_t>(p, p + length) : std::vector<uint8_t>()});
}

void RecordDesc(const Net::MsgDesc* desc)
{
    Capture(desc->m_id, desc->m_length, desc->m_compression, desc->m_data);
    Record({0xDE5C, static_cast<uint32_t>(desc->m_priority), static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_compression});
    RecordMessage(desc->m_id, desc->m_length, static_cast<const uint8_t*>(desc->m_data));
}

uint32_t Allocate(uint32_t size, bool zero)
{
    uint32_t at = (g_heapUsed + 0xF) & ~0xFu;
    g_heapUsed = at + size;
    uint8_t* p = At(kPlayerHeap) + at;
    std::memset(p, zero ? 0 : 0xCD, size);
    Record({0xA110C, size});
    return reinterpret_cast<uint32_t>(p);
}

void StandInWorld(StandIns& s)
{
    using namespace Net;
    // The net library.
    s.At({0x00458D50, AddressOf(&App::IsConnectionBanned)}, 4, [](Regs& r) {
        Record({0xBA4, Rel(r.ecx), Rel(r.args[0])});
        return static_cast<uint32_t>(g_script.banned);
    });
    s.At({0x004597C0, AddressOf(&App::NewConnection)}, 12, [](Regs& r) {
        Record({0x4E3C, Rel(r.ecx), r.args[0], r.args[1] & 0xFFFF, r.args[2]});
        return reinterpret_cast<uint32_t>(At(static_cast<BlockId>(g_nextConn++)));
    });
    s.At({0x00456B40, AddressOf(&App::AliasConnections)}, 8, [](Regs& r) {
        Record({0xA11A5, Rel(r.ecx), Rel(r.args[0]), Rel(r.args[1])});
        return 0u;
    });
    s.At({0x00459F40, AddressOf(&Client::ConnectToServer)}, 8, [](Regs& r) {
        Record({0xC0C7, Rel(r.ecx), r.args[0], r.args[1] & 0xFFFF});
        return 1u;
    });
    s.At({0x00459E50, AddressOf(&Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        Record({0xE45, Rel(r.ecx)});
        RecordDesc(reinterpret_cast<MsgDesc*>(r.args[0]));
        return 0u;
    });
    s.At({0x00457EB0, AddressOf(&App::EnqueueMessage)}, 8, [](Regs& r) {
        Record({0xE4, Rel(r.ecx), r.args[0]});
        RecordDesc(reinterpret_cast<MsgDesc*>(r.args[1]));
        return 0u;
    });
    s.At({0x00458CA0, AddressOf(&App::SendData)}, 4, [](Regs& r) {
        Record({0x5E4D, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x00457280, AddressOf(&App::SendMessageTo)}, 24, [](Regs& r) {
        Record({0x5E2D, Rel(r.ecx), r.args[3], r.args[4] & 0xFFFF, r.args[5]});
        Capture(static_cast<uint8_t>(r.args[0]), r.args[1] & 0xFFFF, 0, reinterpret_cast<const void*>(r.args[2]));
        RecordMessage(static_cast<uint8_t>(r.args[0]), r.args[1] & 0xFFFF, reinterpret_cast<const uint8_t*>(r.args[2]));
        return 1u;
    });
    s.At({0x0045A5C0, AddressOf(&Dispatcher::AddHandler)}, 20, [](Regs& r) {
        Record({0xADD, Rel(r.ecx), r.args[0] & 0xFF, HandlerAddress(r.args[1]), r.args[2], Rel(r.args[3]), r.args[4]});
        return 0u;
    });
    s.At({0x00455A40, AddressOf(&Conn::SetBandwidthType)}, 4, [](Regs& r) {
        Record({0xBA2D, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00455C10, AddressOf(&Conn::Invalidate)}, 0, [](Regs& r) {
        Record({0x12F, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00458BE0, AddressOf(&App::FirstConnection)}, 4, [](Regs& r) {
        Record({0xF125, Rel(r.ecx), *reinterpret_cast<uint32_t*>(r.args[0])});
        return reinterpret_cast<uint32_t>(At(static_cast<BlockId>(g_script.firstConn)));
    });
    s.At({0x00454910, AddressOf(&Net::Manager::GetLocalIP)}, 0, [](Regs& r) {
        Record({0x10CA1, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>("10.0.0.7");
    });
    s.At({0x005BEAAE}, 4, [](Regs& r) {
        Record({0x1E7, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0x0700000Au;
    });
    s.At({0x004553F0, AddressOf(&Net::Manager::CreateNewAppClient)}, 20, [](Regs& r) {
        Record({0xC11E, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2] & 0xFFFF,
                r.args[3], r.args[4]});
        return g_script.createFails ? 0u : reinterpret_cast<uint32_t>(At(kNewApp));
    });
    s.At({0x00455270, AddressOf(&Net::Manager::CreateNewAppServer)}, 24, [](Regs& r) {
        Record({0x5E2E, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2],
                r.args[3] & 0xFFFF, r.args[4], r.args[5]});
        return g_script.createFails ? 0u : reinterpret_cast<uint32_t>(At(kNewApp));
    });

    // The GameNet manager's own (not source yet).
    auto answer = [&](uint32_t address, uint32_t tag, int* value) {
        s.At({address}, 0, [tag, value](Regs& r) {
            Record({tag, Rel(r.ecx)});
            return static_cast<uint32_t>(*value);
        });
    };
    answer(0x00539C70, 0x0B51, &g_script.numObservers);
    answer(0x00532D60, 0x0B52, &g_script.maxObservers);
    answer(0x00539B00, 0x0B53, &g_script.numPlayers);
    answer(0x00532C50, 0x0B54, &g_script.maxPlayers);
    answer(0x00532B30, 0x0B55, &g_script.rankLimit);
    answer(0x00532AE0, 0x0B56, &g_script.rankTest);
    answer(0x0054D2D0, 0x0B57, &g_script.rank);
    s.At({0x00534310}, 0, [](Regs& r) {
        Record({0x0B58, Rel(r.ecx)});
        return reinterpret_cast<uint32_t>(g_script.password);
    });
    s.At({0x005532A0}, 0, [](Regs& r) {
        Record({0x0B59, Rel(r.ecx)});
        return g_script.gameMode;
    });
    s.At({0x0053AC40, AddressOf(&GameNet::Manager::NextPlayerObjectId)}, 0, [](Regs& r) {
        Record({0x0B5A, Rel(r.ecx)});
        return 5u;
    });
    s.At({0x0053A310}, 8, [](Regs& r) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(r.args[0]);
        Record({0xDEFE, Rel(r.ecx), Rel(r.args[0]), HashBytes(p, 16), p[0x10], Rel(*reinterpret_cast<const uint32_t*>(p + 0x14)),
                *reinterpret_cast<const uint32_t*>(p + 0x20), *reinterpret_cast<const uint32_t*>(p + 0x2C),
                *reinterpret_cast<const uint32_t*>(p + 0x30), *reinterpret_cast<const uint32_t*>(p + 0x34),
                *reinterpret_cast<const uint32_t*>(p + 0x40), r.args[1]});
        return 0u;
    });
    s.At({0x00537140}, 4, [](Regs& r) {
        Record({0x4EAD, Rel(r.ecx), r.args[0] & 0xFF});
        return 0u;
    });
    s.At({0x005359F0}, 0, [](Regs& r) {
        Record({0x4A2D, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x0053C6E0}, 0, [](Regs& r) {
        Record({0xCA2C, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x005531B0}, 4, [](Regs& r) {
        Record({0x3A85, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005434D0}, 0, [](Regs& r) {
        Record({0xC702, Rel(r.ecx)});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x1C) = reinterpret_cast<uint32_t>(At(kProfileNew));
        return r.ecx;
    });
    s.At({0x00543560}, 0, [](Regs& r) {
        Record({0xD702, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x00564FC0}, 4, [](Regs& r) {
        Record({0x9F0F, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(At(kProfileLocal));
    });
    s.At({0x00587050, AddressOf(&Obj::CSkaterProfile::WriteToBuffer)}, 12, [](Regs& r) {
        Record({0x3417, Rel(r.ecx), r.args[1], r.args[2] & 0xFF});
        auto* buffer = reinterpret_cast<uint8_t*>(r.args[0]);
        for (int i = 0; i < g_script.writeSize; ++i)
            buffer[i] = static_cast<uint8_t>(i * 7 + 1);
        return static_cast<uint32_t>(g_script.writeSize);
    });
    s.At({0x00586DC0, AddressOf(&Obj::CSkaterProfile::ReadFromBuffer)}, 4, [](Regs& r) {
        Record({0x4EAF, Rel(r.ecx), HashBytes(reinterpret_cast<const uint8_t*>(r.args[0]), GameNet::kAppearanceSize)});
        return r.args[0];
    });
    s.At({0x00548320}, 12, [](Regs& r) {
        Record({0x9E5E, Rel(r.ecx), r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])),
                Hash(reinterpret_cast<const char*>(r.args[2]))});
        return 0u;
    });
    // GameSpy's CD-key SDK.
    s.At({0x0052D6A0}, 0, [](Regs& r) {
        Record({0xCDC0});
        std::strcpy(reinterpret_cast<char*>(r.args[0]), "CDKEY-1234-5678");
        return 0u;
    });
    s.At({0x005F8950}, 0, [](Regs& r) {
        Record({0xCD2E, Hash(reinterpret_cast<const char*>(r.args[0])), Hash(reinterpret_cast<const char*>(r.args[1]))});
        std::snprintf(reinterpret_cast<char*>(r.args[2]), 73, "RESPONSE-%s-%072d", reinterpret_cast<const char*>(r.args[1]), 0);
        reinterpret_cast<char*>(r.args[2])[72] = '\0';
        return 0u;
    });
    s.At({0x005F9620}, 0, [](Regs& r) {
        uint32_t callback = Resolve(reinterpret_cast<const void*>(r.args[5])) ==
                                    Resolve(reinterpret_cast<const void*>(&Manager::ClientAuthorized))
                                ? 0x00536530u
                                : r.args[5];
        Record({0x6CDA, r.args[0], r.args[1], r.args[2], Hash(reinterpret_cast<const char*>(r.args[3])),
                Hash(reinterpret_cast<const char*>(r.args[4])), callback, Rel(r.args[6])});
        if (r.args[6]) {
            auto* instance = reinterpret_cast<const uint32_t*>(r.args[6]);
            Record({Rel(instance[0]), Rel(instance[1]), Rel(instance[2])});
        }
        return 0u;
    });
    s.At({0x006234FE}, 0, TestStandIns::StandInSprintf);
    // The memory manager.
    s.At({0x004015D0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::New))}, 0,
         [](Regs& r) { return Allocate(r.args[0], false); });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0,
         [](Regs& r) { return Allocate(r.args[0], true); });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) { return Allocate(r.args[0], false); });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF2EE, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0x9054, Rel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Record({0x9090, Rel(r.ecx)});
        return 0u;
    });
    s.At({0x004E1950, AddressOf(&Mem::EnsureManager)}, 0, [](Regs&) {
        Record({0xE45E});
        return 1u;
    });
    // Scripts.
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Record({0x9E4F, Rel(r.ecx), r.args[0]});
        return reinterpret_cast<uint32_t>(At(kPrefStruct));
    });
    s.At({0x00476C60, AddressOf(static_cast<bool (::Script::CStruct::*)(uint32_t, const char**, int)>(
                          &::Script::CStruct::GetString))},
         12, [](Regs& r) {
             Record({0x6575, Rel(r.ecx), r.args[0], r.args[2] & 0xFF});
             *reinterpret_cast<const char**>(r.args[1]) = "Server Name Is Long";
             return 1u;
         });
    s.At({0x00476C80}, 12, [](Regs& r) {
        Record({0x6575, Rel(r.ecx), Crc::GenerateCRCFromString(reinterpret_cast<const char*>(r.args[0])),
                r.args[2] & 0xFF});
        *reinterpret_cast<const char**>(r.args[1]) = "Server Name Is Long";
        return 1u;
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&::Script::GetString))}, 0, [](Regs& r) {
        Record({0x6576, r.args[0]});
        return reinterpret_cast<uint32_t>("status");
    });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, ::Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &::Script::RunScript))},
         0, [](Regs& r) {
             Record({0x4C5, r.args[0], r.args[1], r.args[2]});
             return 0u;
         });
    // The main loop's tasks and time.
    s.At({0x005BE970}, 0, [](Regs&) {
        Record({0x7143});
        return 4242u;
    });
    s.At({0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, [](Regs& r) {
        Record({0x7A5C, Rel(r.ecx), Rel(r.args[0])});
        return 0u;
    });
    s.At({0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, [](Regs& r) {
        Record({0x7A5D, Rel(r.ecx)});
        return 0u;
    });
}

std::vector<TestStandIns::Saved> g_saved;
bool g_swapped = false;

void CleanUp()
{
    TestStandIns::RemoveStandIns();
    GameNet::Thugpro::SetPeerModeOverride(-1);
    if (g_swapped) {
        TestStandIns::RestoreAll(g_saved);
        g_swapped = false;
    }
    if (g_world) {
        VirtualFree(g_world, 0, MEM_RELEASE);
        g_world = nullptr;
    }
}

// A fresh world for one run: the blocks zeroed (the heap dirty), the globals pointing at them.
void SetUpWorld()
{
    std::memset(g_world, 0, kWorldSize);
    std::memset(At(kPlayerHeap), 0xCD, kBlocks[kPlayerHeap].size);
    g_heapUsed = 0;
    g_nextConn = kConn2;
    GameVariable<void*>(0x006EFDD8) = At(kNetManager);
    GameVariable<void*>(0x00696D68) = At(kMainLoop);
    GameVariable<void*>(0x007D1800) = At(kMemManager);
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<void*>(0x007CE470) = At(kGameMode);
    GameVariable<void*>(0x007CE478) = At(kSkate);
    std::memset(&GameVariable<char>(0x007CCDFC), 0, 22);
    As<Mem::Manager>(kMemManager)->m_frontEndHeap = reinterpret_cast<Mem::Heap*>(0x0F00F00F);
    auto* m = As<Manager>(kManager);
    m->m_server = As<Net::App>(kServerApp);
    m->m_client = As<Net::App>(kClient0);
    m->m_client1 = As<Net::App>(kClient1);
    m->m_lobbyMan = At(kLobby);
    m->m_buddyMan = At(kBuddy);
    m->m_statsMan = At(kStats);
    std::strcpy(m->m_challenge, "CHALLENG");
    auto* tasks = reinterpret_cast<Tsk::BaseTask*>(At(kTasks));
    m->m_serverAddNewPlayersTask = tasks;
    m->m_joinTimeoutTask = reinterpret_cast<Tsk::BaseTask*>(At(kTasks) + 0x10);
    m->m_joinStateTask = reinterpret_cast<Tsk::BaseTask*>(At(kTasks) + 0x20);
    m->m_refusedReason = 0x12345678;
    m->m_joinState = 7;
    m->m_observerCommand = 9;
    m->m_observerInputHandler = reinterpret_cast<void*>(0x77);
    m->m_hostMode = 3;
    m->m_gameOver = true;
    m->m_gamePending = true;
    m->m_proSetFlags = 0xFF;
    m->m_latestReadyQuery = 0x66;
    m->m_camPlayer = reinterpret_cast<void*>(0x55);
    m->m_lastScoreUpdate = 0x44;
    m->m_waitingForGameToStart = true;
    m->m_joinIp = 0x0100007F;
    m->m_joinPrivateIp = 0x0200A8C0;
    m->m_joinPort = 5153;
    *reinterpret_cast<int32_t*>(At(kBuddy) + 4) = 0xB0B0;  // the GameSpy profile id
    *reinterpret_cast<uint32_t*>(At(kStats) + 0x14) = 777; // the rating
    std::strcpy(reinterpret_cast<char*>(At(kLobby) + 0x34), "peer");
    for (BlockId conn : {kConn0, kConn1, kConn2, kConn3}) {
        auto* c = As<Net::Conn>(conn);
        c->m_ip = 0x0A0B0C0D + conn;
        c->m_port = static_cast<uint16_t>(6000 + conn);
        c->m_handle = 40 + conn;
        c->m_id = 900 + conn;
        c->m_status = 0x1;
    }
    As<Net::Conn>(kConn1)->m_bandwidthType = Net::Conn::kBandwidthModem;
}

// One case: the world set up, `body` run as the original or ours, then the manager's state, the passwords and the
// connections recorded.
template <typename Body>
std::vector<uint32_t> Run(bool original, Body body)
{
    g_trace.clear();
    SetUpWorld();
    std::vector<OriginalCode*> guards;
    if (original) {
        for (uint32_t address : {0x0052F560u, 0x00536530u})
            guards.push_back(new OriginalCode(address));
    }
    body(original);
    for (OriginalCode* guard : guards)
        delete guard;
    std::vector<uint32_t> trace = g_trace;
    const uint32_t* m = reinterpret_cast<const uint32_t*>(At(kManager));
    for (uint32_t i = 0; i < 0x1D0 / 4; ++i)
        trace.push_back(Rel(m[i]));
    trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&GameVariable<char>(0x007CCDFC)), 22));
    for (BlockId conn : {kConn0, kConn1, kConn2, kConn3}) {
        auto* c = As<Net::Conn>(conn);
        trace.push_back(c->m_status);
        trace.push_back(c->m_sendInterval);
    }
    return trace;
}

int g_cases = 0;
template <typename Body>
void Compare(const char* what, Body body)
{
    std::vector<uint32_t> expected = Run(true, body);
    std::vector<uint32_t> actual = Run(false, body);
    ++g_cases;
    TestStandIns::Compare(expected, actual, what);
}

Manager* M() { return As<Manager>(kManager); }
void SetFlag(BlockId app, uint32_t flags) { reinterpret_cast<Net::App*>(At(app))->m_flags = flags; }
void SetConnFlags(BlockId conn, uint32_t flags) { As<Net::Conn>(conn)->m_flags = flags; }

Net::MsgHandlerContext g_context;
Net::MsgHandlerContext* Context(uint8_t id, uint32_t length, BlockId conn, BlockId app, uint32_t packetFlags)
{
    std::memset(&g_context, 0, sizeof(g_context));
    g_context.m_Msg = reinterpret_cast<char*>(At(kMessage));
    g_context.m_MsgId = id;
    g_context.m_MsgLength = length;
    g_context.m_Conn = As<Net::Conn>(conn);
    g_context.m_App = As<Net::App>(app);
    g_context.m_PacketFlags = packetFlags;
    g_context.m_data = M();
    return &g_context;
}
GameNet::ConnectInfo* Request() { return reinterpret_cast<GameNet::ConnectInfo*>(At(kMessage)); }
void RecordResult(Net::MsgHandlerContext* context, int result)
{
    Record({0x2E5, static_cast<uint32_t>(result), Rel(context->m_Conn), context->m_Acknowledge});
}

using ContextFn = int(__cdecl*)(Net::MsgHandlerContext*);
int Handle(bool original, uint32_t address, ContextFn ours, Net::MsgHandlerContext* context)
{
    int result = original ? Original<ContextFn>(address)(context) : ours(context);
    RecordResult(context, result);
    return result;
}

void TestGameNetJoin()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestGameNetJoin skipped (the memory manager is already set up)");
        return;
    }
    g_saved = {{0x006EFDD8, 4, {}}, {0x00696D68, 4, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
               {0x007CE470, 4, {}}, {0x007CE478, 4, {}}, {0x007CCDFC, 22, {}}};
    TestStandIns::SaveAll(g_saved);
    g_swapped = true;
    g_world = static_cast<uint8_t*>(VirtualAlloc(nullptr, kWorldSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* s = new StandIns;
    StandInWorld(*s);
    GameNet::Thugpro::SetPeerModeOverride(0);
    g_cases = 0;

    // --- ok_to_join 0x0052F560: every refusal and the acceptance, with each check on and off ---
    struct OkCase {
        int banned, observer, numObservers, maxObservers, numPlayers, maxPlayers;
        int version;
        bool remote;
        int rankTest, rankLimit, rank;
        bool willing;
        uint32_t mode;
        bool gameOver;
        const char* password;
        const char* given;
        int cdKeyLength;
        int checks; // bit 0 password, 1 version, 2 CD key
    };
    const OkCase okCases[] = {
        {1, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 7}, // banned
        {0, 1, 2, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 7}, // observers full
        {0, 1, 1, 2, 9, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 7}, // observer: ok
        {0, 0, 5, 2, 8, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 7}, // full
        {0, 0, 0, 2, 9, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 7}, // fuller
        {0, 0, 0, 2, 1, 8, 0x4000A, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 7},          // version
        {0, 0, 0, 2, 1, 8, 0x4000A, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 72, 5},          // not checked
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 2, 4, 4, true, 0x1C471C60, false, "", "", 72, 7}, // rank low
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 2, 3, 4, true, 0x1C471C60, false, "", "", 72, 7}, // above: ok
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 2, 3, -4, true, 0x1C471C60, false, "", "", 72, 7}, // signed rank
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 1, 4, 4, true, 0x1C471C60, false, "", "", 72, 7}, // rank high
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 1, 5, 4, true, 0x1C471C60, false, "", "", 72, 7}, // below: ok
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, false, 2, 9, 4, true, 0x1C471C60, false, "", "", 72, 7}, // local: no rank
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 5, 9, 4, true, 0x1C471C60, false, "", "", 72, 7},  // other test
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, false, 0x12345678, false, "", "", 72, 7}, // in progress
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, false, 0x12345678, true, "", "", 72, 7},  // game over
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, false, 0x1C471C60, false, "", "", 72, 7}, // lobby
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x12345678, false, "", "", 72, 7},  // willing
        {0, 1, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, false, 0x12345678, false, "", "", 72, 7}, // observing
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "pw", "px", 72, 7}, // password
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "pw", "pw", 72, 7}, // right one
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "pw", "px", 72, 6}, // unchecked
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "px", 72, 7},  // no password
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 71, 7},   // CD key
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 73, 7},   // one too long
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 0, 7},    // none
        {0, 0, 0, 2, 1, 8, GameNet::kVersion, true, 0, 3, 4, true, 0x1C471C60, false, "", "", 71, 3},   // unchecked
    };
    for (const OkCase& c : okCases) {
        Compare("GameNet ok_to_join 0x0052F560", [&](bool original) {
            g_script.banned = c.banned;
            g_script.numObservers = c.numObservers;
            g_script.maxObservers = c.maxObservers;
            g_script.numPlayers = c.numPlayers;
            g_script.maxPlayers = c.maxPlayers;
            g_script.rankTest = c.rankTest;
            g_script.rankLimit = c.rankLimit;
            g_script.gameMode = c.mode;
            g_script.password = c.password;
            M()->m_gameOver = c.gameOver;
            auto* info = Request();
            info->version = c.version;
            info->observer = c.observer != 0;
            info->willingToWait = c.willing;
            std::strcpy(info->password, c.given);
            info->rank = static_cast<int8_t>(c.rank);
            std::memset(info->cdKeyResponse, 'K', c.cdKeyLength);
            SetConnFlags(kConn0, c.remote ? Net::Conn::kFlagRemote : Net::Conn::kFlagLocal);
            int reason = -1;
            bool ok = original ? Original<bool(__thiscall*)(Manager*, int*, GameNet::ConnectInfo*, Net::Conn*, bool, bool,
                                                            bool)>(0x0052F560)(M(), &reason, info, As<Net::Conn>(kConn0),
                                                                               (c.checks & 1) != 0, (c.checks & 2) != 0,
                                                                               (c.checks & 4) != 0)
                               : M()->OkToJoin(reason, info, As<Net::Conn>(kConn0), (c.checks & 1) != 0,
                                               (c.checks & 2) != 0, (c.checks & 4) != 0);
            Record({0x0C70, ok, static_cast<uint32_t>(reason)});
        });
    }

    // --- SpawnClient 0x00537220 / SpawnMatchClient 0x00534430 / SpawnServer 0x0053C380 ---
    for (int flags = 0; flags < 16; ++flags) {
        for (int index : {0, 1}) {
            Compare("GameNet SpawnClient 0x00537220", [&](bool original) {
                g_script.createFails = (flags & 8) != 0;
                bool b = (flags & 1) != 0, l = (flags & 2) != 0, sec = (flags & 4) != 0;
                Net::App* app = original ? Original<Net::App*(__thiscall*)(Manager*, bool, bool, bool, int)>(0x00537220)(
                                               M(), b, l, sec, index)
                                         : M()->SpawnClient(b, l, sec, index);
                Record({0x5C11, Rel(app)});
            });
        }
    }
    g_script.createFails = false;
    Compare("GameNet SpawnMatchClient 0x00534430", [&](bool original) {
        Net::App* app = original ? Original<Net::App*(__thiscall*)(Manager*)>(0x00534430)(M()) : M()->SpawnMatchClient();
        Record({0x5A7C, Rel(app)});
    });
    for (int flags = 0; flags < 8; ++flags) {
        Compare("GameNet SpawnServer 0x0053C380", [&](bool original) {
            g_script.createFails = (flags & 4) != 0;
            M()->m_hostMode = (flags & 2) ? 0 : 3;
            bool l = (flags & 1) != 0, sec = (flags & 2) != 0;
            Net::App* app = original ? Original<Net::App*(__thiscall*)(Manager*, bool, bool)>(0x0053C380)(M(), l, sec)
                                     : M()->SpawnServer(l, sec);
            Record({0x5E2F, Rel(app)});
        });
    }
    g_script.createFails = false;

    // --- JoinServer 0x00534480: local / remote client, index 0 / 1, a password typed or not, observing, modem ---
    for (int flags = 0; flags < 32; ++flags) {
        Compare("GameNet JoinServer 0x00534480", [&](bool original) {
            bool local = (flags & 1) != 0;
            int index = (flags & 2) ? 1 : 0;
            SetFlag(index ? kClient1 : kClient0, local ? Net::App::kLocal : 0);
            if (index == 1)
                SetFlag(kClient0, (flags & 16) ? Net::App::kLocal : 0); // (client 0's handlers depend on its own)
            if (flags & 4)
                std::strcpy(&GameVariable<char>(0x007CCDFC), "secret");
            M()->m_joinMode = (flags & 8) ? 1 : 0;
            *reinterpret_cast<int*>(At(kNetManager) + 0x128) = (flags & 16) ? 1 : 0; // a modem
            g_script.rankDefined = !local; // (the local path leaves the rank as its stack had it)
            // (a port other than the world's own 5153, so the join's port is seen written)
            uint16_t port = static_cast<uint16_t>(6000 + flags);
            if (original)
                Original<void(__thiscall*)(Manager*, uint32_t, uint16_t, int)>(0x00534480)(M(), 0x11223344, port, index);
            else
                M()->JoinServer(0x11223344, port, index);
        });
    }
    g_script.rankDefined = true;

    // --- ReattemptJoin 0x00534AF0 / with password 0x00534BD0 ---
    for (int flags = 0; flags < 8; ++flags) {
        Compare("GameNet ReattemptJoin 0x00534AF0", [&](bool original) {
            M()->m_flags = (flags & 1) ? 4 : 0; // internet: the rank
            M()->m_joinMode = (flags & 2) ? 1 : 0;
            *reinterpret_cast<int*>(At(kNetManager) + 0x128) = (flags & 4) ? 1 : 0;
            g_script.rankDefined = (flags & 1) != 0;
            if (original)
                Original<void(__thiscall*)(Manager*, Net::App*)>(0x00534AF0)(M(), As<Net::App>(kClient1));
            else
                M()->ReattemptJoin(As<Net::App>(kClient1));
        });
        Compare("GameNet ReattemptJoinWithPassword 0x00534BD0", [&](bool original) {
            M()->m_joinMode = (flags & 2) ? 1 : 0;
            *reinterpret_cast<int*>(At(kNetManager) + 0x128) = (flags & 4) ? 1 : 0;
            g_script.rankDefined = true;
            const char* password = (flags & 1) ? "letmein" : "";
            if (original)
                Original<void(__thiscall*)(Manager*, const char*)>(0x00534BD0)(M(), password);
            else
                M()->ReattemptJoinWithPassword(password);
        });
    }

    // --- The connection request handler 0x0052F790 (host) ---
    for (int flags = 0; flags < 64; ++flags) {
        Compare("GameNet HandleConnection 0x0052F790", [&](bool original) {
            bool remote = (flags & 1) != 0;
            SetConnFlags(kConn0, remote ? Net::Conn::kFlagRemote : Net::Conn::kFlagLocal);
            SetFlag(kServerApp, (flags & 2) ? 0x10u : 0);
            uint32_t packet = (flags & 4) ? 1 : 0;
            M()->m_flags = (flags & 8) ? 4 : 0; // internet: the CD key checked by GameSpy
            g_script.banned = (flags & 16) ? 1 : 0;
            g_script.proceedIps = false;
            g_script.proceedAll = remote;
            auto* info = Request();
            info->version = GameNet::kVersion;
            info->willingToWait = true;
            info->broadband = (flags & 32) != 0;
            std::memset(info->cdKeyResponse, 'K', 72);
            auto* context = Context(3, 92, kConn0, kServerApp, packet);
            Handle(original, 0x0052F790, &Manager::HandleConnection, context);
        });
    }
    // A refusal with the CD-key challenge (the response too short).
    Compare("GameNet HandleConnection 0x0052F790 (CD key)", [&](bool original) {
        SetConnFlags(kConn0, Net::Conn::kFlagRemote);
        SetFlag(kServerApp, 0x10u);
        g_script.banned = 0; // (the loop above ends banned)
        auto* info = Request();
        info->version = GameNet::kVersion;
        info->willingToWait = true;
        std::memset(info->cdKeyResponse, 'K', 70);
        Handle(original, 0x0052F790, &Manager::HandleConnection, Context(3, 92, kConn0, kServerApp, 1));
    });
    g_script.proceedAll = true;

    // --- GameSpy's answer 0x00536530 ---
    for (int flags = 0; flags < 8; ++flags) {
        Compare("GameNet ClientAuthorized 0x00536530", [&](bool original) {
            M()->m_flags = (flags & 2) ? 4 : 0;
            g_script.proceedIps = (flags & 2) != 0;
            g_script.challengeDefined = false; // (the original's refusal has only its reason set)
            auto* instance = reinterpret_cast<uint32_t*>(At(kPlayerHeap) + 0x3000);
            instance[0] = reinterpret_cast<uint32_t>(At(kServerApp));
            instance[1] = reinterpret_cast<uint32_t>(M());
            instance[2] = reinterpret_cast<uint32_t>(At(kConn2));
            void* data = (flags & 4) ? nullptr : instance;
            int authenticated = flags & 1;
            char error[] = "error";
            if (original)
                Original<void(__cdecl*)(int, int, int, char*, void*)>(0x00536530)(1003, 5, authenticated, error, data);
            else
                Manager::ClientAuthorized(1003, 5, authenticated, error, data);
        });
    }
    g_script.proceedIps = false;
    g_script.challengeDefined = true;

    // --- The join request handler 0x0052FA10 (host) ---
    for (int flags = 0; flags < 32; ++flags) {
        Compare("GameNet HandleJoinRequest 0x0052FA10", [&](bool original) {
            bool remote = (flags & 1) != 0;
            SetConnFlags(kConn0, remote ? Net::Conn::kFlagRemote : Net::Conn::kFlagLocal);
            g_script.banned = (flags & 2) ? 1 : 0;
            M()->m_flags = (flags & 4) ? 4 : 0;
            g_script.gameMode = (flags & 8) ? 0x12345678 : 0x1C471C60;
            M()->m_gameOver = false; // (a game in progress: the joiner waits as a pending observer)
            auto* join = reinterpret_cast<GameNet::JoinInfo*>(At(kMessage));
            join->version = GameNet::kVersion;
            join->observer = (flags & 16) != 0;
            join->willingToWait = true;
            std::strcpy(join->name, "A long name here!");
            join->profile = 0x1234;
            join->rating = 55;
            join->vehicleControlType = 3;
            for (int i = 0; i < 200; ++i)
                join->appearance[i] = static_cast<uint8_t>(i ^ 0x5A);
            Handle(original, 0x0052FA10, &Manager::HandleJoinRequest,
                   Context(80, GameNet::kJoinInfoHeaderSize + 200, kConn0, kServerApp, 0));
        });
    }

    // --- Join proceed 0x0052E490 (client) ---
    for (int flags = 0; flags < 64; ++flags) {
        Compare("GameNet HandleJoinProceed 0x0052E490", [&](bool original) {
            bool remote = (flags & 1) != 0;
            SetConnFlags(kConn0, remote ? Net::Conn::kFlagRemote : Net::Conn::kFlagLocal);
            *(At(kBuddy) + 0x10) = (flags & 2) ? 1 : 0; // presence logged in
            M()->m_joinMode = (flags & 4) ? 1 : 0;
            *reinterpret_cast<int*>(At(kNetManager) + 0x128) = (flags & 8) ? 1 : 0;
            g_script.firstConn = (flags & 16) ? kConn1 : kConn2; // a narrowband server
            std::strcpy(&GameVariable<char>(0x007CCE08), (flags & 32) ? "pw" : "");
            g_script.rankDefined = false;
            g_script.cdKeyDefined = false;
            auto* proceed = reinterpret_cast<GameNet::ProceedInfo*>(At(kMessage));
            proceed->publicIp = 0x01020304;
            proceed->privateIp = 0x05060708;
            proceed->port = 5155;
            std::strcpy(proceed->serverName, "Host 15 chars!!");
            proceed->maxPlayers = (flags & 32) ? -3 : 6;
            proceed->broadband = (flags & 8) != 0;
            Handle(original, 0x0052E490, &Manager::HandleJoinProceed, Context(79, 32, kConn0, kClient0, 0));
        });
    }
    g_script.rankDefined = true;

    // --- Join refused 0x0052E1E0 (client): every reason, the password and CD-key states ---
    for (int reason : {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 30, 38, 127}) {
        for (int state : {0, 4, 5}) {
            Compare("GameNet HandleJoinRefused 0x0052E1E0", [&](bool original) {
                M()->m_joinState = state;
                M()->m_joinMode = state == 4 ? 1 : 0;
                std::strcpy(&GameVariable<char>(0x007CCE08), "pw");
                g_script.cdKeyDefined = true;
                uint8_t* msg = At(kMessage);
                msg[0] = static_cast<uint8_t>(reason);
                std::strcpy(reinterpret_cast<char*>(msg + 1), "CHALLENG");
                Handle(original, 0x0052E1E0, &Manager::HandleJoinRefused, Context(71, 10, kConn0, kClient0, 0));
            });
        }
    }
    g_script.cdKeyDefined = false;

    Log("game net join: %d cases compared", g_cases);
    delete s;
    CleanUp();
}

// The THUGPRO peer mode's changes to the join (ours alone): the connection request with THUGPRO's trailer from all four
// senders, the join request's machine id, the host without GameSpy's CD-key check, THUGPRO's refusal reasons, and the
// bounds on what the host reads (in and out of the mode).
namespace TP = GameNet::Thugpro;

const Sent* LastSent(uint8_t id)
{
    for (auto it = g_sent.rbegin(); it != g_sent.rend(); ++it)
        if (it->id == id)
            return &*it;
    return nullptr;
}
bool CountOf(uint32_t tag)
{
    for (uint32_t word : g_trace)
        if (word == tag)
            return true;
    return false;
}
// The request THUGPRO's release host gets from us (`info` THUG2's part as sent): THUG2's 92 bytes, then its trailer.
bool IsThugproRequest(const Sent* sent, int* reason)
{
    *reason = -1;
    if (!sent || sent->id != 3 || sent->length != 124 || sent->bytes.size() != 124 || sent->compression != 3)
        return false;
    const uint8_t* b = sent->bytes.data();
    auto dword = [&](int at) {
        uint32_t v;
        std::memcpy(&v, b + at, 4);
        return v;
    };
    // What its host checks (thugpro.dll's connection request handler): THUG2's version, the text, the version
    // byte-swapped at least 0x700, then (unless its developers' release type 2) the "modified" flag.
    if (dword(0) != 0x0004000B || std::memcmp(b + 92, "_THUG_PRO_", 10) != 0) {
        *reason = 6;
        return false;
    }
    if (_byteswap_ulong(dword(104)) < 0x700) {
        *reason = 31;
        return false;
    }
    if (dword(108) != 2 && b[120]) {
        *reason = 32;
        return false;
    }
    return b[102] == 0 && b[103] == 0 && dword(112) == TP::ScriptsHash() && dword(116) == TP::MachineId() &&
           b[121] == 0 && b[122] == 0 && b[123] == 0;
}

// The THUGPRO texts' symbols, stood in.
::Script::CSymbolTableEntry g_symbol;
std::vector<uint32_t> g_symbols;

void TestGameNetJoinThugpro()
{
    if (GameVariable<void*>(0x007D1800)) {
        Log("self-test: TestGameNetJoinThugpro skipped (the memory manager is already set up)");
        return;
    }
    g_saved = {{0x006EFDD8, 4, {}}, {0x00696D68, 4, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
               {0x007CE470, 4, {}}, {0x007CE478, 4, {}}, {0x007CCDFC, 22, {}}};
    TestStandIns::SaveAll(g_saved);
    g_swapped = true;
    g_world = static_cast<uint8_t*>(VirtualAlloc(nullptr, kWorldSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* s = new StandIns;
    StandInWorld(*s);
    s->At({AddressOf(&::Script::FindSymbol)}, 0, [](Regs& r) {
        for (uint32_t name : g_symbols)
            if (name == r.args[0])
                return reinterpret_cast<uint32_t>(&g_symbol);
        return 0u;
    });
    s->At({AddressOf(&::Script::CreateSymbol)}, 0, [](Regs& r) {
        g_symbols.push_back(r.args[0]);
        std::memset(&g_symbol, 0, sizeof(g_symbol));
        g_symbol.name = r.args[0];
        return reinterpret_cast<uint32_t>(&g_symbol);
    });
    s->At({AddressOf(&::Script::CreateString)}, 0, [](Regs& r) { return r.args[0]; });
    g_script = Scripted();
    TP::SetPeerModeOverride(1);
    auto sendRun = [&](auto body) {
        g_sent.clear();
        g_trace.clear();
        SetUpWorld();
        body();
    };
    int reason = 0;

    // The four senders of the connection request: THUGPRO's 124 bytes (LZHAM asked for), its host lets them in.
    sendRun([&] { M()->JoinServer(0x11223344, 5153, 0); });
    const Sent* sent = LastSent(3);
    Check(IsThugproRequest(sent, &reason), "THUGPRO join: JoinServer sends THUGPRO's connection request, accepted by its host");
    Check(sent && std::memcmp(sent->bytes.data(), "\x0B\x00\x04\x00", 4) == 0 && sent->bytes[92] == '_' &&
              std::memcmp(sent->bytes.data() + 104, "\x00\x00\x07\x00", 4) == 0 &&
              std::memcmp(sent->bytes.data() + 108, "TUG2", 4) == 0,
          "THUGPRO join: the request's bytes (THUG2's version, the text at 92, version 0.7 at 104, our release type)");
    sendRun([&] { M()->ReattemptJoin(As<Net::App>(kClient0)); });
    Check(IsThugproRequest(LastSent(3), &reason), "THUGPRO join: ReattemptJoin sends THUGPRO's connection request");
    sendRun([&] { M()->ReattemptJoinWithPassword("pw"); });
    sent = LastSent(3);
    Check(IsThugproRequest(sent, &reason) && sent && std::strcmp(reinterpret_cast<const char*>(sent->bytes.data() + 6), "pw") == 0,
          "THUGPRO join: ReattemptJoinWithPassword sends it with the password");
    sendRun([&] {
        At(kMessage)[0] = 8;
        std::strcpy(reinterpret_cast<char*>(At(kMessage) + 1), "CHALLENG");
        Manager::HandleJoinRefused(Context(71, 10, kConn0, kClient0, 0));
    });
    sent = LastSent(3);
    Check(IsThugproRequest(sent, &reason) && sent && sent->bytes[0x12] == 'R',
          "THUGPRO join: the CD-key answer sends it with the response");
    // "Modified" when the hash isn't the release's: THUGPRO's host refuses it (32) - honest when the user says so.
    TP::SetScriptsHashOverride(0x12345678);
    sendRun([&] { M()->JoinServer(0x11223344, 5153, 0); });
    Check(!IsThugproRequest(LastSent(3), &reason) && reason == 32,
          "THUGPRO join: another scripts hash goes as 'modified' (THUGPRO's host: refused, 32)");
    TP::SetScriptsHashOverride(0);
    Check(TP::ScriptsHash() == TP::kReleaseScriptsHash, "THUGPRO join: the release's scripts hash by default");
    // Out of the mode: THUG2's 92 bytes.
    TP::SetPeerModeOverride(0);
    sendRun([&] { M()->JoinServer(0x11223344, 5153, 0); });
    sent = LastSent(3);
    Check(sent && sent->length == 92 && sent->compression == 0, "THUGPRO join: THUG2's request outside the mode");
    TP::SetPeerModeOverride(1);

    // The join request: our machine id as the profile, LZSS asked for (CompressMessage makes it LZHAM in the mode).
    sendRun([&] { Manager::HandleJoinProceed(Context(79, 32, kConn0, kClient0, 0)); });
    sent = LastSent(80);
    int32_t profile = 0;
    if (sent && sent->bytes.size() >= 0x60)
        std::memcpy(&profile, sent->bytes.data() + 0x5C, 4);
    Check(sent && profile == static_cast<int32_t>(TP::MachineId()) && sent->compression == 2,
          "THUGPRO join: the join request carries the machine id (THUGPRO's profile field)");

    // As host: no GameSpy CD-key check on the internet - join proceed at once, with the addresses; a made-up key
    // passes ok_to_join.
    sendRun([&] {
        SetConnFlags(kConn0, Net::Conn::kFlagRemote);
        SetFlag(kServerApp, 0x10);
        M()->m_flags = 4;
        auto* info = Request();
        info->version = GameNet::kVersion;
        info->willingToWait = true;
        std::memcpy(info->cdKeyResponse, "made-up", 8);
        Manager::HandleConnection(Context(3, 124, kConn0, kServerApp, 1));
    });
    sent = LastSent(79);
    uint32_t publicIp = 0;
    if (sent && sent->bytes.size() == 32)
        std::memcpy(&publicIp, sent->bytes.data(), 4);
    Check(sent && publicIp == 0x0100007F && !CountOf(0x6CDA) && !LastSent(71),
          "THUGPRO join: as host, a THUGPRO client proceeds at once (no GameSpy CD-key check, its key not checked)");
    sendRun([&] {
        SetConnFlags(kConn0, Net::Conn::kFlagLocal);
        M()->m_flags = 4;
        Manager::HandleJoinRequest(Context(80, GameNet::kJoinInfoHeaderSize, kConn0, kServerApp, 0));
    });
    Check(!CountOf(0xCDC0) && !CountOf(0x6CDA), "THUGPRO join: the host's own CD key isn't checked either");

    // THUGPRO's refusal reasons: its texts' names, in our words; then as THUG2's other refusals.
    for (int r = 31; r <= 37; ++r) {
        sendRun([&] {
            At(kMessage)[0] = static_cast<uint8_t>(r);
            int result = Manager::HandleJoinRefused(Context(71, 10, kConn0, kClient0, 0));
            Record({0x2E5, static_cast<uint32_t>(result)});
        });
        static const char* const names[] = {"tp_net_reason_version_low", "tp_net_reason_modifications",
                                            "tp_net_reason_release_type", "tp_net_reason_player_name_dupe",
                                            "tp_net_reason_player_name_tooshort", "tp_net_reason_banned",
                                            "tp_net_reason_test"};
        uint32_t name = Crc::GenerateCRCFromString(names[r - 31]);
        char what[96];
        std::snprintf(what, sizeof(what), "THUGPRO join: refusal %d shown as %s", r, names[r - 31]);
        Check(M()->m_refusedReason == name && M()->m_joinState == 9 && CountOf(0xCA2C) && g_trace.back() == 3 &&
                  g_symbol.name == name && g_symbol.type == ::Script::ESYMBOLTYPE_STRING &&
                  g_symbol.raw == reinterpret_cast<uint32_t>(TP::RefusedReasonWords(r)),
              what);
    }
    Check(!TP::RefusedReasonWords(30) && !TP::RefusedReasonWords(38) && TP::RefusedReasonText(30) == 0,
          "THUGPRO join: only 31-37 are THUGPRO's");

    // Bounds (in and out of the mode): a connection request shorter than THUG2's, a join request shorter than its
    // header - not read; a short appearance read with zeros after it; a cut CD-key challenge.
    for (int mode : {0, 1}) {
        TP::SetPeerModeOverride(mode);
        sendRun([&] {
            SetConnFlags(kConn0, Net::Conn::kFlagRemote);
            SetFlag(kServerApp, 0x10);
            int result = Manager::HandleConnection(Context(3, 91, kConn0, kServerApp, 1));
            Record({0x2E5, static_cast<uint32_t>(result)});
        });
        Check(g_sent.empty() && g_trace.back() == 4 && !CountOf(0xBA4), "join bounds: a short connection request isn't read");
        sendRun([&] {
            SetConnFlags(kConn0, Net::Conn::kFlagRemote);
            int result = Manager::HandleJoinRequest(Context(80, GameNet::kJoinInfoHeaderSize - 1, kConn0, kServerApp, 0));
            Record({0x2E5, static_cast<uint32_t>(result)});
        });
        Check(g_sent.empty() && g_trace.back() == 4 && !CountOf(0xDEFE), "join bounds: a short join request isn't read");
        sendRun([&] {
            SetConnFlags(kConn0, Net::Conn::kFlagRemote);
            std::memset(At(kMessage), 0x77, kBlocks[kMessage].size);
            auto* join = reinterpret_cast<GameNet::JoinInfo*>(At(kMessage));
            join->observer = false;
            join->willingToWait = true;
            join->version = GameNet::kVersion;
            join->name[15] = '\0';
            Manager::HandleJoinRequest(Context(80, GameNet::kJoinInfoHeaderSize + 10, kConn0, kServerApp, 0));
        });
        std::vector<uint8_t> expected(GameNet::kAppearanceSize, 0);
        std::memset(expected.data(), 0x77, 10);
        bool read = false;
        for (size_t i = 0; i + 2 < g_trace.size(); ++i)
            if (g_trace[i] == 0x4EAF && g_trace[i + 2] == HashBytes(expected.data(), expected.size()))
                read = true;
        Check(read, "join bounds: a short appearance is read with zeros after its end");
        sendRun([&] {
            At(kMessage)[0] = 8;
            std::strcpy(reinterpret_cast<char*>(At(kMessage) + 1), "CHALLENGE-LONGER");
            Manager::HandleJoinRefused(Context(71, 3, kConn0, kClient0, 0));
        });
        bool cut = false;
        for (size_t i = 0; i + 2 < g_trace.size(); ++i)
            if (g_trace[i] == 0xCD2E && g_trace[i + 2] == Hash("CH"))
                cut = true;
        Check(cut, "join bounds: the CD-key challenge read from the refusal's own bytes");
    }

    delete s;
    CleanUp();
}

SELFTEST(Early, TestGameNetJoin, CleanUp);
SELFTEST(Early, TestGameNetJoinThugpro, CleanUp);

} // namespace JoinTests


