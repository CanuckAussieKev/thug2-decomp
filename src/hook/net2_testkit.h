#pragma once

// More of the network manager's test kit (on gamenet_testkit.h) for the tests of the online code at
// 0x0053A910-0x00541870 (selftests_net_*.cpp): the manager's real player lists in the arena (so the originals' inlined
// list walks and the list functions run on them), script structures built by hand, a stand-in script, the camera
// object and its components, recording vtables.

#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_app.h"
#include "game/net/net_client.h"
#include "game/obj/object.h"
#include "game/obj/objman.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/gamenet_testkit.h"

namespace Net2Kit {

using namespace GameNetKit;

// Arena layout (beyond gamenet_testkit.h's): the connection area (0x14000-0x18000) is free here.
constexpr uint32_t kNodes = 0x14000;     // list nodes, 0x20 apart (0x400)
constexpr uint32_t kStructs = 0x14400;   // hand-built script structures (0x400)
constexpr uint32_t kScript = 0x14800;    // the stand-in script (+0x14 its params: kScriptParams)
constexpr uint32_t kScriptParams = 0x14880;
constexpr uint32_t kObjects = 0x14900;   // stand-in objects with recording vtables, 0x40 apart (0x400)
constexpr uint32_t kVtables = 0x14D00;   // their vtables (0x300)
constexpr uint32_t kCam = 0x15000;       // the camera object; its components at +0x100 ... (0x800)
constexpr uint32_t kInput = 0x15800;     // the input manager (0x200)
constexpr uint32_t kNewPlayers = 0x15A00; // NewPlayerInfo records, 0x44 apart (0x200)
constexpr uint32_t kFree = 0x15C00;      // a test's own (0x2400)

inline uint32_t g_nextNode;

// A list (Lst::Head at `head`): +4 -1 (the head), +0x0C the first node, +0x10 the last; each node (0x20 apart) +4
// its item, +0x0C the next, +0x10 the previous.
inline void BuildList(uint32_t head, const std::vector<uint32_t>& items)
{
    Word<uint32_t>(head + 4) = 0xFFFFFFFFu;
    uint32_t previous = head;
    for (uint32_t item : items) {
        uint32_t node = kNodes + g_nextNode;
        g_nextNode += 0x20;
        Word<uint32_t>(node + 0) = 0x0D0D0000u | g_nextNode;
        Word<uint32_t>(node + 4) = item;
        Word<uint32_t>(previous + 0xC) = At(node);
        Word<uint32_t>(node + 0x10) = At(previous);
        previous = node;
    }
    Word<uint32_t>(previous + 0xC) = At(head);
    Word<uint32_t>(head + 0x10) = At(previous);
}
inline uint32_t PlayerAt(int i) { return kPlayers + kPlayerSize * static_cast<uint32_t>(i); }
// The manager's players (by index into the arena's eight), in that order.
inline void SetPlayers(const std::vector<int>& indexes)
{
    g_nextNode = 0; // (the lists built anew: the player list first)
    std::vector<uint32_t> items;
    for (int i : indexes)
        items.push_back(At(PlayerAt(i)));
    BuildList(kManager + 0x11C, items);
}
inline uint32_t& PlayerFlags(int i) { return Word<uint32_t>(PlayerAt(i) + 0x1D0); }
// The joining players (NewPlayerInfo, 0x44 bytes): object ids and flags.
inline void SetNewPlayers(const std::vector<std::pair<int8_t, uint32_t>>& infos)
{
    std::vector<uint32_t> items;
    uint32_t at = kNewPlayers;
    for (const auto& info : infos) {
        Word<int8_t>(at + 0x10) = info.first;
        Word<uint32_t>(at + 0x20) = info.second;
        items.push_back(At(at));
        at += 0x44;
    }
    BuildList(kManager + 0x130, items);
}

// A script structure built by hand in the arena: components chained in the order added.
class ArenaStruct {
public:
    explicit ArenaStruct(uint32_t offset) : m_offset(offset) { std::memset(&Word<uint8_t>(offset), 0, 0x80); }
    Script::CStruct* Struct() { return &Word<Script::CStruct>(m_offset); }
    ArenaStruct& Add(Script::ESymbolType type, uint32_t name, uint32_t raw)
    {
        uint32_t c = m_offset + 0x10 + 0x10 * m_count;
        auto* component = &Word<Script::CComponent>(c);
        component->typeAndFlag = static_cast<uint8_t>(type << 1);
        component->name = name;
        component->raw = raw;
        component->next = nullptr;
        if (m_count)
            Word<Script::CComponent>(m_offset + 0x10 + 0x10 * (m_count - 1)).next = component;
        else
            Word<Script::CComponent*>(m_offset + 4) = component;
        ++m_count;
        return *this;
    }
    ArenaStruct& Integer(uint32_t name, int value)
    {
        return Add(Script::ESYMBOLTYPE_INTEGER, name, static_cast<uint32_t>(value));
    }
    ArenaStruct& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
    ArenaStruct& String(uint32_t name, const char* value)
    {
        return Add(Script::ESYMBOLTYPE_STRING, name, reinterpret_cast<uint32_t>(value));
    }

private:
    uint32_t m_offset;
    int m_count = 0;
};
static_assert(sizeof(Script::CComponent) <= 0x10, "components 0x10 apart");

// The stand-in script: its params at kScriptParams (AddInteger / AddChecksum ... on it are recorded).
inline Script::CScript* TestScript()
{
    Word<uint32_t>(kScript + 0x14) = At(kScriptParams);
    return &Word<Script::CScript>(kScript);
}

// A stand-in object at kObjects + 0x40 * `index` whose vtable's slots record (tag 0x7B00 | index << 4 | slot, ECX,
// `pops[slot]` / 4 arguments).
inline uint32_t RecordingObject(StandIns& s, int index, std::initializer_list<std::pair<int, uint16_t>> slots)
{
    uint32_t object = kObjects + 0x40 * static_cast<uint32_t>(index);
    uint32_t vtable = kVtables + 0x40 * static_cast<uint32_t>(index);
    Word<uint32_t>(object) = At(vtable);
    for (const auto& slot : slots) {
        uint32_t tag = 0x7B00u | (static_cast<uint32_t>(index) << 4) | static_cast<uint32_t>(slot.first);
        uint16_t pop = slot.second;
        Word<uint32_t>(vtable + 4 * slot.first) = reinterpret_cast<uint32_t>(s.Thunk(pop, [tag, pop](Regs& r) {
            g_trace.push_back(tag);
            g_trace.push_back(Rel(r.ecx));
            for (int i = 0; i < pop / 4; ++i)
                g_trace.push_back(Rel(r.args[i]));
            return 0u;
        }));
    }
    return object;
}

// Common stand-ins of these tests (beyond InstallCommon): object lookups and components, message sends, tasks, the
// time, the integer / checksum adds by name, allocations zeroed.
inline uint32_t g_now;
inline void InstallNet2(StandIns& s)
{
    InstallCommon(s);
    StubCdecl(s, {0x005BE970, AddressOf(&Tmr::GetTime)}, 0xE970, 0, [](Regs&) { return g_now; });
    Stub(s, {0x004619F0, AddressOf(&Obj::CBaseManager::GetObjectByID)}, 4, 0x19F0, [](Regs&) { return At(kCam); });
    // Components: the stand-in objects 0-3 for the camera object's, 4-7 for any skater's.
    Stub(s, {0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, 0xDB90, [](Regs& r) {
        uint32_t base = r.ecx == At(kCam) ? 0 : 4;
        switch (r.args[0]) {
        case 0x5E43A604: return At(kObjects + 0x40 * (base + 0)); // SkaterCamera
        case 0xC7D37EC2: return At(kObjects + 0x40 * (base + 1)); // WalkCamera
        case 0xD9151D4D: return At(kObjects + 0x40 * (base + 2)); // SkaterPhysicsControl
        case 0xD262802F: return At(kObjects + 0x40 * (base + 3)); // the score
        case 0x18223FD6: return At(0x36000);                       // the vehicle (its +0x1104)
        default: return 0u;
        }
    });
    s.At({0x00459E50, AddressOf(&Net::Client::EnqueueMessageToServer)}, 4, [](Regs& r) {
        const auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[0]);
        if (desc->m_id == 0x61) { // (kick / ban: two bytes and a name - the rest is the stack's)
            Net::MsgDesc copy = *desc;
            copy.m_data = nullptr;
            RecordDesc(0x9E50, r.ecx, &copy);
            Rec(0x9E51, {HashBytes(static_cast<const uint8_t*>(desc->m_data), 2),
                         Hash(static_cast<const char*>(desc->m_data) + 2)});
        } else {
            RecordDesc(0x9E50, r.ecx, desc);
        }
        return 0u;
    });
    s.At({0x00457EB0, AddressOf(&Net::App::EnqueueMessage)}, 8, [](Regs& r) {
        Rec(0x7EB0, {r.args[0]});
        const auto* desc = reinterpret_cast<const Net::MsgDesc*>(r.args[1]);
        if (desc->m_id == 0x22 && desc->m_length >= 0x30 && desc->m_data) { // (a new player: its fields, name, appearance)
            const auto* msg = static_cast<const uint8_t*>(desc->m_data);
            Net::MsgDesc copy = *desc;
            copy.m_data = nullptr;
            RecordDesc(0x7EB1, r.ecx, &copy);
            Rec(0x7EB2, {HashBytes(msg, 0x1A), HashBytes(msg + 0x1C, 4), Hash(reinterpret_cast<const char*>(msg + 0x20)),
                         HashBytes(msg + 0x30, desc->m_length - 0x30u)});
        } else if (desc->m_id == 0x34 && desc->m_length == 0x48) { // (the start of a game: bytes it never writes masked)
            uint8_t copy[0x48];
            std::memcpy(copy, desc->m_data, sizeof(copy));
            std::memset(copy + 0x10, 0, 4);
            copy[0x17] = 0;
            std::memset(copy + 0x25, 0, 3);
            Net::MsgDesc masked = *desc;
            masked.m_data = copy;
            RecordDesc(0x7EB1, r.ecx, &masked);
        } else {
            RecordDesc(0x7EB1, r.ecx, desc);
        }
        return 0u;
    });
    Stub(s, {0x00406C90, AddressOf(&Tsk::Stack::AddTask)}, 4, 0x6C90, [](Regs&) {
        g_trace.push_back(GameVariable<uint8_t>(0x007CCDF8)); // (a flag set around task adds)
        return 0u;
    });
    Stub(s, {0x004068F0, AddressOf(&Tsk::BaseTask::Remove)}, 0, 0x68F0);
    s.At({0x00477C30, AddressOf(static_cast<void (Script::CStruct::*)(const char*, int)>(&Script::CStruct::AddInteger))},
         8, [](Regs& r) {
             Rec(0x7C30, {r.ecx, Hash(reinterpret_cast<const char*>(r.args[0])), r.args[1]});
             return 0u;
         });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0, r.args[0]);
        return block;
    });
    // The current heap's malloc / free (script arrays' elements).
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Rec(0xBAE0, {r.args[0]});
        uint32_t block = At(g_heapNext);
        g_heapNext += (r.args[0] + 15) & ~15u;
        std::memset(reinterpret_cast<void*>(block), 0xCD, r.args[0]);
        return block;
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Rec(0xBB60, {r.args[0]});
        return 0u;
    });
    // The task list node's constructor (priority), recorded; its vtable written as the original's does.
    Stub(s, {0x00406890}, 4, 0x6890, [](Regs& r) {
        Word<uint32_t>(r.ecx - g_base + 4) = r.args[0];
        return r.ecx;
    });
}

inline std::vector<Saved> NetGlobals(std::initializer_list<Saved> extra)
{
    std::vector<Saved> list = {{0x006F0FB8, 4, {}}, {0x00696D40, 4, {}}, {0x007CCDEC, 4, {}}};
    list.insert(list.end(), extra);
    return list;
}

using Scenario = void (*)(bool original);

// Our functions the code under test hands on or stores (callbacks, task code) and the original's own addresses for
// them: recorded / compared as the original's.
inline std::vector<std::pair<uint32_t, uint32_t>> g_aliases;
template <typename F>
void AddAlias(F function, uint32_t original)
{
    uint32_t raw;
    std::memcpy(&raw, &function, sizeof(raw));
    g_aliases.push_back({raw, original}); // (an incremental-link jump)
    g_aliases.push_back({AddressOf(function), original});
}
inline bool g_aliasesReady;
inline uint32_t Aliased(uint32_t value)
{
    if (!g_aliasesReady) {
        g_aliasesReady = true;
        AddAlias(&GameNet::Manager::ObserverTask, 0x0053BED0);
    }
    for (const auto& alias : g_aliases)
        if (value == alias.first)
            return alias.second;
    return value;
}

// Each case named in the log as it starts (to find a case that crashes).
inline bool g_net2Verbose = false;

// Run `scenario` on a fresh arena for the original and ours; compare the calls and the arenas. `prepare` sets up
// what both runs share (after the arena and the stand-ins).
inline void RunNet2Case(const char* what, Scenario scenario, void (*install)(StandIns&) = nullptr)
{
    auto run = [&](bool original) {
        if (g_net2Verbose)
            Log("  net2 case %s (%s)", what, original ? "original" : "ours");
        SetUpArena(original ? 0 : 1);
        g_answers.clear();
        g_nextNode = 0;
        g_now = 0x1234;
        GameVariable<uint32_t>(0x007CCDEC) = 0;
        GameVariable<uint8_t>(0x007CCDF8) = 0;
        GameVariable<uint32_t>(0x006F0FB8) = At(kMisc + 0x500); // the composite object manager
        GameVariable<uint32_t>(0x00696D40) = At(kInput);
        StandIns* s = new StandIns;
        InstallNet2(*s);
        if (install)
            install(*s);
        scenario(original);
        delete s;
        // Our functions stored where the original stores its own (task code...): written back as the original's.
        for (uint32_t i = 0; i < kArenaSize; i += 4)
            Word<uint32_t>(i) = Aliased(Word<uint32_t>(i));
        return Finish();
    };
    // An empty symbol table (structures' lookups of unnamed names find nothing - the real one isn't set up offline).
    static Script::CSymbolTableEntry* emptyTable[0x1000];
    std::memset(emptyTable, 0, sizeof(emptyTable));
    auto* savedTable = GameVariable<Script::CSymbolTableEntry**>(0x006F3624);
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = emptyTable;
    std::vector<uint32_t> expected = run(true);
    std::vector<uint32_t> actual = run(false);
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = savedTable;
    Compare(expected, actual, what);
}

} // namespace Net2Kit
