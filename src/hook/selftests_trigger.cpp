// Self-tests for the trigger component's node lookup (0x00434A60, Obj::CTriggerComponent::TriggerNode): the original
// against ours on hand-built nodes, every call it makes stood in for by recording code (the node manager's GetNode,
// the skate module's 0x00565BB0, the object's SpawnAndRunScript), so they're compared call by call; and ours alone where
// the original crashes (no node).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/obj/object.h"
#include "game/obj/trigger_component.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Script::CArray;
using Script::CComponent;
using Script::CStruct;
using Script::ESymbolType;

constexpr uint32_t kName = 0xA1DC81F9;          // name
constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint32_t kNetEnabled = 0x20209C31;
constexpr uint32_t kPermanent = 0x23627FD7;
constexpr uint32_t kParams = 0x5180A4DB;

// A script structure built by hand: components chained in the order added.
struct TestNode {
    alignas(16) uint8_t storage[8];
    CComponent components[6];
    int count = 0;
    TestNode() { std::memset(storage, 0, sizeof(storage)); std::memset(components, 0, sizeof(components)); }
    CStruct* Struct() { return reinterpret_cast<CStruct*>(storage); }
    TestNode& Add(ESymbolType type, uint32_t name, uint32_t raw)
    {
        CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.checksum = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<CComponent**>(storage)[1] = &c;
        ++count;
        return *this;
    }
    TestNode& Checksum(uint32_t name, uint32_t value) { return Add(Script::ESYMBOLTYPE_NAME, name, value); }
    TestNode& Flag(uint32_t flag) { return Add(Script::ESYMBOLTYPE_NAME, 0, flag); }
    TestNode& Structure(uint32_t name, CStruct* value)
    {
        return Add(Script::ESYMBOLTYPE_STRUCTURE, name, reinterpret_cast<uint32_t>(value));
    }
};

std::map<uint32_t, CStruct*> g_nodes; // the node manager's nodes, by name
std::map<uint32_t, bool> g_skipped;    // the skate module's answer, by node name
void* g_savedSkate;
Script::CSymbolTableEntry** g_savedTable;

void RestoreTrigger()
{
    GameVariable<void*>(0x007CE478) = g_savedSkate;
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = g_savedTable;
    RemoveStandIns();
}

void TestTriggerNode()
{
    g_savedSkate = GameVariable<void*>(0x007CE478);
    // (an empty symbol table: a flag - an unnamed name - is looked up as a global structure first)
    g_savedTable = GameVariable<Script::CSymbolTableEntry**>(0x006F3624);
    static Script::CSymbolTableEntry* table[0x1000];
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
    alignas(16) static uint8_t skate[0x40];
    GameVariable<void*>(0x007CE478) = skate;
    auto* standIns = new StandIns;
    standIns->At({0x0045C1B0, AddressOf(static_cast<CStruct* (*)(uint32_t, bool)>(&SkateScript::GetNode))}, 0,
                 [](Regs& r) {
                     Record({0xC1B0, r.args[0], r.args[1] & 0xFF});
                     auto found = g_nodes.find(r.args[0]);
                     return found == g_nodes.end() ? 0u : reinterpret_cast<uint32_t>(found->second);
                 });
    standIns->At({0x00565BB0, AddressOf(&Mdl::Skate::ShouldBeAbsent)}, 4, [](Regs& r) {
        auto* node = reinterpret_cast<CStruct*>(r.args[0]);
        uint32_t name = SkateScript::GetNodeName(node);
        Record({0x5BB0, name}); // (no `this` to check: ours is a static)
        return g_skipped[name] ? 1u : 0u;
    });
    standIns->At({0x00461480, AddressOf(&Obj::CObject::SpawnAndRunScript)}, 0x14, [](Regs& r) {
        Record({0x1480, r.ecx, r.args[0], r.args[1], r.args[2] & 0xFF, r.args[3] & 0xFF, r.args[4]});
        return 0x5C41u;
    });

    // The nodes: A (a script, both flags), B (a script, no flags), C (no script), D (a script; the skate module skips
    // it), E (the other object's node, with parameters), F (the other object's node, none), G (a script, NetEnabled
    // only).
    TestNode params;
    params.Checksum(0x11111111, 0x22222222);
    TestNode a, b, c, d, e, f, g;
    a.Checksum(kName, 0xA0A0A0A0).Checksum(kTriggerScript, 0x5C0A0A0A).Flag(kNetEnabled).Flag(kPermanent);
    b.Checksum(kName, 0xB0B0B0B0).Checksum(kTriggerScript, 0x5C0B0B0B);
    c.Checksum(kName, 0xC0C0C0C0);
    d.Checksum(kName, 0xD0D0D0D0).Checksum(kTriggerScript, 0x5C0D0D0D).Flag(kPermanent);
    e.Checksum(kName, 0xE0E0E0E0).Structure(kParams, params.Struct());
    f.Checksum(kName, 0xF0F0F0F0);
    g.Checksum(kName, 0x60606060).Checksum(kTriggerScript, 0x5C060606).Flag(kNetEnabled);
    for (TestNode* node : {&a, &b, &c, &d, &e, &f, &g})
        g_nodes[node->components[0].checksum] = node->Struct();
    g_skipped.clear();
    g_skipped[0xD0D0D0D0] = true;

    // A node array (the second way to find a node): C, B, A - and one of only C (not found: element 0).
    std::vector<uint32_t> list = {reinterpret_cast<uint32_t>(c.Struct()), reinterpret_cast<uint32_t>(b.Struct()),
                                  reinterpret_cast<uint32_t>(a.Struct())};
    CArray array{};
    array.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    array.m_size = 3;
    array.m_elements = list.data();
    // (none named so in [A, C]: the search ends on element 0 - A, whose script runs)
    std::vector<uint32_t> list2 = {reinterpret_cast<uint32_t>(a.Struct()), reinterpret_cast<uint32_t>(c.Struct())};
    CArray array2{};
    array2.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    array2.m_size = 2;
    array2.m_elements = list2.data();
    CArray empty{};
    empty.m_type = Script::ESYMBOLTYPE_STRUCTURE;
    // (the other object: its name at +0x14)
    uint8_t otherE[0x20] = {}, otherF[0x20] = {}, otherNone[0x20] = {};
    *reinterpret_cast<uint32_t*>(otherE + 0x14) = 0xE0E0E0E0;
    *reinterpret_cast<uint32_t*>(otherF + 0x14) = 0xF0F0F0F0;
    *reinterpret_cast<uint32_t*>(otherNone + 0x14) = 0x99999999;

    const int events[][3] = {{0, 0, 0}, {3, 0, 0}, {0, 5, 0}, {0, 0, 31}, {1, 1, 2}, {33, 0, 0}};
    struct Case {
        uint32_t name;
        CArray* nodes;
        const uint8_t* other;
    };
    const Case cases[] = {
        {0xA0A0A0A0, nullptr, nullptr},   {0xB0B0B0B0, nullptr, otherE},    {0xA0A0A0A0, nullptr, otherF},
        {0xB0B0B0B0, nullptr, otherNone}, {0xC0C0C0C0, nullptr, nullptr},   {0xD0D0D0D0, nullptr, otherE},
        {0xA0A0A0A0, &array, nullptr},    {0xB0B0B0B0, &array, otherE},     {0xC0C0C0C0, &array, nullptr},
        {0x12345678, &array, nullptr}, // none named so: the search ends on element 0 (C: no script)
        {0x12345678, &array2, otherE}, // none named so: element 0 - A - runs
        {0x60606060, nullptr, otherE},  // NetEnabled only
        {0x12345678, &empty, nullptr}, // an empty array: no node - the original crashes too; not compared
    };

    using OriginalFn = bool(__thiscall*)(void*, const int*, uint32_t, CArray*, const uint8_t*);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        g_trace.clear();
        for (size_t i = 0; i + 1 < sizeof(cases) / sizeof(cases[0]); ++i) {
            const Case& k = cases[i];
            for (const int* ev : events) {
                alignas(16) uint8_t component[0x40];
                std::memset(component, 0xCD, sizeof(component));
                *reinterpret_cast<uint32_t*>(component + 0x0C) = 0x0B1EC700u;
                bool result;
                if (run == 0)
                    result = Original<OriginalFn>(0x00434A60)(component, ev, k.name, k.nodes, k.other);
                else
                    result = reinterpret_cast<Obj::CTriggerComponent*>(component)->TriggerNode(ev, k.name, k.nodes,
                                                                                              k.other);
                Record({0xCA5E, static_cast<uint32_t>(i), result ? 1u : 0u,
                        *reinterpret_cast<uint32_t*>(component + 0x28)});
            }
        }
        traces[run] = g_trace;
    }
    Check(traces[0] == traces[1], "TriggerNode: GetNode / a node array, scripts, flags, parameters, events, skipped");
    if (traces[0].size() != traces[1].size())
        Log("  TriggerNode trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i) {
        if (traces[0][i] != traces[1][i]) {
            Log("  TriggerNode entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i],
                traces[1][i]);
            ++shown;
        }
    }

    // Ours alone: no node (GetNode finds none; an empty array) - false, nothing run, the events untouched.
    for (const Case& k : {Case{0x99999999, nullptr, nullptr}, cases[sizeof(cases) / sizeof(cases[0]) - 1]}) {
        g_trace.clear();
        alignas(16) uint8_t component[0x40];
        std::memset(component, 0xCD, sizeof(component));
        bool result = reinterpret_cast<Obj::CTriggerComponent*>(component)->TriggerNode(events[1], k.name, k.nodes,
                                                                                        k.other);
        bool spawned = false;
        for (uint32_t word : g_trace)
            spawned |= word == 0x1480;
        Check(!result && !spawned && *reinterpret_cast<uint32_t*>(component + 0x28) == 0xCDCDCDCD,
              "TriggerNode: no node - false, nothing run (ours: the original reads through the null)");
    }
    RestoreTrigger();
}

} // namespace

SELFTEST(Early, TestTriggerNode, RestoreTrigger);
