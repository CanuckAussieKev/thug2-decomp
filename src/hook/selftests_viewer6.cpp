// Self-tests for the rail manager (src/game/obj/rail_manager.cpp): the original and ours run the same queries and
// updates on an identical stand-in level of rails, ladders and lone nodes in an arena; what they call outside (the
// collision feelers - their hits scripted call by call -, the node scripts' structures and the node array, the
// rail checkers, the quaternion / matrix helpers, the trick objects, memory, script globals, debug output) is
// recorded (viewer_testkit.h; float-returning ones by C++ functions jumped to), and the calls, the results and the
// whole arena are compared. The geometry helpers (closest points, vectors) run as they are, the same for both.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <vector>

#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/obj/object.h"
#include "game/obj/rail_manager.h"
#include "game/obj/smtptr.h"
#include "game/script/nodes.h"
#include "hook/viewer_testkit.h"

namespace {

using namespace ViewerTestKit;
using Obj::CRailManager;
using Obj::CRailNode;

// Arena layout.
constexpr uint32_t kManager = 0x0000; // 0x54
constexpr uint32_t kNodes = 0x0100;   // the rail nodes (made by hand), 0x54 each
constexpr uint32_t kParams = 0x0500;  // node structures, 0x10 apart
constexpr uint32_t kArrays = 0x0700;  // script arrays (0xC) and their elements
constexpr uint32_t kChecker = 0x0900;
constexpr uint32_t kRecords = 0x0A00; // result records
constexpr uint32_t kOther = 0x0C00;   // a second manager (built from a node array)
constexpr uint32_t kRefObject = 0x0D00; // what a feeler hit (a CRefCounted)

std::deque<int> g_hits;       // the feelers' answers, in order (none left: no hit)
std::deque<float> g_hitHeights;
std::deque<int> g_checks;     // the checkers' answers
std::map<uint32_t, float> g_floats;
std::set<std::pair<uint32_t, uint32_t>> g_flagsSet;
std::map<uint32_t, uint32_t> g_nodeNames;   // structure -> name
std::map<uint32_t, uint32_t> g_nodesByName; // name -> structure
std::map<uint32_t, std::vector<float>> g_positions;
std::set<uint32_t> g_skippedNodes;

Mth::Matrix* __stdcall SameMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    return &Mth::Multiply(*out, *a, *b);
}
float __cdecl GetFloatStandIn(uint32_t name)
{
    Rec(0x8F00, {name});
    auto it = g_floats.find(name);
    return it == g_floats.end() ? 0.0f : it->second;
}
void RecVector(uint32_t pointer)
{
    const auto* words = reinterpret_cast<const uint32_t*>(pointer);
    g_trace.insert(g_trace.end(), words, words + 4);
}

void Install(StandIns& s)
{
    InstallCommon(s);
    for (uint32_t address : {0x00478F00u, AddressOf(static_cast<float (*)(uint32_t)>(&Script::GetFloat))})
        s.Bytes(address, Branch(0xE9, address, reinterpret_cast<void*>(&GetFloatStandIn)));
    s.Bytes(0x005BFA8C, Branch(0xE9, 0x005BFA8C, reinterpret_cast<void*>(&SameMultiply)));
    // (the closest points the same for both: ours, which the game uses, differs from the x87 original by an ulp now
    // and then)
    s.Bytes(0x00402F00, Branch(0xE9, 0x00402F00, reinterpret_cast<void*>(AddressOf(&Mth::ClosestPointsBetweenLines))));
    // The feeler.
    s.At({0x00417760}, 0, [](Regs& r) {
        Rec(0x7760, {});
        std::memset(reinterpret_cast<void*>(r.ecx), 0x5A, 0xB0);
        *reinterpret_cast<uint32_t*>(r.ecx + 0x98) = 0;
        return r.ecx;
    });
    s.At({0x004171D0}, 8, [](Regs& r) {
        Rec(0x71D0, {});
        RecVector(r.args[0]);
        RecVector(r.args[1]);
        std::memcpy(reinterpret_cast<void*>(r.ecx), reinterpret_cast<void*>(r.args[0]), 16);
        std::memcpy(reinterpret_cast<void*>(r.ecx + 0x10), reinterpret_cast<void*>(r.args[1]), 16);
        return 0u;
    });
    s.At({0x004172F0}, 8, [](Regs& r) {
        Rec(0x72F0, {r.args[0], r.args[1]});
        RecVector(r.ecx);
        RecVector(r.ecx + 0x10);
        int hit = 0;
        if (!g_hits.empty()) {
            hit = g_hits.front();
            g_hits.pop_front();
        }
        if (hit) {
            float y = 0.0f;
            if (!g_hitHeights.empty()) {
                y = g_hitHeights.front();
                g_hitHeights.pop_front();
            }
            *reinterpret_cast<float*>(r.ecx + 0x7C) = y;
            *reinterpret_cast<uint32_t*>(r.ecx + 0x98) = hit == 2 ? At(kRefObject) : 0u;
        }
        return hit ? 1u : 0u;
    });
    s.At({0x0041C350}, 0, [](Regs& r) {
        Rec(0xC350, {*reinterpret_cast<uint32_t*>(r.ecx + 0x98)});
        return 0u;
    });
    s.At({0x004637E0, AddressOf(&Obj::CRefCounted::RemoveSmartPointer)}, 4, [](Regs& r) {
        Rec(0x37E0, {r.ecx});
        return 0u;
    });
    s.At({0x0042BBF0}, 4, [](Regs& r) {
        Rec(0xBBF0, {r.args[0]});
        RecVector(r.ecx);
        auto* v = reinterpret_cast<float*>(r.ecx);
        v[0] = v[0] * 0.25f + 1.0f, v[2] = v[2] * 0.5f - 2.0f;
        return 0u;
    });
    s.At({0x0042BC40}, 4, [](Regs& r) {
        Rec(0xBC40, {});
        RecVector(r.ecx);
        auto* m = reinterpret_cast<float*>(r.args[0]);
        for (int i = 0; i < 16; ++i)
            m[i] = reinterpret_cast<float*>(r.ecx)[i % 4] * (i + 1) + 0.5f;
        return 0u;
    });
    s.At({0x0041A730}, 4, [](Regs& r) {
        Rec(0xA730, {});
        RecVector(r.args[0]);
        auto* m = reinterpret_cast<float*>(r.ecx);
        for (int i = 0; i < 16; ++i)
            m[i] = reinterpret_cast<float*>(r.args[0])[i % 3] + i;
        return r.ecx;
    });
    s.At({0x0042E1F0}, 4, [](Regs& r) {
        Rec(0xE1F0, {});
        auto* q = reinterpret_cast<float*>(r.ecx);
        const auto* m = reinterpret_cast<float*>(r.args[0]);
        q[0] = m[0], q[1] = m[5], q[2] = m[10], q[3] = m[15];
        return 0u;
    });
    s.At({0x004442C0}, 0, [](Regs& r) {
        Rec(0x42C0, {r.args[0]});
        return (r.args[0] >> 4) & 0x7Fu;
    });
    // Node structures.
    auto present = [](uint32_t structure, uint32_t name) {
        return g_params.count({structure, name}) || g_flagsSet.count({structure, name});
    };
    s.At({0x00476950, 0x00476EC0, AddressOf(static_cast<bool (CStruct::*)(uint32_t, uint32_t*, int)>(&CStruct::GetChecksum))},
         12, [](Regs& r) {
             Rec(0x6950, {r.ecx, r.args[0], r.args[2] & 0xFF});
             auto it = g_params.find({r.ecx, r.args[0]});
             if (it == g_params.end())
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476860, AddressOf(static_cast<bool (CStruct::*)(uint32_t, Script::CArray**, int)>(&CStruct::GetArray))},
         12, [](Regs& r) {
             Rec(0x6860, {r.ecx, r.args[0]});
             auto it = g_params.find({r.ecx, r.args[0]});
             if (it == g_params.end())
                 return 0u;
             *reinterpret_cast<uint32_t*>(r.args[1]) = it->second;
             return 1u;
         });
    s.At({0x00476B40, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsFlag))}, 4,
         [present](Regs& r) {
             Rec(0x6B40, {r.ecx, r.args[0]});
             return g_flagsSet.count({r.ecx, r.args[0]}) ? 1u : 0u;
         });
    s.At({0x00476AF0, AddressOf(static_cast<bool (CStruct::*)(uint32_t)>(&CStruct::ContainsComponentNamed))}, 4,
         [present](Regs& r) {
             Rec(0x6AF0, {r.ecx, r.args[0]});
             return present(r.ecx, r.args[0]) ? 1u : 0u;
         });
    s.At({0x0045BE40, AddressOf(&SkateScript::GetNodeName)}, 0, [](Regs& r) {
        Rec(0xBE40, {r.args[0]});
        return g_nodeNames[r.args[0]];
    });
    s.At({0x0045C1B0, AddressOf(&SkateScript::GetNode)}, 0, [](Regs& r) {
        Rec(0xC1B0, {r.args[0], r.args[1] & 0xFF});
        return g_nodesByName[r.args[0]];
    });
    s.At({0x0045C050, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetPosition))}, 0,
         [](Regs& r) {
             Rec(0xC050, {r.args[0], r.args[1]});
             const auto& v = g_positions[r.args[0]];
             std::memcpy(reinterpret_cast<void*>(r.args[1]), v.data(), 16);
             return 1u;
         });
    s.At({0x0045C180, AddressOf(static_cast<bool (*)(Script::CStruct*, Mth::Vector*)>(&SkateScript::GetAngles))}, 0,
         [](Regs& r) {
             Rec(0xC180, {r.args[0]});
             auto* v = reinterpret_cast<float*>(r.args[1]);
             v[0] = 0.25f, v[1] = static_cast<float>(r.args[0] & 0xFF) * 0.01f, v[2] = -0.5f, v[3] = 1.0f;
             return 1u;
         });
    s.At({0x00565BB0}, 4, [](Regs& r) {
        Rec(0x5BB0, {r.ecx, r.args[0]});
        return g_skippedNodes.count(r.args[0]) ? 1u : 0u;
    });
    s.At({0x005896D0}, 4, [](Regs& r) {
        Rec(0x96D0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00589080}, 8, [](Regs& r) {
        Rec(0x9080, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x005BBAE0, AddressOf(&Mem::Malloc)}, 0, [](Regs& r) {
        Rec(0xBAE0, {r.args[0]});
        return Allocate(r.args[0]);
    });
    s.At({0x005BBB60, AddressOf(&Mem::FreeMalloc)}, 0, [](Regs& r) {
        Rec(0xBB60, {r.args[0]});
        return 0u;
    });
    // The checkers (record what they're given; answer from g_checks).
    auto checker = [](uint32_t tag, int vectors, int extra) {
        return [tag, vectors, extra](Regs& r) {
            Rec(tag, {r.ecx});
            for (int i = 0; i < vectors; ++i)
                RecVector(r.args[i]);
            const auto* m = reinterpret_cast<const uint32_t*>(r.args[vectors]);
            g_trace.insert(g_trace.end(), m, m + 16);
            for (int i = 0; i < extra; ++i)
                g_trace.push_back(Rel(r.args[vectors + 1 + i]));
            int answer = 0;
            if (!g_checks.empty()) {
                answer = g_checks.front();
                g_checks.pop_front();
            }
            return static_cast<uint32_t>(answer);
        };
    };
    s.At({0x004418B0}, 16, checker(0x18B0, 2, 1));
    s.At({0x00441260}, 16, checker(0x1260, 2, 1));
    s.At({0x004413E0}, 24, [](Regs& r) {
        Rec(0x13E0, {r.ecx});
        RecVector(r.args[0]);
        RecVector(r.args[1]);
        const auto* m = reinterpret_cast<const uint32_t*>(r.args[2]);
        g_trace.insert(g_trace.end(), m, m + 16);
        RecVector(r.args[3]);
        g_trace.push_back(r.args[4]);
        auto* record = reinterpret_cast<uint32_t*>(r.args[5]);
        for (int i = 0; i < 9; ++i)
            record[i] = 0x24000000u + r.args[4] + i;
        int answer = 0;
        if (!g_checks.empty())
            answer = g_checks.front(), g_checks.pop_front();
        return static_cast<uint32_t>(answer);
    });
    s.At({0x00440690}, 28, [](Regs& r) {
        Rec(0x0690, {r.ecx});
        for (int i = 0; i < 4; ++i)
            RecVector(r.args[i]);
        g_trace.push_back(r.args[4]);
        auto* record = reinterpret_cast<uint32_t*>(r.args[5]);
        g_trace.push_back(Rel(record[12]));
        for (int i = 0; i < 12; ++i)
            record[i] = 0x34000000u + r.args[4] + i;
        g_trace.push_back(r.args[6]);
        int answer = 0;
        if (!g_checks.empty())
            answer = g_checks.front(), g_checks.pop_front();
        return static_cast<uint32_t>(answer);
    });
}

void SaveRailGlobals() { SaveGlobals({{0x007CE5D8, 4, {}}, {0x007CE478, 4, {}}, {0x007CE500, 4, {}}}); }

void SetNode(int index, float x, float y, float z, int next, int prev, uint32_t flags)
{
    auto* node = Ptr<CRailNode>(kNodes + 0x54 * index);
    node->m_pos = {x, y, z, 1.0f};
    node->m_next = next >= 0 ? Ptr<CRailNode>(kNodes + 0x54 * next) : nullptr;
    node->m_prev = prev >= 0 ? Ptr<CRailNode>(kNodes + 0x54 * prev) : nullptr;
    node->m_flags = flags;
    node->m_name = 0x100u + index;
    node->m_terrain = static_cast<uint8_t>(index);
    node->m_climb = {0.1f * index, 0.2f, -0.3f, 0.9f};
}
void SetBoxes(int count)
{
    for (int i = 0; i < count; ++i) {
        auto* node = Ptr<CRailNode>(kNodes + 0x54 * i);
        const Mth::Vector& a = node->m_pos;
        const Mth::Vector& b = node->m_next ? node->m_next->m_pos : node->m_pos;
        node->m_min = {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z), 1.0f};
        node->m_max = {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z), 1.0f};
    }
}

// Every function of the rail manager and its helpers, on a level of two rails (one overlapping the other), a ladder,
// a lone node, and switched-off and climbing nodes: see the steps below.
void TestRailManager()
{
    SaveRailGlobals();
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        SetUpArena(run);
        g_integers.clear();
        g_hits.clear();
        g_hitHeights.clear();
        g_checks.clear();
        g_flagsSet.clear();
        g_nodeNames.clear();
        g_nodesByName.clear();
        g_positions.clear();
        g_skippedNodes.clear();
        g_floats = {{0xF840753E, 9.0f}, {0x30CE7F2C, 7.5f}};
        std::vector<uint32_t>& t = traces[run];
        GameVariable<uint32_t>(0x007CE5D8) = At(0x0E00); // the park editor (+0x40 its level)
        GameVariable<uint32_t>(0x007CE478) = At(0x0E80);
        GameVariable<uint32_t>(0x007CE500) = At(0x0EC0);
        auto* m = Ptr<CRailManager>(kManager);
        auto call0 = [&](uint32_t address, void (CRailManager::*ours)()) {
            if (original)
                Original<void(__thiscall*)(void*)>(address)(m);
            else
                (m->*ours)();
        };
        std::memset(m, 0xCD, sizeof(CRailManager)); // (so every field Init sets shows)
        t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0057CC10)(m) : m->Init())));
        t.push_back(Rel(reinterpret_cast<uint32_t>(m->m_nodes)));
        // The level.
        constexpr uint32_t A = Obj::RAIL_FLAG_ACTIVE, L = Obj::RAIL_FLAG_LADDER | Obj::RAIL_FLAG_CLIMBING,
                           D = Obj::RAIL_FLAG_DEFAULT_LINE;
        SetNode(0, 0.0f, 10.0f, 0.0f, 1, -1, A);
        SetNode(1, 40.0f, 10.0f, 0.0f, 2, 0, A | D);
        SetNode(2, 80.0f, 12.0f, 5.0f, -1, 1, A);
        SetNode(3, 20.0f, 10.5f, 3.0f, 4, -1, A);
        SetNode(4, 60.0f, 10.5f, 3.0f, -1, 3, A);
        SetNode(5, 30.0f, 0.0f, 30.0f, 6, -1, A | L);
        SetNode(6, 30.0f, 20.0f, 30.0f, -1, 5, A | L);
        SetNode(7, 100.0f, 5.0f, -20.0f, -1, -1, A);
        SetNode(8, 0.5f, 10.25f, -0.5f, 9, -1, A | Obj::RAIL_FLAG_NO_CLIMBING);
        SetNode(9, 30.0f, 11.0f, 1.0f, -1, 8, 0);
        SetBoxes(10);
        m->m_nodes = Ptr<CRailNode>(kNodes);
        m->m_count = 10;
        for (int i = 0; i < 16; ++i)
            (&m->m_objectMatrix.row[0].x)[i] = static_cast<float>(std::cos(i * 0.8)) * (i % 5 == 0 ? 1.0f : 0.3f);
        auto* nodes = Ptr<CRailNode>(kNodes);
        // The small helpers.
        for (int i = 0; i < 9; ++i) {
            Mth::Vector lo, hi;
            if (original)
                Original<void(__cdecl*)(const void*, const void*, void*, void*)>(0x0057C260)(&nodes[i].m_pos, &nodes[i + 1].m_pos, &lo, &hi);
            else
                Obj::BoxOf(&nodes[i].m_pos, &nodes[i + 1].m_pos, &lo, &hi);
            RecVector(reinterpret_cast<uint32_t>(&lo));
            RecVector(reinterpret_cast<uint32_t>(&hi));
            const float v[] = {nodes[i].m_pos.x, nodes[i + 1].m_pos.x, nodes[i].m_pos.z - 3.0f, nodes[i + 1].m_pos.y};
            double span = original ? Original<double(__cdecl*)(float, float, float, float)>(0x0057C170)(v[0], v[1], v[2], v[3])
                                   : Obj::Span4(v[0], v[1], v[2], v[3]);
            double overlap = original ? Original<double(__cdecl*)(float, float, float, float)>(0x0057C360)(v[3], v[0], v[2], v[1])
                                      : Obj::Overlap(v[3], v[0], v[2], v[1]);
            t.push_back(Bits(static_cast<float>(span)));
            t.push_back(Bits(static_cast<float>(overlap)));
            t.push_back(static_cast<uint32_t>(
                original ? Original<int(__cdecl*)(float, float, float, float, float)>(0x0057C3A0)(v[0], v[1], v[2], v[3], 6.0f * i)
                         : Obj::NearlyEqual4(v[0], v[1], v[2], v[3], 6.0f * i)));
            t.push_back(original ? Original<bool(__stdcall*)(const void*, const void*)>(0x0057C5A0)(&nodes[i], &nodes[(i + 8) % 10])
                                 : Obj::NodesTogether(&nodes[i], &nodes[(i + 8) % 10]));
            t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057C200)(&nodes[i], 0x103u)
                                 : nodes[i].IsNamed(0x103u));
        }
        {
            auto* pair = Ptr<Obj::BoxPair>(kRecords + 0x100);
            for (int i = 0; i < 8; ++i)
                (&pair->first.x)[i] = static_cast<float>(i) - 2.5f;
            if (original)
                Original<void(__thiscall*)(void*)>(0x0057C210)(pair);
            else
                pair->Swap();
            auto* r24 = Ptr<Obj::Record24>(kRecords + 0x140);
            auto* r34 = Ptr<Obj::Record34>(kRecords + 0x180);
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, const void*)>(0x0057C500)(r24, Ptr<void>(kNodes))
                                                                : r24->Assign(Ptr<Obj::Record24>(kNodes)))));
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, const void*)>(0x0057C540)(r34, Ptr<void>(kNodes + 4))
                                                                : r34->Assign(Ptr<Obj::Record34>(kNodes + 4)))));
        }
        // Positions, finding, switching.
        auto world = [&]() {
            for (int i = 0; i < 3; ++i) {
                Mth::Vector out;
                t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, void*, const void*)>(0x0057CB60)(m, &out, &nodes[i])
                                                                    : m->GetWorldPos(&out, &nodes[i]))) == Rel(reinterpret_cast<uint32_t>(&out)));
                RecVector(reinterpret_cast<uint32_t>(&out));
                t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, void*, const void*)>(0x0057CB20)(m, &out, &nodes[i].m_min)
                                                                    : m->ToWorld(&out, &nodes[i].m_min))) == Rel(reinterpret_cast<uint32_t>(&out)));
                RecVector(reinterpret_cast<uint32_t>(&out));
                Mth::Matrix climb;
                t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, void*, const void*)>(0x0057CFB0)(m, &climb, &nodes[i + 4])
                                                                    : m->GetClimbMatrix(&climb, &nodes[i + 4]))) == Rel(reinterpret_cast<uint32_t>(&climb)));
                const auto* words = reinterpret_cast<const uint32_t*>(&climb);
                t.insert(t.end(), words, words + 16);
            }
        };
        world();
        Mth::Matrix objectMatrix = m->m_objectMatrix;
        if (original)
            Original<void*(__thiscall*)(void*, const void*)>(0x0057C4F0)(m, &objectMatrix);
        else
            m->SetObjectMatrix(objectMatrix);
        world();
        m->m_onObject = false;
        for (uint32_t name : {0x100u, 0x107u, 0x999u})
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x0057CBD0)(m, name)
                                                                : m->FindNode(name))));
        auto setActive = [&](uint32_t name, int on, bool whole) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, int, bool)>(0x0057D7C0)(m, name, on, whole);
            else
                m->SetActive(name, on, whole);
            for (uint32_t n : {0x100u, 0x101u, 0x102u, 0x109u, 0x998u})
                t.push_back(original ? Original<bool(__thiscall*)(void*, uint32_t)>(0x0057D840)(m, n) : m->IsActive(n));
        };
        setActive(0x100, 0, true);
        setActive(0x101, 1, false);
        setActive(0x998, 0, false);
        setActive(0x100, 1, true);
        setActive(0x109, 0, false);
        Mth::Vector moved = {1.0f, 10.0f, -1.0f, 1.0f};
        for (uint32_t name : {0x101u, 0x997u}) {
            if (original)
                Original<void(__thiscall*)(void*, uint32_t, const void*)>(0x0057C3F0)(m, name, &moved);
            else
                m->MoveNode(name, &moved);
        }
        for (int i = 0; i < 3; ++i) { // (the moved node's box, and its previous one's, as they are now)
            const auto* words = reinterpret_cast<const uint32_t*>(&nodes[i]);
            for (int w = 0; w < 8; ++w)
                t.push_back(words[w]);
        }
        moved = {40.0f, 10.0f, 0.0f, 1.0f};
        if (original)
            Original<void(__thiscall*)(void*, uint32_t, const void*)>(0x0057C3F0)(m, 0x101, &moved);
        else
            m->MoveNode(0x101, &moved);
        for (int i = 0; i < 10; ++i) { // (the boxes moving changed)
            const auto* words = reinterpret_cast<const uint32_t*>(&nodes[i]);
            for (size_t w = 0; w < sizeof(CRailNode) / 4; ++w)
                t.push_back(Rel(words[w]));
        }
        SetBoxes(10);
        {
            // Two nodes near in x and z but not y; four values near but the last.
            auto* a = Ptr<CRailNode>(0x2000);
            auto* b = Ptr<CRailNode>(0x2060);
            a->m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
            b->m_pos = {0.5f, 3.0f, 0.5f, 1.0f};
            t.push_back(original ? Original<bool(__stdcall*)(const void*, const void*)>(0x0057C5A0)(a, b) : Obj::NodesTogether(a, b));
            t.push_back(static_cast<uint32_t>(original ? Original<int(__cdecl*)(float, float, float, float, float)>(0x0057C3A0)(1.0f, 1.0f, 1.0f, 20.0f, 6.0f)
                                                       : Obj::NearlyEqual4(1.0f, 1.0f, 1.0f, 20.0f, 6.0f)));
            // No nodes but a count: none found.
            CRailNode* held = m->m_nodes;
            m->m_nodes = nullptr;
            t.push_back(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, uint32_t)>(0x0057CBD0)(m, 0x100u)
                                                                : m->FindNode(0x100u))));
            m->m_nodes = held;
        }
        // The feelers: which side has a wall, whether a rail is clear.
        const int hitPlans[][4] = {{0, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}, {2, 1, 0, 0}, {0, 2, 0, 0}};
        Mth::Vector direction = {1.0f, 0.2f, -0.5f, 0.0f};
        for (const auto& plan : hitPlans) {
            for (int dir = 0; dir < 2; ++dir) {
                g_hits.assign(plan, plan + 4);
                direction.x = dir ? -1.0f : 1.0f;
                t.push_back(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, const void*)>(0x0057CC30)(&nodes[dir * 3], &direction)
                                                           : nodes[dir * 3].GetWallSide(&direction)));
            }
        }
        const struct {
            int hits[4];
            float heights[2];
        } clearPlans[] = {{{1, 0, 0, 0}, {0, 0}},   {{0, 2, 0, 0}, {0, 0}},     {{0, 0, 0, 0}, {0, 0}},
                          {{0, 0, 1, 0}, {3, 0}},   {{0, 0, 2, 1}, {3, 3}},     {{0, 0, 1, 1}, {14.0f, 14.5f}},
                          {{0, 0, 1, 1}, {9, 14}},  {{0, 0, 1, 1}, {15.5f, 9}}, {{0, 0, 1, 1}, {6.0f, 15.0f}},
                          {{0, 0, 1, 1}, {16, 16}},  {{0, 0, 1, 1}, {12.0f, 7.5f}}};
        for (const auto& plan : clearPlans) {
            g_hits.assign(plan.hits, plan.hits + 4);
            g_hitHeights.assign(plan.heights, plan.heights + 2);
            t.push_back(original ? Original<bool(__cdecl*)(Mth::Vector, Mth::Vector)>(0x0057C600)(nodes[0].m_pos, nodes[1].m_pos)
                                 : Obj::RailClear(nodes[0].m_pos, nodes[1].m_pos));
        }
        *Ptr<uint32_t>(0x0E00 + 0x40) = 0xD443A2BC; // the park editor: always clear
        t.push_back(original ? Original<bool(__cdecl*)(Mth::Vector, Mth::Vector)>(0x0057C600)(nodes[2].m_pos, nodes[3].m_pos)
                             : Obj::RailClear(nodes[2].m_pos, nodes[3].m_pos));
        call0(0x0057D7A0, &CRailManager::RemoveOverlapsIfNotPark);
        *Ptr<uint32_t>(0x0E00 + 0x40) = 0;
        // Ladders.
        for (int k = 0; k < 6; ++k) {
            g_checks = {k % 3 == 0 ? 1 : 0, 1};
            Mth::Vector at = {30.0f + k, 2.0f * k, 30.5f - k, 1.0f};
            CRailNode* found = Ptr<CRailNode>(0x0FF0);
            t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, float, float, bool, void*, uint32_t, void*)>(0x0057D880)(
                                       m, &at, 3.0f, 4.0f + k, k & 1, Ptr<void>(kChecker), 0x77u + k, &found)
                                 : m->FindLadder(&at, 3.0f, 4.0f + k, k & 1, Ptr<void>(kChecker), 0x77u + k, &found));
            t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
            Mth::Vector from = {25.0f + k, 10.0f, 25.0f, 1.0f}, to = {35.0f, 12.0f - k, 34.0f + k, 1.0f};
            g_checks = {k % 2, 1};
            t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, const void*, void*, void*, void*)>(0x0057DA20)(
                                       m, &from, &to, Ptr<void>(kChecker), Ptr<void>(kRecords), &found)
                                 : m->FindLadderOnLine(&from, &to, Ptr<void>(kChecker), Ptr<Obj::Record24>(kRecords), &found));
            t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
        }
        // Rails near a line, grind rails, joining rails.
        for (int k = 0; k < 8; ++k) {
            Mth::Vector from = {5.0f + 9 * k, 14.0f, -4.0f + k, 1.0f}, to = {15.0f + 9 * k, 8.0f, 4.0f - k, 1.0f};
            Mth::Vector dir = {0.0f, -1.0f, k & 1 ? 0.5f : -0.5f, 0.0f};
            g_checks = {k % 3 != 1, 1, 0, 1};
            g_hits.clear();
            t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, const void*, const void*, void*, void*, float)>(0x0057DDA0)(
                                       m, &from, &to, &dir, Ptr<void>(kChecker), Ptr<void>(kRecords + 0x40), 5.0f + k)
                                 : m->FindRail(&from, &to, &dir, Ptr<void>(kChecker), Ptr<Obj::Record34>(kRecords + 0x40), 5.0f + k));
            Mth::Vector point = {};
            CRailNode* found = nullptr;
            g_hits = {0, 0, k % 4 == 0 ? 1 : 0, 0, 1, 0};
            t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, const void*, void*, void*, const void*, float, int)>(0x0057E2D0)(
                                       m, &from, &to, &point, &found, k == 3 ? &nodes[0] : nullptr, k & 2 ? 0.5f : 1.0f, (k % 3) - 1)
                                 : m->FindGrindRail(&from, &to, &point, &found, k == 3 ? &nodes[0] : nullptr, k & 2 ? 0.5f : 1.0f,
                                                    (k % 3) - 1));
            RecVector(reinterpret_cast<uint32_t>(&point));
            t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
            g_hits.clear();
            found = nullptr;
            t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, uint32_t, void*)>(0x0057E170)(m, &nodes[k], k & 1 ? 2u : 0u, &found)
                                 : m->FindJoiningRail(&nodes[k], k & 1 ? 2u : 0u, &found));
            t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
        }
        {
            // Small scenes on a second manager: ladders near a line (the closer one first, then a farther one;
            // TRG_Ladder03 counted 4 times as far; one going down), two rails near a line (the one above counted a
            // third nearer), rails overlapping (parallel, opposite, angled; the count printed), a ladder node with
            // both links.
            auto* scene = Ptr<CRailManager>(0x2200);
            std::memset(scene, 0, sizeof(CRailManager));
            auto* extra = Ptr<CRailNode>(0x2300);
            auto node = [&](int i, float x, float y, float z, int next, uint32_t flags, uint32_t name) {
                CRailNode* n = &extra[i];
                std::memset(n, 0, sizeof(CRailNode));
                n->m_pos = {x, y, z, 1.0f};
                n->m_next = next >= 0 ? &extra[next] : nullptr;
                n->m_flags = flags;
                n->m_name = name;
            };
            auto boxes = [&](int count) {
                for (int i = 0; i < count; ++i) {
                    CRailNode* n = &extra[i];
                    if (n->m_next)
                        n->m_next->m_prev = n;
                    const Mth::Vector& a = n->m_pos;
                    const Mth::Vector& b = n->m_next ? n->m_next->m_pos : n->m_pos;
                    n->m_min = {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z), 1.0f};
                    n->m_max = {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z), 1.0f};
                }
            };
            auto onLine = [&](int first, int count, Mth::Vector from, Mth::Vector to) {
                scene->m_nodes = &extra[first];
                scene->m_count = count;
                g_checks = {1, 1, 1, 1, 1, 1};
                CRailNode* found = nullptr;
                t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, const void*, void*, void*, void*)>(0x0057DA20)(
                                           scene, &from, &to, Ptr<void>(kChecker), Ptr<void>(kRecords), &found)
                                     : scene->FindLadderOnLine(&from, &to, Ptr<void>(kChecker), Ptr<Obj::Record24>(kRecords), &found));
                t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
            };
            constexpr uint32_t SA = Obj::RAIL_FLAG_ACTIVE, SL = Obj::RAIL_FLAG_LADDER | Obj::RAIL_FLAG_CLIMBING;
            node(0, 0.0f, 0.0f, 0.0f, 1, SA | SL, 0x300);
            node(1, 0.0f, 10.0f, 0.0f, -1, SA | SL, 0x301);
            node(2, 2.0f, 0.0f, 0.0f, 3, SA | SL, 0x302);
            node(3, 2.0f, 10.0f, 0.0f, -1, SA | SL, 0x303);
            node(4, 5.0f, 0.0f, 0.0f, 5, SA | SL, 0xF5B17B09);
            node(5, 5.0f, 10.0f, 0.0f, -1, SA | SL, 0x305);
            node(6, 0.0f, 10.0f, 0.0f, 7, SA | SL, 0x306);
            node(7, 0.0f, 0.0f, 0.0f, -1, SA | SL, 0x307);
            boxes(8);
            onLine(0, 4, {-1.0f, 5.0f, -1.0f, 1.0f}, {1.0f, 5.0f, 1.0f, 1.0f});
            onLine(4, 2, {0.0f, 5.0f, -1.0f, 1.0f}, {0.0f, 5.0f, 1.0f, 1.0f});
            onLine(6, 2, {-1.0f, 4.0f, -1.0f, 1.0f}, {1.0f, 6.0f, 1.0f, 1.0f});
            // A ladder node with both links: the next one is the other end.
            extra[2].m_prev = &extra[7];
            scene->m_nodes = &extra[0];
            scene->m_count = 4;
            for (int up : {0, 1}) {
                g_checks = {0, 1};
                Mth::Vector at = {2.0f, 1.0f, 0.5f, 1.0f};
                CRailNode* found = nullptr;
                t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, float, float, bool, void*, uint32_t, void*)>(0x0057D880)(
                                           scene, &at, 3.0f, 4.0f, up != 0, Ptr<void>(kChecker), 0x88u, &found)
                                     : scene->FindLadder(&at, 3.0f, 4.0f, up != 0, Ptr<void>(kChecker), 0x88u, &found));
                t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
            }
            // Two rails near a line.
            node(8, 0.0f, -1.2f, 0.0f, 9, SA, 0x308);
            node(9, 10.0f, -1.2f, 0.0f, -1, SA, 0x309);
            node(10, 0.0f, 3.0f, 0.0f, 11, SA, 0x30A);
            node(11, 10.0f, 3.0f, 0.0f, -1, SA, 0x30B);
            boxes(12);
            scene->m_nodes = &extra[8];
            scene->m_count = 4;
            {
                Mth::Vector from = {2.0f, 0.0f, -1.0f, 1.0f}, to = {8.0f, 0.0f, 1.0f, 1.0f}, dir = {0.0f, 1.0f, 0.0f, 0.0f};
                g_checks = {1, 1, 1, 1};
                g_hits.clear();
                t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, const void*, const void*, void*, void*, float)>(0x0057DDA0)(
                                           scene, &from, &to, &dir, Ptr<void>(kChecker), Ptr<void>(kRecords + 0x40), 5.0f)
                                     : scene->FindRail(&from, &to, &dir, Ptr<void>(kChecker), Ptr<Obj::Record34>(kRecords + 0x40), 5.0f));
                t.push_back(Rel(Ptr<uint32_t>(kRecords + 0x40)[12]));
            }
            // Grind rails: a vertical line (alignment 1); a good rail then a better-scoring one out of reach (none
            // found); a lone node's doubled distance against a later rail.
            auto grind = [&](int first, int count, Mth::Vector from, Mth::Vector to) {
                scene->m_nodes = &extra[first];
                scene->m_count = count;
                g_hits.clear();
                Mth::Vector point = {};
                CRailNode* found = nullptr;
                t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, const void*, void*, void*, const void*, float, int)>(0x0057E2D0)(
                                           scene, &from, &to, &point, &found, nullptr, 1.0f, 0)
                                     : scene->FindGrindRail(&from, &to, &point, &found, nullptr, 1.0f, 0));
                RecVector(reinterpret_cast<uint32_t>(&point));
                t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
            };
            node(24, 0.0f, 5.0f, 0.0f, 25, SA, 0x318);
            node(25, 10.0f, 5.0f, 0.0f, -1, SA, 0x319);
            node(26, 10.0f, 3.0f, -5.0f, 27, SA, 0x31A);
            node(27, 10.0f, 3.0f, 5.0f, -1, SA, 0x31B);
            node(28, 5.5f, 8.5f, -2.18f, 29, SA, 0x31C);
            node(29, 14.5f, 8.5f, 2.18f, -1, SA, 0x31D);
            node(30, 3.0f, 4.0f, 0.0f, -1, SA, 0x31E);
            node(31, 15.0f, 4.46f, -5.0f, 32, SA, 0x31F);
            node(32, 15.0f, 4.46f, 5.0f, -1, SA, 0x320);
            boxes(33);
            grind(24, 2, {5.0f, 0.0f, 7.0f, 1.0f}, {5.0f, 10.0f, 7.0f, 1.0f});
            grind(26, 4, {0.0f, 0.0f, 0.0f, 1.0f}, {20.0f, 0.0f, 0.0f, 1.0f});
            grind(30, 3, {0.0f, 0.0f, 0.0f, 1.0f}, {20.0f, 0.0f, 0.0f, 1.0f});
            // A joining rail: the node there is excluded by its flags, its previous one isn't.
            node(33, 90.0f, 0.0f, 100.0f, 34, SA, 0x321);
            node(34, 100.0f, 0.0f, 100.0f, 35, SA | 2, 0x322);
            node(35, 110.0f, 0.0f, 100.0f, -1, SA, 0x323);
            node(36, 100.0f, 0.0f, 100.0f, -1, SA, 0x324);
            boxes(37);
            scene->m_nodes = &extra[33];
            scene->m_count = 4;
            {
                CRailNode* found = nullptr;
                g_hits.clear();
                t.push_back(original ? Original<bool(__thiscall*)(void*, const void*, uint32_t, void*)>(0x0057E170)(scene, &extra[36], 2u, &found)
                                     : scene->FindJoiningRail(&extra[36], 2u, &found));
                t.push_back(Rel(reinterpret_cast<uint32_t>(found)));
            }
            // Overlapping rails: parallel and close (the lower goes), opposite and close, angled.
            node(12, 0.0f, 10.0f, 0.0f, 13, SA, 0x30C);
            node(13, 20.0f, 10.0f, 0.0f, -1, SA, 0x30D);
            node(14, 2.0f, 11.0f, 1.0f, 15, SA, 0x30E);
            node(15, 18.0f, 11.0f, 1.0f, -1, SA, 0x30F);
            node(16, 20.0f, 30.0f, 50.0f, 17, SA, 0x310);
            node(17, 0.0f, 30.0f, 50.0f, -1, SA, 0x311);
            node(18, 1.0f, 29.0f, 51.0f, 19, SA, 0x312);
            node(19, 19.0f, 29.0f, 51.0f, -1, SA, 0x313);
            node(20, 0.0f, 50.0f, 0.0f, 21, SA, 0x314);
            node(21, 20.0f, 50.0f, 3.5f, -1, SA, 0x315);
            node(22, 1.0f, 50.5f, 0.0f, 23, SA, 0x316);
            node(23, 19.0f, 50.5f, 0.0f, -1, SA, 0x317);
            boxes(24);
            scene->m_nodes = &extra[12];
            scene->m_count = 12;
            g_printfArgs = 1;
            if (original)
                Original<void(__thiscall*)(void*)>(0x0057D260)(scene);
            else
                scene->RemoveOverlaps();
            g_printfArgs = 0;
            for (int i = 12; i < 24; ++i)
                t.push_back(extra[i].m_flags);
        }
        // Overlaps.
        call0(0x0057D260, &CRailManager::RemoveOverlaps);
        for (int i = 0; i < 10; ++i)
            t.push_back(nodes[i].m_flags);
        // Building from a node array.
        auto* other = Ptr<CRailManager>(kOther);
        std::memset(other, 0, sizeof(CRailManager));
        auto* array = Ptr<Script::CArray>(kArrays);
        array->m_type = Script::ESYMBOLTYPE_STRUCTURE;
        array->m_size = 6;
        array->m_elements = Ptr<uint32_t>(kArrays + 0x10);
        const uint32_t classes[] = {0x8E6B02AD, 0x8E6B02AD, 0x30C19600, 0x12345, 0x8E6B02AD, 0x30C19600};
        for (int i = 0; i < 6; ++i) {
            uint32_t node = At(kParams + 0x10 * i);
            array->m_elements[i] = node;
            g_params[{node, 0x12B4E660}] = classes[i];
            g_nodeNames[node] = 0x4000u * (i % 2) + 0x200u + i / 2; // (pairs share a hash slot: probing)
            g_nodesByName[g_nodeNames[node]] = node;
            g_positions[node] = {10.0f * i, 2.0f * i, -3.0f * i, 1.0f};
            g_params[{node, 0xA1DC81F9}] = 0xA0 + i;
            if (i == 1)
                g_params[{node, 0x1A3A966B}] = 0xC1;
            if (i == 2)
                g_params[{node, 0x7321A8D6}] = 0xC84243DA; // a ladder
            g_params[{node, 0x54CF8532}] = 0x50 + i;
            if (i == 1)
                g_flagsSet.insert({node, 0x1645B830}); // TrickObject
            if (i != 4)
                g_flagsSet.insert({node, 0x7C2552B9}); // CreatedAtStart
            if (i == 0)
                g_flagsSet.insert({node, 0x4DE721DE}), g_flagsSet.insert({node, 0xF4C67F0F});
            if (i == 2)
                g_flagsSet.insert({node, 0xF6DD21CA}), g_flagsSet.insert({node, 0xF22BFC6D});
            if (i == 5)
                g_flagsSet.insert({node, 0x5E9CE29B}), g_flagsSet.insert({node, 0x4DE721DE});
        }
        g_skippedNodes.insert(At(kParams + 0x10 * 4));
        // Links: 0 -> 1 -> 5, 2 -> 1 (1 already has a next: 2 isn't DefaultLine), 5 none.
        auto links = [&](int node, std::vector<int> targets, uint32_t at) {
            auto* links = Ptr<Script::CArray>(at);
            links->m_type = Script::ESYMBOLTYPE_NAME;
            links->m_size = static_cast<uint32_t>(targets.size());
            links->m_elements = Ptr<uint32_t>(at + 0x10);
            if (targets.size() == 1)
                links->m_single = g_nodeNames[At(kParams + 0x10 * targets[0])];
            else
                for (size_t k = 0; k < targets.size(); ++k)
                    links->m_elements[k] = g_nodeNames[At(kParams + 0x10 * targets[k])];
            g_params[{At(kParams + 0x10 * node), 0x2E7D5EE7}] = At(at);
        };
        links(0, {1}, kArrays + 0x40);
        links(1, {5, 2}, kArrays + 0x80);
        links(2, {1, 0}, kArrays + 0xC0);
        links(5, {1}, kArrays + 0x100); // (5 is DefaultLine: it becomes 1's previous over 0)
        for (int all : {0, 1}) {
            g_integers[0xB675F8F6] = all;
            if (original)
                Original<void(__thiscall*)(void*, void*)>(0x0057E960)(other, array);
            else
                other->AddFromNodeArray(array);
            t.push_back(static_cast<uint32_t>(other->m_count));
            if (all == 0) {
                // (clear it again for the second build)
                if (original)
                    Original<void(__thiscall*)(void*)>(0x0057C330)(other);
                else
                    other->Cleanup();
            }
        }
        array->m_size = 1;
        array->m_single = At(kParams + 0x10 * 3); // no rails at all
        if (original)
            Original<void(__thiscall*)(void*, void*)>(0x0057E960)(Ptr<void>(kOther + 0x60), array);
        else
            Ptr<CRailManager>(kOther + 0x60)->AddFromNodeArray(array);
        m->m_onObject = true; // (cleared too)
        call0(0x0057C330, &CRailManager::Cleanup);
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "rail manager");
}

} // namespace

SELFTEST(Early, TestRailManager, ViewerTestKit::Cleanup);
