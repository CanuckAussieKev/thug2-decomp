// Self-tests for AN-17: a scene's collision grid (src/game/nx/coll_grid.cpp) - the original and ours on identical
// stand-in objects (a fake vtable for their boxes) and memory (a recording bump allocator); the cells, the lists
// the queries return, the calls out and the arena compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/nx/coll_grid.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::BBox;
using Mth::Vector;
using Nx::CCollGrid;
using Nx::CCollObj;

constexpr int kStatics = 40;
constexpr int kMany = 2400;

struct FakeStatic { // (as a CCollStatic: 0x18 bytes)
    void** vtable;
    uint32_t checksum;
    uint16_t flags;
    uint8_t stamp;
    uint8_t pad;
    Nx::CCollObjTriData* geometry;
    uint32_t buffer;
    uint32_t unknown14;
};
static_assert(sizeof(FakeStatic) == 0x18, "a static object is 0x18 bytes");

struct GridArena {
    alignas(16) uint8_t grid[sizeof(CCollGrid)];
    FakeStatic statics[kStatics];
    FakeStatic many[kMany];
    BBox boxes[kStatics];
    Nx::CCollObjTriData geometry[2];
    Mem::ListNode heads[3];
    Mem::ListNode nodes[3][kStatics];
    Nx::CollGridCell spare[2];
    uint8_t manager[0x200];
    uint8_t heap[0x400000];
};
GridArena* g_ga;
bool g_gaOriginal;
uint32_t g_gaUsed;
uint32_t g_reallocAnswer; // whether ReallocateUp / ReallocateShrink work in place (bit per call)
uint32_t g_reallocCalls;

template <typename F>
F GaFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_gaOriginal, address));
}
uint32_t P(const void* p) { return reinterpret_cast<uint32_t>(p); }
uint32_t Where(uint32_t address)
{
    if (address >= P(g_ga) && address < P(g_ga) + sizeof(GridArena))
        return 0xA0000000u + (address - P(g_ga));
    return address;
}
uint32_t Bump(uint32_t size)
{
    if (g_gaUsed + size > sizeof(g_ga->heap)) { // (never: the stand-in heap would have to be made bigger)
        Check(false, "grid test: the stand-in heap is too small");
        g_gaUsed = 0;
    }
    uint32_t at = P(g_ga->heap) + g_gaUsed;
    g_gaUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xE7, size);
    return at;
}
int StaticIndex(const void* object) { return static_cast<int>((P(object) - P(g_ga->statics)) / sizeof(FakeStatic)); }

const BBox* __fastcall StaticBox(FakeStatic* self, void*)
{
    Record({0xB0C50021u, static_cast<uint32_t>(StaticIndex(self))});
    return &g_ga->boxes[StaticIndex(self)];
}
void* g_staticVtable[24];

float Rand(Lcg& random, float low, float high)
{
    return low + (high - low) * static_cast<float>(random.Below(10001)) / 10000.0f;
}
BBox RandomBox(Lcg& random, const BBox& bounds, float size)
{
    BBox box;
    box.min = {Rand(random, bounds.min.x + 1, bounds.max.x + 40), Rand(random, -50, 50),
               Rand(random, bounds.min.z + 1, bounds.max.z + 40), 1};
    box.max = {box.min.x + Rand(random, 0, size), box.min.y + Rand(random, 0, 100), box.min.z + Rand(random, 0, size), 1};
    return box;
}

void RecordList(CCollObj** list)
{
    int n = 0;
    for (; list[n] && n < 0x900; ++n)
        g_trace.push_back(Where(P(list[n])));
    Record({0x11570000u + static_cast<uint32_t>(n)});
}
void RecordCells(CCollGrid* grid)
{
    for (auto& column : grid->m_cells) {
        for (Nx::CollGridCell& cell : column) {
            Record({static_cast<uint32_t>(cell.count), Where(P(cell.objects))});
            for (int i = 0; i < cell.count && i < 64; ++i)
                g_trace.push_back(Where(P(cell.objects[i])));
        }
    }
}

// A list (Lst::Head<CCollObj>) of the given statics.
Mem::ListNode* MakeList(int which, const std::vector<int>& objects)
{
    Mem::ListNode* head = &g_ga->heads[which];
    head->vtable = reinterpret_cast<void**>(0x00645D34);
    head->data = reinterpret_cast<void*>(-1);
    head->next = head;
    head->prev = head;
    for (size_t k = 0; k < objects.size(); ++k) {
        Mem::ListNode* node = &g_ga->nodes[which][k];
        node->vtable = reinterpret_cast<void**>(0x00645AA0);
        node->data = &g_ga->statics[objects[k]];
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
    }
    return head;
}

// The grid: set up over random bounds, its cells built; filled from statics (some without faces, some reaching past
// the far edges); the box and line queries (flags 2 / 4 left out, objects in several cells, single-cell and
// zero-length lines, the count wanted or not); four updates with random added / removed / moved lists (boxes
// changed between them; the blocks resized in place or moved); a line over 2100 objects (the 2047 limit); cleared;
// the cells' constructor and destructor, the pending list's node and head destructors.
void TestAnimCollGrid()
{
    GameMemory memory({{0x00691998, 0x2000}, {0x00693998, 1}, {0x007D1800, 4}, {0x007CC979, 1}});
    StandIns& s = *new StandIns();
    s.At({0x005BBAE0, AddressOf(Mem::Malloc)}, 0, [](Regs& r) {
        Record({0x3A110C00u, r.args[0]});
        return Bump(r.args[0]);
    });
    s.At({0x005BBB60, AddressOf(Mem::FreeMalloc)}, 0, [](Regs& r) {
        Record({0xF4EE3A00u, Where(r.args[0])});
        return 0u;
    });
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Record({0xCA110C00u, r.args[0]});
        uint32_t at = Bump(r.args[0]);
        std::memset(reinterpret_cast<void*>(at), 0, r.args[0]);
        return at;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        return Bump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, Where(r.args[0])});
        return 0u;
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Record({0xC7A00000u, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs&) {
        Record({0xC7A10000u});
        return 0u;
    });
    for (uint32_t address : {0x005BB060u, 0x005BB0A0u}) {
        uint32_t ours = address == 0x005BB060u ? AddressOf(&Mem::Manager::ReallocateUp)
                                               : AddressOf(&Mem::Manager::ReallocateShrink);
        void* thunk = s.At({address}, 12, [address](Regs& r) {
            bool works = (g_reallocAnswer >> (g_reallocCalls++ % 32)) & 1;
            Record({address, r.args[0], Where(r.args[1]), r.args[2], works});
            return works ? r.args[1] : 0u;
        });
        s.Bytes(ours, Branch(0xE9, ours, thunk));
    }
    s.At({0x005BBBA0, AddressOf(Mem::Realloc)}, 0, [](Regs& r) {
        Record({0x5BBBA000u, Where(r.args[0]), r.args[1]});
        uint32_t at = Bump(r.args[1]);
        if (r.args[0])
            std::memcpy(reinterpret_cast<void*>(at), reinterpret_cast<void*>(r.args[0]), r.args[1]);
        return at;
    });

    auto runOnce = [&](bool original) {
        g_gaOriginal = original;
        g_trace.clear();
        std::memset(g_ga, 0xB3, sizeof(GridArena));
        g_gaUsed = 0;
        g_reallocCalls = 0;
        g_reallocAnswer = 0xA5C3965Au;
        memory.Fill(0x418F60);
        GameVariable<void*>(0x007D1800) = g_ga->manager;
        *reinterpret_cast<uint32_t*>(g_ga->manager + 0x110) = 0x70D0110u; // (the top-down and bottom-up heaps)
        *reinterpret_cast<uint32_t*>(g_ga->manager + 0x114) = 0x80770114u;
        GameVariable<bool>(0x007CC979) = true;
        if (!original)
            UseOurSlots({{0x00645D7C, 1}, {0x00645D80, 1}, {0x00645AA0, 1}});
        Lcg random(0x418440);
        auto* grid = reinterpret_cast<CCollGrid*>(g_ga->grid);
        BBox bounds;
        for (int b = 0; b < 40; ++b) { // (sizes that aren't exact in floats)
            bounds.min = {Rand(random, -3000, 0) - 0.0013f * b, -10, Rand(random, -3000, 3000) + 0.0007f * b, 1};
            bounds.max = {bounds.min.x + Rand(random, 10, 9000) + 0.0011f * b, 300, bounds.min.z + Rand(random, 10, 9000), 1};
            GaFn<void(__thiscall*)(CCollGrid*, const BBox*)>(0x00418440)(grid, &bounds);
            for (int k = 0; k < 0x30 / 4; ++k)
                g_trace.push_back(reinterpret_cast<uint32_t*>(grid)[k]);
            for (int c : {0, 7 * 20 + 13, 399}) {
                const auto* w = reinterpret_cast<const uint32_t*>(&grid->m_cells[0][0] + c);
                g_trace.insert(g_trace.end(), w, w + 8);
            }
        }
        bounds.min = {-1234.567f, -10, 777.2501f, 1};
        bounds.max = {2345.789f, 300, 4321.1257f, 1};
        // (a width whose inverse is far from a float: the float and double inverses truncate differently more often)
        for (int t = 0; t < 1000; ++t) {
            float width = static_cast<float>((static_cast<double>(bounds.max.x) - bounds.min.x) * 0.05f);
            double exact = 1.0 / width;
            if (std::fabs(static_cast<float>(exact) - exact) / exact > 2.6e-8)
                break;
            bounds.max.x = std::nextafter(bounds.max.x, 1e30f);
        }
        GaFn<void(__thiscall*)(CCollGrid*, const BBox*)>(0x00418440)(grid, &bounds);
        for (int k = 0; k < 0x30 / 4; ++k)
            g_trace.push_back(reinterpret_cast<uint32_t*>(grid)[k]);
        for (auto& column : grid->m_cells) {
            for (Nx::CollGridCell& cell : column) {
                const auto* w = reinterpret_cast<const uint32_t*>(&cell.box);
                g_trace.insert(g_trace.end(), w, w + 8);
                cell.count = 0;
                cell.objects = nullptr;
            }
        }
        // cell edges (as floats), for boxes and lines right at them
        auto xEdge = [&](int k) { return bounds.min.x + static_cast<float>(k) * grid->m_cellWidth; };
        auto zEdge = [&](int k) { return bounds.min.z + static_cast<float>(k) * grid->m_cellDepth; };
        const float nudges[] = {0.0f, 0.001f, -0.001f, 0.0001f, -0.0001f, 0.3f, -0.3f, 0.5f, -0.5f, 0.49f, 0.51f};
        // Values whose cell depends on the last bits of the arithmetic (found by trying the neighbours of cell
        // edges): box sides where a float and a double inverse cell size truncate differently (Fill's x, the box
        // query's z).
        auto truncate = [](double v) { return static_cast<int>(static_cast<int64_t>(v)); };
        auto neighbours = [](float centre, int steps, std::vector<float>& out) {
            float below = centre, above = centre;
            out.push_back(centre);
            for (int n = 0; n < steps; ++n) {
                below = std::nextafter(below, -1e30f);
                above = std::nextafter(above, 1e30f);
                out.push_back(below);
                out.push_back(above);
            }
        };
        std::vector<float> fillX, boxZ;
        {
            float wf = static_cast<float>(1.0 / grid->m_cellWidth);
            double wd = 1.0 / grid->m_cellWidth;
            float df = static_cast<float>(1.0 / grid->m_cellDepth);
            double dd = 1.0 / grid->m_cellDepth;
            for (int k = 1; k < 19; ++k) {
                std::vector<float> around;
                neighbours(xEdge(k), 512, around);
                for (float c : around) {
                    double d = static_cast<double>(c) - bounds.min.x;
                    if (truncate(d * wf) != truncate(d * wd))
                        fillX.push_back(c);
                }
                around.clear();
                neighbours(zEdge(k) + 0.5f, 512, around);
                for (float c : around) {
                    double d = static_cast<double>(c) - bounds.min.z - 0.5f;
                    if (truncate(d * df) != truncate(d * dd))
                        boxZ.push_back(c);
                }
            }
        }
        Record({static_cast<uint32_t>(fillX.size()), static_cast<uint32_t>(boxZ.size())});
        // the objects
        for (int g = 0; g < 2; ++g)
            g_ga->geometry[g].m_numFaces = static_cast<uint16_t>(g * 12);
        for (int i = 0; i < kStatics; ++i) {
            FakeStatic& o = g_ga->statics[i];
            o.vtable = g_staticVtable;
            o.checksum = 0x5E000000u + i;
            o.flags = static_cast<uint16_t>(i % 9 == 4 ? 2 : i % 11 == 6 ? 4 : i % 5 == 2 ? 0x100 : 0);
            o.stamp = static_cast<uint8_t>(random.Below(256));
            o.geometry = &g_ga->geometry[i % 7 == 3 ? 0 : 1];
            g_ga->boxes[i] = RandomBox(random, bounds, i % 6 == 0 ? 1500.0f : 300.0f);
            if (i % 3 == 2 && !fillX.empty()) { // (where the rounding of the inverse cell size decides)
                g_ga->boxes[i].min.x = fillX[(i * 7) % fillX.size()];
                g_ga->boxes[i].max.x = fillX[(i * 13 + 5) % fillX.size()] + 200.0f;
            }
            if (i % 3 == 1) { // (at cell edges)
                int k = 1 + static_cast<int>(random.Below(15));
                g_ga->boxes[i].min.x = xEdge(k);
                g_ga->boxes[i].max.x = xEdge(k + 1 + static_cast<int>(random.Below(3)));
                g_ga->boxes[i].min.z = zEdge(k + 1);
                g_ga->boxes[i].max.z = zEdge(k + 2);
            }
        }
        GaFn<void(__thiscall*)(CCollGrid*, void*, int)>(0x00418BD0)(grid, g_ga->statics, kStatics);
        RecordCells(grid);
        // the queries
        auto inBox = GaFn<CCollObj**(__thiscall*)(CCollGrid*, const BBox*)>(0x00418710);
        auto along = GaFn<CCollObj**(__thiscall*)(CCollGrid*, const Mth::Line*, int*)>(0x004188C0);
        for (int q = 0; q < 600; ++q) {
            BBox box = RandomBox(random, bounds, q % 4 == 0 ? 2000.0f : 200.0f);
            if (q % 10 == 0)
                box.min.x = bounds.min.x - 100; // (clamped)
            if (q % 2 == 1) { // (half a unit off cell edges, give or take)
                int k = 1 + static_cast<int>(random.Below(17));
                box.min.x = xEdge(k) + 0.5f + random.Pick(nudges);
                box.max.x = xEdge(k + static_cast<int>(random.Below(3))) - 0.5f + random.Pick(nudges);
                box.min.z = zEdge(k) + 0.5f + random.Pick(nudges);
                box.max.z = zEdge(k + 1) - 0.5f + random.Pick(nudges);
            }
            RecordList(inBox(grid, &box));
            Mth::Line line;
            line.start = {Rand(random, bounds.min.x - 50, bounds.max.x + 50), Rand(random, -50, 50),
                          Rand(random, bounds.min.z - 50, bounds.max.z + 50), 1};
            line.end = line.start;
            if (q % 7 != 0) {
                float reach = q % 3 == 0 ? 60.0f : 1500.0f;
                line.end.x += Rand(random, -reach, reach);
                line.end.y += Rand(random, -reach, reach);
                line.end.z += Rand(random, -reach, reach);
            }
            if (q % 5 == 4 && !boxZ.empty()) // (where the rounding of the inverse cell size decides)
                box.min.z = boxZ[q % boxZ.size()];
            if (q % 7 == 1) { // (shorter than a unit)
                line.end = line.start;
                line.end.x += 0.4f;
                line.end.z -= 0.2f;
            }
            if (q % 3 == 2) { // (ends near cell edges)
                int k = 1 + static_cast<int>(random.Below(17));
                line.start.x = xEdge(k) + random.Pick(nudges);
                line.start.z = zEdge(k + 1) + random.Pick(nudges);
                line.end.x = q % 2 ? line.start.x + random.Pick(nudges) : xEdge(k + 1) + random.Pick(nudges);
                line.end.z = zEdge(k + 1 + static_cast<int>(random.Below(2))) + random.Pick(nudges);
            }
            int count = -1;
            RecordList(along(grid, &line, q % 2 ? &count : nullptr));
            Record({static_cast<uint32_t>(count), GameVariable<uint8_t>(0x00693998)});
        }
        // the updates
        auto update = GaFn<void(__thiscall*)(CCollGrid*, Mem::ListNode*, Mem::ListNode*, Mem::ListNode*)>(0x00418F60);
        // (the grid filled again from boxes off the cell edges: at an edge Fill and Update can disagree about a
        // cell, and a removal there that finds nothing makes the original write before the cell's list)
        GaFn<void(__thiscall*)(CCollGrid*)>(0x004183D0)(grid);
        for (int i = 0; i < kStatics; ++i)
            g_ga->boxes[i] = RandomBox(random, bounds, i % 6 == 0 ? 1500.0f : 300.0f);
        GaFn<void(__thiscall*)(CCollGrid*, void*, int)>(0x00418BD0)(grid, g_ga->statics, kStatics);
        RecordCells(grid);
        // (the lists as the game keeps them: objects added that aren't in the grid, removed and moved ones that are,
        // their boxes as they were when they went in unless moved)
        bool inGrid[kStatics];
        for (int i = 0; i < kStatics; ++i)
            inGrid[i] = g_ga->statics[i].geometry->m_numFaces != 0;
        for (int round = 0; round < 16; ++round) {
            std::vector<int> lists[3];
            for (int i = 0; i < kStatics; ++i) {
                uint32_t pick = random.Below(6);
                if (!inGrid[i] && pick < 4 && round != 3)
                    lists[0].push_back(i);
                else if (inGrid[i] && pick == 2)
                    lists[1].push_back(i);
                else if (inGrid[i] && pick >= 3)
                    lists[2].push_back(i);
            }
            for (int i : lists[0])
                g_ga->boxes[i] = RandomBox(random, bounds, 400.0f);
            for (int i : lists[2])
                g_ga->boxes[i] = RandomBox(random, bounds, 400.0f);
            for (int i : lists[0])
                inGrid[i] = true;
            for (int i : lists[1])
                inGrid[i] = false;
            g_reallocAnswer = random.Next() ^ (random.Next() << 8);
            update(grid, MakeList(0, lists[0]), MakeList(1, lists[1]), MakeList(2, lists[2]));
            RecordCells(grid);
        }
        // a line over many objects
        for (int i = 0; i < kMany; ++i) {
            g_ga->many[i].flags = static_cast<uint16_t>(i % 53 == 5 ? 4 : 0);
            g_ga->many[i].stamp = 0;
        }
        for (int c = 0; c < 7; ++c) {
            Nx::CollGridCell* cell = grid->Cell(3, 4 + c);
            cell->count = 400;
            cell->objects = reinterpret_cast<CCollObj**>(Bump(400 * 4));
            for (int i = 0; i < 400; ++i)
                cell->objects[i] = reinterpret_cast<CCollObj*>(&g_ga->many[(c * 370 + i) % kMany]);
        }
        Mth::Line line;
        line.start = grid->Cell(3, 4)->box.min;
        line.start.y = 0;
        line.end = grid->Cell(3, 10)->box.max;
        line.end.y = 0;
        line.end.x = line.start.x + 1;
        int count = 0;
        RecordList(along(grid, &line, &count));
        Record({static_cast<uint32_t>(count)});
        // cleared; the cells' and the pending list's pieces
        GaFn<void(__thiscall*)(CCollGrid*)>(0x004183D0)(grid);
        RecordCells(grid);
        for (Nx::CollGridCell& cell : g_ga->spare) {
            Record({Where(P(GaFn<void*(__thiscall*)(void*)>(0x004183B0)(&cell)))});
            g_trace.insert(g_trace.end(), reinterpret_cast<uint32_t*>(&cell), reinterpret_cast<uint32_t*>(&cell + 1));
        }
        g_ga->spare[0].unknown28 = &g_ga->heap[0x100];
        g_ga->spare[0].objects = reinterpret_cast<CCollObj**>(&g_ga->heap[0x200]);
        for (Nx::CollGridCell& cell : g_ga->spare)
            GaFn<void(__thiscall*)(void*)>(0x00418410)(&cell);
        Mem::ListNode* a = &g_ga->nodes[0][0];
        Mem::ListNode* b = &g_ga->nodes[0][1];
        Mem::ListNode* c = &g_ga->nodes[0][2];
        a->next = b; b->prev = a; b->next = c; c->prev = b; c->next = a; a->prev = c;
        GaFn<void(__thiscall*)(void*)>(0x00418ED0)(a);
        Record({Where(P(GaFn<void*(__thiscall*)(void*, int)>(0x00418EF0)(b, 1)))});
        Record({Where(P(GaFn<void*(__thiscall*)(void*, int)>(0x00418F30)(c, 0)))});
        EndRunPatches();
        for (int i = 0; i < kStatics; ++i)
            Record({g_ga->statics[i].stamp});
        const auto* words = reinterpret_cast<const uint32_t*>(g_ga);
        for (size_t k = 0; k < offsetof(GridArena, manager) / 4; ++k)
            g_trace.push_back(Where(words[k]));
        const auto* heap = reinterpret_cast<const uint32_t*>(g_ga->heap);
        for (uint32_t k = 0; k < (g_gaUsed + 3) / 4; ++k)
            g_trace.push_back(Where(heap[k]));
        Record({g_gaUsed});
        if (original)
            Log("grid: %u bytes of the stand-in heap used", g_gaUsed);
        return g_trace;
    };
    for (void*& slot : g_staticVtable)
        slot = nullptr;
    g_staticVtable[21] = reinterpret_cast<void*>(&StaticBox);
    g_ga = static_cast<GridArena*>(VirtualAlloc(nullptr, sizeof(GridArena), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    VirtualFree(g_ga, 0, MEM_RELEASE);
    delete &s;
    Log("grid: %u trace words", static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx collision grid");
}
SELFTEST(Early, TestAnimCollGrid, TestStandIns::RemoveStandIns);

} // namespace
