#pragma once

// A kit for the network manager's handler tests (selftests_skater_gamenet*.cpp): an arena holding a stand-in manager,
// players, skaters, the message, an app and a connection; recording stand-ins (standins.h) for what the handlers
// call; and a Handle() that runs the original or ours on a context built over the arena. Both runs start from the same
// arena and the traces plus the whole arena (arena-relative) are compared.

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
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
}

inline void SaveGlobals(std::initializer_list<Saved> extra = {})
{
    g_saved = {{0x007CCE14, 4, {}}, {0x007CE478, 4, {}}, {0x007CE470, 4, {}}, {0x007CE500, 4, {}},
               {0x007CE5D8, 4, {}}, {0x00696D68, 4, {}}, {0x007D1800, 4, {}}};
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
