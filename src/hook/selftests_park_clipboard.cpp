// Self-tests for the park editor's clipboards (src/game/ed/clipboard.cpp): the original and ours on the same stand-in
// arena (viewer_testkit.h's), the park manager, the rail editor and the park memory stood in for by recording code;
// every result, the calls and the whole arena compared.

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <new>
#include <vector>

#include "game/core/pool.h"
#include "game/ed/park.h"
#include "hook/appmain_testkit.h"
#include "hook/viewer_testkit.h"

namespace {

// Our replacement of a game function: stood in for too (our code calls it directly).
inline uint32_t OursOf(uint32_t address)
{
    void* ours = AppMainTests::OursAt(address);
    return ours ? TestStandIns::Resolve(ours) : address;
}


using namespace ViewerTestKit;
using Ed::CClipboard;
using Ed::CClipboardEntry;
using Ed::CCursor;
using Ed::CMapListNode;
using Ed::CMetaPiece;
using Ed::CParkManager;
using Ed::GridDims;

constexpr uint32_t kManager = 0x2000;
constexpr uint32_t kCursor = 0x2100;
constexpr uint32_t kClipboard = 0x2200;
constexpr uint32_t kMetas = 0x3000;   // placed metapieces, 0x40 apart
constexpr uint32_t kSources = 0x3800; // palette metapieces, 0x40 apart
constexpr uint32_t kNodes = 0x4000;   // list nodes, 0x10 apart
constexpr uint32_t kMade = 0x5000;    // metapieces "made" by CreateConcreteMeta, 0x40 apart
constexpr uint32_t kRails = 0x6000;   // rails, 0x20 apart
constexpr uint32_t kMemory = 0x6800;
constexpr uint32_t kCursorPos = 0x6900;

std::map<uint32_t, uint32_t> g_cells; // x << 8 | z -> first node offset
int g_made;
int g_floor;
int g_floorSteps;
bool g_room;
int g_freeRailPoints;
int g_railsMade;
uint32_t g_railList;
uint32_t g_areaList;

uint32_t Area(const Regs& r, int i) { return r.args[i]; }
uint32_t AreaHigh(const Regs& r, int i) { return r.args[i] & 0xFFFF; }
void RecVector(uint32_t pointer)
{
    for (int i = 0; i < 4; ++i)
        g_trace.push_back(reinterpret_cast<const uint32_t*>(pointer)[i]);
}

void Install(StandIns& s)
{
    InstallCommon(s);
    s.At({0x00595200, AddressOf(&CParkManager::sInstance)}, 0, [](Regs&) { return At(kManager); });
    s.At({0x00599CD0, AddressOf(&CParkManager::CanChangeFloorHere)}, 0xC, [](Regs& r) {
        Rec(0x9CD0, {r.ecx, Area(r, 0), AreaHigh(r, 1), r.args[2] & 0xFF});
        return (r.args[0] & 0xFF) == 33 ? 0u : 1u; // (cell x 33 can't be changed)
    });
    s.At({0x005959D0, AddressOf(&CParkManager::GetFloorHeight)}, 0xC, [](Regs& r) {
        Rec(0x59D0, {r.ecx, Area(r, 0), AreaHigh(r, 1)}); // (the flag in: uninitialised in GetHeight)
        *reinterpret_cast<uint8_t*>(r.args[2]) = (g_floor & 1) ? 1 : 0;
        return static_cast<uint32_t>(g_floor + (r.args[0] & 3)); // (by the area's x)
    });
    s.At({0x0059D080, AddressOf(static_cast<bool (CParkManager::*)(GridDims, int)>(&CParkManager::ChangeFloorHeight))},
         0xC, [](Regs& r) {
             Rec(0xD080, {r.ecx, Area(r, 0), AreaHigh(r, 1), r.args[2]});
             g_floor += g_floorSteps;
             return 1u;
         });
    s.At({0x00595670, AddressOf(&CParkManager::CreateConcreteMeta)}, 8, [](Regs& r) {
        Rec(0x5670, {r.ecx, r.args[0], r.args[1]});
        uint32_t made = At(kMade + 0x40 * g_made++);
        std::memset(reinterpret_cast<void*>(made), 0, 0x3C);
        reinterpret_cast<CMetaPiece*>(made)->m_flags = 0x11;
        return made;
    });
    s.At({0x005907B0, OursOf(0x005907B0)}, 4, [](Regs& r) {
        Rec(0x07B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00599A20, AddressOf(&CParkManager::AddMetaPieceToPark)}, 0xC, [](Regs& r) {
        Rec(0x9A20, {r.ecx, Area(r, 0), AreaHigh(r, 1), r.args[2]});
        return 0u;
    });
    s.At({0x00597A00, AddressOf(&CParkManager::GetAreaMetaPieces)}, 8, [](Regs& r) {
        Rec(0x7A00, {r.ecx, Area(r, 0), AreaHigh(r, 1)});
        return g_areaList;
    });
    s.At({0x00591BE0, AddressOf(&CMapListNode::DestroyList)}, 0, [](Regs& r) {
        Rec(0x1BE0, {r.ecx});
        return 0u;
    });
    s.At({0x00591A50, OursOf(0x00591A50)}, 8, [](Regs& r) {
        Rec(0x1A50, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00591800, OursOf(0x00591800)}, 0x14, [](Regs& r) {
        Rec(0x1800, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[4]}); // (w left out: the original's is garbage)
        return 0u;
    });
    s.At({0x0059AE90, AddressOf(&CParkManager::DestroyConcreteMeta)}, 8, [](Regs& r) {
        Rec(0xAE90, {r.ecx, r.args[0], r.args[1]});
        return 0u;
    });
    s.At({0x00595140, AddressOf(&CParkManager::RebuildParkGeometry)}, 4, [](Regs& r) {
        Rec(0x5140, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00595620, AddressOf(&CParkManager::GetAbstractMetaPiece)}, 8, [](Regs& r) {
        Rec(0x5620, {r.ecx, r.args[0], r.args[1]});
        return At(kSources + 0x40 * (r.args[0] & 7));
    });
    s.At({0x005951E0, AddressOf(&CParkManager::GetMapListNode)}, 8, [](Regs& r) {
        Rec(0x51E0, {r.ecx, r.args[0], r.args[1]});
        auto it = g_cells.find(r.args[0] << 8 | r.args[1]);
        return it == g_cells.end() ? 0u : At(it->second);
    });
    s.At({0x00590750, OursOf(0x00590750)}, 4, [](Regs& r) {
        Rec(0x0750, {r.ecx, *reinterpret_cast<uint32_t*>(r.args[0]), *reinterpret_cast<uint16_t*>(r.args[0] + 4)});
        return (reinterpret_cast<CMetaPiece*>(r.ecx)->m_flags & 0x40) ? 0u : 1u; // (0x40: "not within")
    });
    s.At({0x00595A90, AddressOf(&CParkManager::GridCoordinatesToWorld)}, 0xC, [](Regs& r) {
        const auto* area = reinterpret_cast<const GridDims*>(r.args[1]);
        Rec(0x5A90, {r.ecx, *reinterpret_cast<const uint32_t*>(area), *reinterpret_cast<const uint16_t*>(r.args[1] + 4)});
        auto* out = reinterpret_cast<float*>(r.args[0]);
        out[0] = area->x * 120.0f - 3480.0f;
        out[1] = area->y * 48.0f + 0.5f;
        out[2] = area->z * 120.0f - 3479.25f;
        out[3] = 1.0f;
        if (r.args[2]) {
            auto* size = reinterpret_cast<float*>(r.args[2]);
            size[0] = area->w * 120.0f;
            size[1] = area->h * 48.0f;
            size[2] = area->l * 120.0f + 0.375f;
            size[3] = 0.0f;
        }
        return r.args[0];
    });
    // The rail editor.
    s.At({0x004F4970}, 0, [](Regs&) { return At(0x6F00); });
    s.At({0x004F7230}, 0x10, [](Regs& r) {
        Rec(0x7230, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return g_railList;
    });
    s.At({0x004F6BF0}, 0, [](Regs& r) {
        Rec(0x6BF0, {r.ecx});
        return static_cast<uint32_t>(g_freeRailPoints);
    });
    s.At({0x004F72C0}, 0, [](Regs& r) {
        Rec(0x72C0, {r.ecx});
        return At(kRails + 0x100 + 0x20 * g_railsMade++);
    });
    s.At({0x004F3B90}, 0, [](Regs& r) {
        Rec(0x3B90, {r.ecx});
        return 3u;
    });
    s.At({0x004F6F40}, 4, [](Regs& r) {
        Rec(0x6F40, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004F4370}, 0xC, [](Regs& r) {
        Rec(0x4370, {r.ecx, r.args[2]}); // (the vectors by value: ours are on the stack)
        RecVector(r.args[0]);
        RecVector(r.args[1]);
        return 0u;
    });
    for (uint32_t address : {0x004F61C0u, 0x004F6210u})
        s.At({address}, 0xC, [address](Regs& r) {
            Rec(address & 0xFFFF, {r.ecx, r.args[2]});
            RecVector(r.args[0]);
            RecVector(r.args[1]);
            return 0u;
        });
    for (uint32_t address : {0x004F4220u, 0x004F41C0u, 0x004F70F0u})
        s.At({address}, 0, [address](Regs& r) {
            Rec(address & 0xFFFF, {r.ecx});
            return 0u;
        });
    // The park memory.
    s.At({0x00590160, OursOf(0x00590160)}, 0, [](Regs& r) {
        Rec(0x0160, {r.args[0] & 0xFF});
        return At(kMemory);
    });
    s.At({0x0058D290}, 0, [](Regs&) {
        Rec(0xD290, {});
        return 0u;
    });
    s.At({0x0058D6D0}, 8, [](Regs& r) {
        Rec(0xD6D0, {r.ecx, r.args[0], r.args[1]});
        return g_room ? 1u : 0u;
    });
    s.At({0x0058D6F0}, 0, [](Regs& r) {
        Rec(0xD6F0, {r.ecx});
        return At(kCursorPos);
    });
}

CMetaPiece* Meta(uint32_t offset, GridDims area, uint32_t flags, int rot, uint32_t name)
{
    auto* meta = Ptr<CMetaPiece>(offset);
    std::memset(meta, 0, 0x3C);
    meta->m_cell_area = area;
    meta->m_flags = flags;
    meta->m_rot = rot;
    meta->m_name_checksum = name;
    return meta;
}

void TestParkClipboard()
{
    SaveGlobals({{0x007CE5C4, 16, {}}, {0x007CCD40, 16, {}}});
    StandIns* s = new StandIns;
    Install(*s);
    std::vector<uint32_t> traces[2];
    for (int run = 0; run < 2; ++run) {
        const bool original = run == 0;
        std::vector<uint32_t>& t = traces[run];
        SetUpArena(run);
        g_cells.clear();
        g_made = 0;
        g_railsMade = 0;
        g_areaList = 0;
        g_floor = -2;
        g_floorSteps = 1;
        // The entries' pool: 6 entries, real (made the same way in both runs).
        std::memset(reinterpret_cast<void*>(0x007CE5C4), 0, 16);
        std::memset(reinterpret_cast<void*>(0x007CCD40), 0, 16);
        void* poolMemory = Mem::New(sizeof(Mem::Pool));
        GameVariable<Mem::Pool*>(0x007CE5C4 + 8) = new (poolMemory) Mem::Pool(sizeof(CClipboardEntry), 8, "entries");
        auto rec = [&](uint32_t value) { t.push_back(Rel(value)); };

        // The palette (names 0-7 by their low bits) and the placed pieces over a 3 x 2 area at (30, 40): a 2x1 one in
        // two cells (a duplicate), a riser below (lowest, 15 up), a riser floor, a restart piece, one not wholly
        // inside, one at height -3, an inner-shell one.
        for (int i = 0; i < 8; ++i)
            Meta(kSources + 0x40 * i, GridDims::Make(0, 0, 0, static_cast<uint8_t>(1 + (i & 1)), 1,
                                                     static_cast<uint8_t>(1 + (i >> 1 & 1))),
                 i == 5 ? CMetaPiece::kInnerShell : 0, 0, 0x1000 + i);
        CMetaPiece* metas[10] = {
            Meta(kMetas + 0x000, GridDims::Make(30, 1, 40, 2, 1, 1), 0, 1, 0x1001),
            Meta(kMetas + 0x040, GridDims::Make(31, -16, 41, 1, 1, 1), 0, 0, 0x4A0D22DB),
            Meta(kMetas + 0x080, GridDims::Make(32, -2, 40, 1, 1, 1), 0, 2, 0xB0A98147),
            Meta(kMetas + 0x0C0, GridDims::Make(30, 0, 41, 1, 1, 1), 0, 0, 0xCA07B4FC),
            Meta(kMetas + 0x100, GridDims::Make(32, 2, 41, 1, 1, 1), 0x40, 0, 0x1003),
            Meta(kMetas + 0x140, GridDims::Make(31, 0, 40, 1, 2, 2), 0, 3, 0x1006),
            Meta(kMetas + 0x180, GridDims::Make(32, 0, 41, 1, 1, 1), 0, 0, 0x1005),
            Meta(kMetas + 0x1C0, GridDims::Make(30, 4, 40, 1, 1, 1), 0, 2, 0x1002),
            // (the first one again, one step higher / turned: not duplicates)
            Meta(kMetas + 0x240, GridDims::Make(30, 2, 40, 2, 1, 1), 0, 1, 0x1001),
            Meta(kMetas + 0x280, GridDims::Make(30, 1, 40, 2, 1, 1), 0, 2, 0x1001),
        };
        auto node = [&](int index, int meta, int next) {
            auto* n = Ptr<CMapListNode>(kNodes + 0x10 * index);
            n->m_dy = 0;
            n->mp_meta = metas[meta];
            n->mp_next = next < 0 ? nullptr : Ptr<CMapListNode>(kNodes + 0x10 * next);
        };
        node(0, 0, 1); node(1, 7, 9); node(9, 8, 10); node(10, 9, -1); // (30, 40)
        node(2, 0, 3); node(3, 5, -1);    // (31, 40)
        node(4, 2, -1);                   // (32, 40)
        node(5, 3, -1);                   // (30, 41)
        node(6, 1, -1);                   // (31, 41)
        node(7, 4, 8); node(8, 6, -1);    // (32, 41)
        g_cells[30 << 8 | 40] = kNodes + 0x00;
        g_cells[31 << 8 | 40] = kNodes + 0x20;
        g_cells[32 << 8 | 40] = kNodes + 0x40;
        g_cells[30 << 8 | 41] = kNodes + 0x50;
        g_cells[31 << 8 | 41] = kNodes + 0x60;
        g_cells[32 << 8 | 41] = kNodes + 0x70;
        // The rails copied: two.
        *Ptr<uint32_t>(kRails) = At(kRails + 0x20);
        *Ptr<uint32_t>(kRails + 0x20) = 0;
        g_railList = At(kRails);

        auto* clipboard = Ptr<CClipboard>(kClipboard);
        std::memset(clipboard, 0xCD, sizeof(CClipboard));
        rec(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*)>(0x0058BDF0)(clipboard)
                                                : clipboard->Construct()));
        auto isEmpty = [&] {
            rec(original ? Original<bool(__thiscall*)(void*)>(0x0058B7E0)(clipboard) : clipboard->IsEmpty());
        };
        isEmpty();
        const GridDims area = GridDims::Make(30, 2, 40, 3, 1, 2);
        rec(original ? Original<bool(__thiscall*)(void*, GridDims)>(0x0058C590)(clipboard, area)
                     : clipboard->CopySelectionToClipboard(area));
        isEmpty();
        CClipboardEntry* entries = clipboard->mp_metas;
        clipboard->mp_metas = nullptr; // (rails only)
        isEmpty();
        clipboard->mp_metas = entries;
        PushArena(t);

        // The cursor, turned 0-4.
        auto* cursor = Ptr<CCursor>(kCursor);
        std::memset(cursor, 0, sizeof(CCursor));
        cursor->m_cell_area = GridDims::Make(20, 1, 25, 1, 1, 1);
        cursor->m_clipboardHeight = 2;
        for (int rot = 0; rot < 5; ++rot) {
            cursor->m_rot = rot;
            GridDims out = GridDims::Make(0xEE, -18, 0xEE, 0xEE, 0xEE, 0xEE);
            original ? Original<void(__thiscall*)(void*, void*, void*)>(0x0058BF10)(clipboard, cursor, &out)
                     : clipboard->GetArea(cursor, &out);
            rec(*reinterpret_cast<uint32_t*>(&out));
            rec(*reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(&out) + 4));
            g_floor = rot - 3;
            rec(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, void*)>(0x0058BE30)(clipboard, cursor)
                                               : clipboard->GetHeight(cursor)));
            original ? Original<void(__thiscall*)(void*, void*, int)>(0x0058BEB0)(clipboard, cursor, rot & 1)
                     : clipboard->HighlightIntersectingMetas(cursor, rot & 1);
            // Each entry's area directly too (an entry without a metapiece once).
            for (CClipboardEntry* entry = clipboard->mp_metas; entry; entry = entry->mp_next) {
                GridDims dims = GridDims::Make(0xEE, -18, 0xEE, 0xEE, 0xEE, 0xEE);
                original ? Original<void(__thiscall*)(void*, void*, uint8_t, uint8_t, void*)>(0x0058B800)(entry, &dims, 31, 40, cursor)
                         : entry->GetArea(&dims, 31, 40, cursor);
                rec(*reinterpret_cast<uint32_t*>(&dims));
                rec(*reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(&dims) + 4));
            }
        }
        {
            CClipboardEntry bare = {};
            bare.m_x = 40;
            bare.m_z = 29;
            bare.m_w = 2;
            bare.m_l = 3;
            bare.m_y = -2;
            bare.m_rot = 3;
            for (int rot = 0; rot < 4; ++rot) {
                cursor->m_rot = rot;
                GridDims dims = GridDims::Make(0xEE, -18, 0xEE, 0xEE, 0xEE, 0xEE);
                original ? Original<void(__thiscall*)(void*, void*, uint8_t, uint8_t, void*)>(0x0058B800)(&bare, &dims, 31, 40, cursor)
                         : bare.GetArea(&dims, 31, 40, cursor);
                rec(*reinterpret_cast<uint32_t*>(&dims));
                rec(*reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(&dims) + 4));
            }
        }
        // The highlight's list: two metapieces, one of them the inner shell's.
        *Ptr<CMapListNode>(kNodes + 0x100) = {0, metas[5], Ptr<CMapListNode>(kNodes + 0x110)};
        *Ptr<CMapListNode>(kNodes + 0x110) = {0, Meta(kMetas + 0x200, GridDims::Make(1, 1, 1, 1, 1, 1), 8, 0, 0x1007), nullptr};
        g_areaList = At(kNodes + 0x100);
        cursor->m_rot = 1;
        original ? Original<void(__thiscall*)(void*, void*, int)>(0x0058BEB0)(clipboard, cursor, 1)
                 : clipboard->HighlightIntersectingMetas(cursor, 1);
        PushArena(t);

        // Shown at angles (the previews made the first time), its centre, hidden again.
        const float angles[] = {0.0f, 37.5f, -90.0f, 1234.5f};
        for (float angle : angles) {
            const Mth::Vector at = {100.25f, 50.5f, -75.125f, 1.0f};
            original ? Original<void(__thiscall*)(void*, Mth::Vector, float, int)>(0x0058C130)(clipboard, at, angle, 3)
                     : clipboard->ShowMetas(at, angle, 3);
        }
        Mth::Vector centre = {9, 9, 9, 9};
        rec(Rel(reinterpret_cast<uint32_t>(original ? Original<void*(__thiscall*)(void*, void*)>(0x0058C050)(clipboard, &centre)
                                                    : clipboard->GetCentre(&centre))));
        for (int i = 0; i < 4; ++i)
            rec(std::bit_cast<uint32_t>((&centre.x)[i]));
        PushArena(t);
        original ? Original<void(__thiscall*)(void*)>(0x0058C200)(clipboard) : clipboard->DestroyMetas();
        PushArena(t);

        // Pasted: no room; an entry that can't go (cursor at x 33 for one); too many rail points; then it goes (the
        // floor raised step by step, stopped by a flat floor or the limit).
        auto showAt = [&] {
            rec(original ? Original<bool(__thiscall*)(void*, void*)>(0x0058C780)(clipboard, cursor) : clipboard->ShowAt(cursor));
        };
        *Ptr<Mth::Vector>(kCursorPos) = {1.5f, 2.5f, 3.5f, 1.0f};
        g_room = false;
        showAt();
        g_room = true;
        cursor->m_rot = 0;
        cursor->m_cell_area.x = 33;
        g_freeRailPoints = 10;
        showAt();
        cursor->m_cell_area.x = 21;
        g_freeRailPoints = 5;
        showAt();
        g_freeRailPoints = 6;
        for (int variant = 0; variant < 3; ++variant) {
            cursor->m_rot = variant + 1;
            cursor->m_clipboardHeight = variant == 2 ? 20 : variant - 1;
            g_floor = variant == 1 ? -40 : -1;
            g_floorSteps = variant == 1 ? 0 : 1;
            showAt();
        }
        PushArena(t);

        // Entries straight: paste one of the inner shell, heights over 15.
        {
            CClipboardEntry shell = {};
            shell.mp_meta = Ptr<CMetaPiece>(kSources + 0x40 * 5);
            shell.mp_meta->m_cell_area.h = 4;
            shell.m_y = 13;
            original ? Original<void(__thiscall*)(void*, uint8_t, int, uint8_t, void*)>(0x0058B9C0)(&shell, 31, 1, 40, cursor)
                     : shell.Paste(31, 1, 40, cursor);
            CClipboardEntry none = {};
            original ? Original<void(__thiscall*)(void*, uint8_t, int, uint8_t, Mth::Vector, float)>(0x0058BCA0)(
                           &none, 1, 2, 3, Mth::Vector{1, 2, 3, 4}, 5.0f)
                     : none.ShowPreview(1, 2, 3, Mth::Vector{1, 2, 3, 4}, 5.0f);
        }

        // Shown again (previews), then destroyed; a copy that runs out of entries.
        original ? Original<void(__thiscall*)(void*, Mth::Vector, float, int)>(0x0058C130)(clipboard, Mth::Vector{1, 2, 3, 1}, 10.0f, 0)
                 : clipboard->ShowMetas(Mth::Vector{1, 2, 3, 1}, 10.0f, 0);
        original ? Original<void(__thiscall*)(void*)>(0x0058C430)(clipboard) : clipboard->Destroy();
        isEmpty();
        PushArena(t);
        auto* small = Ptr<CClipboard>(kClipboard + 0x40);
        std::memset(small, 0xCD, sizeof(CClipboard));
        small->Construct();
        rec(static_cast<uint32_t>(original ? Original<int(__thiscall*)(void*, void*)>(0x0058BE30)(small, cursor)
                                           : small->GetHeight(cursor))); // (no entries)
        Mem::Pool* pool = GameVariable<Mem::Pool*>(0x007CE5C4 + 8);
        for (int i = 0; i < 4; ++i)
            pool->Allocate(); // (4 left: the copy needs 7)
        rec(original ? Original<bool(__thiscall*)(void*, GridDims)>(0x0058C590)(small, area)
                     : small->CopySelectionToClipboard(area));
        PushArena(t);
        t.insert(t.end(), g_trace.begin(), g_trace.end());
    }
    Cleanup();
    Compare(traces[0], traces[1], "park clipboard: copy, turn, show, paste, destroy");
}

} // namespace

SELFTEST(Early, TestParkClipboard, ViewerTestKit::Cleanup);
