// Self-tests for the park editor's manager and cursor (0x0059C900-0x0059E0F7, src/game/park/park_editor.cpp): each
// original against ours on identical stand-in objects; every game function they call is stood in for by recording
// code (over the game's function and our version of it alike), so they're compared call by call.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <vector>

#include "game/core/manager.h"
#include "game/park/park_editor.h"
#include "game/park/park_memory.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using namespace TestStandIns;
using Ed::GridDims;

alignas(16) uint8_t g_edManager[0x17800];
alignas(16) uint8_t g_edCursor[0x100];
alignas(16) uint8_t g_edMetas[12][0x40];
alignas(16) uint8_t g_edNodes[12][12];
alignas(16) uint8_t g_edClipboards[4][0x20];
alignas(16) uint8_t g_edFakeManager[sizeof(Mem::Manager)];
uint32_t g_edGenerator = 0x6E7E6A70;
uint32_t g_edEngine = 0xE6E6E6E0;
int g_edListLength; // nodes GetMetasWithin hands out
int g_edRestartEvery;
bool g_edFull;
int g_edC520;
int g_edHeight;
char g_edLevel;

uint32_t EdRel(uint32_t value)
{
    struct Range {
        const void* base;
        uint32_t size;
        uint32_t tag;
    };
    const Range ranges[] = {{g_edManager, sizeof(g_edManager), 0xB0000000u},
                            {g_edCursor, sizeof(g_edCursor), 0xB1000000u},
                            {g_edMetas, sizeof(g_edMetas), 0xB2000000u},
                            {g_edNodes, sizeof(g_edNodes), 0xB3000000u},
                            {g_edClipboards, sizeof(g_edClipboards), 0xB4000000u}};
    for (const Range& r : ranges) {
        auto base = reinterpret_cast<uint32_t>(r.base);
        if (value >= base && value < base + r.size)
            return r.tag + (value - base);
    }
    return value;
}
uint32_t Area(const Regs& r, int index) // a GridDims passed by value at args[index] (two words)
{
    return r.args[index] ^ ((r.args[index + 1] & 0xFFFF) << 8);
}
uint32_t AreaAt(uint32_t pointer)
{
    if (!pointer)
        return 0xA0A0A0A0;
    auto* bytes = reinterpret_cast<const uint8_t*>(pointer);
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | bytes[3] << 24 | (bytes[4] ^ bytes[5] << 4) << 1;
}

void StandInParkEditor(StandIns& s)
{
    auto plain = [&s](uint32_t address, uint16_t pop, uint32_t tag, int args, uint32_t ours = 0) {
        s.At(ours ? std::initializer_list<uint32_t>{address, ours} : std::initializer_list<uint32_t>{address}, pop, [tag, args](Regs& r) {
            Record({tag, EdRel(r.ecx)});
            for (int i = 0; i < args; ++i)
                Record({EdRel(r.args[i])});
            return 0u;
        });
    };
    plain(0x0059AE90, 8, 0xAE90, 2);  // DestroyConcreteMeta
    s.At({0x0059C600}, 8, [](Regs& r) { Record({0xC600, EdRel(r.ecx), AreaAt(r.args[0]), r.args[1]}); return 0u; });
    s.At({0x00597A00}, 8, [](Regs& r) {
        Record({0x7A00, EdRel(r.ecx), Area(r, 0)});
        // a list of g_edListLength nodes over the metas
        for (int i = 0; i < 12; ++i) {
            auto* node = reinterpret_cast<uint32_t*>(g_edNodes[i]);
            node[1] = reinterpret_cast<uint32_t>(g_edMetas[i]);
            node[2] = i + 1 < g_edListLength ? reinterpret_cast<uint32_t>(g_edNodes[i + 1]) : 0;
        }
        return g_edListLength ? reinterpret_cast<uint32_t>(g_edNodes[0]) : 0u;
    });
    plain(0x00593610, 0, 0x3610, 0);
    plain(0x00592090, 0, 0x2090, 0);
    s.At({0x004F4970}, 0, [](Regs&) { Record({0x4970}); return g_edEngine; });
    s.At({0x00591C60}, 0, [](Regs& r) {
        Record({0x1C60, *reinterpret_cast<uint32_t*>(r.ecx) ? EdRel(*reinterpret_cast<uint32_t*>(r.ecx)) : 0u});
        return 0u;
    });
    plain(0x00596E50, 0, 0x6E50, 0);
    plain(0x00596980, 0, 0x6980, 0);
    plain(0x0059B020, 4, 0xB020, 1);
    s.At({0x0059E110, AddressOf(&Ed::ClearStats)}, 0, [](Regs&) { Record({0xE110}); return 0u; });
    plain(0x0059B750, 0, 0xB750, 0);
    s.At({0x0059BED0}, 8, [](Regs& r) {
        Record({0xBED0, EdRel(r.ecx), Area(r, 0), GameVariable<uint8_t>(0x0067F398)});
        return 0u;
    });
    plain(0x004F6650, 0, 0x6650, 0);
    s.At({0x00590160}, 0, [](Regs& r) { Record({0x0160, r.args[0] & 0xFF}); return 0x5E1F0000u; });
    s.At({0x0058DDC0}, 0, [](Regs& r) { Record({0xDDC0, r.ecx}); return g_edFull ? 1u : 0u; });
    plain(0x0058D290, 0, 0xD290, 0);
    s.At({0x0059E730, AddressOf(&Ed::GetMemoryStats)}, 0, [](Regs& r) {
        Record({0xE730});
        return r.args[0];
    });
    s.At({0x005902F0}, 0, [](Regs& r) {
        uint32_t type = *reinterpret_cast<uint32_t*>(r.ecx + 0x38);
        Record({0x02F0, EdRel(r.ecx)});
        return g_edRestartEvery && type % g_edRestartEvery == 0 ? 1u : 0u;
    });
    s.At({0x00595A90}, 12, [](Regs& r) {
        Record({0x5A90, EdRel(r.ecx), AreaAt(r.args[1])});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        auto* size = reinterpret_cast<float*>(r.args[2]);
        out[0] = 1.0e8f + 3.0f;
        out[1] = -4.0f;
        out[2] = 0.3f;
        size[0] = 7.0f; // (1e8+3 + 7: rounded differently through double than in float)
        size[1] = 2.5f;
        size[2] = 1.0e-9f;
        return r.args[0];
    });
    s.At({0x004F74B0}, 0x14, [](Regs& r) {
        Record({0x74B0, r.ecx, r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        return 0u;
    });
    s.At({0x0059B090}, 0x10, [](Regs& r) {
        Record({0xB090, EdRel(r.ecx), EdRel(r.args[0]), r.args[1], r.args[2], r.args[3]});
        // (it changes the piece's height: the cells cleared must be from the copy taken before)
        reinterpret_cast<uint8_t*>(r.args[0])[4] ^= 1;
        reinterpret_cast<uint8_t*>(r.args[0])[7] ^= 1;
        return 0u;
    });
    s.At({0x005959D0}, 0xC, [](Regs& r) {
        Record({0x59D0, EdRel(r.ecx), Area(r, 0)});
        *reinterpret_cast<char*>(r.args[2]) = g_edLevel;
        return static_cast<uint32_t>(g_edHeight);
    });
    s.At({0x0059C520}, 0x18, [](Regs& r) {
        Record({0xC520, EdRel(r.ecx), Area(r, 0), r.args[2], r.args[3], r.args[4] & 0xFF, r.args[5] & 0xFF});
        return static_cast<uint32_t>(g_edC520);
    });
    plain(0x00595CB0, 8, 0x5CB0, 2);
    plain(0x0059B280, 0, 0xB280, 0);
    plain(0x005988A0, 0, 0x88A0, 0);
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xABA0, EdRel(r.args[0])});
        return 0u;
    });
    s.At({0x00595210}, 8, [](Regs& r) {
        Record({0x5210, EdRel(r.ecx), r.args[0], EdRel(r.args[1])});
        return reinterpret_cast<uint32_t>(Ed::Sets(reinterpret_cast<Ed::CParkManager*>(r.ecx)) + r.args[0]);
    });
    plain(0x00595450, 4, 0x5450, 1);
    plain(0x0059BC00, 4, 0xBC00, 1);
    plain(0x0058C200, 0, 0xC200, 0, AddressOf(&Ed::CClipboard::DestroyMetas));
    plain(0x005954D0, 0, 0x54D0, 0);
}

// The manager, the cursor, the pieces and clipboards of one case (the same for both runs).
void SetUpParkEditor(int variant)
{
    std::memset(g_edManager, 0, sizeof(g_edManager));
    std::memset(g_edCursor, 0, sizeof(g_edCursor));
    auto* manager = reinterpret_cast<Ed::CParkManager*>(g_edManager);
    auto* cursor = reinterpret_cast<Ed::CCursor*>(g_edCursor);
    manager->Generator() = reinterpret_cast<void*>(g_edGenerator);
    manager->ParkDims() = {3, 0, 4, 40, 9, 30};
    manager->FloorBuilt() = variant % 3 != 0;
    manager->Flags() = 0x10 | (variant & 7);
    manager->At<uint16_t>(0x13C96) = variant % 4 == 1 ? 0x1111 : 0x4E24;
    // a dirty grid
    for (uint32_t i = 0x3520 + 8; i < 0x3520 + 8 + 58 * 58 * 16; ++i)
        g_edManager[i] = static_cast<uint8_t>(i * 7 + variant);
    // sets: visible or not, clipboard sets, entries
    manager->NumSets() = 7;
    for (int i = 0; i < 7; ++i) {
        Ed::PieceSet& set = Ed::Sets(manager)[i];
        set.m_visible = ((variant >> (i % 5)) & 1) || i == 4;
        set.m_isClipboard = i == 2 || i == 5;
        set.m_numEntries = 3 + i;
        set.m_selected = (i * 2 + variant) % set.m_numEntries;
        for (int e = 0; e < 40; ++e)
            set.m_entries[e].checksum = 0x7E000000u + i * 0x100 + e;
    }
    for (int i = 0; i < 12; ++i) {
        uint8_t* meta = g_edMetas[i];
        std::memset(meta, 0, 0x40);
        const uint32_t flags[] = {0, 8, 0x100, 0x200, 0x108, 0, 0x300, 0, 0x100, 8, 0, 0x4};
        *reinterpret_cast<uint32_t*>(meta + 0xC) = flags[i] ^ ((variant & 1) ? 0x100 : 0);
        const uint8_t areas[][6] = {{2, 0, 3, 2, 1, 2}, {1, 1, 1, 3, 2, 1},   {5, 0xFE, 5, 1, 1, 1},
                                    {0, 2, 0, 4, 1, 4}, {10, 3, 12, 2, 2, 3}, {6, 0, 7, 1, 1, 2},
                                    {3, 4, 3, 2, 1, 2}, {8, 0xFF, 2, 3, 1, 1}, {2, 0, 2, 1, 3, 1},
                                    {4, 1, 6, 2, 1, 2}, {9, 0, 9, 0, 1, 3},   {1, 5, 1, 2, 2, 2}};
        std::memcpy(meta + 4, areas[i], 6);
        *reinterpret_cast<uint32_t*>(meta + 0x38) = 100 + i;
    }
    for (int i = 0; i < 4; ++i)
        *reinterpret_cast<uint32_t*>(g_edClipboards[i] + 0x14) =
            i < 3 ? reinterpret_cast<uint32_t>(g_edClipboards[i + 1]) : 0;
    cursor->Manager() = manager;
    cursor->Meta() = variant % 2 ? g_edMetas[3] : nullptr;
    cursor->Clipboards() = variant % 3 ? g_edClipboards[0] : nullptr;
    cursor->SetIndex() = variant % 7;
    cursor->Mode() = variant % 4 == 2 ? 5 : 4;
    g_edListLength = variant % 5 == 4 ? 0 : 12 - variant % 3;
    g_edRestartEvery = 2 + variant % 3;
    g_edFull = variant % 2 == 1;
    g_edC520 = variant % 3 != 1;
    g_edHeight = variant * 3 - 4;
    g_edLevel = static_cast<char>(variant & 1);
}

void RecordParkEditor()
{
    Record({HashBytes(g_edManager, sizeof(g_edManager)), HashBytes(g_edCursor, sizeof(g_edCursor)),
            HashBytes(&g_edMetas[0][0], sizeof(g_edMetas)), GameVariable<uint8_t>(0x0067F398)});
    const auto* cursor = reinterpret_cast<const uint32_t*>(g_edCursor);
    for (int i = 0; i < 0x70 / 4; ++i)
        Record({EdRel(cursor[i])});
}

void TestParkEditor()
{
    std::vector<Saved> saved = {{0x007CE604, 8, {}}, {0x0067F398, 1, {}}, {0x007D1800, 4, {}}};
    SaveAll(saved);
    auto* standIns = new StandIns;
    StandInParkEditor(*standIns);
    std::memset(g_edFakeManager, 0, sizeof(g_edFakeManager));
    GameVariable<void*>(0x007D1800) = g_edFakeManager;

    using Manager = Ed::CParkManager;
    using Cursor = Ed::CCursor;
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_trace.clear();
        for (int variant = 0; variant < 12; ++variant) {
            auto* manager = reinterpret_cast<Manager*>(g_edManager);
            auto* cursor = reinterpret_cast<Cursor*>(g_edCursor);
            GameVariable<Manager*>(0x007CE604) = manager;
            GridDims area = {static_cast<uint8_t>(2 + variant), static_cast<int8_t>(variant - 5),
                             static_cast<uint8_t>(variant % 4), static_cast<uint8_t>(1 + variant % 3), 2,
                             static_cast<uint8_t>(3 - variant % 3)};
            bool flags[3] = {(variant & 1) != 0, (variant & 2) != 0, (variant & 4) != 0};

            SetUpParkEditor(variant);
            for (uint32_t how : {0u, 0x1A4E0EF9u}) {
                if (original)
                    Original<void(__thiscall*)(Manager*, uint32_t)>(0x0059CB10)(manager, how);
                else
                    manager->RebuildPark(how);
                RecordParkEditor();
            }
            SetUpParkEditor(variant);
            for (bool check : {false, true}) {
                bool ok = original ? Original<bool(__thiscall*)(Manager*, const GridDims*, bool)>(0x0059CC70)(manager, &area, check)
                                   : manager->RebuildArea(&area, check);
                Record({0xCC70, ok ? 1u : 0u});
            }
            SetUpParkEditor(variant);
            bool destroyed = original ? Original<bool(__thiscall*)(Manager*, GridDims, bool, bool, bool)>(0x0059CD20)(
                                            manager, area, flags[0], flags[1], flags[2])
                                      : manager->DestroyMetasInAreaAndRebuild(area, flags[0], flags[1], flags[2]);
            Record({0xCD20, destroyed ? 1u : 0u});
            RecordParkEditor();
            SetUpParkEditor(variant);
            if (original)
                Original<void(__thiscall*)(Manager*, GridDims)>(0x0059CEC0)(manager, area);
            else
                manager->ClearArea(area);
            RecordParkEditor();
            SetUpParkEditor(variant);
            for (int dir : {1, -1, 0}) {
                bool changed = original ? Original<bool(__thiscall*)(Manager*, GridDims, int)>(0x0059D080)(manager, area, dir)
                                        : manager->ChangeFloorHeight(area, dir);
                Record({0xD080, changed ? 1u : 0u});
            }
            // the cursor
            SetUpParkEditor(variant);
            for (int dir : {1, -1, 3, -9, 1, 1, 1, 1}) {
                if (original)
                    Original<void(__thiscall*)(Cursor*, int)>(0x0059C950)(cursor, dir);
                else
                    cursor->ChangePieceInSet(dir);
                RecordParkEditor();
            }
            for (int dir : {1, -1, 2, -3, 1, 1, 1, 6, -1}) {
                SetUpParkEditor(variant + dir + 20);
                if (original)
                    Original<void(__thiscall*)(Cursor*, int)>(0x0059C9B0)(cursor, dir);
                else
                    cursor->ChangeSet(dir);
                RecordParkEditor();
            }
            SetUpParkEditor(variant);
            GameVariable<Cursor*>(0x007CE608) = variant % 4 == 3 ? nullptr : cursor;
            if (original)
                Original<void(__cdecl*)()>(0x0059C900)();
            else
                Cursor::Destroy();
            Record({EdRel(GameVariable<uint32_t>(0x007CE608))});
            RecordParkEditor();
        }
        traces[run] = g_trace;
    }
    RemoveStandIns();
    RestoreAll(saved);
    Check(traces[0].size() > 3000, "park editor: the original reached its calls");
    Compare(traces[0], traces[1], "park editor's manager and cursor (0x0059C900-0x0059D080)");
}

} // namespace

SELFTEST(Early, TestParkEditor, RemoveStandIns);
