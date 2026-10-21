// Self-tests for the network manager's smaller message handlers and helpers (src/game/net/gamenet_handlers.cpp): the
// original and ours handle the same messages on the same stand-in arena (gamenet_testkit.h); what they call outside
// is stood in for by recording code, and the calls, results and the arena are compared. GetCdKey runs against
// stood-in registry functions; the list search destructors against a stood-in Free.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/task.h"
#include "game/net/gamenet_handlers.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_client.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "hook/gamenet_testkit.h"
#include "hook/log.h"

namespace {

using namespace GameNetKit;
using GameNet::Manager;

// Answers the scenarios set.
uint32_t g_conn2Player;   // the connection's player (arena offset; 0 none)
uint32_t g_presentIds;    // object ids (0x10 + i) that have a player: bits
bool g_observing, g_teamGame, g_localSkater;
uint32_t g_gameType;

void Install(StandIns& s)
{
    Stub(s, {0x0053AE10}, 4, 0xAE10, [](Regs&) { return g_conn2Player ? At(g_conn2Player) : 0u; });
    s.At({0x0053AEA0}, 4, [](Regs& r) {
        uint32_t id = r.args[0] & 0xFFFF; // (pushed as 16 bits)
        Rec(0xAEA0, {r.ecx, id});
        uint32_t i = id - 0x10;
        return i < 8 && ((g_presentIds >> i) & 1) ? At(kPlayers + kPlayerSize * i) : 0u;
    });
    for (uint32_t a : {0x005446C0u, 0x00543C70u, 0x00543F00u, 0x00543A90u})
        Stub(s, {a}, 4, a & 0xFFFF);
    Stub(s, {0x00544110}, 0, 0x4110);
    StubCdecl(s, {0x0053B940}, 0xB940, 2);
    Stub(s, {0x0053E650}, 0, 0xE650);
    Stub(s, {0x00456210, AddressOf(&Net::Conn::DestroyImportantMessages)}, 0, 0x6210);
    Stub(s, {0x00455920, AddressOf(&Net::Conn::UpdateCommTime)}, 4, 0x5920);
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, Script::CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x005376F0}, 0x20, [](Regs& r) {
        Rec(0x76F0, {r.ecx, r.args[0] & 0xFF, r.args[1], Str(r.args[2]), Str(r.args[3]), r.args[4], r.args[5],
                     r.args[6] & 0xFF, r.args[7]});
        return 0u;
    });
    s.At({0x0046C510, AddressOf(&Prefs::Preferences::GetPreferenceString)}, 8, [](Regs& r) {
        Rec(0xC510, {r.ecx, r.args[0], r.args[1]});
        return At(kMisc + 0xF00);
    });
    s.At({0x0054CA10}, 12, [](Regs& r) {
        Rec(0xCA10, {r.ecx, r.args[0], Str(r.args[1])});
        std::memset(reinterpret_cast<void*>(r.args[2]), 0x41, 0x20);
        reinterpret_cast<char*>(r.args[2])[0x20] = 0;
        return 0u;
    });
    Stub(s, {0x0054CA30}, 8, 0xCA30);
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        RecordDesc(0x9E50, r.ecx, reinterpret_cast<Net::MsgDesc*>(r.args[0]));
        return 0u;
    });
    s.At({0x00458AB0, AddressOf(&Net::App::StreamMessage)}, 32, [](Regs& r) {
        Rec(0x8AB0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2] & 0xFFFF, r.args[3], Str(r.args[4]),
                     r.args[5] & 0xFF, r.args[6] & 0xFF, r.args[7] & 0xFF});
        return 0u;
    });
    Stub(s, {0x0056EB50}, 8, 0xEB50);
    Stub(s, {0x00537140}, 4, 0x7140);
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90);
    Stub(s, {0x0058D6B0}, 8, 0xD6B0);
    Stub(s, {0x0058D690}, 4, 0xD690, [](Regs&) { return At(kManager); });
    StubCdecl(s, {0x005A0D90}, 0x0D90, 2, [](Regs&) { return g_observing ? 1u : 0u; });
    Stub(s, {0x005535F0}, 0, 0x35F0, [](Regs&) { return g_teamGame ? 1u : 0u; });
    Stub(s, {0x005532A0}, 0, 0x32A0, [](Regs&) { return g_gameType; });
    Stub(s, {0x005535A0}, 4, 0x35A0);
    Stub(s, {0x005889F0}, 0, 0x89F0);
    Stub(s, {0x00537620}, 0, 0x7620);
    Stub(s, {0x00566520}, 0, 0x6520, [](Regs&) { return g_localSkater ? At(kSkaters + 0x300) : 0u; });
    Stub(s, {0x00566370}, 12, 0x6370);
    Stub(s, {0x005695A0}, 0, 0x95A0, [](Regs&) { return 0xC0FFEE11u; });
    StubCdecl(s, {0x004F4970}, 0x4970, 0, [](Regs&) { return At(kMisc + 0x500); });
    Stub(s, {0x004F45B0}, 0, 0x45B0, [](Regs& r) { return r.ecx + 0x34; });
    StubCdecl(s, {0x004E8470}, 0x8470, 0, [](Regs&) { return At(kMisc + 0x600); });
    Stub(s, {0x004E7E50}, 4, 0x7E50, [](Regs&) { return g_answers[0x7E50]; });
}

using Scenario = void (*)(bool original);

void RunCase(const char* what, Scenario scenario)
{
    auto run = [&](bool original) {
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_conn2Player = kPlayers + kPlayerSize * 2;
        g_presentIds = 0xFF;
        g_observing = g_teamGame = g_localSkater = false;
        g_gameType = 0;
        StandIns* s = new StandIns;
        Install(*s);
        scenario(original);
        delete s;
        return Finish();
    };
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    Compare(expected, actual, what);
}

void Small(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    Message({0x78, 0x56, 0x34, 0x12});
    handle(0x0052D7B0, GameNet::HandleAckId, 0x25);
    handle(0x0052D7D0, GameNet::HandleAck, 0x26);
    g_conn2Player = 0;
    handle(0x0052D7D0, GameNet::HandleAck, 0x26);
    handle(0x0052D800, GameNet::HandleObserve, 0x4D);
    Word<uint32_t>(kConn + 0x64) = 0;
    handle(0x0052D830, GameNet::HandleFlushImportant, 0x79);
    Word<uint32_t>(kConn + 0x64) = 0xFFFFFFFF;
    handle(0x0052D830, GameNet::HandleFlushImportant, 0x79);
    // The flag messages: a known player with a negative team, an unknown one, a negative id.
    g_presentIds = 0x0F;
    for (uint8_t id : std::initializer_list<uint8_t>{0x12, 0x15, 0xFF}) {
        Message({id, 0xFE});
        handle(0x0052D850, GameNet::HandleStoleFlag, 0x74);
        Message({id, 0x03});
        handle(0x0052D880, GameNet::HandleTookFlag, 0x71);
        Message({id, 0x81});
        handle(0x0052D8B0, GameNet::HandleCapturedFlag, 0x72);
        handle(0x0052D8E0, GameNet::HandleRetrievedFlag, 0x73);
    }
    handle(0x0052D910, GameNet::HandleAutoServerNotice, 0x5E);
    handle(0x0052D930, GameNet::HandleKillTeamFlags, 0x7A);
    Message({0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC});
    std::memcpy(&Word<char>(kMsg + 0x0C), "first string", 13);
    std::memcpy(&Word<char>(kMsg + 0x1C), "second", 7);
    handle(0x0052D950, GameNet::HandlePanelMessage, 0x3F);
    handle(0x0052DAC0, GameNet::HandleNothing, 0x70);
    for (uint8_t seconds : std::initializer_list<uint8_t>{0x05, 0xFB}) {
        Message({seconds});
        handle(0x0052DCA0, GameNet::HandleKeepAlive, 0x7B);
    }
}

void CdKeyAndJoin(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    Message({1, 2, 3, 4, 5, 6, 7, 8});
    handle(0x0052DAD0, GameNet::HandleCdKeyChallenge, 0x7C);
    // Joined: on a client with both, one or no local clients; on the server.
    Word<uint8_t>(kManager) = 0;
    Word<uint32_t>(kManager + 0x08) = At(kMisc + 0x700);
    Word<uint32_t>(kManager + 0x0C) = At(kMisc + 0x780);
    handle(0x0052DCC0, GameNet::HandleJoinAccepted, 0x53);
    Word<uint32_t>(kManager + 0x08) = 0;
    handle(0x0052DCC0, GameNet::HandleJoinAccepted, 0x53);
    Word<uint8_t>(kManager) = 0x0D;
    handle(0x0052DCC0, GameNet::HandleJoinAccepted, 0x53);
    // The game's state sent: a remote app (its task added), a local one.
    Word<uint32_t>(kManager + 0xD0) = At(kMisc + 0x480);
    Word<Net::App>(kApp).m_flags = 0;
    handle(0x0052E1A0, GameNet::HandleGameStateSent, 0x42);
    Word<Net::App>(kApp).m_flags = Net::App::kLocal;
    handle(0x0052E1A0, GameNet::HandleGameStateSent, 0x42);
    handle(0x0052EB70, GameNet::HandleParkMap, 0x6C);
    // Every player ready: not observing; observing a goal attack; observing something else.
    handle(0x0052ED80, GameNet::HandleAllPlayersReady, 0x5A);
    g_observing = true;
    g_gameType = 0xEC200EAA;
    handle(0x0052ED80, GameNet::HandleAllPlayersReady, 0x5A);
    g_gameType = 0x6C5FF266;
    handle(0x0052ED80, GameNet::HandleAllPlayersReady, 0x5A);
    handle(0x0052EDD0, GameNet::HandleGameEnded, 0x6A);
    // Teams: none / negative, a team game, the first count with and without a local skater.
    for (uint8_t teams : std::initializer_list<uint8_t>{0x00, 0xFE}) {
        Message({teams});
        handle(0x0052F2D0, GameNet::HandleChooseTeam, 0x69);
    }
    Message({3});
    g_teamGame = true;
    handle(0x0052F2D0, GameNet::HandleChooseTeam, 0x69);
    g_teamGame = false;
    handle(0x0052F2D0, GameNet::HandleChooseTeam, 0x69);
    g_localSkater = true;
    Message({4});
    handle(0x0052F2D0, GameNet::HandleChooseTeam, 0x69);
    Message({9, 8, 7, 6, 5, 4, 3, 2, 1});
    handle(0x0052F370, GameNet::HandleChecksumRequest, 0x80);
}

void ServerSmall(bool original)
{
    auto handle = [&](uint32_t address, MsgCode ours, uint8_t id) { Handle(original, address, ours, id); };
    // Team objects: not a team game; an unknown sender; filling a team's five, a repeat, a sixth.
    std::memset(&Word<uint8_t>(kManager + 0x70), 0, 0x50);
    Message({0x01, 0x00, 0x00, 0xA0});
    handle(0x0052EBB0, GameNet::HandleTeamObject, 0x90);
    g_teamGame = true;
    g_conn2Player = 0;
    handle(0x0052EBB0, GameNet::HandleTeamObject, 0x90);
    g_conn2Player = kPlayers + kPlayerSize * 2; // team 2
    for (uint8_t v : std::initializer_list<uint8_t>{1, 2, 2, 3, 4, 5, 6, 1}) {
        Message({v, 0x00, 0x00, 0xA0});
        handle(0x0052EBB0, GameNet::HandleTeamObject, 0x90);
    }
    g_conn2Player = kPlayers + kPlayerSize * 1; // team 1
    handle(0x0052EBB0, GameNet::HandleTeamObject, 0x90);
    handle(0x0052F3D0, GameNet::HandleCdKeyAnswer, 0x7D);
    // The created data: rails, goals (none / some), another id.
    handle(0x0052F400, GameNet::HandleCreatedDataRequest, 0x8B);
    Word<uint32_t>(kManager + 0x1948) = 0x12345;
    handle(0x0052F400, GameNet::HandleCreatedDataRequest, 0x8A);
    g_answers[0x7E50] = 3;
    handle(0x0052F400, GameNet::HandleCreatedDataRequest, 0x8A);
    handle(0x0052F400, GameNet::HandleCreatedDataRequest, 0x89);
}

// Object updates: the message's length from its bits.
void ObjectUpdates(bool original)
{
    // Bits, least significant first: 8 present bits, then per skater 16 time, 9 flags, the fields.
    struct Writer {
        uint8_t* out;
        uint32_t bit = 0;
        void Put(uint32_t value, int count)
        {
            for (int i = 0; i < count; ++i, ++bit) {
                if ((value >> i) & 1)
                    out[bit / 8] |= static_cast<uint8_t>(1 << (bit % 8));
            }
        }
    };
    const uint32_t flagSets[][3] = {{0x0A5, 0x1FF, 0x040}, {0x100, 0x080, 0x000}, {0x03F, 0x0C1, 0x13E}};
    const uint32_t presents[] = {0x00, 0x81, 0x25, 0xFF, 0x5A};
    for (uint32_t present : presents) {
        for (const auto& flags : flagSets) {
            std::memset(&Word<uint8_t>(kMsg), 0, 0x400);
            Writer w{&Word<uint8_t>(kMsg)};
            w.Put(present, 8);
            int n = 0;
            for (int skater = 0; skater < 8; ++skater) {
                if (!((present >> skater) & 1))
                    continue;
                uint32_t f = flags[n++ % 3];
                w.Put(0x1234 + skater, 16);
                w.Put(f, 9);
                for (int field = 0; field < 6; ++field) {
                    if (f & (1u << field))
                        w.Put(0x8000 | field, 16);
                }
                if (f & 0x40)
                    w.Put(0xFFF, 12);
                if (f & 0x80)
                    w.Put(0xA5, 8);
                if (f & 0x100)
                    w.Put(0xDEADBEEF, 32);
            }
            Handle(original, 0x0052D980, GameNet::HandleObjectUpdateLength, 0x23, 0x999);
        }
    }
}

// GetCdKey against stood-in registry functions: each path through the fallback.
struct RegScript {
    LONG createUser, queryUser, createMachine, queryMachine;
};
RegScript g_reg;
int g_regQueries;
void CdKey(bool original)
{
    StandIns* s = StandIns::s_current;
    StandInApi(*s, 0x00645000, reinterpret_cast<const void*>(&RegCreateKeyA), 12, [](Regs& r) {
        bool user = r.args[0] == reinterpret_cast<uint32_t>(HKEY_CURRENT_USER);
        Rec(0x4E61, {r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        *reinterpret_cast<uint32_t*>(r.args[2]) = user ? 0x1111u : 0x2222u;
        return static_cast<uint32_t>(user ? g_reg.createUser : g_reg.createMachine);
    });
    StandInApi(*s, 0x00645008, reinterpret_cast<const void*>(&RegQueryValueExA), 24, [](Regs& r) {
        auto* size = reinterpret_cast<DWORD*>(r.args[5]);
        Rec(0x4E62, {r.args[0], Hash(reinterpret_cast<const char*>(r.args[1])), r.args[2], *size});
        bool user = r.args[0] == 0x1111u;
        LONG result = user ? g_reg.queryUser : g_reg.queryMachine;
        if (result == ERROR_SUCCESS) {
            const char* key = user ? "USERKEY-1234" : "MACHINEKEY-99";
            std::memcpy(reinterpret_cast<char*>(r.args[4]), key, std::strlen(key) + 1);
            *size = static_cast<DWORD>(std::strlen(key) + 1);
        } else {
            *size = 77; // (ERROR_MORE_DATA style: the size it wanted)
        }
        return static_cast<uint32_t>(result);
    });
    StandInApi(*s, 0x00645014, reinterpret_cast<const void*>(&RegCloseKey), 4, [](Regs& r) {
        Rec(0x4E63, {r.args[0]});
        return 0u;
    });
    const RegScript scripts[] = {{0, 0, 0, 0}, {0, 2, 0, 0}, {0, 2, 0, 2}, {0, 2, 5, 0}, {5, 0, 0, 0},
                                 {5, 0, 0, 2}, {5, 0, 5, 0}};
    for (const RegScript& script : scripts) {
        g_reg = script;
        char* out = &Word<char>(kMsg + 0x100);
        std::memset(out, 0x7E, 0x80);
        if (original)
            Original<void(__cdecl*)(char*)>(0x0052D6A0)(out);
        else
            GameNet::GetCdKey(out);
        Rec(0x4E64, {Hash(out)});
    }
}

// The search destructors: the vtable reset; the deleting ones free with flag 1.
void Searches(bool original)
{
    StandIns* s = StandIns::s_current;
    s->At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.ecx, r.args[0]});
        return 0u;
    });
    using Body = void(__thiscall*)(void*);
    using Deleting = void*(__thiscall*)(void*, unsigned int);
    for (int which = 0; which < 2; ++which) {
        void* search = &Word<uint32_t>(kMisc + 0xC00 + 0x10 * which);
        Word<uint32_t>(kMisc + 0xC00 + 0x10 * which) = 0x12345678;
        if (original)
            Original<Body>(which ? 0x0052DB70 : 0x0052DB60)(search);
        else if (which)
            static_cast<GameNet::NewPlayerSearchDestructors*>(search)->DestroyBody();
        else
            static_cast<GameNet::PlayerSearchDestructors*>(search)->DestroyBody();
        Rec(0x5EA0, {Word<uint32_t>(kMisc + 0xC00 + 0x10 * which)});
        for (unsigned int flags : {0u, 1u, 2u, 3u}) {
            Word<uint32_t>(kMisc + 0xC00 + 0x10 * which) = 0x12345678;
            void* result;
            if (original)
                result = Original<Deleting>(which ? 0x0052F760 : 0x0052F730)(search, flags);
            else if (which)
                result = static_cast<GameNet::NewPlayerSearchDestructors*>(search)->Destroy(flags);
            else
                result = static_cast<GameNet::PlayerSearchDestructors*>(search)->Destroy(flags);
            Rec(0x5EA1, {reinterpret_cast<uint32_t>(result), Word<uint32_t>(kMisc + 0xC00 + 0x10 * which)});
        }
    }
}

void TestGameNetHandlersSmall()
{
    SaveGlobals();
    RunCase("GameNet handlers: acknowledgements, observing, flags, scripts, panel messages, keep-alive", Small);
    RunCase("GameNet handlers: CD key challenge, join, game state, park map, ready, game end, teams, checksum",
            CdKeyAndJoin);
    RunCase("GameNet handlers (server): team objects, CD key answer, created rails / goals", ServerSmall);
    RunCase("GameNet::HandleObjectUpdateLength", ObjectUpdates);
    RunCase("GameNet::GetCdKey", CdKey);
    RunCase("GameNet: the player search destructors", Searches);
    Cleanup();
}

} // namespace

SELFTEST(Early, TestGameNetHandlersSmall, GameNetKit::Cleanup);
