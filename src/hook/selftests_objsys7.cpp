// Self-tests for the objsys agent's batch OS-7: the path follower (src/game/obj/path_follower.cpp). The original
// and ours following random node graphs (chains, branches, dead ends, coincident and close nodes) with corner
// distances that make arcs, blends and straight turns; the nodes, the owner's path component and the random
// numbers stood in for, the vector maths the same in both runs (ours: rewritten and tested before), the follower
// compared after every update.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "game/mth/matrix.h"
#include "game/mth/random.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/path_follower.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/appmain_testkit.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/objsys_testkit.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace AppMainTests;

template <typename F>
F Fn(bool original, uint32_t address)
{
    return reinterpret_cast<F>(Code(original, address));
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The node graph: node i is named 0x0D0D0000 + i; its structure stand-in is 0x5D0D0000 + i.
struct Node {
    Mth::Vector pos;
    std::vector<uint32_t> links; // node indexes
};
std::vector<Node> g_graph;
Lcg g_pathAnswers(1);

uint32_t NodeName(uint32_t i) { return 0x0D0D0000u + i; }
uint32_t NodeIndex(uint32_t nameOrStruct) { return nameOrStruct & 0xFFFF; }

double __cdecl AcosForBoth(double x) { return std::acos(x); }
// The C runtime's _CIacos (argument and result in ST0), answered by std::acos for both runs.
__declspec(naked) void CIacosStandIn()
{
    __asm {
        sub esp, 8
        fstp qword ptr [esp]
        call AcosForBoth
        add esp, 8
        ret
    }
}

void StandInPathWorld(StandIns& s)
{
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Record({0x90DE0000, r.args[0]});
        return 0x5D0D0000u + NodeIndex(r.args[0]);
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))},
         0, [](Regs& r) {
             *reinterpret_cast<Mth::Vector*>(r.args[1]) = g_graph[NodeIndex(r.args[0])].pos;
             Record({0x9050000, r.args[0]});
             return 1u;
         });
    s.At({0x0045C3A0, AddressOf(static_cast<uint32_t (*)(uint32_t)>(&SkateScript::GetLinkCount))}, 0,
         [](Regs& r) {
             auto count = static_cast<uint32_t>(g_graph[NodeIndex(r.args[0])].links.size());
             Record({0x11C40000, r.args[0], count});
             return count;
         });
    s.At({0x0045C3E0, AddressOf(static_cast<uint32_t (*)(uint32_t, int)>(&SkateScript::GetLink))}, 0, [](Regs& r) {
        uint32_t link = NodeName(g_graph[NodeIndex(r.args[0])].links[r.args[1]]);
        Record({0x11C40001, r.args[0], r.args[1], link});
        return link;
    });
    s.At({0x0045C460, AddressOf(static_cast<Script::CStruct* (*)(uint32_t, int)>(&SkateScript::GetLinkedNode))}, 0,
         [](Regs& r) {
             uint32_t node = 0x5D0D0000u + g_graph[NodeIndex(r.args[0])].links[r.args[1]];
             Record({0x11C40002, r.args[0], r.args[1], node});
             return node;
         });
    s.At({0x0045DB90, AddressOf(&Obj::CCompositeObject::GetComponent)}, 4, [](Regs& r) {
        Record({0xC0390000, r.args[0]});
        return 0xC0C0C000u;
    });
    s.At({0x00423520}, 0, [](Regs&) {
        uint32_t random = g_pathAnswers.Below(2);
        Record({0x4A4D0000, random});
        return random;
    });
    s.At({0x00424020}, 4, [](Regs& r) {
        Record({0x9A55ED00, r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00403DE0, AddressOf(&Mth::Rnd)}, 0, [](Regs& r) {
        uint32_t value = g_pathAnswers.Below(r.args[0]);
        Record({0x4A4E0000, r.args[0], value});
        return value;
    });
    s.Bytes(0x00623630, Branch(0xE9, 0x00623630, reinterpret_cast<void*>(&CIacosStandIn)));
    // the vector maths the same in both runs (not the length and dot product: the original's keep their results
    // unrounded, and ours reproduce that)
    for (uint32_t address : {0x00404E20u, 0x004023F0u, 0x00402550u, 0x00402470u, 0x00402320u, 0x00402340u,
                             0x00402370u, 0x004024A0u}) {
        void* ours = OursAt(address);
        Check(ours != nullptr, "path follower: our maths found");
        if (ours)
            s.Bytes(address, Branch(0xE9, address, ours));
    }
}

void BuildGraph(Lcg& random)
{
    g_graph.clear();
    uint32_t count = 3 + random.Below(6);
    for (uint32_t i = 0; i < count; ++i) {
        Node n;
        n.pos = {(static_cast<float>(random.Below(4001)) - 2000.0f) / 10.0f,
                 (static_cast<float>(random.Below(401)) - 200.0f) / 10.0f,
                 (static_cast<float>(random.Below(4001)) - 2000.0f) / 10.0f, 1.0f};
        if (i > 0 && random.Below(6) == 0)
            n.pos = g_graph[i - 1].pos; // (coincident)
        else if (i > 0 && random.Below(6) == 0)
            n.pos = {g_graph[i - 1].pos.x + 0.5f, g_graph[i - 1].pos.y, g_graph[i - 1].pos.z, 1.0f}; // (close)
        g_graph.push_back(n);
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t links = random.Below(5) == 0 ? 0 : 1 + random.Below(2);
        for (uint32_t k = 0; k < links; ++k)
            g_graph[i].links.push_back(random.Below(3) == 0 ? random.Below(count) : (i + 1) % count);
    }
}

alignas(16) uint8_t g_follower[0x100];

void PushFollower()
{
    // (a NaN's sign not compared: once a degenerate corner has made the state NaN, the x87's choice between two
    // NaN operands - the positive one - and SSE's - the first - differ, which nothing reads)
    for (uint32_t i = 0; i < 0xE4; i += 4) {
        uint32_t word = *reinterpret_cast<uint32_t*>(g_follower + i);
        g_trace.push_back((word & 0x7FFFFFFF) > 0x7F800000 ? (word & 0x7FFFFFFF) : word);
    }
}

// Start (at a node, near one, on a node of the path), then updates of random distances (some with a direction to
// weigh them by, perpendicular ones too), with corner distances of none / small / large: every state (straight,
// arc, blend, done), the nodes told, random and first links, dead ends.
void TestPathFollower()
{
    StandIns& s = *new StandIns();
    StandInPathWorld(s);
    static const float kCorners[] = {0.0f, 0.0f, 5.0f, 20.0f, 60.0f, 400.0f};
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        auto run = [&](bool original) {
            g_trace.clear();
            Lcg random(0x9A740000 + seed);
            g_pathAnswers = Lcg(0x9A7A0000 + seed);
            BuildGraph(random);
            std::memset(g_follower, 0xCD, sizeof(g_follower));
            uint32_t owner = 0x0B0B0B00;
            Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x0046A1D0)(g_follower, owner) -
                    reinterpret_cast<uint32_t>(g_follower)});
            PushFollower(); // (as constructed)
            float corner = random.Pick(kCorners);
            std::memcpy(g_follower + 0x94, &corner, 4);
            Mth::Vector start = g_graph[0].pos;
            if (random.Below(2)) {
                start.x += static_cast<float>(random.Below(41)) - 20.0f;
                start.z += static_cast<float>(random.Below(41)) - 20.0f;
            }
            Fn<void(__thiscall*)(void*, uint32_t, const Mth::Vector*)>(original, 0x0046A3F0)(
                g_follower, NodeName(random.Below(static_cast<uint32_t>(g_graph.size()))), &start);
            PushFollower();
            for (int step = 0; step < 80; ++step) {
                float distance = static_cast<float>(1 + random.Below(300)) / 10.0f;
                Mth::Vector direction{};
                const Mth::Vector* weigh = nullptr;
                if (random.Below(5) == 0) {
                    direction = {static_cast<float>(random.Below(3)) - 1.0f, 0.0f,
                                 static_cast<float>(random.Below(3)) - 1.0f, 0.0f};
                    weigh = &direction;
                }
                bool done = Fn<bool(__thiscall*)(void*, float, const Mth::Vector*)>(original, 0x0046A640)(
                    g_follower, distance, weigh);
                Record({0xD0E00000u | static_cast<uint32_t>(done)});
                PushFollower();
                if (random.Below(10) == 0) {
                    Mth::Vector heading{};
                    Fn<void(__thiscall*)(void*, Mth::Vector*)>(original, 0x0046A1B0)(g_follower, &heading);
                    Record({Bits(heading.x), Bits(heading.y), Bits(heading.z), Bits(heading.w)});
                }
            }
            Record({Fn<uint32_t(__thiscall*)(void*, uint32_t)>(original, 0x00469B50)(g_follower, 0) -
                    reinterpret_cast<uint32_t>(g_follower)});
            std::vector<uint32_t> trace = g_trace;
            return trace;
        };
        std::vector<uint32_t> expected = run(true);
        std::vector<uint32_t> actual = run(false);
        Compare(expected, actual, "path follower: start, straight, arcs, blends, dead ends");
        if (expected != actual) {
            Log("  (seed %u)", seed);
            break;
        }
    }
    ObjsysTests::CleanUp();
}
SELFTEST(Early, TestPathFollower, ObjsysTests::CleanUp);

} // namespace
