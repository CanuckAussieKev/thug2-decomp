// Self-tests for the park editor's park (game/ed): CParkManager's accessors, compressed-map header, piece sets,
// special pieces, floor heights, road mask and floor-slide bookkeeping. Each case builds the same random park in
// the same memory, runs the original and then ours on it, and compares the results and the whole park.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/ed/cursor.h"
#include "game/ed/menu.h"
#include "game/ed/park_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
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
    Ed::CCursor cursor;
    Ed::CUpperMenuManager menu;
    Ed::CClipboard clipboards[8];
    Ed::GapInfo gaps[3];
    Script::CComponent components[6];
    uint8_t heap[0x800];
    alignas(4) uint8_t pools[2][sizeof(Mem::Pool)]; // (Pool has a constructor)
    // the map of metapieces
    CMapListNode poolNodes[160];
    CMapListNode bucketNodes[300];
    Ed::MetaPieceEntry moreEntries[32][3];
    CMapListNode* list;
    uint8_t pieces[16][4];
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
using CaseFn = void (*)(bool original, Random& r, std::vector<uint32_t>& trace, StandIns& s);

void BuildEditor(uint32_t seed);
void StandInEditor(StandIns& s);
void StandInEditorRest(StandIns& s);

std::vector<uint32_t> RunCase(CaseFn fn, uint32_t seed, bool original)
{
    std::vector<uint32_t> trace;
    BuildPark(seed);
    BuildEditor(seed);
    Random r{seed ^ 0x5A5A5A5Au};
    g_trace.clear();
    {
        StandIns s;
        StandInEditor(s);
        StandInEditorRest(s);
        fn(original, r, trace, s);
    }
    trace.insert(trace.end(), g_trace.begin(), g_trace.end());
    RecordArena(trace);
    return trace;
}

void CaseCompressedMap(bool original, Random& r, std::vector<uint32_t>& t, StandIns&)
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

void CaseShell(bool original, Random&, std::vector<uint32_t>& t, StandIns& s)
{
    CParkManager& p = g_arena.park;
    if (original)
        s.At({0x006234FE}, 0, StandInSprintf);
    t.push_back(original ? Orig<uint32_t(__thiscall*)(void*)>(0x00595170)(&p) : p.GetShellChecksum());
}

void CaseRebuild(bool original, Random& r, std::vector<uint32_t>& t, StandIns& s)
{
    CParkManager& p = g_arena.park;
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

void CaseLookups(bool original, Random& r, std::vector<uint32_t>& t, StandIns&)
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

void CaseFloor(bool original, Random& r, std::vector<uint32_t>& t, StandIns& s)
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

// --- the cursor, the piece menu, the gap manager copy -------------------------------------------------------------

alignas(16) uint8_t g_fakeManager[sizeof(Mem::Manager)];
bool g_allocFails = false;
uint32_t g_allocUsed = 0;
GridDims g_clipboardArea;
int g_gapFor = 0; // which gap 0x00597AC0 finds: -1 none
const char* const kSetNames[] = {"Ramps", "Rails", "Gaps", "Misc"};

void BuildEditor(uint32_t seed)
{
    Random r{seed * 2654435761u + 7};
    CParkManager& p = g_arena.park;
    Ed::CCursor& c = g_arena.cursor;
    c.mp_manager = &p;
    c.mp_meta = r.Range(0, 2) ? &g_arena.metas[r.Range(0, 31)] : nullptr;
    c.m_mode = r.Range(0, 7);
    if (r.Range(0, 2) == 0)
        c.m_mode = Ed::CCursor::kModePasteClipboard;
    c.m_areaSelectState = static_cast<uint8_t>(r.Range(0, 3));
    for (GridDims& corner : c.m_areaCorner)
        corner = RandomArea(r, 20);
    if (r.Range(0, 1)) { // a selection near the 11 x 11 limit
        c.m_areaCorner[1].x = static_cast<uint8_t>(c.m_areaCorner[0].x + r.Range(-11, 11));
        c.m_areaCorner[1].z = static_cast<uint8_t>(c.m_areaCorner[0].z + r.Range(-12, 12));
    }
    c.m_cellDims = RandomArea(r, 20);
    // the park's inner bounds: inside the grid
    GridDims& bounds = p.m_parkNearBounds;
    bounds.x = static_cast<uint8_t>(r.Range(1, 20));
    bounds.z = static_cast<uint8_t>(r.Range(1, 20));
    bounds.w = static_cast<uint8_t>(r.Range(8, 36));
    bounds.l = static_cast<uint8_t>(r.Range(8, 36));
    if (r.Range(0, 1)) { // the cursor at the edges
        c.m_cellDims.x = static_cast<uint8_t>(bounds.x + r.Range(-2, 2) + (r.Range(0, 1) ? bounds.w - c.m_cellDims.w : 0));
        c.m_cellDims.z = static_cast<uint8_t>(bounds.z + r.Range(-2, 2) + (r.Range(0, 1) ? bounds.l - c.m_cellDims.l : 0));
    }
    // clipboards
    c.mp_clipboards = nullptr;
    int count = r.Range(0, 5);
    for (int i = count - 1; i >= 0; --i) {
        g_arena.clipboards[i].mp_next = c.mp_clipboards;
        c.mp_clipboards = &g_arena.clipboards[i];
    }
    c.mp_currentClipboard = r.Range(0, 3) ? &g_arena.clipboards[r.Range(0, 7)] : nullptr;
    // the gap being built
    Ed::GapInfo& gap = c.m_gapInfo;
    gap.m_numCompleteHalves = r.Range(0, 2);
    gap.m_tabIndex = r.Range(-2, 3);
    for (char& ch : gap.m_text)
        ch = static_cast<char>(r.Range('a', 'z'));
    gap.m_text[31] = 0;
    for (Ed::GapInfo& other : g_arena.gaps) {
        other.m_pos[0] = gap.m_pos[0];
        if (r.Range(0, 1))
            reinterpret_cast<uint8_t*>(&other.m_pos[0])[r.Range(0, 2)] ^= 1;
    }
    g_gapFor = r.Range(-1, 2);
    g_clipboardArea.x = static_cast<uint8_t>(r.Range(-6, 60));
    g_clipboardArea.z = static_cast<uint8_t>(r.Range(-6, 60));
    g_clipboardArea.w = static_cast<uint8_t>(r.Range(1, 12));
    g_clipboardArea.l = static_cast<uint8_t>(r.Range(1, 12));
    // a structure's components (cancel flags)
    Script::CComponent* next = nullptr;
    for (int i = r.Range(0, 5) - 1; i >= 0; --i) {
        g_arena.components[i].next = next;
        g_arena.components[i].checksum = kNames[r.Range(0, 4)] + static_cast<uint32_t>(i);
        next = &g_arena.components[i];
    }
    g_arena.components[5].next = next; // (the first one, for the stand-in)
    // the piece menu and its sets
    p.m_numSets = r.Range(0, 3) ? r.Range(0, 40) : 0;
    for (Ed::CPieceSet& set : p.m_sets)
        set.m_name = kSetNames[r.Range(0, 3)];
    g_arena.menu.mp_manager = &p;
    GameVariable<Ed::CUpperMenuManager*>(0x007CE60C) = r.Range(0, 2) ? nullptr : &g_arena.menu;
    GameVariable<CParkManager*>(0x007CE604) = &p;
    GameVariable<Ed::CCursor*>(0x007CE608) = &c;
    GameVariable<bool>(0x007CC979) = true;
    GameVariable<void*>(0x007D1800) = g_fakeManager;
    auto* manager = reinterpret_cast<Mem::Manager*>(g_fakeManager);
    manager->m_bottomUpHeap = reinterpret_cast<Mem::Heap*>(0x11400000);
    manager->m_scriptHeap = reinterpret_cast<Mem::Heap*>(0x12400000);
    p.mp_gapManagerCopy = r.Range(0, 2) ? &g_arena.heap[0x100] : nullptr;
    p.mp_gapManager = &g_arena.heap[0x180];
    GameVariable<void*>(0x007CE5D4) = &g_arena.heap[0x40];
    g_allocFails = r.Range(0, 4) == 0;
    g_allocUsed = 0;
    // the clipboard pools: none, or one holding the first clipboards
    auto& pools = GameVariable<Mem::PoolPair>(0x007CE5B4);
    pools.current = r.Range(0, 1);
    pools.pools[0] = pools.pools[1] = nullptr;
    if (r.Range(0, 1)) {
        Mem::Pool& pool = *reinterpret_cast<Mem::Pool*>(g_arena.pools[0]);
        pool.m_start = reinterpret_cast<uint8_t*>(&g_arena.clipboards[0]);
        pool.m_end = reinterpret_cast<uint8_t*>(&g_arena.clipboards[r.Range(0, 4)]);
        pools.pools[r.Range(0, 1)] = &pool;
    }
}

void StandInGapInfo(StandIns& s);

void StandInEditor(StandIns& s)
{
    auto record = [](uint32_t tag) {
        return [tag](Regs& r) {
            Record({tag, Offset(reinterpret_cast<void*>(r.ecx))});
            return r.ecx;
        };
    };
    auto record1 = [](uint32_t tag) {
        return [tag](Regs& r) {
            Record({tag, Offset(reinterpret_cast<void*>(r.ecx)), Offset(reinterpret_cast<void*>(r.args[0]))});
            return r.ecx;
        };
    };
    s.At({0x0058C200}, 0, record(0xC200));     // CClipboard::Hide
    s.At({0x0058C780}, 4, record1(0xC780));    // CClipboard::ShowAt
    s.At({0x0058C980}, 0, record(0xC980));     // CClipboard destructor
    s.At({0x005903A0}, 4, record1(0x03A0));    // CMetaPiece visibility
    s.At({0x0058CF30}, 4, record1(0xCF30));    // CGapManager copy
    s.At({0x0058CF20}, 0, record(0xCF20));     // CGapManager destructor
    s.At({0x0058CCC0}, 4, [](Regs& r) { // CGapManager constructor (makes itself current)
        Record({0xCCC0, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0]});
        GameVariable<uint32_t>(0x007CE5D4) = r.ecx;
        return r.ecx;
    });
    s.At({0x00592290}, 4, record1(0x2290));    // CParkGenerator refresh
    s.At({0x0058CB70}, 4, record1(0xCB70));    // CGapManager current gap
    s.At({0x00591A50}, 8, [](Regs& r) {        // CMetaPiece highlight
        Record({0x1A50, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0] & 0xFF, r.args[1]});
        return 0u;
    });
    s.At({0x0058CDA0}, 8, [](Regs& r) { // CGapManager::SetGapInfo
        Record({0xCDA0, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0], Offset(reinterpret_cast<void*>(r.args[1]))});
        return 0u;
    });
    s.At({0x0058BF10}, 8, [](Regs& r) { // CClipboard::GetArea
        Record({0xBF10, Offset(reinterpret_cast<void*>(r.ecx)), Offset(reinterpret_cast<void*>(r.args[0]))});
        *reinterpret_cast<GridDims*>(r.args[1]) = g_clipboardArea;
        return 0u;
    });
    (void)s;
}

void StandInGapInfo(StandIns& s)
{
    s.At({0x00597AC0, AddressOf(&CParkManager::GetGapInfo)}, 8, [](Regs& r) { // the gap a piece is a half of
        auto* area = reinterpret_cast<const uint8_t*>(r.args[0]);
        Record({0x7AC0, Offset(reinterpret_cast<void*>(r.ecx)), HashBytes(area, 6)});
        *reinterpret_cast<int*>(r.args[1]) = 1;
        return g_gapFor < 0 ? 0u : reinterpret_cast<uint32_t>(&g_arena.gaps[g_gapFor]);
    });
}

void StandInEditorRest(StandIns& s)
{
    // scripts
    using namespace Script;
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Record({0x1A17});
        return r.ecx;
    });
    s.At({0x00477130, AddressOf(&CStruct::Clear)}, 0, [](Regs&) {
        Record({0xC1EA});
        return 0u;
    });
    s.At({0x00477D40, AddressOf(static_cast<void (CStruct::*)(uint32_t, uint32_t)>(&CStruct::AddChecksum))}, 8,
         [](Regs& r) {
             Record({0x7D40, r.args[0], r.args[1]});
             return r.ecx;
         });
    s.At({0x004779D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Record({0x79D0, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return r.ecx;
         });
    s.At({0x00475790, AddressOf(static_cast<void (*)(uint32_t, CStruct*, Obj::CObject*, bool, const char*)>(&RunScript))},
         0, [](Regs& r) {
             Record({0x5790, r.args[0], r.args[1] != 0, r.args[2], r.args[3] & 0xFF, r.args[4]});
             return 0u;
         });
    s.At({0x00478F60, AddressOf(static_cast<int (*)(uint32_t)>(&GetInteger))}, 0, [](Regs& r) {
        Record({0x8F60, r.args[0]});
        return r.args[0] * 0x9E3779B1u;
    });
    s.At({0x00476490, AddressOf(&CStruct::GetNextComponent)}, 4, [](Regs& r) {
        auto* previous = reinterpret_cast<CComponent*>(r.args[0]);
        return reinterpret_cast<uint32_t>(previous ? previous->next : g_arena.components[5].next);
    });
    // memory
    using Mem::Manager;
    s.At({0x005BAFC0, AddressOf(&Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.ecx == reinterpret_cast<uint32_t>(g_fakeManager), r.args[0], r.args[1], r.args[2]});
        if (g_allocFails)
            return 0u;
        uint32_t at = reinterpret_cast<uint32_t>(&g_arena.heap[g_allocUsed]);
        g_allocUsed += (r.args[0] + 15) & ~15u;
        return at;
    });
    s.At({0x005BB0E0, AddressOf(&Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xB0E0, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Manager::PopContext)}, 0, [](Regs&) {
        Record({0xB110});
        return 0u;
    });
    s.At({0x005BABA0, AddressOf(&Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, Offset(reinterpret_cast<void*>(r.args[0]))});
        return 0u;
    });
    s.At({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, [](Regs& r) {
        Record({0xA400, Offset(reinterpret_cast<void*>(r.ecx)), Offset(reinterpret_cast<void*>(r.args[0]))});
        return 0u;
    });
}

void CaseCursor(bool original, Random& r, std::vector<uint32_t>& t, StandIns& s)
{
    Ed::CCursor& c = g_arena.cursor;
    using Ed::CCursor;
    auto call0 = [&](uint32_t address, void (CCursor::*ours)()) {
        if (original)
            Orig<void(__thiscall*)(void*)>(address)(&c);
        else
            (c.*ours)();
    };
    switch (r.Range(0, 13)) {
    case 0:
        call0(0x00595340, &CCursor::HideClipboards);
        break;
    case 1: {
        const char* names[] = {nullptr, "", "Big gap", "Some other gap"};
        const char* name = names[r.Range(0, 3)];
        const int scores[] = {-5, -1, 0, 1, 250};
        int score = scores[r.Range(0, 4)];
        if (original)
            Orig<void(__thiscall*)(void*, const char*, int)>(0x00595360)(&c, name, score);
        else
            c.SetGapNameAndScore(name, score);
        break;
    }
    case 2: {
        auto* flags = r.Range(0, 4) ? reinterpret_cast<Script::CStruct*>(&g_arena.heap[0x1F0]) : nullptr;
        if (original)
            Orig<void(__thiscall*)(void*, Script::CStruct*)>(0x005953C0)(&c, flags);
        else
            c.SetGapCancelFlags(flags);
        break;
    }
    case 3:
        t.push_back(Offset(original ? Orig<char*(__thiscall*)(void*)>(0x00595430)(&c) : c.GetGapName()));
        break;
    case 4:
        call0(0x00595440, &CCursor::ShowClipboard);
        break;
    case 5: {
        int index = r.Range(-1, 6);
        if (original)
            Orig<void(__thiscall*)(void*, int)>(0x00595450)(&c, index);
        else
            c.SelectClipboard(index);
        break;
    }
    case 6: {
        bool visible = r.Range(0, 1) != 0;
        if (original)
            Orig<void(__thiscall*)(void*, bool)>(0x005954A0)(&c, visible);
        else
            c.SetVisibility(visible);
        break;
    }
    case 7:
        call0(0x005954D0, &CCursor::UpdateHelperText);
        break;
    case 8:
        call0(0x00597100, &CCursor::RestartAreaSelection);
        break;
    case 9: {
        GridDims area;
        std::memset(&area, 0xEE, sizeof(area));
        if (original)
            Orig<void(__thiscall*)(void*, GridDims*)>(0x00597110)(&c, &area);
        else
            c.GetAreaSelectDims(&area);
        t.push_back(HashBytes(reinterpret_cast<uint8_t*>(&area), sizeof(area)));
        break;
    }
    case 10:
        call0(0x00597190, &CCursor::DestroyLastClipboard);
        break;
    case 11:
        call0(0x005971E0, &CCursor::ForceInBounds);
        break;
    case 12:
        call0(0x005988A0, &CCursor::DestroyClipboards);
        break;
    case 13:
        StandInGapInfo(s);
        if (r.Range(0, 1)) {
            CMetaPiece* meta = &g_arena.metas[r.Range(0, 31)];
            if (original)
                Orig<void(__thiscall*)(void*, CMetaPiece*)>(0x00598940)(&c, meta);
            else
                c.ForgetGapOnPiece(meta);
        } else {
            if (r.Range(0, 1))
                c.m_areaSelectState = 1;
            t.push_back(original ? Orig<uint32_t(__thiscall*)(void*)>(0x005989C0)(&c) & 0xFF
                                 : static_cast<uint32_t>(c.AdvanceAreaSelection()) & 0xFF);
        }
        break;
    }
}

void CaseMenuAndGaps(bool original, Random& r, std::vector<uint32_t>& t, StandIns&)
{
    CParkManager& p = g_arena.park;
    Ed::CUpperMenuManager& m = g_arena.menu;
    using Ed::CUpperMenuManager;
    switch (r.Range(0, 7)) {
    case 0:
        t.push_back(Offset(original ? Orig<void*(__cdecl*)()>(0x00595570)() : CUpperMenuManager::sInstance()));
        t.push_back(Offset(original ? Orig<void*(__cdecl*)()>(0x00595330)() : Ed::CCursor::sInstance()));
        t.push_back(Offset(original ? Orig<void*(__cdecl*)()>(0x00595200)() : CParkManager::sInstance()));
        break;
    case 1:
        if (original)
            Orig<void(__cdecl*)()>(0x00597400)();
        else
            CUpperMenuManager::Create();
        t.push_back(Offset(GameVariable<void*>(0x007CE60C)));
        break;
    case 2:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x00597490)(&m);
        else
            m.Enable();
        break;
    case 3:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x00595580)(&m);
        else
            m.Disable();
        break;
    case 4:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x00595BC0)(&p);
        else
            p.CreateGapManagerCopy();
        t.push_back(Offset(GameVariable<void*>(0x007CE5D4)));
        break;
    case 5:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x00595C70)(&p);
        else
            p.UseGapManagerCopy();
        t.push_back(Offset(GameVariable<void*>(0x007CE5D4)));
        break;
    case 6:
        if (original)
            Orig<void(__thiscall*)(void*)>(0x00595C80)(&p);
        else
            p.DestroyGapManagerCopy();
        break;
    case 7: {
        bool on = r.Range(0, 1) != 0;
        Ed::GapInfo* gap = r.Range(0, 3) ? &g_arena.cursor.m_gapInfo : nullptr;
        for (int i = 0; i < 32; ++i)
            g_arena.metas[i].m_flags = r.Next();
        if (original)
            Orig<void(__thiscall*)(void*, bool, Ed::GapInfo*)>(0x00595CB0)(&p, on, gap);
        else
            p.HighlightGapPieces(on, gap);
        break;
    }
    }
}

void* g_vtable[4]; // (outside the arena: the stand-ins' code moves between runs)

// --- the map of metapieces ---------------------------------------------------------------------------------------

int MetaIndex(const void* meta)
{
    auto at = reinterpret_cast<uintptr_t>(meta);
    auto first = reinterpret_cast<uintptr_t>(&g_arena.metas[0]);
    return at >= first && at < first + sizeof(g_arena.metas) ? static_cast<int>((at - first) / sizeof(CMetaPiece)) : 99;
}

// A random map: metapieces in a 16 x 16 patch of the grid (from 20, 20), each listed in a few buckets.
void BuildMap(Random& r)
{
    CParkManager& p = g_arena.park;
    for (auto& row : p.mp_bucketList)
        for (CMapListNode*& bucket : row)
            bucket = nullptr;
    void* const kVtable = g_vtable;
    for (int i = 0; i < 32; ++i) {
        CMetaPiece& meta = g_arena.metas[i];
        meta.m_vtable = kVtable;
        meta.m_cellArea.x = static_cast<uint8_t>(r.Range(20, 34));
        meta.m_cellArea.z = static_cast<uint8_t>(r.Range(20, 34));
        meta.m_cellArea.y = static_cast<int8_t>(r.Range(-4, 6));
        meta.m_cellArea.w = static_cast<uint8_t>(r.Range(1, 3));
        meta.m_cellArea.l = static_cast<uint8_t>(r.Range(1, 3));
        meta.m_cellArea.h = static_cast<uint8_t>(r.Range(1, 3));
        meta.m_flags = 0;
        for (uint32_t flag : {0x4u, 0x8u, 0x20u, 0x100u, 0x400u, 0x4000u})
            if (r.Range(0, 3) == 0)
                meta.m_flags |= flag;
        if (r.Range(0, 1))
            meta.m_flags |= 0x4;
        meta.m_numEntries = static_cast<uint32_t>(r.Range(1, 4));
        meta.mp_moreEntries = g_arena.moreEntries[i];
        for (uint32_t e = 0; e < 5; ++e) {
            Ed::MetaPieceEntry& entry = e < 2 ? meta.m_entries[e] : g_arena.moreEntries[i][e - 2];
            entry.x = static_cast<uint8_t>(r.Range(0, 2));
            entry.y = static_cast<int8_t>(r.Range(-1, 2));
            entry.z = static_cast<uint8_t>(r.Range(0, 2));
            entry.rot = static_cast<uint8_t>(r.Range(0, 3));
            entry.isReal = r.Range(0, 3) != 0;
            entry.piece = kNames[r.Range(0, 4)];
        }
    }
    int used = 0;
    for (int x = 20; x < 36; ++x) {
        for (int z = 20; z < 36; ++z) {
            for (int n = r.Range(0, 2); n > 0 && used < 290; --n) {
                CMapListNode& node = g_arena.bucketNodes[used++];
                node.mp_meta = &g_arena.metas[r.Range(0, 31)];
                node.mp_next = p.mp_bucketList[x][z];
                p.mp_bucketList[x][z] = &node;
            }
        }
    }
    // the lists of metapieces
    p.mp_concreteMetaList = nullptr;
    for (int n = r.Range(0, 12); n > 0 && used < 290; --n) {
        CMapListNode& node = g_arena.bucketNodes[used++];
        node.mp_meta = &g_arena.metas[r.Range(0, 31)];
        node.mp_next = p.mp_concreteMetaList;
        p.mp_concreteMetaList = &node;
    }
    g_arena.list = nullptr;
    for (int n = r.Range(0, 6); n > 0 && used < 290; --n) {
        CMapListNode& node = g_arena.bucketNodes[used++];
        node.mp_meta = &g_arena.metas[r.Range(0, 31)];
        node.mp_next = g_arena.list;
        g_arena.list = &node;
    }
    // the node pools: a real pool (most of the time), or the heap
    auto& pools = GameVariable<Mem::PoolPair>(0x007CE5F0);
    pools.current = r.Range(0, 1);
    pools.pools[0] = pools.pools[1] = nullptr;
    if (r.Range(0, 3)) {
        auto& pool = *reinterpret_cast<Mem::Pool*>(g_arena.pools[1]);
        pool.m_start = reinterpret_cast<uint8_t*>(g_arena.poolNodes);
        pool.m_end = reinterpret_cast<uint8_t*>(g_arena.poolNodes + 160);
        pool.m_count = 160;
        pool.m_itemSize = sizeof(CMapListNode);
        pool.m_used = 0;
        pool.m_peakUsed = 0;
        for (int i = 0; i < 160; ++i)
            *reinterpret_cast<void**>(&g_arena.poolNodes[i]) = i < 159 ? &g_arena.poolNodes[i + 1] : nullptr;
        pool.m_freeList = &g_arena.poolNodes[0];
        pools.pools[pools.current] = &pool;
    }
    GameVariable<void*>(0x007CE688) = reinterpret_cast<void*>(0x68800000); // the park heap
    p.mp_generator = &g_arena.pieces[15];
    p.mp_gapManager = &g_arena.pieces[14];
}

void StandInMap(StandIns& s)
{
    s.At({0x00590CE0}, 12, [](Regs& r) { // CMetaPiece::Intersects
        uint32_t h = HashBytes(reinterpret_cast<const uint8_t*>(&r.args[0]), 6) ^ (r.args[2] & 0xFF);
        h ^= static_cast<uint32_t>(MetaIndex(reinterpret_cast<void*>(r.ecx))) * 0x9E3779B1u;
        Record({0x0CE0, static_cast<uint32_t>(MetaIndex(reinterpret_cast<void*>(r.ecx))), h});
        return (h >> 7) & 1;
    });
    s.At({0x005907B0}, 4, [](Regs& r) { // CMetaPiece::SetRot
        Record({0x07B0, static_cast<uint32_t>(MetaIndex(reinterpret_cast<void*>(r.ecx))), r.args[0]});
        return 0u;
    });
    s.At({0x00590400}, 0, [](Regs& r) { // CMetaPiece::CountPieces
        return reinterpret_cast<CMetaPiece*>(r.ecx)->m_numEntries;
    });
    s.At({0x00590AD0}, 4, [](Regs& r) { // CMetaPiece::Initialize
        Record({0x0AD0, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0]});
        return 0u;
    });
    s.At({0x00591AB0}, 0, [](Regs& r) { // CMetaPiece::FinishPieces
        Record({0x1AB0, Offset(reinterpret_cast<void*>(r.ecx))});
        return 0u;
    });
    s.At({0x005911D0}, 8, [](Regs& r) { // the abstract metapiece's entry, copied
        auto* meta = reinterpret_cast<CMetaPiece*>(r.ecx);
        int i = static_cast<int>(r.args[1]);
        *reinterpret_cast<Ed::MetaPieceEntry*>(r.args[0]) = i < 2 ? meta->m_entries[i] : meta->mp_moreEntries[i - 2];
        return r.args[0];
    });
    s.At({0x00591D90}, 4, [](Regs& r) { // CParkGenerator: a piece's source
        Record({0x1D90, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0]});
        return r.args[0] ^ 0x5A5A;
    });
    s.At({0x005923C0}, 8, [](Regs& r) { // CParkGenerator: a new piece
        Record({0x23C0, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0], r.args[1]});
        return reinterpret_cast<uint32_t>(&g_arena.pieces[r.args[0] & 7]);
    });
    s.At({0x00590260}, 8, [](Regs& r) { // a piece's rotation
        Record({0x0260, Offset(reinterpret_cast<void*>(r.ecx)), r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00590B60}, 0, [](Regs& r) { // the concrete metapiece's constructor
        auto* meta = reinterpret_cast<CMetaPiece*>(r.ecx);
        std::memset(meta, 0, sizeof(CMetaPiece));
        meta->m_vtable = g_vtable;
        Record({0x0B60, Offset(meta)});
        return r.ecx;
    });
    s.At({0x005904C0}, 4, [](Regs& r) { // a piece's size in cells
        auto* dims = reinterpret_cast<GridDims*>(r.args[0]);
        uint32_t k = Offset(reinterpret_cast<void*>(r.ecx));
        dims->Set(static_cast<uint8_t>(k), 0, static_cast<uint8_t>(k >> 3), static_cast<uint8_t>(1 + (k >> 2) % 3),
                  static_cast<uint8_t>(1 + (k >> 4) % 3), static_cast<uint8_t>(1 + (k >> 3) % 2));
        return 0u;
    });
    s.At({0x0058CA10}, 8, [](Regs& r) { // CGapManager: the gap a metapiece is a half of
        Record({0xCA10, Offset(reinterpret_cast<void*>(r.ecx)), static_cast<uint32_t>(MetaIndex(reinterpret_cast<void*>(r.args[0])))});
        *reinterpret_cast<int*>(r.args[1]) = MetaIndex(reinterpret_cast<void*>(r.args[0])) % 5;
        return static_cast<uint32_t>(MetaIndex(reinterpret_cast<void*>(r.args[0])) % 3 != 0);
    });
    s.At({0x0058CB20}, 8, [](Regs& r) { // CGapManager: a gap's info
        Record({0xCB20, r.args[0]});
        *reinterpret_cast<int*>(r.args[1]) = 1;
        return reinterpret_cast<uint32_t>(&g_arena.gaps[r.args[0] % 3]);
    });
    // the metapieces' virtual functions
    g_vtable[0] = s.Thunk(4, [](Regs& r) { // GetPiece
        return reinterpret_cast<uint32_t>(&g_arena.pieces[(MetaIndex(reinterpret_cast<void*>(r.ecx)) + r.args[0]) & 7]);
    });
    g_vtable[2] = s.Thunk(16, [](Regs& r) { // AddPiece
        Record({0xADD0, Offset(reinterpret_cast<void*>(r.ecx)), Offset(reinterpret_cast<void*>(r.args[0])),
                HashBytes(reinterpret_cast<const uint8_t*>(r.args[1]), 6), r.args[2], r.args[3] & 0xFF});
        return 0u;
    });
}

void RecordList(std::vector<uint32_t>& t, const CMapListNode* list)
{
    int n = 0;
    for (const CMapListNode* node = list; node && n < 1000; node = node->mp_next, ++n)
        t.insert(t.end(), {Offset(node), Offset(node->mp_meta), static_cast<uint32_t>(node->m_slideAmount)});
    t.push_back(0x1157);
}

GridDims MapArea(Random& r)
{
    GridDims area;
    area.x = static_cast<uint8_t>(r.Range(18, 36));
    area.z = static_cast<uint8_t>(r.Range(18, 36));
    area.y = static_cast<int8_t>(r.Range(-5, 6));
    area.w = static_cast<uint8_t>(r.Range(0, 5));
    area.l = static_cast<uint8_t>(r.Range(0, 5));
    area.h = static_cast<uint8_t>(r.Range(0, 4));
    return area;
}

void CaseMap(bool original, Random& r, std::vector<uint32_t>& t, StandIns& s)
{
    CParkManager& p = g_arena.park;
    BuildMap(r);
    StandInMap(s);
    CMetaPiece* meta = &g_arena.metas[r.Range(0, 31)];
    g_allocFails = false; // (the originals crash when a node or metapiece can't be had)
    switch (r.Range(0, 11)) {
    case 0: {
        uint32_t flags = r.Next() & 0x3F;
        auto* made = original ? Orig<CMetaPiece*(__thiscall*)(void*, CMetaPiece*, uint32_t)>(0x00595670)(&p, meta, flags)
                              : p.CreateConcreteMeta(meta, flags);
        t.push_back(Offset(made));
        RecordList(t, p.mp_concreteMetaList);
        break;
    }
    case 1: {
        GridDims area = MapArea(r);
        area.w = static_cast<uint8_t>(r.Range(0, 16));
        area.l = static_cast<uint8_t>(r.Range(0, 16));
        t.push_back(original ? Orig<bool(__thiscall*)(void*, GridDims)>(0x005958B0)(&p, area) : p.HasPiecesOutside(area));
        break;
    }
    case 2:
        if (original)
            Orig<void(__thiscall*)(void*, CMetaPiece*, CMapListNode**)>(0x00595DF0)(&p, meta, &g_arena.list);
        else
            p.AddMetaToList(meta, &g_arena.list);
        RecordList(t, g_arena.list);
        break;
    case 3:
        if (r.Range(0, 2) && g_arena.list) // (usually one that's in the list)
            meta = g_arena.list->mp_next && r.Range(0, 1) ? g_arena.list->mp_next->mp_meta : g_arena.list->mp_meta;
        if (original)
            Orig<void(__thiscall*)(void*, CMetaPiece*, CMapListNode**)>(0x00595E80)(&p, meta, &g_arena.list);
        else
            p.RemoveMetaFromList(meta, &g_arena.list);
        RecordList(t, g_arena.list);
        break;
    case 4:
        if (original)
            Orig<void(__thiscall*)(void*, CMetaPiece*)>(0x00595EE0)(&p, meta);
        else
            p.AddMetaToBuckets(meta);
        break;
    case 5:
        if (r.Range(0, 1)) { // the metapiece twice in a bucket under it
            CMapListNode*& bucket = p.mp_bucketList[meta->m_cellArea.x][meta->m_cellArea.z];
            for (int k = 0; k < 2; ++k) {
                CMapListNode& extra = g_arena.bucketNodes[295 + k];
                extra.mp_meta = meta;
                extra.mp_next = bucket;
                bucket = &extra;
            }
        }
        if (original)
            Orig<void(__thiscall*)(void*, CMetaPiece*)>(0x00595FC0)(&p, meta);
        else
            p.RemoveMetaFromBuckets(meta);
        break;
    case 6: {
        GridDims area = MapArea(r);
        int rot = r.Range(0, 3);
        CMetaPiece* with = r.Range(0, 2) ? nullptr : meta;
        bool precise = r.Range(0, 1) != 0;
        CMapListNode* list =
            original ? Orig<CMapListNode*(__thiscall*)(void*, GridDims, int, CMetaPiece*, bool)>(0x00597740)(
                           &p, area, rot, with, precise)
                     : p.GetAreaMetaPieces(area, rot, with, precise);
        RecordList(t, list);
        break;
    }
    case 7: {
        GridDims area = MapArea(r);
        CMapListNode* list = original ? Orig<CMapListNode*(__thiscall*)(void*, GridDims)>(0x00597A00)(&p, area)
                                      : p.GetMetaPiecesAt(area);
        RecordList(t, list);
        break;
    }
    case 8: {
        GridDims cell = MapArea(r);
        int half = -7;
        Ed::GapInfo* gap = original ? Orig<Ed::GapInfo*(__thiscall*)(void*, GridDims*, int*)>(0x00597AC0)(&p, &cell, &half)
                                    : p.GetGapInfo(&cell, &half);
        t.insert(t.end(), {Offset(gap), static_cast<uint32_t>(half), HashBytes(reinterpret_cast<uint8_t*>(&cell), 6)});
        break;
    }
    case 9: {
        GridDims area = MapArea(r);
        t.push_back(original ? Orig<bool(__thiscall*)(void*, GridDims)>(0x005983D0)(&p, area) : p.IsColumnUnslidable(area));
        break;
    }
    case 10: {
        GridDims area = MapArea(r);
        int y = r.Range(-6, 6);
        int height = r.Range(-17, 16);
        uint32_t flags = r.Next() & 7;
        CMapListNode* list = r.Range(0, 1) ? g_arena.list : nullptr;
        bool ok = original ? Orig<bool(__thiscall*)(void*, GridDims, int, int, uint32_t, CMapListNode**)>(0x005984A0)(
                                 &p, area, y, height, flags, &list)
                           : p.SlideColumn(area, y, height, flags, &list);
        t.push_back(ok);
        RecordList(t, list);
        break;
    }
    case 11: {
        bool add = r.Range(0, 1) != 0;
        if (original)
            Orig<void(__thiscall*)(void*, CMetaPiece*, bool)>(0x00598150)(&p, meta, add);
        else
            p.ApplyMetaToFloor(meta, add);
        break;
    }
    }
}

void TestParkManagerAccessors()
{
    std::vector<Saved> saved = {{0x007CE604, 12, {}}, {0x007CE5D4, 4, {}}, {0x007CE5B4, 16, {}},
                                {0x007D1800, 4, {}}, {0x007CC979, 1, {}},
                                {0x007CE5F0, 16, {}}, {0x007CE688, 4, {}}};
    SaveAll(saved);
    struct Named {
        CaseFn fn;
        const char* what;
    };
    for (Named c : {Named{CaseCompressedMap, "CParkManager compressed map header"},
                    Named{CaseShell, "CParkManager::GetShellChecksum"},
                    Named{CaseRebuild, "CParkManager::RebuildParkGeometry"},
                    Named{CaseLookups, "CParkManager lookups (bucket, set, map info, special, abstract meta)"},
                    Named{CaseFloor, "CParkManager floor (slide undo/reset, height, world position, road mask)"},
                    Named{CaseCursor, "CCursor (clipboards, gap name/flags, helper text, area selection, bounds)"},
                    Named{CaseMenuAndGaps, "CUpperMenuManager, instances, CParkManager gap manager copy/highlight"},
                    Named{CaseMap, "CParkManager map (meta lists, buckets, area queries, gap info, floor slide, floor)"}}) {
        std::vector<uint32_t> expected, actual;
        for (uint32_t seed = 1; seed <= 1000; ++seed) {
            std::vector<uint32_t> e = RunCase(c.fn, seed * 7919u, true);
            std::vector<uint32_t> a = RunCase(c.fn, seed * 7919u, false);
            expected.insert(expected.end(), e.begin(), e.end());
            actual.insert(actual.end(), a.begin(), a.end());
        }
        Compare(expected, actual, c.what);
    }
    RestoreAll(saved);
}

} // namespace

SELFTEST(Early, TestParkManagerAccessors, RemoveStandIns);
