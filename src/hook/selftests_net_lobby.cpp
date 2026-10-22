// Self-tests for the GameSpy lobby's connection, hosting and NAT negotiation (src/game/net/gamenet_lobby_peer.cpp)
// and its script commands (gamenet_lobby_cfuncs.cpp): the original and ours on the same stand-in arena
// (net2_testkit.h); the peer / NAT negotiation SDKs, the game's C runtime malloc / free and inet_addr stood in for by
// recording code; the calls, the packet queue and the arenas compared.

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/net/gamenet_lobby.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "hook/log.h"
#include "hook/net2_testkit.h"

#pragma warning(disable : 4996) // (strcpy into the stand-in texts)

namespace {

using namespace Net2Kit;
using GameNet::LobbyMan;
using GameNet::Manager;

bool g_inNetGame;
bool g_connected;     // the peer SDK's 0x00603360
int g_reportResult;   // 0x005FCC70's
int g_natError;       // 0x005FC090's
uint32_t g_clock;

constexpr uint32_t kLobby = kFree + 0x1000;
constexpr uint32_t kBuddy = kFree + 0x1200;
constexpr uint32_t kConns = kFree + 0x400;
constexpr uint32_t kTexts = kFree + 0x1800;
constexpr uint32_t kNet = kFree + 0x1A00;   // Net::Manager (its +0x144 the local address)
constexpr uint32_t kTask = kFree + 0x1300;  // a task (+0x1C the lobby)
constexpr uint32_t kServerApp = 0x30000; // (Net::App: its socket at +0x1428)
constexpr uint32_t kClientApp = 0x32000;
constexpr uint32_t kQueueAddress = 0x007CCE50;
constexpr uint32_t kQueueSize = 199 * 0x1C;

uint32_t ConnAt(int i) { return kConns + 0x80 * static_cast<uint32_t>(i); }

// The queue (a global): each entry's type, length, data (text), sender.
void RecordQueue()
{
    int count = GameVariable<int>(0x007CE438);
    Rec(0xE438, {static_cast<uint32_t>(count)});
    for (int i = 0; i < count && i < 199; ++i) {
        const auto& packet = GameVariable<GameNet::QueuedPacket>(kQueueAddress + 0x1C * static_cast<uint32_t>(i));
        Rec(0xE439, {static_cast<uint32_t>(packet.type), static_cast<uint32_t>(packet.length),
                     reinterpret_cast<uint32_t>(packet.data), HashBytes(reinterpret_cast<const uint8_t*>(packet.data),
                                                                        static_cast<size_t>(packet.length) + 1),
                     HashBytes(packet.from, 16)});
    }
}

void Install(StandIns& s)
{
    AddAlias(&LobbyMan::QueuePacket, 0x0053F6C0);
    AddAlias(&LobbyMan::NatNegotiationCompleted, 0x00540300);
    AddAlias(&LobbyMan::NickErrorCallback, 0x0053FEE0);
    AddAlias(&LobbyMan::ConnectCallback, 0x0053FFC0);
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_clock += 7; });
    Stub(s, {0x0044E4F0, AddressOf(&Manager::InNetGame)}, 0, 0xE4F0, [](Regs&) { return g_inNetGame ? 1u : 0u; });
    // The debug output: the message as printed.
    s.At({0x00401C30, AddressOf(&Dbg::Printf)}, 0, [](Regs& r) {
        char text[512];
        std::vsnprintf(text, sizeof(text), reinterpret_cast<const char*>(r.args[0]),
                       reinterpret_cast<va_list>(&r.args[1]));
        Rec(0x1C30, {Hash(text)});
        return 0u;
    });
    // The game's C runtime.
    StubCdecl(s, {0x00624031}, 0x4031, 1, [](Regs& r) {
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    StubCdecl(s, {0x0062411B}, 0x411B, 1);
    s.At({0x005BEAAE}, 4, [](Regs& r) { return static_cast<uint32_t>(inet_addr(reinterpret_cast<const char*>(r.args[0]))); });
    // The SDKs.
    auto sdk = [&s](uint32_t address, int args, std::function<uint32_t(Regs&)> answer = nullptr) {
        s.At({address}, 0, [address, args, answer](Regs& r) {
            g_trace.push_back(address);
            for (int i = 0; i < args; ++i)
                g_trace.push_back(Rel(Aliased(r.args[i])));
            return answer ? answer(r) : 0u;
        });
    };
    sdk(0x005FA010, 1, [](Regs& r) {
        Rec(0xA011, {Str(r.args[1])});
        return 0u;
    });
    sdk(0x005FB350, 2);
    sdk(0x00603360, 1, [](Regs&) { return g_connected ? 1u : 0u; });
    sdk(0x005FD1F0, 1);
    sdk(0x005FC660, 0);
    sdk(0x005FC970, 4, [](Regs& r) {
        Rec(0xC971, {Hash(reinterpret_cast<const char*>(r.args[1])), HashBytes(reinterpret_cast<const uint8_t*>(r.args[3]), 16)});
        return 0u;
    });
    sdk(0x005FC3B0, 3, [](Regs& r) {
        Rec(0xC3B1, {Hash(reinterpret_cast<const char*>(r.args[0])), HashBytes(reinterpret_cast<const uint8_t*>(r.args[2]), 16)});
        return 0u;
    });
    sdk(0x005FCC70, 3, [](Regs&) { return static_cast<uint32_t>(g_reportResult); });
    sdk(0x005FCCF0, 1);
    sdk(0x005FC130, 1);
    sdk(0x005FBCD0, 0);
    sdk(0x005FC090, 6, [](Regs&) { return static_cast<uint32_t>(g_natError); });
    sdk(0x005FC9A0, 4);
    sdk(0x005FD380, 2, [](Regs& r) {
        Rec(0xD381, {r.args[2]});
        return 0u;
    });
    sdk(0x005FC9B0, 2);
    sdk(0x005FC960, 2);
    sdk(0x005FD130, 1);
    sdk(0x005FC950, 1, [](Regs&) { return 0x0102A8C0u; });
    sdk(0x005FC940, 1, [](Regs&) { return 0x0302A8C0u; });
    s.At({0x005FC6F0}, 0, [](Regs& r) {
        Rec(0xC6F0, {Rel(r.args[0]), Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x005FD9F0}, 0, [](Regs& r) { // the nick made valid: "fixed-<nick>"
        Rec(0xD9F0, {Hash(reinterpret_cast<const char*>(r.args[1]))});
        std::snprintf(reinterpret_cast<char*>(r.args[0]), 64, "fixed-%s", reinterpret_cast<const char*>(r.args[1]));
        return 0u;
    });
    s.At({0x005FD580}, 0, [](Regs& r) {
        Rec(0xD580, {Rel(r.args[0]), Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], Aliased(r.args[3]),
                     Aliased(r.args[4]), Rel(r.args[5]), r.args[6]});
        return 0u;
    });
    StubCdecl(s, {0x005A1130}, 0x1130, 2);
    // The manager's and the presence's calls.
    Stub(s, {0x00534480, AddressOf(&Manager::JoinServer)}, 12, 0x4480);
    Stub(s, {0x00537220, AddressOf(&Manager::SpawnClient)}, 16, 0x7220, [](Regs&) { return At(kClientApp); });
    s.At({0x00548320}, 12, [](Regs& r) { // the presence's status (texts by their contents)
        Rec(0x8320, {r.ecx, r.args[0], Str(r.args[1]), Str(r.args[2])});
        return 0u;
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Rec(0x8FD0, {r.args[0]});
        char* text = &Word<char>(kTexts + 0x100 + (r.args[0] & 0x1E0));
        std::snprintf(text, 32, "<%08X>", r.args[0]);
        return reinterpret_cast<uint32_t>(text);
    });
    s.At({0x0046C4A0, AddressOf(&Prefs::Preferences::GetPreference)}, 4, [](Regs& r) {
        Rec(0xC4A0, {r.ecx, r.args[0]});
        return At(kStructs);
    });
    s.At({0x0046C510, AddressOf(&Prefs::Preferences::GetPreferenceString)}, 8, [](Regs& r) {
        Rec(0xC510, {r.ecx, r.args[0], r.args[1]});
        return At(kTexts + 0x40);
    });
}

void Prepare()
{
    g_inNetGame = true;
    g_connected = true;
    g_reportResult = 1;
    g_natError = 0;
    g_clock = 0x10000;
    std::strcpy(&Word<char>(kTexts + 0x40), "Skater1");
    ArenaStruct(kStructs).String(Crc::GenerateCRCFromString("ui_string"), "Host Name");
    Word<uint8_t>(kManager) = 1;
    Word<uint32_t>(kManager + 0x04) = At(kServerApp);
    Word<uint32_t>(kManager + 0x08) = At(kClientApp);
    Word<uint32_t>(kManager + 0x10) = At(kLobby);
    Word<uint32_t>(kManager + 0x18) = At(kBuddy);
    Word<uint32_t>(kServerApp + offsetof(Net::App, m_socket)) = 0x50C0;
    Word<uint32_t>(kClientApp + offsetof(Net::App, m_socket)) = 0x50C1;
    Word<uint8_t>(kBuddy + 0x10) = 1;
    Word<uint32_t>(kBuddy + 4) = 0x01A766CC;
    Word<uint32_t>(kLobby + 0x140) = At(kLobby + 0x180); // the peer
    std::strcpy(&Word<char>(kLobby + 0x34), "Room");
    Word<uint32_t>(kLobby + 0x28) = 0x28;
    Word<uint32_t>(kLobby + 0x148) = At(kMisc + 0x480);
    Word<uint32_t>(kLobby + 0x14C) = At(kMisc + 0x4A0);
    Word<uint32_t>(kLobby + 0x150) = At(kMisc + 0x4C0);
    Word<uint32_t>(kTask + 0x1C) = At(kLobby);
    Word<uint32_t>(kMisc + 0x800 + 0x13C) = 0xBEE0013Cu;
    Word<uint32_t>(kMisc + 0x800 + 0x140) = 0xBEE00140u;
    GameVariable<uint32_t>(0x006EFDD8) = At(kNet);
    for (int i = 0; i < 8; ++i)
        PlayerFlags(i) = 0;
    SetPlayers({0, 1, 2});
    SetNewPlayers({});
    GameVariable<int>(0x007CE438) = 0;
    GameVariable<uint8_t>(0x007CCE48) = 0;
    GameVariable<uint8_t>(0x007CCE3A) = 0;
    GameVariable<uint32_t>(0x007CCE40) = 0;
    GameVariable<uint32_t>(0x007CCE44) = 0;
    GameVariable<uint32_t>(0x007CCE3C) = 0;
    GameVariable<uint32_t>(0x007CE434) = 0x8000;
}

LobbyMan* TheLobby() { return &Word<LobbyMan>(kLobby); }
void* ThePeer() { return &Word<uint8_t>(kLobby + 0x180); }

void Callbacks(bool original)
{
    Prepare();
    using DiscFn = void(__cdecl*)(void*, const char*, void*);
    for (int variant = 0; variant < 3; ++variant) {
        Word<uint8_t>(kLobby + 0x138) = variant == 0;
        Word<uint8_t>(kLobby + 0x136) = variant == 2;
        original ? Original<DiscFn>(0x0053F570)(ThePeer(), "a reason", TheLobby())
                 : LobbyMan::DisconnectedCallback(ThePeer(), "a reason", TheLobby());
        Rec(0xF570, {Word<uint8_t>(kLobby + 0x136), Word<uint8_t>(kLobby + 0x138)});
    }
    using NickFn = void(__cdecl*)(void*, int, const char*, int, const char**, void*);
    for (int type : {0, 1, 2})
        original ? Original<NickFn>(0x0053FEE0)(ThePeer(), type, "Taken", 0, nullptr, TheLobby())
                 : LobbyMan::NickErrorCallback(ThePeer(), type, "Taken", 0, nullptr, TheLobby());
    using ConnFn = void(__cdecl*)(void*, int, int, void*);
    for (int success : {1, 0}) {
        Word<uint8_t>(kLobby + 0x138) = 1;
        Word<uint8_t>(kLobby + 0x136) = 1;
        original ? Original<ConnFn>(0x0053FFC0)(ThePeer(), success, 3, TheLobby())
                 : LobbyMan::ConnectCallback(ThePeer(), success, 3, TheLobby());
        Rec(0xFFC0, {Word<uint8_t>(kLobby + 0x136), Word<uint8_t>(kLobby + 0x138)});
    }
    // The hosted game's address: a client (nothing), no peer, logged out, logged in.
    using HostFn = void(__cdecl*)(void*, uint32_t, uint16_t, void*);
    for (int variant = 0; variant < 4; ++variant) {
        Word<uint8_t>(kManager) = variant == 0 ? 0 : 1;
        Word<uint32_t>(kLobby + 0x140) = variant == 1 ? 0 : At(kLobby + 0x180);
        Word<uint8_t>(kBuddy + 0x10) = variant == 3;
        uint16_t port = static_cast<uint16_t>(0x9000 + variant);
        original ? Original<HostFn>(0x0053FDA0)(ThePeer(), 0x0A00000Au + variant, port, TheLobby())
                 : LobbyMan::PublicAddressCallback(ThePeer(), 0x0A00000Au + variant, port, TheLobby());
    }
}

void PlayerKeys(bool original)
{
    Prepare();
    SetPlayers({0, 1, 2, 3});
    PlayerFlags(1) = 4;     // an observer: not counted
    PlayerFlags(2) = 4 | 8; // an observer counted
    Word<uint32_t>(PlayerAt(0) + 0x2C) = 1500;
    Word<uint32_t>(PlayerAt(2) + 0x2C) = 1502;
    Word<uint32_t>(PlayerAt(3) + 0x2C) = 1503;
    std::strcpy(&Word<char>(kNewPlayers), "New0");
    SetNewPlayers({{1, 0}, {2, 4}, {3, 0xC}, {4, 0x14}});
    std::strcpy(&Word<char>(kNewPlayers + 0x44), "New1");
    std::strcpy(&Word<char>(kNewPlayers + 0x88), "New2");
    std::strcpy(&Word<char>(kNewPlayers + 0xCC), "New3");
    for (int i = 0; i < 4; ++i)
        Word<uint32_t>(kNewPlayers + 0x44 * static_cast<uint32_t>(i) + 0x30) = 1600u + static_cast<uint32_t>(i);
    using Fn = void(__cdecl*)(void*, int, int, void*, void*);
    void* buffer = &Word<uint8_t>(kFree + 0x1F00);
    for (int key : {21, 57, 24, 22})
        for (int index = -1; index <= 7; ++index) {
            GameVariable<uint8_t>(0x007CCE48) = 0;
            original ? Original<Fn>(0x0053FB80)(ThePeer(), key, index, buffer, nullptr)
                     : LobbyMan::PlayerKeyCallback(ThePeer(), key, index, buffer, nullptr);
            Rec(0xFB80, {GameVariable<uint8_t>(0x007CCE48)});
        }
}

void Packets(bool original)
{
    Prepare();
    using Fn = void(__cdecl*)(char*, int, const void*);
    uint8_t from[16] = {2, 0, 0x14, 0x21, 10, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8};
    auto queue = [&](const char* bytes, int length) {
        char data[32] = {};
        std::memcpy(data, bytes, std::strlen(bytes));
        original ? Original<Fn>(0x0053F6C0)(data, length, from) : LobbyMan::QueuePacket(data, length, from);
        from[15]++;
    };
    queue("\xFE\xFD\x01query", 8);
    queue("\xFE\x01\x01", 3); // not one
    queue("\\status\\", 8);
    queue("\xFD\xFC\x1E\x66\x6A\xB2nn", 8);
    queue("\xFD\xFC\x1E\x66\x6A\xB2", 5); // too short (even with the magic's last byte after it)
    queue("\xFD\xFC\x1E\x66\x6A\xB3", 6);
    queue("hello", 5);
    RecordQueue();
    GameVariable<int>(0x007CE438) = 199;
    queue("\\full\\", 6);
    GameVariable<int>(0x007CE438) = 3;
    // The query task hands them on and frees them; the server's reporting watched.
    using TaskFn = void(__cdecl*)(void*);
    GameVariable<GameNet::QueuedPacket>(kQueueAddress + 0x1C * 2).type = 5; // (neither kind: only freed)
    struct Report {
        bool reported, netGame;
        uint8_t server;
        uint32_t since, last;
        bool shown;
    };
    const Report reports[] = {
        {true, true, 1, 0, 0, false},           // reported
        {false, false, 1, 0, 0, false},         // not a network game
        {false, true, 0, 0, 0, false},          // a client
        {false, true, 1, 0x10000, 0, false},    // 90 s not reported: the dialog
        {false, true, 1, 0x10000, 0, true},     // ... once
        {false, true, 1, 0x22007u - 85000u, 0, false}, // 85 s: a report
        {false, true, 1, 0x21000, 0x10000, false}, // a report again (5 s on)
        {false, true, 1, 0x21000, 0x21000, false}, // not yet
    };
    for (const Report& r : reports) {
        g_clock = 0x22000;
        GameVariable<uint8_t>(0x007CCE48) = r.reported;
        g_inNetGame = r.netGame;
        Word<uint8_t>(kManager) = r.server;
        GameVariable<uint32_t>(0x007CCE44) = r.since == 0x10000 ? 0x22007u - 90001u : r.since;
        GameVariable<uint32_t>(0x007CCE40) = r.last;
        GameVariable<uint8_t>(0x007CCE3A) = r.shown;
        original ? Original<TaskFn>(0x005400E0)(&Word<uint8_t>(kTask)) : LobbyMan::QueryTask(&Word<uint8_t>(kTask));
        Rec(0x00E0, {GameVariable<uint8_t>(0x007CCE3A), GameVariable<uint32_t>(0x007CCE40)});
        RecordQueue();
    }
    // The think and NAT negotiation tasks.
    for (int variant = 0; variant < 3; ++variant) {
        g_connected = variant != 1;
        Word<uint8_t>(kLobby + 0x137) = variant == 2;
        original ? Original<TaskFn>(0x00540040)(&Word<uint8_t>(kTask)) : LobbyMan::ThinkTask(&Word<uint8_t>(kTask));
        Rec(0x0040, {Word<uint8_t>(kLobby + 0x137)});
    }
    original ? Original<TaskFn>(0x005400B0)(&Word<uint8_t>(kTask)) : LobbyMan::NatNegotiationTask(&Word<uint8_t>(kTask));
}

void Hosting(bool original)
{
    using Method = void(__thiscall*)(LobbyMan*);
    for (int result : {1, 0}) {
        Prepare();
        g_reportResult = result;
        GameVariable<int>(0x007CE438) = 2;
        GameVariable<GameNet::QueuedPacket>(kQueueAddress).data = &Word<char>(kTexts + 0x200);
        GameVariable<GameNet::QueuedPacket>(kQueueAddress + 0x1C).data = &Word<char>(kTexts + 0x210);
        GameVariable<uint8_t>(0x007CCE3A) = 1;
        GameVariable<uint8_t>(0x007CCE48) = 1;
        original ? Original<Method>(0x00540220)(TheLobby()) : TheLobby()->StartHosting();
        Rec(0x0220, {GameVariable<uint32_t>(0x007CCE44), GameVariable<uint32_t>(0x007CCE3C), GameVariable<uint32_t>(0x007CCE40),
                     GameVariable<uint8_t>(0x007CCE3A), GameVariable<uint8_t>(0x007CCE48)});
        RecordQueue();
    }
    for (int peer = 1; peer >= 0; --peer) {
        Prepare();
        Word<uint32_t>(kLobby + 0x140) = peer ? At(kLobby + 0x180) : 0;
        original ? Original<Method>(0x00540750)(TheLobby()) : TheLobby()->Disconnect();
    }
    Prepare();
    using ThreadFn = unsigned long(__stdcall*)(void*);
    unsigned long code = original ? Original<ThreadFn>(0x00540AB0)(TheLobby()) : LobbyMan::ConnectThread(TheLobby());
    Rec(0x0AB0, {static_cast<uint32_t>(code)});
}

void Negotiation(bool original)
{
    Prepare();
    using CompletedFn = void(__cdecl*)(int, int, const void*, void*);
    uint8_t address[16] = {2, 0, 0x14, 0x22, 192, 168, 2, 9};
    // A client: success (it joins), failure.
    Word<uint8_t>(kManager) = 0;
    for (int result : {0, 2}) {
        original ? Original<CompletedFn>(0x00540300)(result, 0x50C1, address, reinterpret_cast<void*>(0x77))
                 : LobbyMan::NatNegotiationCompleted(result, 0x50C1, address, reinterpret_cast<void*>(0x77));
    }
    // The server counting down.
    Word<uint8_t>(kManager) = 1;
    Word<int>(kLobby + 0x13C) = 2;
    for (int i = 0; i < 2; ++i) {
        original ? Original<CompletedFn>(0x00540300)(0, 0x50C0, address, reinterpret_cast<void*>(0x78))
                 : LobbyMan::NatNegotiationCompleted(0, 0x50C0, address, reinterpret_cast<void*>(0x78));
        Rec(0x0300, {Word<uint32_t>(kLobby + 0x13C)});
    }
    // Asked by a client: one going on (nothing), failing to start, started (twice: the task once).
    using AskFn = void(__cdecl*)(void*, int, void*);
    auto ask = [&](int cookie) {
        original ? Original<AskFn>(0x00540C90)(ThePeer(), cookie, TheLobby())
                 : LobbyMan::NatNegotiateCallback(ThePeer(), cookie, TheLobby());
        Rec(0x0C90, {Word<uint32_t>(kLobby + 0x13C)});
    };
    Word<int>(kLobby + 0x13C) = 1;
    ask(0x101);
    Word<int>(kLobby + 0x13C) = 0;
    g_natError = 3;
    ask(0x102);
    g_natError = 0;
    ask(0x103);
    ask(0x104);
}

using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void RunCFunc(bool original, uint32_t address, CFunc ours, Script::CStruct* params)
{
    bool result = original ? Original<CFunc>(address)(params, TestScript()) : ours(params, TestScript());
    Rec(0xCF00, {address, result ? 1u : 0u});
}

void Commands(bool original)
{
    Prepare();
    ArenaStruct nat(kStructs + 0x100);
    nat.String(0, "10.0.0.7").Integer(0, 5154).Checksum(0x751F4599, 0xC00C1E5u);
    for (int error : {0, 4}) {
        g_natError = error;
        GameVariable<int>(0x007CE438) = 1;
        GameVariable<GameNet::QueuedPacket>(kQueueAddress).data = &Word<char>(kTexts + 0x200);
        RunCFunc(original, 0x00540430, GameNet::CFunc_StartNatNegotiation, nat.Struct());
        RecordQueue();
    }
    RunCFunc(original, 0x005403E0, GameNet::CFunc_CancelNatNegotiation, nullptr);
    ArenaStruct none(kStructs + 0x180);
    ArenaStruct preserve(kStructs + 0x200);
    preserve.Checksum(0xB2AB4DAB, 0);
    ArenaStruct off(kStructs + 0x280);
    off.Add(Script::ESYMBOLTYPE_NAME, 0, 0xD443A2BC);
    for (int variant = 0; variant < 3; ++variant) {
        Word<uint8_t>(kLobby + 0x134) = 1;
        Word<uint8_t>(kBuddy + 0x10) = variant != 2;
        RunCFunc(original, 0x00540590, GameNet::CFunc_LeaveLobby, variant == 1 ? preserve.Struct() : none.Struct());
    }
    Word<uint8_t>(kLobby + 0x138) = 1;
    Word<uint8_t>(kLobby + 0x136) = 0;
    RunCFunc(original, 0x00540640, GameNet::CFunc_CancelLobbyConnect, nullptr);
    RunCFunc(original, 0x00540660, GameNet::CFunc_SetQuietMode, none.Struct());
    RunCFunc(original, 0x00540660, GameNet::CFunc_SetQuietMode, off.Struct());
    // Who may host.
    struct Host {
        bool restricted, loggedIn;
        uint32_t profile;
        int a, b;
    };
    const Host hosts[] = {{false, true, 0, 3, 4}, {false, true, 0, 4, 4}, {false, true, 0, -1, 4},
                          {true, false, 0x01A7665B, 0, 0}, {true, true, 0x01A7665B, 0, 0}, {true, true, 0x01A767B1, 0, 0},
                          {true, true, 0x01A76700, 0, 0}};
    for (const Host& h : hosts) {
        Word<uint8_t>(kLobby + 0x135) = h.restricted;
        Word<uint8_t>(kBuddy + 0x10) = h.loggedIn;
        Word<uint32_t>(kBuddy + 4) = h.profile;
        Word<int>(kLobby + 0x30) = h.a;
        Word<int>(kLobby + 0x2C) = h.b;
        RunCFunc(original, 0x005406C0, GameNet::CFunc_CanHostGame, nullptr);
    }
}

std::vector<uint8_t> g_savedQueue;

void RestoreLobby()
{
    if (!g_savedQueue.empty())
        std::memcpy(reinterpret_cast<void*>(kQueueAddress), g_savedQueue.data(), g_savedQueue.size());
    Cleanup();
}

void TestNetLobby()
{
    SaveGlobals({{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}, {0x006EFDD8, 4, {}},
                 {0x007CE438, 4, {}}, {0x007CCE48, 1, {}}, {0x007CCE3A, 1, {}}, {0x007CCE40, 4, {}},
                 {0x007CCE44, 4, {}}, {0x007CCE3C, 4, {}}, {0x007CE434, 4, {}}});
    g_savedQueue.assign(reinterpret_cast<uint8_t*>(kQueueAddress), reinterpret_cast<uint8_t*>(kQueueAddress) + kQueueSize);
    RunNet2Case("GameNet lobby: disconnected, nick errors, connected, the hosted game's address", Callbacks, Install);
    RunNet2Case("GameNet lobby: the hosted game's player keys", PlayerKeys, Install);
    RunNet2Case("GameNet lobby: packets queued, the query / think / NAT tasks", Packets, Install);
    RunNet2Case("GameNet lobby: hosting started, disconnected, the connect thread", Hosting, Install);
    RunNet2Case("GameNet lobby: NAT negotiation asked and completed", Negotiation, Install);
    RunNet2Case("GameNet lobby: its script commands", Commands, Install);
    RestoreLobby();
    g_savedQueue.clear();
}

} // namespace

SELFTEST(Early, TestNetLobby, RestoreLobby);
