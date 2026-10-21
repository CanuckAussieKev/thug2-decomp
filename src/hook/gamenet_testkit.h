#pragma once

// A kit for the network manager's handler tests (selftests_skater_gamenet*.cpp): an arena holding a stand-in manager,
// players, skaters, the message, an app and a connection; recording stand-ins (standins.h) for what the handlers
// call; and a Handle() that runs the original or ours on a context built over the arena. Both runs start from the same
// arena and the traces plus the whole arena (arena-relative) are compared.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include "game/core/manager.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/net/net_conn.h"
#include "game/net/net_dispatcher.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace GameNetKit {

using namespace TestStandIns;
using MsgCode = int(__cdecl*)(Net::MsgHandlerContext*);

constexpr uint32_t kArenaSize = 0x40000;
inline ArenaPair<kArenaSize> g_storage;
inline uint32_t g_base;
inline uint32_t At(uint32_t offset) { return g_base + offset; }
template <typename T>
T& Word(uint32_t offset)
{
    return *reinterpret_cast<T*>(g_base + offset);
}
inline uint32_t Rel(uint32_t word) { return ArenaWord(g_storage.Get(), kArenaSize, word); }
inline void Rec(uint32_t tag, std::initializer_list<uint32_t> values)
{
    g_trace.push_back(tag);
    for (uint32_t v : values)
        g_trace.push_back(Rel(v));
}
// A string argument: its hash, or the raw value for a small number.
inline uint32_t Str(uint32_t text) { return text < 0x10000 ? text : Hash(reinterpret_cast<const char*>(text)); }

// Arena layout.
constexpr uint32_t kManager = 0x00000;  // GameNet::Manager (0x10000)
constexpr uint32_t kPlayers = 0x10000;  // players, 0x200 apart (8)
constexpr uint32_t kSkaters = 0x11000;  // skater objects, 0x100 apart (8): +0x14 id
constexpr uint32_t kMsg = 0x12000;      // the message (0x800)
constexpr uint32_t kApp = 0x12800;      // the context's app (0x800)
constexpr uint32_t kMisc = 0x13000;     // stand-in objects (0x1000): the skate module, game mode, editors...
constexpr uint32_t kConn = 0x14000;     // the context's connection (0x4000)
constexpr uint32_t kHeap = 0x18000;     // allocations (to the end)

constexpr uint32_t kPlayerSize = 0x200;

inline uint32_t g_heapNext;
inline std::vector<Saved> g_saved;
inline std::map<uint32_t, uint32_t> g_answers; // stand-in answers by tag

inline void SetUpArena(int run)
{
    uint8_t* arena = g_storage.Get()[run];
    g_base = reinterpret_cast<uint32_t>(arena);
    std::memset(arena, 0, kArenaSize);
    for (uint32_t i = 0; i < 0x10000; i += 4)
        Word<uint32_t>(kManager + i) = 0x5A000000u + i; // every manager field told apart
    std::memset(arena + kHeap, 0xCD, kArenaSize - kHeap);
    g_heapNext = kHeap;
    g_trace.clear();
    for (uint32_t i = 0; i < 8; ++i) {
        uint32_t player = kPlayers + kPlayerSize * i;
        uint32_t skater = kSkaters + 0x100 * i;
        Word<uint32_t>(player + 0x14) = At(skater);
        Word<uint32_t>(skater + 0x14) = 0x10 + i; // the skaters' ids
        Word<int>(player + 0x30) = static_cast<int>(i % 3);
        std::memcpy(&Word<char>(player + 0x38), "Player0", 8);
        Word<char>(player + 0x3E) = static_cast<char>('0' + i);
        Word<uint32_t>(player + 0x1D0) = 0x00000100u * i;
    }
    Word<int>(kConn + 0x3C) = 7; // the connection's handle
    GameVariable<uint32_t>(0x007CCE14) = At(kManager);
    GameVariable<uint32_t>(0x007CE478) = At(kMisc + 0x000); // the skate module
    GameVariable<uint32_t>(0x007CE470) = At(kMisc + 0x100); // the game mode
    GameVariable<uint32_t>(0x007CE500) = At(kMisc + 0x200); // the trick object manager
    GameVariable<uint32_t>(0x007CE5D8) = At(kMisc + 0x300); // the park editor
    GameVariable<uint32_t>(0x00696D68) = At(kMisc + 0x400); // the main loop's task stacks
    GameVariable<uint32_t>(0x007D1800) = At(kMisc + 0x800); // the memory manager
    GameVariable<bool>(0x007CC979) = true;                  // (set up)
    std::memset(reinterpret_cast<void*>(0x006F3614), 0, 16); // no structure pools
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16); // no array pools
}

inline void SaveGlobals(std::initializer_list<Saved> extra = {})
{
    g_saved = {{0x007CCE14, 4, {}}, {0x007CE478, 4, {}}, {0x007CE470, 4, {}}, {0x007CE500, 4, {}},
               {0x007CE5D8, 4, {}}, {0x00696D68, 4, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
               {0x006F3614, 16, {}}, {0x006F22B4, 16, {}}};
    g_saved.insert(g_saved.end(), extra);
    SaveAll(g_saved);
}
inline void Cleanup()
{
    RemoveStandIns();
    RestoreAll(g_saved);
    g_saved.clear();
}

// A stand-in that records `tag`, ECX and the stack arguments, and answers `answer` (or g_answers[tag], or 0).
inline void Stub(StandIns& s, std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag,
                 std::function<uint32_t(Regs&)> answer = nullptr, int cdeclArgs = 0)
{
    s.At(addresses, pop, [tag, pop, answer, cdeclArgs](Regs& r) {
        g_trace.push_back(tag);
        g_trace.push_back(Rel(r.ecx));
        int count = pop ? pop / 4 : cdeclArgs;
        for (int i = 0; i < count; ++i)
            g_trace.push_back(Rel(r.args[i]));
        if (answer)
            return answer(r);
        auto it = g_answers.find(tag);
        return it == g_answers.end() ? 0u : it->second;
    });
}
// The same for a cdecl function: no ECX recorded (callers leave anything in it).
inline void StubCdecl(StandIns& s, std::initializer_list<uint32_t> addresses, uint32_t tag, int args,
                      std::function<uint32_t(Regs&)> answer = nullptr)
{
    s.At(addresses, 0, [tag, args, answer](Regs& r) {
        g_trace.push_back(tag);
        for (int i = 0; i < args; ++i)
            g_trace.push_back(Rel(r.args[i]));
        if (answer)
            return answer(r);
        auto it = g_answers.find(tag);
        return it == g_answers.end() ? 0u : it->second;
    });
}

// A message description handed to a send: its fields and data (the data's place - a local - not compared).
inline void RecordDesc(uint32_t tag, uint32_t ecx, const Net::MsgDesc* desc)
{
    uint32_t data = desc->m_data ? HashBytes(static_cast<const uint8_t*>(desc->m_data), desc->m_length) : 0;
    uint32_t where = reinterpret_cast<uint32_t>(desc->m_data);
    Rec(tag, {ecx, desc->m_id, desc->m_stream, desc->m_length, data, static_cast<uint32_t>(desc->m_priority),
              static_cast<uint32_t>(desc->m_queue), desc->m_groupId, desc->m_singular,
              static_cast<uint32_t>(desc->m_delay), desc->m_forcedSequenceId, desc->m_compression,
              Rel(where) >= 0xA0000000u ? where : 0u});
}

// Stand-ins most handlers need: scripts, script structures, memory (allocations from the arena's heap), sprintf.
// (Structures come from the stood-in memory manager: the struct pools are emptied by SetUpArena's caller - see
// SaveGlobals - so the original's inline new and ours both allocate there.)
inline void InstallCommon(StandIns& s)
{
    using Script::CStruct;
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(
                          &Script::RunScript))},
         0, [](Regs& r) {
             Rec(0x5790, {r.args[0], r.args[1], r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                  bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return 0u;
         });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Rec(0x7D40, {r.ecx, r.args[0], r.args[1]});
             return 0u;
         });
    s.At({0x00477B80, AddressOf(static_cast<void (CStruct::*)(uint32_t, int)>(&CStruct::AddInteger))}, 8, [](Regs& r) {
        Rec(0x7B80, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Rec(0x79D0, {r.ecx, r.args[0], Str(r.args[1])});
             return 0u;
         });
    s.At({0x00478390, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t, const char*)>(&CStruct::AddString))},
         12, [](Regs& r) {
             Rec(0x8390, {r.ecx, r.args[0], r.args[1], Str(r.args[2])});
             return 0u;
         });
    s.At({0x004783C0, AddressOf(&CStruct::AddComponentOfType)}, 12, [](Regs& r) {
        Rec(0x83C0, {r.ecx, r.args[0], r.args[1] & 0xFF, r.args[2]});
        return 0u;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x7130, {r.ecx});
        return 0u;
    });
    // The text of a global by name: a string in the arena naming it.
    s.At({0x00478FF0, AddressOf(static_cast<const char* (*)(const char*)>(&Script::GetString))}, 0, [](Regs& r) {
        uint32_t h = Hash(reinterpret_cast<const char*>(r.args[0]));
        Rec(0x8FF0, {h});
        char* text = &Word<char>(kMisc + 0xE00);
        std::snprintf(text, 32, "text-%08X", h);
        return reinterpret_cast<uint32_t>(text);
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.args[0], r.args[1] & 0xFF});
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Rec(0xABA0, {r.args[0]});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xB110, {r.ecx});
        return 0u;
    });
    s.At({0x006234FE}, 0, StandInSprintf);
}

// Run a handler (the original at `address` or ours) on the arena's message, app, connection and manager.
inline int Handle(bool original, uint32_t address, MsgCode ours, uint8_t id, uint32_t length = 0)
{
    Net::MsgHandlerContext context;
    std::memset(&context, 0, sizeof(context));
    context.m_Msg = &Word<char>(kMsg);
    context.m_MsgId = id;
    context.m_MsgLength = length;
    context.m_App = &Word<Net::App>(kApp);
    context.m_Conn = &Word<Net::Conn>(kConn);
    context.m_data = &Word<uint8_t>(kManager);
    int result = original ? Original<MsgCode>(address)(&context) : ours(&context);
    Rec(0xEE00 | id, {static_cast<uint32_t>(result), context.m_MsgLength, context.m_Acknowledge});
    return result;
}
inline void Message(std::initializer_list<uint8_t> bytes, uint32_t at = 0)
{
    uint32_t i = at;
    for (uint8_t b : bytes)
        Word<uint8_t>(kMsg + i++) = b;
}

// The arena, arena-relative, after the trace.
inline std::vector<uint32_t> Finish()
{
    std::vector<uint32_t> trace = g_trace;
    for (uint32_t i = 0; i < kArenaSize; i += 4)
        trace.push_back(ArenaWordOrHigh(g_storage.Get(), kArenaSize, Word<uint32_t>(i)));
    return trace;
}

} // namespace GameNetKit
