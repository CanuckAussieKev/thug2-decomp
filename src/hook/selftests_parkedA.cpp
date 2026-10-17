// Self-tests for the park editor's park (game/ed): CParkManager's accessors, compressed-map header, piece sets,
// special pieces, floor heights, road mask and floor-slide bookkeeping. Each case builds the same random park in
// the same memory, runs the original and then ours on it, and compares the results and the whole park.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/ed/park_manager.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Ed::CMapListNode;
using Ed::CMetaPiece;
using Ed::CParkManager;
using Ed::CompressedMap;
using Ed::GridDims;

// Everything a case touches, in one block (pointers are compared as offsets into it).
struct ParkArena {
    CParkManager park;
    CMapListNode nodes[32];
    CMetaPiece metas[32];
    CompressedMap source;
};
alignas(16) ParkArena g_arena;

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int Range(int low, int high) { return low + static_cast<int>(Next() % static_cast<uint32_t>(high - low + 1)); }
};

const uint32_t kNames[] = {0x11111111, 0x22222222, 0x33333333, 0x44444444, 0x55555555};

GridDims RandomArea(Random& r, int maxSize)
{
    GridDims a;
    a.x = static_cast<uint8_t>(r.Range(0, 57));
    a.z = static_cast<uint8_t>(r.Range(0, 57));
    a.y = static_cast<int8_t>(r.Range(-16, 15));
    a.w = static_cast<uint8_t>(r.Range(0, std::min(maxSize, 58 - a.x)));
    a.l = static_cast<uint8_t>(r.Range(0, std::min(maxSize, 58 - a.z)));
    a.h = static_cast<uint8_t>(r.Range(0, 20));
    return a;
}

// A random park (deterministic in `seed`).
void BuildPark(uint32_t seed)
{
    Random r{seed};
    auto* bytes = reinterpret_cast<uint8_t*>(&g_arena);
    for (size_t i = 0; i < sizeof(g_arena); ++i)
        bytes[i] = static_cast<uint8_t>(r.Next());
    CParkManager& p = g_arena.park;
    for (auto& row : p.m_floor) {
        for (auto& cell : row) {
            cell.m_height = r.Range(-20, 20);
            cell.m_markAsSlid = r.Range(0, 1) != 0;
            cell.m_slideAmount = r.Range(-6, 6);
        }
    }
    // uniform patches for GetFloorHeight
    for (int x = 10; x < 20; ++x)
        for (int z = 30; z < 38; ++z)
            p.m_floor[x][z].m_height = 3;
    p.m_parkFarBounds = RandomArea(r, 58);
    p.m_parkNearBounds = RandomArea(r, 58);
    for (auto& set : p.m_sets)
        set.m_isUsed = r.Range(0, 2) != 0;
    p.m_compressedMapFlags = r.Next() & 0x1F;
    p.m_compressedMap.m_version = r.Range(0, 3) ? CompressedMap::kVersion : static_cast<uint16_t>(r.Next());
    p.m_compressedMap.m_theme = static_cast<uint16_t>(r.Range(0, 2) ? r.Range(0, 9) : r.Next());
    p.m_compressedMap.m_unknown18 = r.Range(0, 1) ? 0 : r.Next();
    for (int i = 0; i < 63; ++i)
        p.m_compressedMap.m_parkName[i] = static_cast<char>(r.Range('a', 'z'));
    p.m_compressedMap.m_parkName[r.Range(0, 62)] = 0;
    for (uint32_t& name : p.m_specialPieceNames)
        name = kNames[r.Range(0, 4)];
    p.m_roadMaskLength = static_cast<uint32_t>(r.Range(0, 8));
    for (GridDims& mask : p.m_roadMask)
        mask = RandomArea(r, 12);
    // lists: abstract metas first, then concrete
    int used = 0;
    auto makeList = [&](int count, CMapListNode*& head) {
        head = nullptr;
        for (int i = 0; i < count; ++i) {
            CMapListNode& node = g_arena.nodes[used];
            CMetaPiece& meta = g_arena.metas[used];
            ++used;
            node.mp_meta = &meta;
            node.mp_next = head;
            meta.m_nameChecksum = kNames[r.Range(0, 4)];
            head = &node;
        }
    };
    int abstractCount = r.Range(0, 8);
    makeList(abstractCount, p.mp_abstractMetaList);
    p.m_numAbstractMetas = r.Range(0, 3) ? abstractCount : r.Range(0, 20);
    makeList(r.Range(0, 8), p.mp_concreteMetaList);
    for (int x = 0; x < 58; ++x)
        for (int z = 0; z < 58; ++z)
            p.mp_bucketList[x][z] = r.Range(0, 3) ? nullptr : &g_arena.nodes[r.Range(0, 31)];
}

uint32_t Offset(const void* pointer)
{
    auto p = reinterpret_cast<uintptr_t>(pointer);
    auto base = reinterpret_cast<uintptr_t>(&g_arena);
    return p >= base && p < base + sizeof(g_arena) ? 0xA0000000u + static_cast<uint32_t>(p - base)
                                                   : static_cast<uint32_t>(p);
}
uint32_t FloatBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}
void RecordArena(std::vector<uint32_t>& trace)
{
    trace.push_back(HashBytes(reinterpret_cast<const uint8_t*>(&g_arena), sizeof(g_arena)));
}

template <typename Fn>
Fn Orig(uint32_t address)
{
    return Original<Fn>(address);
}

// One case: build, run (original or ours), record; the caller compares.
using CaseFn = void (*)(bool original, Random& r, std::vector<uint32_t>& trace);

std::vector<uint32_t> RunCase(CaseFn fn, uint32_t seed, bool original)
{
    std::vector<uint32_t> trace;
    BuildPark(seed);
    Random r{seed ^ 0x5A5A5A5Au};
    g_trace.clear();
    fn(original, r, trace);
    trace.insert(trace.end(), g_trace.begin(), g_trace.end());
    RecordArena(trace);
    return trace;
}

void CaseCompressedMap(bool original, Random& r, std::vector<uint32_t>& t)
{
    CParkManager& p = g_arena.park;
    bool flag = r.Range(0, 1) != 0;
    switch (r.Range(0, 5)) {
    case 0:
        t.push_back(Offset(original ? Orig<void*(__thiscall*)(void*, bool)>(0x00595030)(&p, flag)
                                    : p.GetCompressedMapBuffer(flag)));
        break;
    case 1:
        if (original)
            Orig<void(__thiscall*)(void*, const void*, bool)>(0x00595050)(&p, &g_arena.source, flag);
        else
            p.SetCompressedMapBuffer(&g_arena.source, flag);
        break;
    case 2:
        t.push_back(original ? Orig<bool(__thiscall*)(void*)>(0x00595090)(&p) : p.IsMapSavedLocally());
        break;
    case 3:
        t.push_back(static_cast<uint32_t>(original ? Orig<int(__thiscall*)(void*)>(0x005950B0)(&p) : p.GetTheme()));
        break;
    case 4:
        t.push_back(Offset(original ? Orig<const char*(__thiscall*)(void*)>(0x005950D0)(&p) : p.GetParkName()));
        break;
    case 5: {
        const char* names[] = {"", "x", "My park", "a much longer park name, not too long though"};
        const char* name = names[r.Range(0, 3)];
        if (original)
            Orig<void(__thiscall*)(void*, const char*)>(0x005950F0)(&p, name);
        else
            p.SetParkName(name);
        break;
    }
    }
}

void CaseShell(bool original, Random&, std::vector<uint32_t>& t)
{
    CParkManager& p = g_arena.park;
    StandIns s;
    if (original)
        s.At({0x006234FE}, 0, StandInSprintf);
    t.push_back(original ? Orig<uint32_t(__thiscall*)(void*)>(0x00595170)(&p) : p.GetShellChecksum());
}

void CaseRebuild(bool original, Random& r, std::vector<uint32_t>& t)
{
    CParkManager& p = g_arena.park;
    StandIns s;
    for (uint32_t address : {0x00593610u, 0x005947D0u, 0x00594910u})
        s.At({address}, 0, [address](Regs& regs) {
            Record({address, Offset(reinterpret_cast<void*>(regs.ecx))});
            return 0u;
        });
    int how = r.Range(-1, 3);
    if (original)
        Orig<void(__thiscall*)(void*, int)>(0x00595140)(&p, how);
    else
        p.RebuildParkGeometry(how);
    t.push_back(static_cast<uint32_t>(how));
}

void CaseLookups(bool original, Random& r, std::vector<uint32_t>& t)
{
    CParkManager& p = g_arena.park;
    switch (r.Range(0, 4)) {
    case 0: {
        int x = r.Range(0, 57), z = r.Range(0, 57);
        t.push_back(Offset(original ? Orig<void*(__thiscall*)(void*, int, int)>(0x005951E0)(&p, x, z)
                                    : p.GetMapListNode(x, z)));
        break;
    }
    case 1: {
        int index = r.Range(0, 39);
        int before = -7;
        int* out = r.Range(0, 3) ? &before : nullptr;
        t.push_back(Offset(original ? Orig<void*(__thiscall*)(void*, int, int*)>(0x00595210)(&p, index, out)
                                    : p.GetPieceSet(index, out)));
        t.push_back(static_cast<uint32_t>(before));
        break;
    }
    case 2: {
        int gaps = -1, metas = -1, width = -1, length = -1;
        uint16_t theme = 0xFFFF;
        uint32_t other = 0;
        const CompressedMap* map = r.Range(0, 1) ? &p.m_compressedMap : &g_arena.source;
        if (original)
            Orig<void(__thiscall*)(void*, const void*, int*, int*, uint16_t*, uint32_t*, int*, int*)>(0x00595250)(
                &p, map, &gaps, &metas, &theme, &other, &width, &length);
        else
            p.GetMapInfo(map, &gaps, &metas, &theme, &other, &width, &length);
        t.insert(t.end(), {static_cast<uint32_t>(gaps), static_cast<uint32_t>(metas), theme, other,
                           static_cast<uint32_t>(width), static_cast<uint32_t>(length)});
        break;
    }
    case 3: {
        uint32_t name = r.Range(0, 5) ? kNames[r.Range(0, 4)] : r.Next();
        t.push_back(static_cast<uint32_t>(original ? Orig<int(__thiscall*)(void*, uint32_t)>(0x00595300)(&p, name)
                                                   : p.GetSpecialPieceIndex(name)));
        break;
    }
    case 4: {
        uint32_t name = r.Range(0, 5) ? kNames[r.Range(0, 4)] : r.Next();
        int index = -7;
        int* out = r.Range(0, 3) ? &index : nullptr;
        t.push_back(Offset(original ? Orig<void*(__thiscall*)(void*, uint32_t, int*)>(0x00595620)(&p, name, out)
                                    : p.GetAbstractMetaPiece(name, out)));
        t.push_back(static_cast<uint32_t>(index));
        break;
    }
    }
}

void CaseFloor(bool original, Random& r, std::vector<uint32_t>& t)
{
    CParkManager& p = g_arena.park;
    switch (r.Range(0, 5)) {
    case 0:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x005952B0)(&p);
        else
            p.UndoFloorSlide();
        break;
    case 1:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x00595D40)(&p);
        else
            p.ResetSlideMarks();
        break;
    case 2: {
        GridDims area = RandomArea(r, r.Range(0, 1) ? 3 : 20);
        if (r.Range(0, 2) == 0) {
            area.x = static_cast<uint8_t>(r.Range(10, 15));
            area.z = static_cast<uint8_t>(r.Range(30, 33));
            area.w = static_cast<uint8_t>(r.Range(0, 5));
            area.l = static_cast<uint8_t>(r.Range(0, 6));
        }
        bool uniform = false;
        bool* out = r.Range(0, 3) ? &uniform : nullptr;
        if (out)
            *reinterpret_cast<uint8_t*>(out) = 0x5C;
        t.push_back(static_cast<uint32_t>(original
                                              ? Orig<int(__thiscall*)(void*, GridDims, bool*)>(0x005959D0)(&p, area, out)
                                              : p.GetFloorHeight(area, out)));
        t.push_back(*reinterpret_cast<uint8_t*>(&uniform));
        break;
    }
    case 3: {
        GridDims area = RandomArea(r, 58);
        area.x = static_cast<uint8_t>(r.Next());
        area.z = static_cast<uint8_t>(r.Next());
        area.w = static_cast<uint8_t>(r.Next());
        area.l = static_cast<uint8_t>(r.Next());
        area.h = static_cast<uint8_t>(r.Next());
        Mth::Vector dims = {-1, -1, -1, -1};
        Mth::Vector* out = r.Range(0, 3) ? &dims : nullptr;
        // (through a member pointer: a free __thiscall pointer would pass the result's address in ECX)
        using Method = Mth::Vector (CParkManager::*)(const GridDims&, Mth::Vector*);
        Method method = &CParkManager::GridCoordinatesToWorld;
        if (original) {
            void* code = OriginalFunction(0x00595A90);
            std::memcpy(&method, &code, sizeof(code));
        }
        Mth::Vector result = (p.*method)(area, out);
        for (float f : {result.x, result.y, result.z, result.w, dims.x, dims.y, dims.z, dims.w})
            t.push_back(FloatBits(f));
        break;
    }
    case 4: {
        for (int i = 0; i < 30; ++i) {
            GridDims area = RandomArea(r, 15);
            if (i & 1) { // next to a rectangle of the mask: touching or just overlapping its edges
                const GridDims& mask = p.m_roadMask[r.Range(0, 7)];
                area.x = static_cast<uint8_t>(mask.x + r.Range(-area.w, mask.w));
                area.z = static_cast<uint8_t>(mask.z + r.Range(-area.l, mask.l));
            }
            t.push_back(original ? Orig<bool(__thiscall*)(void*, GridDims)>(0x00595B30)(&p, area)
                                 : p.IntersectsRoadMask(area));
        }
        break;
    }
    case 5: {
        StandIns s;
        s.At({0x0059EB30}, 0, [](Regs& regs) {
            Record({0xEB30, regs.args[0], regs.args[1]});
            return (regs.args[0] ^ regs.args[1]) & 0x101u;
        });
        GridDims area = RandomArea(r, 58);
        t.push_back(original ? Orig<bool(__thiscall*)(void*, GridDims)>(0x00595990)(&p, area) : p.CanResizeTo(area));
        break;
    }
    }
}

void TestParkManagerAccessors()
{
    struct Named {
        CaseFn fn;
        const char* what;
    };
    for (Named c : {Named{CaseCompressedMap, "CParkManager compressed map header"},
                    Named{CaseShell, "CParkManager::GetShellChecksum"},
                    Named{CaseRebuild, "CParkManager::RebuildParkGeometry"},
                    Named{CaseLookups, "CParkManager lookups (bucket, set, map info, special, abstract meta)"},
                    Named{CaseFloor, "CParkManager floor (slide undo/reset, height, world position, road mask)"}}) {
        std::vector<uint32_t> expected, actual;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            std::vector<uint32_t> e = RunCase(c.fn, seed * 7919u, true);
            std::vector<uint32_t> a = RunCase(c.fn, seed * 7919u, false);
            expected.insert(expected.end(), e.begin(), e.end());
            actual.insert(actual.end(), a.begin(), a.end());
        }
        Compare(expected, actual, c.what);
    }
}

} // namespace

SELFTEST(Early, TestParkManagerAccessors, RemoveStandIns);
