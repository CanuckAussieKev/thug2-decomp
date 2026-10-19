// Self-tests for AN-8: the collision geometry (CCollObjTriData) and its BSP tree (CCollBSPNode) in
// src/game/nx/coll_tri.cpp, and Mth::Matrix::operator== - the original and ours on identical hand-made geometry;
// allocations, frees, the walk's globals and the arena compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_tri.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Matrix;
using Mth::Vector;
using Nx::CCollBSPNode;
using Nx::CCollObjTriData;

struct BspArena {
    alignas(16) uint8_t heap[0x2400];
    CCollObjTriData geometry[3];
    float plain[2][8][3];
    uint16_t packed[8][3];
    uint8_t faces10[8 * 10];
    uint8_t faces8[8 * 8];
    uint32_t nodes[3][1 + 7 * 2];
    uint16_t lists[3][12];
    uint8_t vertexBytes[8];
    uint16_t gathered[32];
    Vector vectors[12];
    Vector out[8];
    Matrix matrices[2];
    uint8_t manager[0x200];
};
BspArena g_bsp;
uint32_t g_bspUsed;
bool g_bspOriginal;

template <typename F>
F BspFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_bspOriginal, address));
}
float BRand(Lcg& random) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 917.0f; }
uint32_t BspBump(uint32_t size)
{
    uint32_t at = reinterpret_cast<uint32_t>(g_bsp.heap) + g_bspUsed;
    g_bspUsed += (size + 15) & ~15u;
    std::memset(reinterpret_cast<void*>(at), 0xCD, size);
    return at;
}
uint32_t U(const void* p) { return reinterpret_cast<uint32_t>(p); }

// Tree `t`: a root split on x, its children on z and y, four leaves (their face lists out of order, so the first
// list isn't the first leaf's). `file`: offsets as in a file (children from the array's start, leaf lists as u16
// indices) for Relocate; `swaps` sets the side-swap bit on some nodes.
CCollBSPNode* BuildTree(int t, bool file, uint32_t swaps)
{
    uint32_t* memory = g_bsp.nodes[t];
    memory[0] = 7;
    auto* nodes = reinterpret_cast<CCollBSPNode*>(memory + 1);
    uint16_t* lists = g_bsp.lists[t];
    auto child = [&](int index, int bit) {
        uint32_t at = file ? static_cast<uint32_t>(index * 8) : U(&nodes[index]);
        return at | ((swaps >> bit) & 1) | (bit == 1 ? 2u : 0u);
    };
    nodes[0].m_split = (24 << 2) | 0;
    nodes[0].m_link = child(1, 0);
    nodes[1].m_split = (-36 << 2) | 2;
    nodes[1].m_link = child(3, 1);
    nodes[2].m_split = (64 << 2) | 1;
    nodes[2].m_link = child(5, 2);
    const int counts[4] = {2, 3, 1, 3};
    const int starts[4] = {4, 6, 0, 1};
    for (int i = 0; i < 4; ++i) {
        nodes[3 + i].m_split = 3 | (0xA8 << 2) | (counts[i] << 16);
        nodes[3 + i].m_link = file ? static_cast<uint32_t>(starts[i]) : U(lists + starts[i]);
    }
    const uint16_t faces[12] = {5, 1, 7, 2, 0, 3, 4, 6, 1, 2, 0, 0};
    std::memcpy(lists, faces, sizeof(faces));
    return nodes;
}

// Every function on two geometries (plain floats with 10-byte faces, compressed with 8-byte faces) and their
// trees: counts, the first face list, face normals (one face degenerate), raw vertices get / set, faces in
// boxes (some, none, overflowing, no tree), points behind faces (each path), moving, turning (each quarter
// turn, with and without the centre), scaling, a file's offsets fixed up, copies (with and without the vertex
// bytes, none without vertices), the tree's copy, deletion; Matrix ==.
void TestAnimCollGeometry()
{
    GameMemory memory({{0x007D1800, 4}, {0x007CC979, 1}, {0x00691968, 12}});
    StandIns& s = *new StandIns();
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Record({0xA11C0000u, r.args[0], r.args[1], r.args[2]});
        return BspBump(r.args[0]);
    });
    s.At({0x005BABA0, AddressOf(&Mem::Manager::Free)}, 4, [](Regs& r) {
        Record({0xF4EE0000u, r.args[0]});
        return 0u;
    });
    auto runOnce = [&](bool original) {
        g_bspOriginal = original;
        g_trace.clear();
        std::memset(&g_bsp, 0xA9, sizeof(g_bsp));
        GameVariable<void*>(0x007D1800) = g_bsp.manager;
        GameVariable<bool>(0x007CC979) = true;
        std::memset(reinterpret_cast<void*>(0x00691968), 0, 12);
        g_bspUsed = 0;
        Lcg random(0x416BF0);
        for (auto& set : g_bsp.plain)
            for (auto& v : set)
                for (float& f : v)
                    f = BRand(random);
        for (auto& v : g_bsp.packed)
            for (uint16_t& c : v)
                c = static_cast<uint16_t>(random.Below(400));
        // faces: flags, terrain, three corners (face 3 degenerate: two corners the same)
        for (int f = 0; f < 8; ++f) {
            const uint16_t corners[3] = {static_cast<uint16_t>(f), static_cast<uint16_t>((f + 3) % 8),
                                         static_cast<uint16_t>(f == 3 ? f : (f + 5) % 8)};
            uint16_t* wide = reinterpret_cast<uint16_t*>(g_bsp.faces10 + f * 10);
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            uint8_t* small = g_bsp.faces8 + f * 8;
            small[0] = small[1] = small[2] = small[3] = small[7] = static_cast<uint8_t>(random.Next());
            for (int c = 0; c < 3; ++c) {
                wide[2 + c] = corners[c];
                small[4 + c] = static_cast<uint8_t>(corners[c]);
            }
        }
        // vertex 1 (face 1's first corner, the first face of the leaf the points below reach) on a point tested
        g_bsp.plain[0][1][0] = -3.0f;
        g_bsp.plain[0][1][1] = 0.0f;
        g_bsp.plain[0][1][2] = -6.0f;
        for (int i = 0; i < 8; ++i)
            g_bsp.vertexBytes[i] = static_cast<uint8_t>(i * 29 + 3);
        for (Vector& v : g_bsp.vectors)
            v = {BRand(random), BRand(random), BRand(random), BRand(random)};
        for (Matrix& m : g_bsp.matrices)
            for (Vector& row : m.row)
                row = {BRand(random), BRand(random), BRand(random), BRand(random)};
        CCollObjTriData& a = g_bsp.geometry[0];
        CCollObjTriData& b = g_bsp.geometry[1];
        CCollObjTriData& c = g_bsp.geometry[2];
        std::memset(&a, 0, sizeof(a) * 3);
        a.m_checksum = 0xA0A0;
        a.m_unknown04 = 0x44;
        a.m_numVertices = 8;
        a.m_numFaces = 8;
        a.m_faces = g_bsp.faces10;
        a.m_min = {-11, -12, -13, 1};
        a.m_max = {11, 12, 13, 1};
        a.m_vertices = g_bsp.plain[0];
        a.m_bsp = BuildTree(0, false, 0x5);
        a.m_vertexBytes = g_bsp.vertexBytes;
        a.m_unknown3C = 0x3C3C;
        b = a;
        b.m_checksum = 0xB0B0;
        b.m_smallFaces = true;
        b.m_compressed = true;
        b.m_faces = g_bsp.faces8;
        b.m_min = {-7.5f, -3.25f, -9.0f, 1};
        b.m_vertices = g_bsp.packed;
        b.m_bsp = BuildTree(1, false, 0x2);
        b.m_vertexBytes = nullptr;
        auto words = [](const void* p, int n) {
            const auto* w = static_cast<const uint32_t*>(p);
            g_trace.insert(g_trace.end(), w, w + n);
        };
        auto globals = [&]() { words(reinterpret_cast<void*>(0x00691968), 3); };
        // the trees' counts and first lists
        for (CCollObjTriData* g : {&a, &b}) {
            Record({static_cast<uint32_t>(BspFn<int(__thiscall*)(const void*)>(0x00415C90)(g->m_bsp)),
                    static_cast<uint32_t>(BspFn<int(__thiscall*)(const void*)>(0x00415CE0)(g->m_bsp)),
                    U(BspFn<void*(__thiscall*)(const void*)>(0x00415D30)(g->m_bsp)),
                    static_cast<uint32_t>(BspFn<int(__thiscall*)(const void*)>(0x00415C90)(g->m_bsp + 4))});
        }
        // face normals, raw vertices
        for (CCollObjTriData* g : {&a, &b}) {
            for (int f = 0; f < 8; ++f) {
                Vector n{};
                BspFn<Vector*(__thiscall*)(const void*, Vector*, int)>(0x00414D80)(g, &n, f);
                words(&n, 4);
            }
            BspFn<void(__thiscall*)(const void*, Vector*)>(0x00414F90)(g, g_bsp.out);
            words(g_bsp.out, 32);
            BspFn<void(__thiscall*)(const void*, Vector*)>(0x00415C00)(g, g_bsp.out);
            words(g_bsp.out, 32);
        }
        // faces in boxes: a corner, the whole tree (overflows: 9 faces of 8), below every split, no tree
        Mth::BBox boxes[4];
        boxes[0].min = {-20, -20, -20, 1};
        boxes[0].max = {1, 30, -3, 1};
        boxes[1].min = {-20, -20, -20, 1};
        boxes[1].max = {20, 20, 20, 1};
        boxes[2].min = {1.5f, 4.0f, -2.25f, 1};
        boxes[2].max = {1.5f, 4.0f, -2.25f, 1};
        boxes[3].min = {2, 5, 0, 1};
        boxes[3].max = {9, 9, 9, 1};
        auto inBox = BspFn<uint16_t*(__thiscall*)(void*, const Mth::BBox*, int*, uint16_t*, int)>(0x00416320);
        for (CCollObjTriData* g : {&a, &b}) {
            for (const Mth::BBox& box : boxes) {
                int count = -5;
                std::memset(g_bsp.gathered, 0xEE, sizeof(g_bsp.gathered));
                Record({U(inBox(g, &box, &count, g_bsp.gathered, 32)), static_cast<uint32_t>(count)});
                words(g_bsp.gathered, 16);
                globals();
            }
        }
        for (int faces : {12, 9, 4, 2}) {
        a.m_numFaces = static_cast<uint16_t>(faces);
        for (const Mth::BBox& box : boxes) {
            int count = -5;
            Record({U(inBox(&a, &box, &count, g_bsp.gathered, 32)), static_cast<uint32_t>(count)});
            words(g_bsp.gathered, 16);
        }
        }
        a.m_numFaces = 8;
        CCollBSPNode* saved = c.m_bsp;
        c = a;
        c.m_bsp = nullptr;
        {
            int count = -5;
            Record({U(inBox(&c, &boxes[0], &count, g_bsp.gathered, 32)), static_cast<uint32_t>(count)});
        }
        c.m_bsp = saved;
        // points behind faces: down both less sides (the leaf's faces counted), stopped by each split, no tree
        auto behind = BspFn<bool(__thiscall*)(void*, const Vector*, int)>(0x004163B0);
        const Vector points[] = {{-3, 0, -6, 1}, {-9, 7, -8, 1}, {5, 0, -6, 1}, {-3, 0, 2, 1}, {1.5f, 0, -9, 1},
                                 {0, 0, 0, 1}, {-30, -30, -30, 1}, {30, 30, -30, 1}};
        for (CCollObjTriData* g : {&a, &b}) {
            for (int faces : {8, 2, 3}) {
                g->m_numFaces = static_cast<uint16_t>(faces);
                for (const Vector& p : points) {
                    Record({behind(g, &p, 77)});
                    globals();
                }
            }
            g->m_numFaces = 8;
        }
        Record({behind(&c, &points[0], 1)});
        c.m_bsp = nullptr;
        Record({behind(&c, &points[0], 1)});
        // set vertices: one by one (offsets below the minimum too), all with the box
        for (CCollObjTriData* g : {&a, &b}) {
            for (int i = 0; i < 4; ++i)
                BspFn<void(__thiscall*)(void*, int, const Vector*)>(0x00414EE0)(g, i * 2, &g_bsp.vectors[i]);
            words(g->m_vertices, g->m_compressed ? 12 : 24);
        }
        for (int i = 0; i < 8; ++i)
            g_bsp.out[i] = {BRand(random), BRand(random), BRand(random), BRand(random)};
        BspFn<void(__thiscall*)(void*, const Vector*)>(0x00415C10)(&b, g_bsp.out);
        words(&b, 16);
        words(g_bsp.packed, 12);
        b.m_vertices = g_bsp.plain[1];
        b.m_compressed = false;
        BspFn<void(__thiscall*)(void*, const Vector*)>(0x00415C10)(&b, g_bsp.out);
        words(&b, 16);
        b.m_vertices = g_bsp.packed;
        b.m_compressed = true;
        // moved: the vertices (plain), box and tree
        Vector delta{0.3f, -1.71f, 2.55f, 0.5f};
        Vector negative{-0.3f, -40.03f, -0.07f, 0.0f};
        BspFn<void(__thiscall*)(void*, const Vector*)>(0x00416400)(&a, &delta);
        BspFn<void(__thiscall*)(void*, const Vector*)>(0x00416400)(&b, &negative);
        a.m_bsp = nullptr;
        BspFn<void(__thiscall*)(void*, const Vector*)>(0x00416400)(&a, &negative);
        a.m_bsp = reinterpret_cast<CCollBSPNode*>(g_bsp.nodes[0] + 1);
        words(&a, 16);
        words(&b, 16);
        words(g_bsp.nodes, sizeof(g_bsp.nodes) / 4);
        // turned and scaled
        auto turn = BspFn<void(__thiscall*)(void*, const Vector*, int, bool)>(0x00415E00);
        auto scale = BspFn<void(__thiscall*)(void*, const Vector*, const Vector*, bool)>(0x00415F30);
        for (int turns = 0; turns <= 4; ++turns) {
            turn(a.m_bsp, &g_bsp.vectors[4], turns, true);
            words(g_bsp.nodes[0], 15);
            turn(b.m_bsp, &g_bsp.vectors[5], turns, false);
            words(g_bsp.nodes[1], 15);
        }
        b.m_bsp->m_split = (0x1234567 << 2) | (b.m_bsp->m_split & 3); // a split needing more than a float's bits
        Vector factors{1.37f, -0.61f, 2.9f, 7.0f};
        scale(a.m_bsp, &g_bsp.vectors[6], &factors, true);
        words(g_bsp.nodes[0], 15);
        scale(b.m_bsp, &g_bsp.vectors[7], &factors, false);
        words(g_bsp.nodes[1], 15);
        BspFn<void(__thiscall*)(void*, const Vector*)>(0x00415D80)(b.m_bsp, &g_bsp.vectors[8]);
        words(g_bsp.nodes[1], 15);
        // a file's geometry fixed up
        CCollBSPNode* fileTree = BuildTree(2, true, 0x3);
        c = a;
        c.m_vertices = reinterpret_cast<void*>(12);
        c.m_vertexBytes = reinterpret_cast<uint8_t*>(2);
        c.m_faces = reinterpret_cast<uint8_t*>(10);
        c.m_bsp = nullptr;
        Record({BspFn<bool(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(
            0x004169C0)(&c, 0x1234, U(g_bsp.plain[1]), U(g_bsp.vertexBytes), U(g_bsp.faces10), U(fileTree),
                        U(g_bsp.lists[2]))});
        words(&c, 16);
        words(g_bsp.nodes[2], 15);
        Record({BspFn<bool(__cdecl*)(void*, uint32_t, uint32_t)>(0x00416000)(fileTree + 3, 0x100, 0x2000)});
        words(g_bsp.nodes[2], 15);
        // copies: the geometry (with / without vertex bytes; none without vertices), the tree
        auto clone = BspFn<void*(__thiscall*)(const void*, int, bool)>(0x00416BF0);
        void* copyA = clone(&a, 3, true);
        void* copyB = clone(&b, 4, false);
        b.m_numVertices = 0;
        void* none = clone(&b, 5, true);
        b.m_vertexBytes = g_bsp.vertexBytes;
        void* empty = clone(&b, 6, false);
        b.m_vertexBytes = nullptr;
        b.m_numVertices = 8;
        Record({U(copyA), U(copyB), U(none), U(empty)});
        void* treeCopy = BspFn<void*(__thiscall*)(const void*, int)>(0x00416A10)(b.m_bsp, 9);
        void* leafCopy = BspFn<void*(__thiscall*)(const void*, int)>(0x00416A10)(b.m_bsp + 5, 9);
        Record({U(treeCopy), U(leafCopy)});
        auto* copied = static_cast<CCollObjTriData*>(copyA);
        Record({U(BspFn<void*(__thiscall*)(void*, const void*)>(0x00415B70)(&c, copied))});
        words(&c, 16);
        // a leaf's count is 16 bits
        CCollBSPNode bigLeaf{3 | (0x0102 << 16), 0};
        Record({static_cast<uint32_t>(BspFn<int(__thiscall*)(const void*)>(0x00415CE0)(&bigLeaf))});
        // offsets not exact in a float: 1 - 2^-30 is 15.99... sixteenths, rounded to a float 16
        {
            CCollObjTriData tiny = b;
            uint16_t packed[3] = {};
            tiny.m_vertices = packed;
            tiny.m_min = {9.3132257e-10f, 9.3132257e-10f, 9.3132257e-10f, 1.0f};
            Vector one{1.0f, 1.0f, 1.0f, 1.0f};
            BspFn<void(__thiscall*)(void*, int, const Vector*)>(0x00414EE0)(&tiny, 0, &one);
            Record({packed[0], packed[1], packed[2]});
        }
        // the node constructor, deletion
        CCollBSPNode raw{0x12345678, 0x9ABCDEF0};
        Record({BspFn<void*(__thiscall*)(void*)>(0x00414D70)(&raw) == &raw, static_cast<uint32_t>(raw.m_split), raw.m_link});
        Record({BspFn<bool(__thiscall*)(void*)>(0x00416080)(copyB),
                BspFn<bool(__thiscall*)(void*)>(0x00416080)(copyB)});
        BspFn<void(__thiscall*)(void*)>(0x00416970)(copyA);
        static_cast<CCollObjTriData*>(empty)->m_vertices = nullptr;
        BspFn<void(__thiscall*)(void*)>(0x00416970)(empty);
        // Matrix ==: the same, one value different, NaN, -0 and +0
        auto equal = BspFn<bool(__thiscall*)(const void*, const void*)>(0x00416D80);
        Matrix& m0 = g_bsp.matrices[0];
        Matrix& m1 = g_bsp.matrices[1];
        m1 = m0;
        Record({equal(&m0, &m1)});
        for (int k = 0; k < 16; k += 5) {
            float* f = &m1.row[k / 4].x + k % 4;
            float keep = *f;
            *f += 1.0f;
            Record({equal(&m0, &m1), equal(&m1, &m0)});
            *f = std::numeric_limits<float>::quiet_NaN();
            Record({equal(&m0, &m1)});
            float* g = &m0.row[k / 4].x + k % 4;
            *g = *f;
            Record({equal(&m0, &m1), equal(&m1, &m1)});
            *f = 0.0f;
            *g = -0.0f;
            Record({equal(&m0, &m1)});
            *f = keep;
            *g = keep;
        }
        std::vector<uint32_t> trace = g_trace;
        const auto* all = reinterpret_cast<const uint32_t*>(&g_bsp);
        trace.insert(trace.end(), all, all + sizeof(g_bsp) / 4);
        globals();
        trace.insert(trace.end(), g_trace.end() - 3, g_trace.end());
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Nx collision geometry and its BSP tree");
}
SELFTEST(Early, TestAnimCollGeometry, TestStandIns::RemoveStandIns);

} // namespace
