// Self-tests for the park generator (src/game/ed/park_scene.cpp, park_nodes.cpp): each function runs on a stand-in
// generator (pieces, source pieces, rails, restarts, a node array of stand-in nodes) twice - the original, then ours
// - with every game function it calls stood in for by recording code (and scripted results), and the two runs'
// calls, results and memory compared.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/ed/park_scene.h"
#include "game/lst/hashtable.h"
#include "game/nx/coll_tri.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using namespace Ed;

constexpr int kNodes = 24;

// A stand-in node of the node array: what its getters find.
struct NodeSpec {
    uint32_t cls, name, cluster, type, terrain;
    uint32_t link; // its links' first element (0: no links=)
    bool hasPos, hasAngles, noClimbing;
    Mth::Vector pos, angles;
};

// Everything the code under test touches.
struct World {
    CParkGenerator gen;
    ClonedPiece pieces[8];
    SourcePiece sources[6];
    Rail rails[8];
    RailPoint points[20];
    void* vtable[2];
    uint8_t sourceScene[0x100];
    uint8_t scene[0x100];
    uint8_t table[sizeof(Lst::HashTable)];
    Lst::HashTable::Item items[8];
    uint8_t sectors[8][0x20];
    Mth::Vector boxes[8][2];
    uint8_t geometry[8][0x10]; // +0 the vtable, +8 the collision faces
    void* geometryVtable[64];
    Nx::CCollObjTriData coll[8];
    uint8_t vertexBytes[8][8];
    CMetaPiece metas[4];
    SMetaDescriptor extra[2];
    CMapListNode list[6];
    void* metaVtable[3];
    alignas(16) uint8_t memManager[0x600];
    alignas(16) uint8_t heap[0x3000];
    uint32_t heapUsed;
    uint8_t nodes[kNodes][8];
    Script::CArray nodeArray;
    Script::CArray linkArrays[kNodes];
    uint8_t railEditor[0x10];
    uint8_t gapManager[0x10];
    uint8_t nodeManager[0x10];
    uint8_t symbol[0x14];
};
alignas(16) World g_world;
NodeSpec g_spec[kNodes];
int g_iterCount; // the sector table's items

// Scripted results by function (address): the next one is taken at each call (0 when they run out).
std::map<uint32_t, std::deque<uint32_t>> g_script;
uint32_t Next(uint32_t function)
{
    auto& queue = g_script[function];
    if (queue.empty())
        return 0;
    uint32_t value = queue.front();
    queue.pop_front();
    return value;
}
template <typename T>
uint32_t U(T* pointer)
{
    return reinterpret_cast<uint32_t>(pointer);
}
uint32_t F(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}
float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// Pointers to the caller's locals differ between the original and ours: they're recorded as one marker.
uint32_t Norm(uint32_t value)
{
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    auto low = reinterpret_cast<uint32_t>(tib->StackLimit), high = reinterpret_cast<uint32_t>(tib->StackBase);
    return value >= low && value < high ? 0x57AC0000u : value;
}

// Our function registered at a game address (from the replacements' own table), or 0.
uint32_t OursAt(uint32_t address)
{
    static std::map<uint32_t, void*> ours;
    if (ours.empty()) {
        for (const ArgumentSizeCheck& c : CheckArgumentSizes()) {
            if (!c.replacement.fallback)
                ours[c.replacement.address] = c.replacement.function;
        }
    }
    auto it = ours.find(address);
    return it == ours.end() ? 0u : Resolve(it->second);
}

// Stand in for the function at `at` (and ours of it): record the address, ECX (unless `noEcx`) and `n` stack
// arguments, then return `result` (or the scripted value).
// arguments (bit i of `strings`: argument i is a string, recorded by its text).
void Rec(StandIns& s, uint32_t id, uint16_t pop, int n, std::function<uint32_t(Regs&)> result = nullptr,
         bool noEcx = false, uint32_t strings = 0)
{
    void* thunk = s.Thunk(pop, [=](Regs& r) {
        Record({id, noEcx ? 0u : Norm(r.ecx)});
        for (int i = 0; i < n; ++i)
            Record({(strings >> i) & 1 ? Hash(reinterpret_cast<const char*>(r.args[i])) : Norm(r.args[i])});
        return result ? result(r) : Next(id);
    });
    s.Bytes(id, Branch(0xE9, id, thunk));
    if (uint32_t ours = OursAt(id))
        s.Bytes(ours, Branch(0xE9, ours, thunk));
}

int NodeIndex(uint32_t node)
{
    uint32_t base = U(g_world.nodes);
    if (node < base || node >= base + sizeof(g_world.nodes))
        return -1;
    return static_cast<int>((node - base) / 8);
}
int LinkIndex(uint32_t array)
{
    uint32_t base = U(g_world.linkArrays);
    if (array < base || array >= base + sizeof(g_world.linkArrays))
        return -1;
    return static_cast<int>((array - base) / sizeof(Script::CArray));
}
// A script array's contents, as the stand-ins of AddArray record it.
void RecordArray(uint32_t pointer)
{
    auto* array = reinterpret_cast<Script::CArray*>(pointer);
    if (!array) {
        Record({0xA0A0});
        return;
    }
    Record({array->m_type, array->m_size, array->m_size == 1 ? array->m_single : 0u});
}
uint32_t Bump(uint32_t size)
{
    World& w = g_world;
    uint32_t at = (w.heapUsed + 15) & ~15u;
    w.heapUsed = at + size;
    if (w.heapUsed > sizeof(w.heap))
        return 0;
    std::memset(w.heap + at, 0xCD, size);
    return U(w.heap + at);
}

void StandInGame(StandIns& s)
{
    World& w = g_world;
    // memory
    Rec(s, 0x005BAFC0, 0xC, 3, [](Regs& r) { return Bump(r.args[0]); });
    Rec(s, 0x005BABA0, 4, 1, [](Regs&) { return 0u; });
    Rec(s, 0x005BB0E0, 4, 1, [](Regs&) { return 0u; });
    Rec(s, 0x005BB110, 0, 0, [](Regs&) { return 0u; });
    Rec(s, 0x0059E100, 0, 0, [](Regs&) { return 0x4EA90000u; }, true);
    Rec(s, 0x0059E170, 0, 0, nullptr, true);
    Rec(s, 0x0059E5C0, 0, 2, [](Regs& r) { return Hash(reinterpret_cast<const char*>(r.args[0])) & 0xFFFF; }, true, 1);
    // scenes, sectors, geometry
    Rec(s, 0x0048C750, 0, 1, [&w](Regs&) { return U(w.sourceScene); }, true, 1);
    Rec(s, 0x0048CC80, 0, 3, [&w](Regs&) { return U(w.scene); }, true, 1);
    Rec(s, 0x0048C510, 0, 1, [&w](Regs& r) { return U(w.sectors[r.args[0] % 8]); }, true);
    Rec(s, 0x0049CF80, 4, 1, [](Regs&) { return 1u; });
    Rec(s, 0x0049D030, 0, 0);
    Rec(s, 0x0049D780, 4, 1, [](Regs&) {
        Record({GameVariable<uint8_t>(0x0067F398)});
        return 0u;
    });
    Rec(s, 0x0049DF30, 0x14, 5, [](Regs& r) { return r.args[0] + 0x100u + r.args[2] * 0x10 + r.args[3] * 0x20; });
    Rec(s, 0x0049E6C0, 4, 1, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[0]);
        Record({F(v[0]), F(v[1]), F(v[2]), F(v[3])});
        return 0u;
    });
    Rec(s, 0x0049E760, 4, 1);
    Rec(s, 0x0049E6A0, 0, 0, [&w](Regs& r) {
        int i = static_cast<int>((r.ecx - U(w.sectors)) / 0x20);
        return U(w.boxes[i & 7]);
    });
    // (a sector's geometry, +0x14, and the geometry's collision faces, +8, are read as they are: set in the world)
    Rec(s, 0x0048DF70, 0, 0);
    Rec(s, 0x0048DF80, 4, 1, [](Regs& r) {
        auto* out = reinterpret_cast<Mth::Vector*>(r.args[0]);
        int n = static_cast<int>(Next(0x0048DF80 + 1));
        for (int i = 0; i < n; ++i)
            out[i] = {100.5f * i - 700.0f, 3.0f * i, -250.25f * i + 90.0f, 1.0f};
        return 0u;
    });
    Rec(s, 0x0049F3E0, 4, 1, [](Regs& r) {
        auto* out = reinterpret_cast<uint32_t*>(r.args[0]);
        int n = static_cast<int>(Next(0x0049F3E0 + 1));
        for (int i = 0; i < n; ++i)
            out[i] = 0x80402010u + 0x0B0D0703u * i;
        return 0u;
    });
    Rec(s, 0x0048DFA0, 4, 1, [](Regs& r) {
        auto* colours = reinterpret_cast<uint32_t*>(r.args[0]);
        int n = static_cast<int>(Next(0x0048DFA0 + 1));
        for (int i = 0; i < n; ++i)
            Record({colours[i]});
        return 0u;
    });
    Rec(s, 0x0040B230, 8, 2, [](Regs& r) {
        auto* out = reinterpret_cast<float*>(r.args[0]);
        int i = static_cast<int>(r.args[1]);
        out[0] = 37.5f * i - 1200.0f;
        out[1] = 12.0f;
        out[2] = -80.0f * i + 333.0f;
        out[3] = 0.0f;
        return r.args[0];
    });
    Rec(s, 0x00588190, 0, 0, [&w](Regs& r) {
        auto* table = reinterpret_cast<Lst::HashTable*>(r.ecx);
        int bucket = ++table->m_iterBucket;
        if (bucket >= g_iterCount)
            return 0u;
        table->m_iterItem = &w.items[bucket];
        return 1u;
    });
    // the rail editor, the goal editor, the gap manager
    Rec(s, 0x004F4970, 0, 0, [&w](Regs&) { return U(w.railEditor); }, true);
    Rec(s, 0x004F45C0, 0, 0);
    Rec(s, 0x004F45E0, 0, 0);
    Rec(s, 0x004F6650, 0, 0, [](Regs&) {
        Record({GameVariable<uint8_t>(0x0067F398)});
        return 0u;
    });
    Rec(s, 0x004F3C70, 0, 0);
    Rec(s, 0x004F6920, 0xC, 3, [](Regs& r) {
        auto* index = reinterpret_cast<int*>(r.args[1]);
        Record({static_cast<uint32_t>(*index)});
        *index += static_cast<int>(Next(0x004F6920 + 1));
        return 0u;
    });
    Rec(s, 0x004E8F50, 0, 3, [](Regs& r) {
        auto* index = reinterpret_cast<int*>(r.args[1]);
        Record({static_cast<uint32_t>(*index)});
        *index += 5;
        return 0u;
    }, true);
    Rec(s, 0x005A34E0, 0, 2, nullptr, true);
    Rec(s, 0x0058CA60, 4, 1);
    Rec(s, 0x0058CEC0, 4, 1);
    // the generator's own code outside the range
    Rec(s, 0x005923C0, 8, 2);
    Rec(s, 0x00592570, 8, 2);
    Rec(s, 0x00592010, 0, 0);
    Rec(s, 0x005925E0, 4, 1);
    Rec(s, 0x005905C0, 0, 0, [&w](Regs& r) {
        auto* source = reinterpret_cast<SourcePiece*>(r.ecx);
        std::memset(source, 0, sizeof(SourcePiece));
        source->m_vtable = w.vtable;
        source->m_flags = 1;
        return r.ecx;
    });
    Rec(s, 0x00590670, 8, 2);
    Rec(s, 0x00590260, 8, 2);
    Rec(s, 0x00595200, 0, 0, [](Regs&) { return 0x9A2C0000u; }, true);
    // the rail sets
    Rec(s, 0x0058B0C0, 0xC, 3);
    Rec(s, 0x0058B700, 0, 0);
    Rec(s, 0x0058B280, 0, 0);
    // rail `index` named `name`: rails 3, 4, 5 (as many as the name's low 2 bits)
    Rec(s, 0x0058B090, 8, 2, [&w](Regs& r) {
        int index = static_cast<int>(r.args[1]);
        return index < static_cast<int>(r.args[0] & 3) ? U(&w.rails[3 + index]) : 0u;
    });
    Rec(s, 0x0058B370, 0, 0, [&w](Regs&) {
        Rail* rail = &w.rails[Next(0x0058B370 + 1) & 7];
        std::memset(rail, 0, sizeof(Rail));
        rail->m_flags = Rail::kClimbable;
        return U(rail);
    });
    Rec(s, 0x0058B2D0, 0, 0, [&w](Regs&) {
        RailPoint* point = &w.points[Next(0x0058B2D0 + 1) % 20];
        std::memset(point, 0, sizeof(RailPoint));
        return U(point);
    });
    Rec(s, 0x0058B010, 4, 1, [](Regs& r) {
        auto* rail = reinterpret_cast<Rail*>(r.args[0]);
        Record({rail->m_class, rail->m_flags, rail->m_name});
        return 0u;
    });
    Rec(s, 0x0058B5B0, 4, 1);
    Rec(s, 0x0058B070, 0, 0);
    Rec(s, 0x0058AFC0, 4, 1, [](Regs& r) {
        auto* point = reinterpret_cast<RailPoint*>(r.args[0]);
        Record({HashBytes(reinterpret_cast<uint8_t*>(point), sizeof(RailPoint))});
        return 0u;
    });
    Rec(s, 0x0058B040, 0, 0, [](Regs& r) { return 3u + (r.ecx & 0xFF) % 5; });
    Rec(s, 0x0058B420, 0x14, 5, [](Regs& r) {
        auto* v = reinterpret_cast<float*>(r.args[1]);
        Record({F(v[0]), F(v[1]), F(v[2])});
        return 0u;
    });
    // the node manager and symbols
    Rec(s, 0x0045D270, 4, 1);
    Rec(s, 0x0045D1C0, 4, 1, [](Regs&) { return 1u; });
    Rec(s, 0x0045D520, 8, 2, [](Regs&) { return 1u; });
    Rec(s, 0x0045D140, 4, 1);
    Rec(s, 0x00478CC0, 0, 1, [](Regs&) { return Next(0x00478CC0); }, true);
    Rec(s, 0x004711D0, 0, 1, nullptr, true);
    Rec(s, 0x0045C210, 0, 0, [&w](Regs&) { return U(&w.nodeArray); }, true);
    Rec(s, 0x004090D0, 8, 2, [&w](Regs&) { return U(&w.nodeArray); });
    Rec(s, 0x004753C0, 0, 0, nullptr, true);
    Rec(s, 0x00475790, 0, 5, nullptr, true);
    s.At({0x006234FE}, 0, StandInSprintf);

    // the nodes
    Rec(s, 0x0046CC30, 4, 1, [&w](Regs& r) {
        if (r.ecx == U(&w.nodeArray))
            return U(w.nodes[r.args[0] % kNodes]);
        int link = LinkIndex(r.ecx);
        if (link >= 0)
            return g_spec[link].link;
        return Next(0x0046CC30);
    });
    Rec(s, 0x00476950, 0xC, 3, [](Regs& r) {
        int node = NodeIndex(r.ecx);
        if (node < 0)
            return 0u;
        const NodeSpec& spec = g_spec[node];
        uint32_t value = 0;
        switch (r.args[0]) {
        case 0x12B4E660: value = spec.cls; break;
        case 0xA1DC81F9: value = spec.name; break;
        case 0x1A3A966B: value = spec.cluster; break;
        case 0x7321A8D6: value = spec.type; break;
        case 0x54CF8532: value = spec.terrain; break;
        default: break;
        }
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = value;
        return 1u;
    });
    Rec(s, 0x00476FB0, 0xC, 3, [](Regs& r) {
        int node = NodeIndex(r.ecx);
        if (node < 0)
            return 0u;
        const NodeSpec& spec = g_spec[node];
        bool pos = r.args[0] == 0x7F261953;
        if (pos ? !spec.hasPos : !spec.hasAngles)
            return 0u;
        *reinterpret_cast<Mth::Vector*>(r.args[1]) = pos ? spec.pos : spec.angles;
        return 1u;
    });
    Rec(s, 0x00476860, 0xC, 3, [&w](Regs& r) {
        int node = NodeIndex(r.ecx);
        if (node < 0 || !g_spec[node].link)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = U(&w.linkArrays[node]);
        return 1u;
    });
    Rec(s, 0x00476B40, 4, 1, [](Regs& r) {
        int node = NodeIndex(r.ecx);
        return node >= 0 && g_spec[node].noClimbing ? 1u : 0u;
    });
    Rec(s, 0x00477F70, 0x14, 5);
    Rec(s, 0x00477E20, 0x10, 4);
    Rec(s, 0x00477D40, 8, 2);
    Rec(s, 0x004779D0, 8, 2, [](Regs&) { return 0u; }, false, 2);
    Rec(s, 0x00478390, 0xC, 3, [](Regs&) { return 0u; }, false, 4);
    Rec(s, 0x004783C0, 0xC, 3);
    Rec(s, 0x00477C30, 8, 2, [](Regs&) { return 0u; }, false, 1);
    Rec(s, 0x00478B00, 8, 2, [](Regs& r) {
        RecordArray(r.args[1]);
        return 0u;
    });
}

// A starting generator: pieces and source pieces in lists, restarts, lights, a node manager.
void BasicWorld()
{
    World& w = g_world;
    CParkGenerator& d = w.gen;
    for (int i = 0; i < 6; ++i) {
        SourcePiece& s = w.sources[i];
        s.m_vtable = w.vtable;
        s.m_name = 0x50C00000u + i * 0x11;
        s.m_flags = (i == 1 ? SourcePiece::kGameObject : 0u) | (i == 2 ? 0xC00u : 0u) | (i == 3 ? 0x4000u : 0u) |
                    (i == 5 ? 0x4000u : 0u);
        s.mp_next = i < 5 ? &w.sources[i + 1] : nullptr;
        s.m_triggerScript = i == 4 ? 0x7C1660u : 0u;
        s.m_numRailPoints = 3 + i;
        s.m_numRailSegments = 2 + i * 2;
    }
    for (int i = 0; i < 8; ++i) {
        ClonedPiece& p = w.pieces[i];
        p.m_vtable = w.vtable;
        p.m_sector = 0x5EC70000u + i;
        p.m_flags = (i == 1 ? 0x1000u : 0u) | (i == 2 ? 0x1004u : 0u) | (i == 3 ? 0x20u : 0u) | (i == 4 ? 0x40u : 0u) |
                    (i == 5 ? 0x1100u : 0u) | (i == 6 ? 0x80u : 0u) | 0x8u;
        p.mp_next = i < 6 ? &w.pieces[i + 1] : nullptr;
        p.m_id = 0x20u + i;
        p.mp_source = &w.sources[i % 6];
        p.m_pos = {120.0f * i - 300.0f, 48.0f * (i % 3), -60.0f * i + 10.0f, 1.0f};
        p.m_rot = i % 4;
    }
    d.mp_pieces = &w.pieces[0];
    d.mp_sources = &w.sources[0];
    d.mp_scene = w.scene;
    d.mp_sourceScene = w.sourceScene;
    d.m_flags = 0x8 | 0x2 | 0x4;
    d.m_nextPieceId = 0x29;
    d.m_maxPlayers = 4;
    d.m_numLights = 3;
    d.m_ambient[0] = 0.75f;
    d.m_ambient[1] = 0.5f;
    d.m_ambient[2] = 0.33f;
    d.m_falloff[0] = 1200.5f;
    d.m_falloff[1] = 900.25f;
    d.m_falloff[2] = 3000.0f;
    for (int i = 0; i < 16; ++i)
        d.m_lights[i] = {-400.0f + 210.0f * i, 50.0f, 300.0f - 150.0f * i, 1.0f};
    for (int i = 0; i < CParkGenerator::kNumRestarts; ++i) {
        RestartPoint& r = d.m_restarts[i];
        r.m_pos = {10.0f * i + 0.5f, -4.0f * i, 7.25f * i, 1.0f};
        r.m_rot = i % 4;
        r.m_area = GridDims::Make(static_cast<uint8_t>(3 + i), static_cast<int8_t>(i % 3 - 1), static_cast<uint8_t>(20 - i),
                                  1, 1, 2);
    }
    // the rails (2 rails of the placed pieces)
    for (int i = 0; i < 20; ++i) {
        RailPoint& p = w.points[i];
        p.m_type = 0x7E00u + i;
        p.m_terrain = 0x7EA0u + i;
        p.m_pos = {33.0f * i - 100.0f, 5.5f * i, -17.0f * i + 2.0f, 1.0f};
        p.m_angles = {0.0f, 0.25f * i, 0.0f, 0.0f};
        p.m_cluster = i % 3 == 1 ? 0x20u + i : 0u;
        p.mp_next = nullptr;
    }
    w.points[0].mp_next = &w.points[1];
    w.points[1].mp_next = &w.points[2];
    w.points[3].mp_next = &w.points[4];
    w.points[5].mp_next = &w.points[6];
    auto rail = [&w](int i, uint32_t cls, uint8_t flags, uint32_t name, int first, int count, Rail* next) {
        Rail& r = w.rails[i];
        r.m_class = cls;
        r.m_flags = flags;
        r.m_name = name;
        r.mp_points = &w.points[first];
        r.mp_last = &w.points[first + count - 1];
        r.m_numPoints = static_cast<uint16_t>(count);
        r.mp_next = next;
    };
    rail(0, 0x8E6B02AD, Rail::kClimbable, 0x50C00000u, 0, 3, &w.rails[1]);
    rail(1, 0x30C19600, Rail::kLoop, 0x50C00011u, 3, 2, &w.rails[2]);
    rail(2, 0x8E6B02AD, Rail::kLoop | Rail::kClimbable, 0x50C00022u, 5, 2, nullptr);
    d.m_rails.mp_rails = &w.rails[0];
    // the scenes' sector tables
    auto* table = reinterpret_cast<Lst::HashTable*>(w.table);
    *reinterpret_cast<uint32_t*>(w.sourceScene + 0xD0) = U(table);
    *reinterpret_cast<uint32_t*>(w.sourceScene + 0x88) = 0x7E1D0000u;
    for (int i = 0; i < 8; ++i) {
        w.items[i].value = w.sectors[i];
        *reinterpret_cast<uint32_t*>(w.sectors[i] + 4) = 0x5EC00000u + i * 3;
        w.boxes[i][0] = {-50.0f * i - 1.0f, -12.0f * i + 3.0f, 15.0f * i - 61.0f, 1.0f};
        w.boxes[i][1] = {70.0f * i + 9.0f, 17.0f * i - 7.0f + (i == 3 ? 60.0f : 0.0f), 33.0f * i + 1.0f, 1.0f};
        *reinterpret_cast<uint32_t*>(w.sectors[i] + 0x14) = U(w.geometry[i]);
        *reinterpret_cast<uint32_t*>(w.geometry[i]) = U(w.geometryVtable);
        *reinterpret_cast<uint32_t*>(w.geometry[i] + 8) = U(&w.coll[i]);
        w.coll[i].m_numVertices = static_cast<uint16_t>(i % 4 + 1);
        w.coll[i].m_vertexBytes = w.vertexBytes[i];
        for (int j = 0; j < 8; ++j)
            w.vertexBytes[i][j] = static_cast<uint8_t>(0x30 + 0x11 * j + i);
    }
    // the geometry's virtual slots the generator calls: the vertex count (36), positions (37), colours read (38) and
    // set (40) - the stand-ins' addresses
    w.geometryVtable[36] = reinterpret_cast<void*>(0x0048DF70);
    w.geometryVtable[37] = reinterpret_cast<void*>(0x0048DF80);
    w.geometryVtable[38] = reinterpret_cast<void*>(0x0049F3E0);
    w.geometryVtable[40] = reinterpret_cast<void*>(0x0048DFA0);
    // the metapieces of BuildRails
    w.metaVtable[0] = nullptr; // (a recording thunk: piece `index`)
    for (int i = 0; i < 4; ++i) {
        CMetaPiece& m = w.metas[i];
        m.m_vtable = w.metaVtable;
        m.m_flags = (i == 1 ? 0u : CMetaPiece::kInPark) | (i == 2 ? CMetaPiece::kNoRails : 0u) |
                    (i == 3 ? CMetaPiece::kInnerShell : 0u);
        m.m_num_used_entries = static_cast<uint32_t>(i == 3 ? 6 : i + 1);
        w.list[i].mp_meta = &m;
        w.list[i].mp_next = i < 3 ? &w.list[i + 1] : nullptr;
    }
    // the node array: stand-in nodes
    w.nodeArray.m_size = kNodes;
    for (int i = 0; i < kNodes; ++i)
        w.linkArrays[i].m_size = 1;
    GameVariable<void*>(0x007D1800) = w.memManager;
    GameVariable<uint8_t>(0x007CC979) = 1;
    *reinterpret_cast<uint32_t*>(w.memManager + 0x110) = 0x70D00000u; // the top-down and bottom-up heaps
    *reinterpret_cast<uint32_t*>(w.memManager + 0x114) = 0xB0770000u;
    GameVariable<void*>(0x007CE5D4) = w.gapManager;
    GameVariable<void*>(0x006F0D84) = w.nodeManager;
    std::memset(reinterpret_cast<void*>(0x006F22B4), 0, 16); // the script arrays' pools: none (the heap)
    GameVariable<int>(0x007CE600) = 0x55;
}

using Setup = std::function<void()>;
using Call = std::function<uint32_t(bool original)>;

// Run `call` on the original and on ours from the same `setup`; compare calls, results and memory.
void RunCase(const char* what, Setup setup, Call call, std::vector<std::pair<uint32_t, int>> standInOurs = {})
{
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        std::memset(&g_world, 0, sizeof(g_world));
        std::memset(g_spec, 0, sizeof(g_spec));
        g_script.clear();
        g_iterCount = 0;
        BasicWorld();
        setup();
        auto* s = new StandIns;
        StandInGame(*s);
        for (const auto& [address, pop] : standInOurs)
            Rec(*s, address, static_cast<uint16_t>(pop), pop / 4);
        // the pieces' deleting destructor (slot 1), a metapiece's piece (slot 0)
        g_world.vtable[1] = s->Thunk(4, [](Regs& r) {
            Record({0xDE1E7E, r.ecx, r.args[0]});
            return 0u;
        });
        g_world.metaVtable[0] = s->Thunk(4, [](Regs& r) {
            Record({0x9E7E, r.ecx, r.args[0]});
            uint32_t meta = (r.ecx - U(g_world.metas)) / sizeof(CMetaPiece);
            return U(&g_world.pieces[(r.args[0] * 3 + meta * 2 + 2) % 8]);
        });
        g_trace.clear();
        uint32_t result = call(run == 0);
        delete s;
        traces[run] = g_trace;
        traces[run].push_back(0xEEEEEEEE);
        traces[run].push_back(Norm(result));
        g_world.vtable[1] = nullptr;
        g_world.metaVtable[0] = nullptr;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_world);
        traces[run].insert(traces[run].end(), words, words + sizeof(g_world) / 4);
        traces[run].push_back(static_cast<uint32_t>(GameVariable<int>(0x007CE600)));
        traces[run].push_back(GameVariable<uint8_t>(0x0067F398));
    }
    Compare(traces[0], traces[1], what);
}

template <typename Fn, typename... A>
uint32_t Game(uint32_t address, A... args)
{
    using R = decltype(GameFunction<Fn>(address)(args...));
    if constexpr (std::is_void_v<R>) {
        GameFunction<Fn>(address)(args...);
        return 0;
    } else if constexpr (std::is_pointer_v<R>) {
        return reinterpret_cast<uint32_t>(GameFunction<Fn>(address)(args...));
    } else {
        return static_cast<uint32_t>(GameFunction<Fn>(address)(args...));
    }
}

CParkGenerator* Gen() { return reinterpret_cast<CParkGenerator*>(&g_world.gen); }
using G = CParkGenerator*;

std::vector<Saved> SavedGlobals()
{
    return {{0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x007CE5D4, 4, {}}, {0x006F0D84, 4, {}},
            {0x006F22B4, 16, {}}, {0x007CE600, 4, {}}, {0x0067F398, 1, {}}};
}

// --- the restarts, the constructor, the lights ------------------------------------------------------------------

void TestParkGeneratorRestarts()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    World& w = g_world;

    RunCase("park generator: Construct", [&] { std::memset(&w.gen, 0x5A, sizeof(w.gen)); }, [&](bool original) {
        return original ? Game<G(__thiscall*)(G)>(0x00593460, Gen()) : U(Gen()->Construct());
    });
    // AddRestart: every type (free slots, taken ones, automatic ones replaced, the placed one's position kept)
    struct AddCase {
        int type, automatic, keep;
        uint32_t taken;    // bit i: slot i holds a restart of its type
        uint32_t autoBits; // bit i: slot i's was put down automatically
        int automaticByte = 1;
    };
    std::vector<AddCase> adds = {
        {1, 0, 0, 0, 0},           {1, 0, 0, 0x2, 0},       {1, 0, 0, 0x2, 0x2},      {1, 1, 0, 0x2, 0x2},
        {3, 1, 0, 0x7C, 0x40},
        {1, 1, 1, 0x2, 0},         {2, 1, 1, 0x1, 0},       {3, 0, 0, 0xFC, 0},        {3, 0, 0, 0xFC, 0x20},
        {3, 1, 1, 0x7C, 0x40},     {3, 1, 1, 0x84, 0x80},   {4, 1, 0, 0x3F00, 0x3F00}, {4, 0, 1, 0x0F00, 0},
        {5, 1, 1, 0x4000, 0x4000}, {8, 0, 0, 0x20000, 0},   {9, 1, 1, 0, 0},           {0, 0, 0, 0, 0},
        {0, 1, 1, 0x1, 0x1},       {3, 2, 1, 0x84, 0x80, 2},
    };
    for (const AddCase& c : adds) {
        RunCase("park generator: AddRestart", [&] {
            for (int i = 0; i < CParkGenerator::kNumRestarts; ++i) {
                RestartPoint& r = w.gen.m_restarts[i];
                int type = i == 0 ? 2 : i == 1 ? 1 : i < 8 ? 3 : i < 14 ? 4 : i - 9;
                r.m_type = (c.taken >> i) & 1 ? type : 0;
                r.m_automatic = static_cast<uint8_t>((c.autoBits >> i) & 1);
            }
        }, [&](bool original) {
            Mth::Vector pos = {-5.5f, 96.0f, 1234.0f, 1.0f};
            GridDims area = GridDims::Make(40, -2, 7, 2, 1, 3);
            int automatic = c.automatic ? c.automaticByte | 0x100 : 0x100;
            return original ? Game<bool(__thiscall*)(G, Mth::Vector*, int, GridDims*, int, int, uint32_t)>(
                                  0x00593200, Gen(), &pos, 3, &area, c.type, automatic, c.keep ? 0x201u : 0x200u)
                            : Gen()->AddRestart(&pos, 3, &area, c.type, automatic, c.keep ? 0x201u : 0x200u);
        });
    }
    // CountRestarts / NeedsRestarts / RemoveRestart
    for (int type = 0; type <= 9; ++type) {
        auto fill = [&] {
            for (int i = 0; i < CParkGenerator::kNumRestarts; ++i)
                w.gen.m_restarts[i].m_type = (i * 7 + type) % 3 ? 1 + (i + type) % 4 : 0;
            for (int i : {0, 1, 5, 9, 14, 15, 16, 17})
                w.gen.m_restarts[i].m_type = 2 + i % 3;
            w.gen.m_restarts[5].m_area = GridDims::Make(1, 2, 3, 9, 9, 9);
            w.gen.m_restarts[9].m_area = GridDims::Make(1, 2, 3, 0, 0, 0);
            w.gen.m_restarts[15].m_area = GridDims::Make(1, 2, 4, 0, 0, 0);
            w.gen.m_restarts[16].m_area = GridDims::Make(1, 2, 3, 0, 0, 0);
            w.gen.m_restarts[1].m_area = GridDims::Make(1, 3, 3, 0, 0, 0);
            w.gen.m_restarts[0].m_area = GridDims::Make(1, 2, 3, 0, 0, 0);
            w.gen.m_restarts[14].m_area = GridDims::Make(2, 2, 3, 0, 0, 0);
            w.gen.m_restarts[17].m_area = GridDims::Make(1, 1, 3, 0, 0, 0);
            w.gen.m_restarts[3].m_area = GridDims::Make(1, 2, 2, 0, 0, 0);
            w.gen.m_restarts[4].m_area = GridDims::Make(0, 2, 3, 0, 0, 0);
            w.gen.m_restarts[10].m_area = GridDims::Make(1, 2, 0, 0, 0, 0);
            w.gen.m_restarts[11].m_area = GridDims::Make(4, 2, 3, 0, 0, 0);
        };
        RunCase("park generator: CountRestarts", fill, [&](bool original) {
            return original ? Game<int(__thiscall*)(G, int)>(0x00593370, Gen(), type) : static_cast<uint32_t>(Gen()->CountRestarts(type));
        });
        for (int count : {0, 1, 2, 7}) {
            RunCase("park generator: NeedsRestarts", fill, [&](bool original) {
                return original ? Game<bool(__thiscall*)(G, int, int)>(0x005933D0, Gen(), type, count)
                                : Gen()->NeedsRestarts(type, count);
            });
        }
        RunCase("park generator: RemoveRestart", fill, [&](bool original) {
            GridDims area = GridDims::Make(1, 2, 3, 4, 4, 4);
            return original ? Game<void(__thiscall*)(G, GridDims*, int)>(0x005933F0, Gen(), &area, type)
                            : (Gen()->RemoveRestart(&area, type), 0u);
        });
    }
    // SetLightPosition, CalculateLighting (dark, mid, saturated channels; no lights; many lights)
    RunCase("park generator: SetLightPosition", [] {}, [&](bool original) {
        Mth::Vector pos = {1.5f, -2.25f, 3.125f, 0.5f};
        return original ? Game<void(__thiscall*)(G, const Mth::Vector*, int)>(0x00592FE0, Gen(), &pos, 7)
                        : (Gen()->SetLightPosition(&pos, 7), 0u);
    });
    struct LightCase {
        int lights;
        uint32_t rgba;
        float x, z;
        float falloffScale;
        float ambient = 0.0f; // (0: as it is)
        float falloff = 0.0f;
    };
    std::vector<LightCase> lights = {
        {3, 0xFF102030, 0.0f, 0.0f, 1.0f},        {0, 0xFFFFFFFF, 10.0f, 5.0f, 1.0f},  {1, 0x00808080, -400.0f, 300.0f, 30.0f},
        {5, 0x12C0E0F0, 77.7f, -91.3f, 0.01f},    {16, 0x00000000, 0.0f, 0.0f, 200.0f}, {7, 0x00FF00FF, 1e5f, -1e5f, 1.0f},
        {4, 0x00010101, -400.0f, 300.0f, 0.5f},   {2, 0xFF7F7F7F, -399.0f, 301.0f, 1.0f},
    };
    // (the ambient part rounded to a float for red and green, not for blue: 3 * 1.6666666 is 5 as a float)
    lights.push_back({2, 0x00030303, 5.0f, 5.0f, 0.0f, 1.66666662693023682f});
    // (one light right there: 255 / 160000 * 62745.297 is just over 100, / 160001 under)
    lights.push_back({1, 0x00000000, -400.0f, 300.0f, 0.0f, 0.0f, 62745.296875f});
    for (const LightCase& c : lights) {
        RunCase("park generator: CalculateLighting", [&] {
            w.gen.m_numLights = c.lights;
            for (float& f : w.gen.m_falloff)
                f = c.falloff != 0.0f ? c.falloff : f * c.falloffScale;
            if (c.ambient != 0.0f) {
                for (float& f : w.gen.m_ambient)
                    f = c.ambient;
            }
        }, [&](bool original) {
            Mth::Vector pos = {c.x, 12.0f, c.z, 1.0f};
            uint32_t rgba = c.rgba;
            if (original)
                Game<void(__thiscall*)(G, const Mth::Vector*, uint8_t*)>(0x00593010, Gen(), &pos, reinterpret_cast<uint8_t*>(&rgba));
            else
                Gen()->CalculateLighting(&pos, reinterpret_cast<uint8_t*>(&rgba));
            return rgba;
        });
    }
    RestoreAll(saved);
}

// --- the pieces and the scene ------------------------------------------------------------------------------------

// A light piece's geometry: `count` vertices (positions, colours) for each of the stand-ins, for `calls` pieces.
void ScriptVertices(int count, int calls = 1)
{
    for (int i = 0; i < calls; ++i) {
        g_script[0x0048DF70].push_back(static_cast<uint32_t>(count));
        if (count) {
            g_script[0x0048DF80 + 1].push_back(static_cast<uint32_t>(count));
            g_script[0x0049F3E0 + 1].push_back(static_cast<uint32_t>(count));
            g_script[0x0048DFA0 + 1].push_back(static_cast<uint32_t>(count));
        }
    }
}

void TestParkGeneratorScene()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    World& w = g_world;
    using P = CPiece*;

    // CreateGapPiece: with the gap's type (found first or later) and without
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: CreateGapPiece", [&] {
            if (c == 0)
                w.sources[3].m_name = 0xC0341DF7;
            if (c == 1) {
                w.sources[0].m_name = 0xC0341DF7;
                w.sources[4].m_name = 0xC0341DF7;
            }
            g_script[0x005923C0] = {U(&w.pieces[7])};
            w.pieces[7].m_flags = 0x100;
            w.gen.m_flags = 0x2;
        }, [&](bool original) {
            Mth::Vector pos = {11.0f, 22.0f, 33.0f, 1.0f};
            return original ? Game<void*(__thiscall*)(G, Mth::Vector*, float, int)>(0x00593500, Gen(), &pos, 4.5f, 3)
                            : U(Gen()->CreateGapPiece(&pos, 4.5f, 3));
        });
    }
    // DestroyPieces (the rails kept or not), DestroyPiece (at once or marked)
    for (int keep = 0; keep < 2; ++keep) {
        RunCase("park generator: DestroyPieces", [&] { w.gen.m_flags = 0xFF; }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, int)>(0x00593580, Gen(), keep) : (Gen()->DestroyPieces(keep), 0u);
        });
    }
    for (uint32_t flags : {0x0u, 0x200u, 0x1204u}) {
        RunCase("park generator: DestroyPiece", [&] { w.pieces[3].m_flags = flags; }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, void*)>(0x005935F0, Gen(), &w.pieces[3])
                            : (Gen()->DestroyPiece(&w.pieces[3]), 0u);
        });
    }
    // CleanUpOutRefs: marked pieces (first, middle, the cursor's, the last), the super sectors updated or not; one
    // marked piece alone (the list emptied)
    for (int c = 0; c < 4; ++c) {
        RunCase("park generator: CleanUpOutRefs", [&] {
            GameVariable<uint8_t>(0x0067F398) = static_cast<uint8_t>(c & 1);
            w.gen.m_flags = 0x5F;
            if (c >= 2) {
                w.pieces[0].m_flags |= 0x1000;
                w.pieces[5].m_flags |= 0x1000;
            }
            if (c == 3)
                w.pieces[0].mp_next = nullptr;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G)>(0x00593610, Gen()) : (Gen()->CleanUpOutRefs(), 0u);
        });
    }
    // UnloadPieces: with pieces or not (the source pieces go either way)
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: UnloadPieces", [&] {
            w.gen.m_flags = c == 0 ? 0x77 : 0x7F;
            GameVariable<uint8_t>(0x0067F398) = c == 2;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G)>(0x00594610, Gen()) : (Gen()->UnloadPieces(), 0u);
        });
    }
    // LightPiece: no vertices, some (and the collision's: 1-4 by sector)
    for (int c = 0; c < 5; ++c) {
        RunCase("park generator: LightPiece", [&] {
            ScriptVertices(c == 0 ? 0 : c == 4 ? 1 : c * 2 + 1);
            w.pieces[c].m_sector = static_cast<uint32_t>(c * 3 + 1);
            w.sources[c].m_name = static_cast<uint32_t>(c + 5);
            w.gen.m_numLights = 2 + c;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, P)>(0x00594410, Gen(), reinterpret_cast<P>(&w.pieces[c]))
                            : (Gen()->LightPiece(reinterpret_cast<P>(&w.pieces[c])), 0u);
        });
    }
    // AddPieceToScene: a piece, the cursor's, a gap's
    for (uint32_t flags : {0x8u, 0x4u, 0x48u, 0x44u}) {
        RunCase("park generator: AddPieceToScene", [&] {
            w.pieces[2].m_flags = flags;
            ScriptVertices(3);
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, P)>(0x00594730, Gen(), reinterpret_cast<P>(&w.pieces[2]))
                            : (Gen()->AddPieceToScene(reinterpret_cast<P>(&w.pieces[2])), 0u);
        });
    }
    // RebuildAll (the node array there or not, its symbol), RebuildMarked
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: RebuildAll", [&] {
            ScriptVertices(2, 6);
            if (c)
                g_script[0x0045D270] = {1};
            if (c == 2)
                g_script[0x00478CC0] = {U(w.symbol)};
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G)>(0x005947D0, Gen()) : (Gen()->RebuildAll(), 0u);
        });
    }
    for (int c = 0; c < 2; ++c) {
        RunCase("park generator: RebuildMarked", [&] {
            ScriptVertices(2, 6);
            if (c)
                w.sources[0].m_flags |= SourcePiece::kRebuilt;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G)>(0x00594910, Gen()) : (Gen()->RebuildMarked(), 0u);
        });
    }
    RunCase("park generator: CleanUp", [] {}, [&](bool original) {
        return original ? Game<void(__thiscall*)(G, int)>(0x00594C70, Gen(), 5) : (Gen()->CleanUp(5), 0u);
    });
    // LoadSourcePieces: loaded before or not; the sectors (one without a value ends it); their sizes (under a
    // cell, under a step high, flat, upside down)
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: LoadSourcePieces", [&] {
            w.gen.m_flags = c == 0 ? 0x5D : 0x5F;
            g_iterCount = c == 2 ? 8 : 6;
            if (c == 2)
                w.items[5].value = nullptr;
            w.boxes[1][1].x = w.boxes[1][0].x + 0.5f;
            w.boxes[2][1].y = w.boxes[2][0].y;
            w.boxes[4][1].y = w.boxes[4][0].y - 10.0f;
            w.boxes[5][1].z = w.boxes[5][0].z + 238.0f;
            w.boxes[0][1].y = w.boxes[0][0].y + 47.9f;
            w.boxes[3][1].y = w.boxes[3][0].y + 48.0f; // (exactly a step)
            for (int i = 0; i < CParkGenerator::kNumRestarts; ++i)
                w.gen.m_restarts[i].m_type = i % 5;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, int)>(0x00594C90, Gen(), 9) : (Gen()->LoadSourcePieces(9), 0u);
        });
    }
    // LoadPieces: the park editor's own level or another (BuildSourceRails stood in)
    for (uint32_t level : {0xCD3682C1u, 0x12345678u}) {
        RunCase("park generator: LoadPieces", [&] { g_iterCount = 2; }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, uint32_t, int)>(0x00594FD0, Gen(), level, 0)
                            : (Gen()->LoadPieces(level, 0), 0u);
        }, {{0x005936E0, 4}});
    }
    RestoreAll(saved);
}

// --- the node array ----------------------------------------------------------------------------------------------

// The pieces' node array: LevelGeometry nodes and the rail / climbing nodes of their pieces.
void RailNodes()
{
    auto node = [](int i, uint32_t cls, uint32_t name, uint32_t cluster, uint32_t link) {
        NodeSpec& n = g_spec[i];
        n.cls = cls;
        n.name = name;
        n.cluster = cluster;
        n.link = link;
        n.type = 0x7E900000u + i;
        n.terrain = cls == 0x8E6B02AD ? 0x7EA00000u + i : 0u;
        n.hasPos = true;
        n.hasAngles = i % 3 != 0;
        n.pos = {10.0f * i, -2.0f * i, 3.5f * i, 1.0f};
        n.angles = {0.0f, 0.1f * i, 0.0f, 0.0f};
    };
    constexpr uint32_t geometry = 0xBF4D3536, rail = 0x8E6B02AD, climbing = 0x30C19600;
    node(0, geometry, 0x6E01, 0, 0);
    node(1, rail, 0xA001, 0x6E01, 0); // a rail: 1 <- 2 <- 3
    node(2, rail, 0xA002, 0x6E01, 0xA001);
    node(3, rail, 0xA003, 0x6E01, 0xA002);
    g_spec[3].noClimbing = true;
    node(4, climbing, 0xA004, 0x6E01, 0); // a climbing rail of one point
    node(5, geometry, 0, 0, 0);           // (no name: not a piece)
    node(6, 0x12345678, 0xA006, 0x6E01, 0);
    node(7, geometry, 0x6E07, 0, 0);
    node(8, rail, 0xA008, 0x6E07, 0xA009); // a loop: 8 <-> 9
    node(9, rail, 0xA009, 0x6E07, 0xA008);
    node(10, climbing, 0xA00A, 0x6E07, 0);
    node(11, climbing, 0xA00B, 0x6E07, 0xA00A);
    g_spec[11].noClimbing = true;
    node(12, climbing, 0xA00C, 0x6E07, 0xA00B);
    node(13, geometry, 0x6E0D, 0, 0);
    node(14, rail, 0xA00E, 0x6E01, 0); // (another piece's)
    node(15, rail, 0xA00F, 0x6E0D, 0xA00E);
    for (int i = 16; i < kNodes; ++i)
        node(i, 0x4E0DE, 0xB000u + i, 0, 0);
    // the rails and points the rail set hands out
    for (int i = 0; i < 8; ++i)
        g_script[0x0058B370 + 1].push_back(static_cast<uint32_t>(i));
    for (int i = 0; i < 20; ++i)
        g_script[0x0058B2D0 + 1].push_back(static_cast<uint32_t>(i));
}

// The node info BuildSourceRails keeps (for NextRailNode alone).
void NodeInfo(const std::vector<int>& used)
{
    World& w = g_world;
    auto* info = reinterpret_cast<PieceNodeInfo*>(w.heap + 0x2000);
    for (int i = 0; i < kNodes; ++i) {
        info[i].m_class = g_spec[i].cls;
        info[i].m_cluster = g_spec[i].cluster;
        info[i].mp_links = g_spec[i].link ? &w.linkArrays[i] : nullptr;
    }
    w.gen.mp_nodeInfo = info;
    auto* usedNodes = reinterpret_cast<int*>(w.heap + 0x2800);
    for (size_t i = 0; i < used.size(); ++i)
        usedNodes[i] = used[i];
    w.gen.mp_usedNodes = usedNodes;
    w.gen.m_numUsedNodes = static_cast<int>(used.size());
}

void TestParkGeneratorNodes()
{
    std::vector<Saved> saved = SavedGlobals();
    SaveAll(saved);
    World& w = g_world;
    using A = Script::CArray*;
    A nodes = &w.nodeArray;

    // NextGeometryNode from the start, past the nameless one, at the end
    for (int start : {0, 1, 5, 8, 14, 24, 30}) {
        RunCase("park generator: NextGeometryNode", RailNodes, [&](bool original) {
            int index = start;
            uint32_t name = original ? Game<uint32_t(__thiscall*)(G, A, int*)>(0x00592740, Gen(), nodes, &index)
                                     : Gen()->NextGeometryNode(nodes, &index);
            Record({static_cast<uint32_t>(index)});
            return name;
        });
    }
    // NextRailNode: a first node, the next ones, a loop's, none left; nodes taken; the search's start
    struct RailCase {
        uint32_t geometry, previous;
        std::vector<int> used;
        int searchStart;
    };
    std::vector<RailCase> rails = {
        {0x6E01, 0, {}, 0x55},       {0x6E01, 0xA001, {1}, 1},         {0x6E01, 0xA002, {1, 2}, 1},
        {0x6E01, 0xA003, {1, 2, 3}, 1}, {0x6E01, 0, {1, 2, 3}, 9},     {0x6E01, 0, {1, 2, 3, 4, 14}, 1},
        {0x6E07, 0, {}, 0},          {0x6E07, 0xA008, {8}, 8},         {0x6E07, 0xA009, {8, 9}, 8},
        {0x6E07, 0, {8, 9}, 3},      {0x6E07, 0, {8, 9, 10}, 3},       {0x6E07, 0xA00A, {10}, 10},
        {0x6E07, 0xA00A, {11}, 2},   {0x6E07, 0xA00A, {10}, 12},       {0x6E0D, 0, {}, 0},
        {0x6E0D, 0, {15}, 0},        {0x9999, 0, {}, 0},               {0x6E01, 0xA00E, {}, 0},
    };
    for (const RailCase& c : rails) {
        RunCase("park generator: NextRailNode", [&] {
            RailNodes();
            NodeInfo(c.used);
            GameVariable<int>(0x007CE600) = c.searchStart;
        }, [&](bool original) {
            RailNodeInfo info;
            std::memset(&info, 0xEE, sizeof(info));
            uint32_t isLoop = 0x77777777;
            uint32_t name = original ? Game<uint32_t(__thiscall*)(G, A, uint32_t, uint32_t, RailNodeInfo*, bool*)>(
                                           0x005927D0, Gen(), nodes, c.geometry, c.previous, &info,
                                           reinterpret_cast<bool*>(&isLoop))
                                     : Gen()->NextRailNode(nodes, c.geometry, c.previous, &info,
                                                           reinterpret_cast<bool*>(&isLoop));
            Record({isLoop, HashBytes(reinterpret_cast<uint8_t*>(&info), sizeof(info))});
            return name;
        });
    }
    // BuildSourceRails: the whole node array (and a piece type with rails, one without)
    RunCase("park generator: BuildSourceRails", [&] {
        RailNodes();
        w.sources[0].m_name = 0x6E03;
        w.sources[1].m_name = 0x6E00;
        w.sources[2].m_name = 0x6E01;
    }, [&](bool original) {
        return original ? Game<void(__thiscall*)(G, A)>(0x005936E0, Gen(), nodes) : (Gen()->BuildSourceRails(nodes), 0u);
    });
    // FindNearestRailPoint: near one point, another, none near enough, no rails
    for (int c = 0; c < 6; ++c) {
        RunCase("park generator: FindNearestRailPoint", [&] {
            if (c == 3)
                w.gen.m_rails.mp_rails = nullptr;
            if (c == 4) { // (a tie: the first stays)
                w.points[4].m_pos = w.points[1].m_pos;
                w.points[4].m_pos.w = 7.0f;
            }
        }, [&](bool original) {
            Mth::Vector pos = c == 0   ? Mth::Vector{-30.0f, 9.0f, -40.0f, 1.0f}
                              : c == 1 ? Mth::Vector{100.0f, 30.0f, -100.0f, 1.0f}
                              : c == 4 ? Mth::Vector{-67.0f, 5.5f, -15.0f, 1.0f}
                              : c == 5 ? Mth::Vector{12000.0f, 0.0f, 0.0f, 1.0f}
                                       : Mth::Vector{3e4f, 3e4f, 3e4f, 1.0f};
            Mth::Vector point = {9.0f, 9.0f, 9.0f, 9.0f};
            float distance = -1.0f;
            uint32_t found = original ? Game<bool(__thiscall*)(G, const Mth::Vector*, Mth::Vector*, float*)>(
                                            0x00592A10, Gen(), &pos, &point, &distance)
                                      : Gen()->FindNearestRailPoint(&pos, &point, &distance);
            Record({F(point.x), F(point.y), F(point.z), F(point.w), F(distance)});
            return found;
        });
    }
    // GetNodeCounts: the pieces' flags, every restart type
    for (int c = 0; c < 2; ++c) {
        RunCase("park generator: GetNodeCounts", [&] {
            g_script[0x004F3C70] = {17};
            for (int i = 0; i < CParkGenerator::kNumRestarts; ++i)
                w.gen.m_restarts[i].m_type = c ? (i * 5) % 10 : (i % 9);
        }, [&](bool original) {
            int counts[6] = {9, 9, 9, 9, 9, 9};
            if (original)
                Game<void(__thiscall*)(G, int*)>(0x00592AB0, Gen(), counts);
            else
                Gen()->GetNodeCounts(counts);
            for (int value : counts)
                Record({static_cast<uint32_t>(value)});
            return 0u;
        });
    }
    // BuildGoalNodeArray (the old node array there or not), RemoveNodeArray (there or not; its symbol or not)
    for (int c = 0; c < 2; ++c) {
        RunCase("park generator: BuildGoalNodeArray", [&] { g_script[0x0045D270] = {static_cast<uint32_t>(c)}; },
                [&](bool original) {
                    return original ? Game<void(__thiscall*)(G)>(0x00592C10, Gen()) : (Gen()->BuildGoalNodeArray(), 0u);
                });
    }
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: RemoveNodeArray", [&] {
            g_script[0x0045D270] = {c ? 1u : 0u};
            if (c == 2)
                g_script[0x00478CC0] = {U(w.symbol)};
        }, [&](bool original) {
            return original ? Game<void(__cdecl*)()>(0x00592C90) : (CParkGenerator::RemoveNodeArray(), 0u);
        });
    }
    // BuildRails: metapieces in the park or not, without rails, of the inner shell; the ladders, the lamp, the
    // pieces turned a quarter
    RunCase("park generator: BuildRails", [&] {
        w.sources[0].m_name = 0x19E41B83; // (3 rails each, by the low bits)
        w.sources[1].m_name = 0xD48417FF;
        w.sources[2].m_name = 0x649FDDCA;
        w.sources[3].m_name = 0xA94E4D6A;
        w.sources[4].m_name = 0x54055089;
        w.sources[5].m_name = 0x2716A345;
        for (int i = 0; i < 8; ++i)
            g_script[0x0058B370 + 1].push_back(static_cast<uint32_t>(i));
    }, [&](bool original) {
        return original ? Game<void(__thiscall*)(G, CMapListNode*)>(0x00592CD0, Gen(), &w.list[0])
                        : (Gen()->BuildRails(&w.list[0]), 0u);
    });
    // WritePieceNodes: each kind of piece (a game object, absent in net games, with a trigger script, a gap's)
    RunCase("park generator: WritePieceNodes", [&] { g_script[0x0058CA60] = {0x6A7E5C21}; }, [&](bool original) {
        int index = 3;
        if (original)
            Game<void(__thiscall*)(G, A, int*)>(0x00592ED0, Gen(), nodes, &index);
        else
            Gen()->WritePieceNodes(nodes, &index);
        return static_cast<uint32_t>(index);
    });
    // WriteRailNodes: rails, a loop, climbing ones, the clusters; none
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: WriteRailNodes", [&] {
            g_script[0x0058B070] = {c ? 0u : 7u};
            if (c == 2)
                w.gen.m_rails.mp_rails = nullptr; // (none: nothing read)
        }, [&](bool original) {
            int index = 2;
            if (original)
                Game<void(__thiscall*)(G, A, int*)>(0x005949F0, Gen(), nodes, &index);
            else
                Gen()->WriteRailNodes(nodes, &index);
            return static_cast<uint32_t>(index);
        });
    }
    // WriteRestartNodes: every restart type (a player 1 one for the high score or not)
    for (int c = 0; c < 3; ++c) {
        RunCase("park generator: WriteRestartNodes", [&] {
            for (int i = 0; i < CParkGenerator::kNumRestarts; ++i)
                w.gen.m_restarts[i].m_type = c == 0 ? (i % 10) : c == 1 ? (i * 7) % 10 : (i % 3 == 0 ? 0 : 9);
            if (c == 1) {
                w.gen.m_restarts[2].m_type = 1;
                w.gen.m_restarts[13].m_type = 1;
            }
        }, [&](bool original) {
            int index = 1;
            if (original)
                Game<void(__thiscall*)(G, A, int*)>(0x00593AC0, Gen(), nodes, &index);
            else
                Gen()->WriteRestartNodes(nodes, &index);
            return static_cast<uint32_t>(index);
        });
    }
    // BuildNodeArray: the whole of it (the old array there or not)
    for (int c = 0; c < 2; ++c) {
        RunCase("park generator: BuildNodeArray", [&] {
            g_script[0x0045D270] = {static_cast<uint32_t>(c)};
            g_script[0x004F3C70] = {4, 9};
            g_script[0x0058B070] = {6, 7};
            g_script[0x004F6920 + 1] = {4};
            for (int i = 0; i < 16; ++i)
                g_script[0x0058B370 + 1].push_back(6u + i % 2); // (rails 0-2 stay the rails written)
            for (int i = 0; i < CParkGenerator::kNumRestarts; ++i)
                w.gen.m_restarts[i].m_type = i % 10;
        }, [&](bool original) {
            return original ? Game<void(__thiscall*)(G, CMapListNode*)>(0x00594EA0, Gen(), &w.list[0])
                            : (Gen()->BuildNodeArray(&w.list[0]), 0u);
        });
    }
    RestoreAll(saved);
}

} // namespace

SELFTEST(Early, TestParkGeneratorRestarts, RemoveStandIns);
SELFTEST(Early, TestParkGeneratorScene, RemoveStandIns);
SELFTEST(Early, TestParkGeneratorNodes, RemoveStandIns);
