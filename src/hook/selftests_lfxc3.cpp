// Self-tests for the park editor's cursor and the manager's set-up (0x0059D120-0x0059E0F7,
// src/game/park/park_cursor.cpp): each original against ours on identical stand-in objects; every game function
// they call is stood in for by recording code (over the game's function and our version of it alike).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/pool.h"
#include "game/park/park_editor.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/appmain_testkit.h"
#include "hook/standins.h"

namespace {

// Our replacement of a game function: stood in for too (our code calls it directly).
inline uint32_t OursOf(uint32_t address)
{
    void* ours = AppMainTests::OursAt(address);
    return ours ? TestStandIns::Resolve(ours) : address;
}


using namespace TestStandIns;
using Ed::GapDescriptor;
using Ed::GridDims;
using Script::CComponent;
using Script::CStruct;

alignas(16) uint8_t g_cuManager[0x17800];
alignas(16) uint8_t g_cuCursor[0x100];
alignas(16) uint8_t g_cuMetas[12][0x40];
alignas(16) uint8_t g_cuNodes[12][12];
alignas(16) uint8_t g_cuClipboards[4][0x20];
alignas(16) uint8_t g_cuFakeManager[sizeof(Mem::Manager)];
alignas(16) uint8_t g_cuPools[2][sizeof(Mem::Pool)];
alignas(16) uint8_t g_cuArena[0x400];
alignas(16) GapDescriptor g_cuGap;
uint32_t g_cuArenaUsed;
int g_cuListLength;
int g_cuVariant;
bool g_cuFull;
bool g_cuFits;
bool g_cuFreeSlot;
bool g_cuGapFound;
bool g_cuGapAdded;
int g_cuHalf;
int g_cuHeight;
char g_cuLevel;
bool g_cuRaise;
bool g_cuMemoryOk;

uint32_t CuRel(uint32_t value)
{
    struct Range {
        const void* base;
        uint32_t size;
        uint32_t tag;
    };
    const Range ranges[] = {{g_cuManager, sizeof(g_cuManager), 0xC0000000u},
                            {g_cuCursor, sizeof(g_cuCursor), 0xC1000000u},
                            {g_cuMetas, sizeof(g_cuMetas), 0xC2000000u},
                            {g_cuNodes, sizeof(g_cuNodes), 0xC3000000u},
                            {g_cuClipboards, sizeof(g_cuClipboards), 0xC4000000u},
                            {g_cuArena, sizeof(g_cuArena), 0xC5000000u},
                            {&g_cuGap, sizeof(g_cuGap), 0xC6000000u},
                            {g_cuFakeManager, sizeof(g_cuFakeManager), 0xC7000000u}};
    for (const Range& r : ranges) {
        auto base = reinterpret_cast<uint32_t>(r.base);
        if (value >= base && value < base + r.size)
            return r.tag + (value - base);
    }
    // (a pointer to a local: the original's and ours are at different places on the stack)
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    if (value >= reinterpret_cast<uint32_t>(tib->StackLimit) && value < reinterpret_cast<uint32_t>(tib->StackBase))
        return 0x57AC0000;
    return value;
}
uint32_t CuArea(const Regs& r, int index) { return r.args[index] ^ ((r.args[index + 1] & 0xFFFF) << 8); }
uint32_t CuAreaAt(uint32_t pointer)
{
    if (!pointer)
        return 0xA0A0A0A0;
    auto* bytes = reinterpret_cast<const uint8_t*>(pointer);
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24 | (bytes[4] ^ bytes[5] << 4) << 1;
}
uint32_t CuList()
{
    for (int i = 0; i < 12; ++i) {
        auto* node = reinterpret_cast<uint32_t*>(g_cuNodes[i]);
        node[1] = reinterpret_cast<uint32_t>(g_cuMetas[i]);
        node[2] = i + 1 < g_cuListLength ? reinterpret_cast<uint32_t>(g_cuNodes[i + 1]) : 0;
    }
    return g_cuListLength ? reinterpret_cast<uint32_t>(g_cuNodes[0]) : 0u;
}

// The script's structures of the piece sets (built by hand: names, firsts, flags).
struct TestStruct {
    alignas(16) uint8_t storage[8];
    CComponent components[6];
    int count = 0;
    CStruct* Struct() { return reinterpret_cast<CStruct*>(storage); }
    void Reset()
    {
        std::memset(storage, 0, sizeof(storage));
        std::memset(components, 0, sizeof(components));
        count = 0;
    }
    void Add(Script::ESymbolType type, uint32_t name, uint32_t raw)
    {
        CComponent& c = components[count];
        c.typeAndFlag = static_cast<uint8_t>(type << 1);
        c.name = name;
        c.raw = raw;
        if (count)
            components[count - 1].next = &c;
        else
            reinterpret_cast<CComponent**>(storage)[1] = &c;
        ++count;
    }
};
TestStruct g_cuSetStructs[5];
TestStruct g_cuPieceStructs[12];
uint32_t g_cuSetElements[5];
uint32_t g_cuPieceElements[12];
alignas(16) uint8_t g_cuSetsArray[12];
alignas(16) uint8_t g_cuPiecesArray[12];
const char* const g_cuSetNames[5] = {"Ramps", "Rails", "Clipboards", "Hidden", "Misc"};
const char* const g_cuTextNames[12] = {"Quarter", "Half", "Kicker", "Box", "Rail", "Pool",
                                       "Bowl",    "Wall", "Gap",    "Roof", "Pipe", "Stair"};

void SetUpScriptArrays(int variant)
{
    auto crc = [](const char* text) { return Crc::GenerateCRCFromString(text); };
    for (int i = 0; i < 5; ++i) {
        TestStruct& s = g_cuSetStructs[i];
        s.Reset();
        s.Add(Script::ESYMBOLTYPE_STRING, crc("name"), reinterpret_cast<uint32_t>(g_cuSetNames[i]));
        if (i != 2 && !(i == 4 && variant % 2))
            s.Add(Script::ESYMBOLTYPE_NAME, crc("first"), 100u + ((i * 3 + variant) % 12));
        if (i == 2)
            s.Add(Script::ESYMBOLTYPE_NAME, 0, 0x45C6C158);
        if (i == 3)
            s.Add(Script::ESYMBOLTYPE_NAME, 0, crc("hidden"));
        g_cuSetElements[i] = reinterpret_cast<uint32_t>(s.Struct());
    }
    for (int i = 0; i < 12; ++i) {
        TestStruct& s = g_cuPieceStructs[i];
        s.Reset();
        if (i % 4 != 3)
            s.Add(Script::ESYMBOLTYPE_STRING, crc("text_name"), reinterpret_cast<uint32_t>(g_cuTextNames[i]));
        g_cuPieceElements[i] = reinterpret_cast<uint32_t>(s.Struct());
    }
    auto* sets = reinterpret_cast<Script::CArray*>(g_cuSetsArray);
    std::memset(sets, 0, 12);
    sets->m_type = Script::ESYMBOLTYPE_STRUCTURE;
    sets->m_size = 5;
    sets->m_elements = g_cuSetElements;
    auto* pieces = reinterpret_cast<Script::CArray*>(g_cuPiecesArray);
    std::memset(pieces, 0, 12);
    pieces->m_type = Script::ESYMBOLTYPE_STRUCTURE;
    pieces->m_size = 12;
    pieces->m_elements = g_cuPieceElements;
}

void StandInCursor(StandIns& s)
{
    auto plain = [&s](std::initializer_list<uint32_t> addresses, uint16_t pop, uint32_t tag, int args) {
        s.At(addresses, pop, [tag, args](Regs& r) {
            Record({tag, CuRel(r.ecx)});
            for (int i = 0; i < args; ++i)
                Record({CuRel(r.args[i])});
            return 0u;
        });
    };
    s.At({0x0058BF10}, 8, [](Regs& r) {
        Record({0xBF10, CuRel(r.ecx), CuRel(r.args[0])});
        auto* area = reinterpret_cast<uint8_t*>(r.args[1]);
        uint32_t index = (r.ecx - reinterpret_cast<uint32_t>(g_cuClipboards)) / 0x20;
        const uint8_t sizes[4][6] = {{1, 0, 2, 5, 1, 6}, {3, 1, 4, 30, 2, 2}, {0, 0, 0, 2, 3, 41}, {7, 2, 7, 4, 1, 4}};
        std::memcpy(area, sizes[(index + g_cuVariant) % 4], 6);
        return 0u;
    });
    plain({0x0058C980}, 0, 0xC980, 0);
    s.At({0x005BA3B0, AddressOf(&Mem::Pool::Contains)}, 4, [](Regs& r) {
        auto* pool = reinterpret_cast<Mem::Pool*>(r.ecx);
        bool contains = r.args[0] >= reinterpret_cast<uint32_t>(pool->m_start) &&
                        r.args[0] < reinterpret_cast<uint32_t>(pool->m_end);
        Record({0xA3B0, CuRel(r.ecx), CuRel(r.args[0]), contains ? 1u : 0u});
        return contains ? 1u : 0u;
    });
    plain({0x005BA400, AddressOf(&Mem::Pool::Free)}, 4, 0xA400, 1);
    plain({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, 0xABA0, 1);
    s.At({0x0059C950, AddressOf(&Ed::CCursor::ChangePieceInSet)}, 4, [](Regs& r) {
        Record({0xC950, CuRel(r.ecx), r.args[0]});
        return 0u;
    });
    s.At({0x00599CD0}, 0xC, [](Regs& r) { Record({0x9CD0, CuRel(r.ecx), CuArea(r, 0), r.args[2]}); return g_cuFits ? 1u : 0u; });
    s.At({0x005959D0}, 0xC, [](Regs& r) {
        Record({0x59D0, CuRel(r.ecx), CuArea(r, 0), r.args[2] ? 1u : 0u});
        if (r.args[2])
            *reinterpret_cast<char*>(r.args[2]) = g_cuLevel;
        return static_cast<uint32_t>(g_cuHeight);
    });
    s.At({0x00591F50, OursOf(0x00591F50)}, 4, [](Regs& r) { Record({0x1F50, r.ecx, r.args[0]}); return g_cuFreeSlot ? 1u : 0u; });
    s.At({0x00401C30, AddressOf(static_cast<int (*)(const char*, ...)>(&Dbg::Printf))}, 0, [](Regs& r) {
        Record({0x1C30, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0xFFFFFFFFu;
    });
    s.At({0x005BA1D0}, 0, [](Regs& r) { Record({0xA1D0, r.args[0]}); return 0u; }); // (cdecl: ECX is anything)
    s.At({0x00595620}, 8, [](Regs& r) { Record({0x5620, CuRel(r.ecx), r.args[0], r.args[1]}); return 0x5620000u; });
    s.At({0x00595670}, 8, [](Regs& r) { Record({0x5670, CuRel(r.ecx), r.args[0], r.args[1]}); return 0x5670000u; });
    plain({0x005907B0, OursOf(0x005907B0)}, 4, 0x07B0, 1);
    s.At({0x00599A20}, 0xC, [](Regs& r) { Record({0x9A20, CuRel(r.ecx), CuArea(r, 0), r.args[2]}); return 0u; });
    s.At({0x0059CC70, AddressOf(&Ed::CParkManager::RebuildArea)}, 8, [](Regs& r) {
        Record({0xCC70, CuRel(r.ecx), CuRel(r.args[0]), CuAreaAt(r.args[0]), r.args[1] & 0xFF});
        return g_cuMemoryOk || !(r.args[1] & 0xFF) ? 1u : 0u;
    });
    s.At({0x0059CD20, AddressOf(&Ed::CParkManager::DestroyMetasInAreaAndRebuild)}, 0x14, [](Regs& r) {
        Record({0xCD20, CuRel(r.ecx), CuArea(r, 0), r.args[2] & 0xFF, r.args[3] & 0xFF, r.args[4] & 0xFF});
        return static_cast<uint32_t>(g_cuVariant & 1);
    });
    s.At({0x0059CEC0, AddressOf(&Ed::CParkManager::ClearArea)}, 8, [](Regs& r) {
        Record({0xCEC0, CuRel(r.ecx), CuArea(r, 0)});
        return 0u;
    });
    s.At({0x00590160, OursOf(0x00590160)}, 0, [](Regs& r) { Record({0x0160, r.args[0] & 0xFF}); return 0x5E1F0000u; });
    s.At({0x0058DDC0}, 0, [](Regs& r) { Record({0xDDC0, r.ecx}); return g_cuFull ? 1u : 0u; });
    s.At({0x0058D290}, 0, [](Regs&) { Record({0xD290}); return 0u; }); // (cdecl)
    s.At({0x00597740}, 0x14, [](Regs& r) {
        Record({0x7740, CuRel(r.ecx), CuArea(r, 0), r.args[2], r.args[3], r.args[4]});
        return CuList();
    });
    plain({0x00591C60, OursOf(0x00591C60)}, 0, 0x1C60, 0);
    plain({0x00591BE0, OursOf(0x00591BE0)}, 0, 0x1BE0, 0);
    s.At({0x00590210, OursOf(0x00590210)}, 0, [](Regs& r) { Record({0x0210, CuRel(r.ecx), CuAreaAt(r.ecx)}); return 0u; });
    s.At({0x00597AC0}, 8, [](Regs& r) {
        Record({0x7AC0, CuRel(r.ecx), CuRel(r.args[0]), CuAreaAt(r.args[0])});
        *reinterpret_cast<int*>(r.args[1]) = g_cuHalf;
        return g_cuGapFound ? reinterpret_cast<uint32_t>(&g_cuGap) : 0u;
    });
    s.At({0x00597FC0}, 4, [](Regs& r) {
        Record({0x7FC0, CuRel(r.ecx), CuRel(r.args[0]), HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 0x54)});
        return 0u;
    });
    s.At({0x00597BD0}, 4, [](Regs& r) {
        Record({0x7BD0, CuRel(r.ecx), CuRel(r.args[0]), HashBytes(reinterpret_cast<uint8_t*>(r.args[0]), 0x54)});
        return g_cuGapAdded ? 1u : 0u;
    });
    s.At({0x0059C600}, 8, [](Regs& r) { Record({0xC600, CuRel(r.ecx), CuAreaAt(r.args[0]), r.args[1]}); return 0u; });
    plain({0x00595CB0}, 8, 0x5CB0, 2);
    s.At({0x006234FE}, 0, StandInSprintf);
    plain({0x00591A50, OursOf(0x00591A50)}, 8, 0x1A50, 2);
    plain({0x00592290, OursOf(0x00592290)}, 4, 0x2290, 1);
    s.At({0x00597110}, 4, [](Regs& r) {
        Record({0x7110, CuRel(r.ecx)});
        const uint8_t area[6] = {4, 1, 5, static_cast<uint8_t>(3 + g_cuVariant), 2, static_cast<uint8_t>(12 - g_cuVariant)};
        std::memcpy(reinterpret_cast<void*>(r.args[0]), area, 6);
        return 0u;
    });
    s.At({0x0059C520}, 0x18, [](Regs& r) {
        Record({0xC520, CuRel(r.ecx), CuArea(r, 0), r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5] & 0xFF});
        return g_cuRaise ? 1u : 0u;
    });
    plain({0x00598B60}, 8, 0x8B60, 2);
    plain({0x0059B280}, 0, 0xB280, 0);
    plain({0x0058C200}, 0, 0xC200, 0);
    // the manager's set-up
    s.At({0x0059EC00, AddressOf(&Ed::Init)}, 0, [](Regs& r) { Record({0xEC00, r.args[0]}); return 0u; });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xAFC0, r.args[0], r.args[1], r.args[2]});
        if (g_cuVariant % 5 == 3)
            return 0u;
        uint8_t* block = g_cuArena + g_cuArenaUsed;
        g_cuArenaUsed = (g_cuArenaUsed + 0xB0) % 0x300;
        return reinterpret_cast<uint32_t>(block);
    });
    plain({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, 0xB0E0, 1);
    plain({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, 0xB110, 0);
    s.At({0x0058CCC0}, 4, [](Regs& r) { Record({0xCCC0, CuRel(r.ecx), CuRel(r.args[0])}); return r.ecx; });
    plain({0x00594FD0}, 8, 0x4FD0, 2);
    plain({0x005960D0}, 0, 0x60D0, 0);
    plain({0x00596D50}, 0, 0x6D50, 0);
    plain({0x00596CD0}, 0, 0x6CD0, 0);
    plain({0x0059CB10, AddressOf(&Ed::CParkManager::RebuildPark)}, 4, 0xCB10, 1);
    plain({0x00592C10}, 0, 0x2C10, 0);
    s.At({0x00479090, AddressOf(static_cast<Script::CArray* (*)(const char*)>(&Script::GetArray))}, 0, [](Regs& r) {
        auto* name = reinterpret_cast<const char*>(r.args[0]);
        Record({0x9090, Hash(name)});
        return std::strcmp(name, "Ed_piece_sets") == 0 ? reinterpret_cast<uint32_t>(g_cuSetsArray)
                                                       : reinterpret_cast<uint32_t>(g_cuPiecesArray);
    });
    s.At({0x00478FD0, AddressOf(static_cast<const char* (*)(uint32_t)>(&Script::GetString))}, 0, [](Regs& r) {
        Record({0x8FD0, r.args[0]});
        return reinterpret_cast<uint32_t>("Paste");
    });
}

void SetUpCursor(int variant)
{
    g_cuVariant = variant;
    std::memset(g_cuManager, 0, sizeof(g_cuManager));
    std::memset(g_cuCursor, 0, sizeof(g_cuCursor));
    auto* manager = reinterpret_cast<Ed::CParkManager*>(g_cuManager);
    auto* cursor = reinterpret_cast<Ed::CCursor*>(g_cuCursor);
    manager->Generator() = reinterpret_cast<void*>(0x6E7E6A70);
    manager->ParkDims() = {2, 0, 3, static_cast<uint8_t>(30 + variant), 9, static_cast<uint8_t>(40 - variant * 2)};
    manager->Flags() = 0x10 | (variant & 7);
    manager->At<uint16_t>(0x13C98) = static_cast<uint16_t>(0x3000 + variant);
    manager->At<bool>(0x4) = variant % 6 == 5;
    for (uint32_t i = 0x3528; i < 0x3528 + 58 * 58 * 16; ++i)
        g_cuManager[i] = static_cast<uint8_t>(i * 13 + variant);
    for (int slot = 0; slot < 8; ++slot)
        manager->At<uint32_t>(0x177A4 + slot * 4) =
            variant % 3 == 1 ? 100u + (slot * 7 + variant) % 12
                             : (slot == 7 && variant % 2 == 0 ? 100u + (variant % 12 * 5) % 12 : 200u + slot);
    std::memset(g_cuManager + 0x1076C, 0xEE, 5 * 0x154); // (the sets: dirty)
    for (int i = 0; i < 12; ++i) {
        uint8_t* meta = g_cuMetas[i];
        std::memset(meta, 0, 0x40);
        *reinterpret_cast<uint32_t*>(meta + 0xC) = (i % 3 == 0 ? 0x1000u : 0u) | (i % 4 == 1 ? 0x100u : 0u);
        const uint8_t area[6] = {static_cast<uint8_t>(1 + i), static_cast<uint8_t>(i % 3),
                                 static_cast<uint8_t>(9 - i % 5), static_cast<uint8_t>(1 + i % 4),
                                 static_cast<uint8_t>(i % 7 + variant % 3 * 4), static_cast<uint8_t>(2 + i % 3)};
        std::memcpy(meta + 4, area, 6);
        *reinterpret_cast<uint32_t*>(meta + 0x38) = 100 + (i * 5) % 12;
    }
    // the manager's standard pieces: a list (counted)
    g_cuListLength = 12;
    manager->At<uint32_t>(0x80) = CuList();
    manager->At<int>(0x88) = 12;
    manager->At<uint32_t>(0x7C) = CuList();
    g_cuListLength = variant % 5 == 4 ? 0 : 12 - variant % 4;
    for (int i = 0; i < 4; ++i)
        *reinterpret_cast<uint32_t*>(g_cuClipboards[i] + 0x14) =
            i < 3 ? reinterpret_cast<uint32_t>(g_cuClipboards[i + 1]) : 0;
    cursor->Manager() = manager;
    cursor->Area() = {static_cast<uint8_t>(5 + variant), static_cast<int8_t>(variant % 3 - 1), 6, 2,
                      static_cast<uint8_t>(3 + variant), 3};
    cursor->Rotation() = variant % 4;
    cursor->Meta() = g_cuMetas[variant % 12];
    cursor->Clipboards() = variant % 3 ? g_cuClipboards[0] : nullptr;
    cursor->CurrentClipboard() = variant % 4 ? g_cuClipboards[variant % 4] : nullptr;
    cursor->Mode() = 3 + variant % 3;
    cursor->Selecting() = variant % 3 != 1;
    cursor->GapCounter() = variant % 4;
    cursor->At<bool>(0x55) = true;
    GapDescriptor& gap = cursor->Gap();
    std::memset(&gap, 0x11, sizeof(gap));
    gap.m_numCompleteHalves = variant % 3 == 2 ? 1 : 0;
    gap.m_pos[0] = {static_cast<uint8_t>(5 + variant % 2), 0, 6, 1, 1, 1};
    std::memset(&g_cuGap, 0, sizeof(g_cuGap));
    g_cuGap.m_pos[0] = {static_cast<uint8_t>(5 + ((variant / 2) % 2) * 50), 0, 6, 2, 1, 3};
    g_cuGap.m_pos[1] = {static_cast<uint8_t>(variant % 4), 1, static_cast<uint8_t>((variant % 3) * 20), 2, 1, 3};
    for (int h = 0; h < 2; ++h) {
        g_cuGap.m_rot[h] = variant + h * 5;
        g_cuGap.m_leftExtent[h] = (variant * 2 + h) % 8 - 1;
        g_cuGap.m_rightExtent[h] = (variant * 3 + h) % 8;
    }
    g_cuFull = variant % 7 == 6;
    g_cuFits = variant % 5 != 2;
    g_cuFreeSlot = variant % 2 == 0;
    g_cuGapFound = variant % 4 != 1;
    g_cuGapAdded = variant % 5 != 0;
    g_cuHalf = variant % 2;
    g_cuHeight = (variant * 5) % 23 - 9;
    g_cuLevel = static_cast<char>(variant % 2);
    g_cuRaise = variant % 3 != 2;
    g_cuMemoryOk = variant % 2 == 0;
    // clipboard pools: the first two in pool 0, the third in pool 1, the last in neither
    auto* pools = reinterpret_cast<Mem::Pool*>(g_cuPools);
    std::memset(g_cuPools, 0, sizeof(g_cuPools));
    pools[0].m_start = g_cuClipboards[0];
    pools[0].m_end = g_cuClipboards[2];
    pools[1].m_start = g_cuClipboards[2];
    pools[1].m_end = g_cuClipboards[3];
    Mem::PoolPair& pair = Ed::ClipboardPools();
    pair.current = variant % 2;
    pair.pools[0] = &pools[0];
    pair.pools[1] = variant % 3 == 1 ? nullptr : &pools[1];
}

void RecordCursor()
{
    Record({HashBytes(g_cuManager, sizeof(g_cuManager)), HashBytes(g_cuCursor, sizeof(g_cuCursor)),
            HashBytes(&g_cuClipboards[0][0], sizeof(g_cuClipboards)), HashBytes(&g_cuMetas[0][0], sizeof(g_cuMetas))});
    const auto* cursor = reinterpret_cast<const uint32_t*>(g_cuCursor);
    for (int i = 0; i < 0x70 / 4; ++i)
        Record({CuRel(cursor[i])});
}

void TestParkCursor()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x007D1800, 4, {}}, {0x007CC979, 1, {}}, {0x007CE5B4, 16, {}},
                                {0x006F3624, 4, {}}};
    SaveAll(saved);
    static Script::CSymbolTableEntry* table[0x1000]; // (an empty symbol table: flags are looked up in it)
    std::memset(table, 0, sizeof(table));
    GameVariable<Script::CSymbolTableEntry**>(0x006F3624) = table;
    auto* standIns = new StandIns;
    StandInCursor(*standIns);
    std::memset(g_cuFakeManager, 0, sizeof(g_cuFakeManager));
    reinterpret_cast<Mem::Manager*>(g_cuFakeManager)->m_topDownHeap = reinterpret_cast<Mem::Heap*>(0x70D0);
    reinterpret_cast<Mem::Manager*>(g_cuFakeManager)->m_bottomUpHeap = reinterpret_cast<Mem::Heap*>(0xB0B0);
    GameVariable<void*>(0x007D1800) = g_cuFakeManager;
    GameVariable<bool>(0x007CC979) = true;

    using Manager = Ed::CParkManager;
    using Cursor = Ed::CCursor;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        for (int variant = 0; variant < 14; ++variant) {
            auto* manager = reinterpret_cast<Manager*>(g_cuManager);
            auto* cursor = reinterpret_cast<Cursor*>(g_cuCursor);
            GameVariable<Manager*>(0x007CE604) = manager;
            using BoolFn = bool(__thiscall*)(Cursor*);
            auto callBool = [&](uint32_t address, bool (Cursor::*ours)()) {
                bool result = original ? Original<BoolFn>(address)(cursor) : (cursor->*ours)();
                Record({address, result ? 1u : 0u});
                RecordCursor();
            };
            SetUpCursor(variant);
            if (original)
                Original<void(__thiscall*)(Cursor*)>(0x0059D120)(cursor);
            else
                cursor->RemoveClipboardsTooBig();
            RecordCursor();
            SetUpCursor(variant);
            callBool(0x0059D240, &Cursor::PlacePiece);
            for (int extra : {0, 1}) { // (at the height limit, and one over)
                SetUpCursor(variant);
                cursor->Area().h = static_cast<uint8_t>(17 - g_cuHeight + extra);
                callBool(0x0059D240, &Cursor::PlacePiece);
            }
            SetUpCursor(variant);
            callBool(0x0059D380, &Cursor::DestroyUnderCursor);
            SetUpCursor(variant);
            callBool(0x0059D410, &Cursor::AddGapHalf);
            callBool(0x0059D410, &Cursor::AddGapHalf);
            SetUpCursor(variant);
            callBool(0x0059D600, &Cursor::RemoveGapHalf);
            for (int turn : {0, 1, -2}) {
                for (int left : {0, 3, -9}) {
                    for (int right : {0, 2, 9, -1}) {
                        SetUpCursor(variant + turn * 3 + left + right + 20);
                        bool changed = original ? Original<bool(__thiscall*)(Cursor*, int, int, int)>(0x0059D6B0)(
                                                      cursor, turn, left, right)
                                                : cursor->ChangeGap(turn, left, right);
                        Record({0xD6B0, changed ? 1u : 0u});
                        RecordCursor();
                    }
                }
            }
            SetUpCursor(variant);
            callBool(0x0059D8E0, &Cursor::DestroySelection);
            SetUpCursor(variant);
            if (original)
                Original<void(__thiscall*)(Cursor*)>(0x0059D980)(cursor);
            else
                cursor->FlattenSelection();
            RecordCursor();
            SetUpCursor(variant);
            GridDims area = {static_cast<uint8_t>(variant), static_cast<int8_t>(2 - variant), 3,
                             static_cast<uint8_t>(8 + variant % 5), 2, static_cast<uint8_t>(12 - variant % 5)};
            bool raised = original ? Original<bool(__thiscall*)(Cursor*, GridDims, int, int, char)>(0x0059D9F0)(
                                         cursor, area, variant - 3, variant % 3 - 1, static_cast<char>(variant & 1))
                                   : cursor->RaiseArea(area, variant - 3, variant % 3 - 1, static_cast<char>(variant & 1));
            Record({0xD9F0, raised ? 1u : 0u});
            // (and to exactly -16, and high enough that only the pieces on the area go over the top)
            for (int dir : {1, -1, -30, 25, -16 - ((variant * 5) % 23 - 9)}) {
                SetUpCursor(variant);
                bool done = original ? Original<bool(__thiscall*)(Cursor*, int)>(0x0059DAA0)(cursor, dir)
                                     : cursor->RaiseLower(dir);
                Record({0xDAA0, done ? 1u : 0u});
                RecordCursor();
            }
            for (uint32_t level : {0xCD3682C1u, 0x7ECA21E5u, 0x12345678u}) {
                SetUpCursor(variant);
                SetUpScriptArrays(variant);
                g_cuArenaUsed = 0;
                if (original)
                    Original<void(__thiscall*)(Manager*, uint32_t)>(0x0059DCD0)(manager, level);
                else
                    manager->Initialize(level);
                RecordCursor();
            }
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 5000, "park cursor: the original reached its calls");
    Compare(traces[0], traces[1], "park editor's cursor and set-up (0x0059D120-0x0059DCD0)");
}

} // namespace

SELFTEST(Early, TestParkCursor, RemoveStandIns);
