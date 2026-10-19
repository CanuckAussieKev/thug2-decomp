// Self-tests for AN-4: the vector / matrix / box helpers (src/game/mth) and the collision caches
// (src/game/nx/coll_cache.cpp) - the original and ours on identical inputs, compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/core/alloc.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::BBox;
using Mth::Matrix;
using Mth::Vector;

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
void PushVector(std::vector<uint32_t>& t, const Vector& v)
{
    for (float f : {v.x, v.y, v.z, v.w})
        t.push_back(Bits(f));
}

float RandomValue(Lcg& random)
{
    switch (random.Below(16)) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return 1.0f;
    case 3: return std::nanf("");
    case 4: return static_cast<float>(random.Below(5)) - 2.0f; // (small integers: ties and equal values)
    case 5: return random.Below(2) ? 3e19f : -3e19f;            // (cancellations: the order of sums shows)
    default: return (static_cast<float>(random.Below(200001)) - 100000.0f) / 1237.0f;
    }
}
Vector RandomVector(Lcg& random)
{
    return {RandomValue(random), RandomValue(random), RandomValue(random), RandomValue(random)};
}

// The helpers on random values (zeros, NaNs, equal and tiny ones): ==, /, IsAngleBeyond, the matrix transforms
// (also in place), ScaleRows, the box copy, Intersects and Contains.
void TestAnimMathHelpers()
{
    Lcg random(0x40AED0);
    std::vector<uint32_t> expected, actual;
    for (int i = 0; i < 4000; ++i) {
        Vector a = RandomVector(random);
        Vector b = random.Below(4) == 0 ? a : RandomVector(random);
        if (random.Below(5) == 0)
            b = {a.x * 2.5f, a.y * 2.5f, a.z * 2.5f, a.w};
        float f = RandomValue(random);
        // ==
        expected.push_back(Original<int(__thiscall*)(const Vector*, const Vector*)>(0x0040AED0)(&a, &b));
        actual.push_back(a == b ? 1 : 0);
        // /
        Vector e{}, o{};
        Original<Vector*(__cdecl*)(Vector*, const Vector*, float)>(0x0040AF20)(&e, &a, f);
        o = a / f;
        PushVector(expected, e);
        PushVector(actual, o);
        // IsAngleBeyond (degrees 0..180 and odd ones; vectors near 0.5 long; b exactly `degrees` from a)
        if (i % 7 == 0) {
            float length = 0.25f + 0.0625f * static_cast<float>(random.Below(8));
            a = {length, 0.0f, 0.0f, 0.0f};
            if (random.Below(2))
                b = {0.0f, length, 0.0f, 0.0f};
        }
        float degrees = random.Below(3) == 0 ? RandomValue(random) : static_cast<float>(random.Below(181));
        expected.push_back(Original<bool(__cdecl*)(const Vector*, const Vector*, float)>(0x00406560)(&a, &b, degrees));
        actual.push_back(Mth::IsAngleBeyond(a, b, degrees));
        if (i < 180) {
            float d = static_cast<float>(i);
            Vector unit{1.0f, 0.0f, 0.0f, 0.0f};
            Vector at{std::cos(d * 0.017453292f), std::sin(d * 0.017453292f), 0.0f, 0.0f};
            for (float limit : {d, d - 0.0001f, d + 0.0001f}) {
                expected.push_back(Original<bool(__cdecl*)(const Vector*, const Vector*, float)>(0x00406560)(&unit, &at, limit));
                actual.push_back(Mth::IsAngleBeyond(unit, at, limit));
            }
        }
        // the transforms
        Matrix m;
        for (Vector& row : m.row)
            row = RandomVector(random);
        if (i < 81) {
            // Each column a cancelling pattern (two of +-1e20 and ones): the order of the sums decides the result.
            const float big[3] = {1e20f, -1e20f, 1.0f};
            for (int r = 0; r < 4; ++r) {
                for (int c = 0; c < 4; ++c)
                    (&m.row[r].x)[c] = big[(i / (r == 0 ? 1 : r == 1 ? 3 : r == 2 ? 9 : 27) + c) % 3];
            }
            a = {1.0f, 1.0f, 1.0f, 1.0f};
        }
        using TransformFn = Vector*(__thiscall*)(const Matrix*, Vector*, const Vector*);
        for (uint32_t address : {0x0040AF60u, 0x0040B000u, 0x0040B090u}) {
            Vector out1{}, out2{};
            Vector* r1 = Original<TransformFn>(address)(&m, &out1, &a);
            Vector* r2 = address == 0x0040AF60 ? &m.Transform(out2, a)
                         : address == 0x0040B000 ? &m.TransformAsPos(out2, a)
                                                 : &m.RotateVector(out2, a);
            PushVector(expected, out1);
            PushVector(actual, out2);
            expected.push_back(r1 == &out1);
            actual.push_back(r2 == &out2);
            // in place
            Vector in1 = a, in2 = a;
            Original<TransformFn>(address)(&m, &in1, &in1);
            if (address == 0x0040AF60)
                m.Transform(in2, in2);
            else if (address == 0x0040B000)
                m.TransformAsPos(in2, in2);
            else
                m.RotateVector(in2, in2);
            PushVector(expected, in1);
            PushVector(actual, in2);
        }
        Matrix s1 = m, s2 = m;
        Matrix* r1 = Original<Matrix*(__thiscall*)(Matrix*, const Vector*)>(0x0040B100)(&s1, &b);
        Matrix* r2 = &s2.ScaleRows(b);
        for (int k = 0; k < 4; ++k) {
            PushVector(expected, s1.row[k]);
            PushVector(actual, s2.row[k]);
        }
        expected.push_back(r1 == &s1);
        actual.push_back(r2 == &s2);
        // boxes
        BBox x1, x2;
        x1.min = a;
        x1.max = {a.x + std::fabs(b.x), a.y + std::fabs(b.y), a.z + std::fabs(b.z), 1.0f};
        x2.min = random.Below(3) ? b : a;
        x2.max = {x2.min.x + std::fabs(f), x2.min.y + 3.0f, x2.min.z + std::fabs(a.w), 1.0f};
        if (random.Below(4) == 0)
            x2 = x1;
        expected.push_back(Original<bool(__thiscall*)(const BBox*, const BBox*)>(0x0040B170)(&x1, &x2));
        actual.push_back(x1.Intersects(x2));
        expected.push_back(Original<bool(__thiscall*)(const BBox*, const BBox*)>(0x0040B1D0)(&x1, &x2));
        actual.push_back(x1.Contains(x2));
        expected.push_back(Original<bool(__thiscall*)(const BBox*, const BBox*)>(0x0040B1D0)(&x2, &x1));
        actual.push_back(x2.Contains(x1));
        BBox c1, c2;
        BBox* rc1 = Original<BBox*(__thiscall*)(BBox*, const BBox*)>(0x0040AB00)(&c1, &x2);
        BBox* rc2 = &(c2 = x2);
        PushVector(expected, c1.min);
        PushVector(expected, c1.max);
        PushVector(actual, c2.min);
        PushVector(actual, c2.max);
        expected.push_back(rc1 == &c1);
        actual.push_back(rc2 == &c2);
    }
    CompareTraces(expected, actual, "Mth helpers (==, /, IsAngleBeyond, Transform / TransformAsPos / Rotate, ScaleRows, BBox)");
}
SELFTEST(Early, TestAnimMathHelpers);

// --- collision caches -------------------------------------------------------------------------------------------

struct CacheArena {
    alignas(16) uint8_t caches[10][0xFCC + 4];
    uint8_t objects[12][0x20];   // stand-in collision objects: +0 vtable, +8 flags
    void* vtable[24];
    Mem::ListNode movables[6];   // the movable list (the first node at 0x00691984)
    void* gridLists[4];
    void* cellObjects[3][6];
};
CacheArena g_cache;
bool g_cacheOriginal;

template <typename F>
F CacheFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_cacheOriginal, address));
}
void* Object(int i) { return g_cache.objects[i]; }
Nx::CCollCache* Cache(int i) { return reinterpret_cast<Nx::CCollCache*>(g_cache.caches[i]); }

// The caches: constructed (registered; a ninth refused), updated for boxes (no grid lists, lists of objects some
// of which touch, movables with flags 2 / 4 / none, a null data node), objects removed (static, movable, both,
// from all caches; first, middle, last, missing), unregistered (also one that isn't), the shape types.
void TestAnimCollCaches()
{
    GameMemory memory({{0x006918EC, 0x24}, {0x00691984, 4}});
    StandIns& s = *new StandIns();
    uint32_t gridAnswer = 0;
    s.At({0x0048C980}, 0, [&gridAnswer](Regs& r) {
        auto* line = reinterpret_cast<Mth::Line*>(r.args[0]);
        Record({0x6A1D0000u, Bits(line->start.x), Bits(line->end.z)});
        return gridAnswer;
    });
    s.At({0x004188C0, AddressOf(&Nx::CCollGrid::ObjectsAlongLine)}, 8, [](Regs& r) {
        Record({0x6A1D1000u, r.ecx, r.args[1]});
        return r.ecx; // (each grid list stands for its own null-terminated object array)
    });
    void* touches = s.Thunk(4, [](Regs& r) {
        auto* box = reinterpret_cast<const BBox*>(r.args[0]);
        Record({0x70C40000u, r.ecx, Bits(box->min.x), Bits(box->min.y), Bits(box->min.z), Bits(box->min.w),
                Bits(box->max.x), Bits(box->max.y), Bits(box->max.z), Bits(box->max.w)});
        return static_cast<uint32_t>(((r.ecx >> 5) * 7) % 3 != 0);
    });
    void* geometry20 = s.Thunk(0, [](Regs& r) {
        Record({0x6E020000u, r.ecx});
        return r.ecx + 0x20;
    });
    void* geometry21 = s.Thunk(0, [](Regs& r) {
        Record({0x6E021000u, r.ecx});
        return r.ecx + 0x21;
    });
    auto runOnce = [&](bool original) {
        g_cacheOriginal = original;
        g_trace.clear();
        std::memset(&g_cache, 0xCD, sizeof(g_cache));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        for (void*& slot : g_cache.vtable)
            slot = nullptr;
        g_cache.vtable[13] = touches;
        g_cache.vtable[20] = geometry20;
        g_cache.vtable[21] = geometry21;
        for (int i = 0; i < 12; ++i) {
            *reinterpret_cast<void***>(g_cache.objects[i]) = g_cache.vtable;
            reinterpret_cast<uint32_t*>(g_cache.objects[i])[2] = i % 4 == 1 ? 2 : i % 4 == 3 ? 4 : 0x10;
        }
        // the grid lists: objects 0-3, 4-5, none
        for (int k = 0; k < 6; ++k)
            g_cache.cellObjects[0][k] = k < 4 ? Object(k) : nullptr;
        for (int k = 0; k < 6; ++k)
            g_cache.cellObjects[1][k] = k < 2 ? Object(4 + k) : nullptr;
        g_cache.cellObjects[2][0] = nullptr;
        g_cache.gridLists[0] = g_cache.cellObjects[0];
        g_cache.gridLists[1] = g_cache.cellObjects[1];
        g_cache.gridLists[2] = g_cache.cellObjects[2];
        g_cache.gridLists[3] = nullptr;
        // the movables: objects 6-10 and a null one, then the head
        Mem::ListNode* m = g_cache.movables;
        for (int k = 0; k < 6; ++k) {
            m[k].data = k == 5 ? reinterpret_cast<void*>(-1) : k == 2 ? nullptr : Object(6 + k);
            m[k].next = &m[(k + 1) % 6];
        }
        GameVariable<Mem::ListNode*>(0x00691984) = &m[0];

        for (int i = 0; i < 3; ++i)
            Record({reinterpret_cast<uint32_t>(CacheFn<Nx::CCollCache*(__thiscall*)(Nx::CCollCache*)>(0x0040AB50)(Cache(i)))});
        auto reg = CacheFn<void(__cdecl*)(Nx::CCollCache*)>(0x0040AA00);
        for (int i = 3; i < 9; ++i) {
            std::memset(Cache(i), 0, 0xFCC); // (empty caches, registered directly; the ninth is refused)
            reg(Cache(i));
        }
        auto update = CacheFn<void(__thiscall*)(Nx::CCollCache*, const BBox*)>(0x0040ABA0);
        BBox box;
        box.min = {-10.25f, 2.5f, 7.0f, 1.0f};
        box.max = {30.5f, 12.75f, 9.125f, 0.5f};
        gridAnswer = 0;
        update(Cache(0), &box); // (no grid lists: emptied, box not kept)
        gridAnswer = reinterpret_cast<uint32_t>(g_cache.gridLists);
        update(Cache(1), &box);
        box.min.x = -3.0f;
        update(Cache(2), &box);
        auto removeStatic = CacheFn<void(__thiscall*)(Nx::CCollCache*, void*)>(0x0040A910);
        auto removeMovable = CacheFn<void(__thiscall*)(Nx::CCollCache*, void*)>(0x0040A990);
        removeStatic(Cache(1), Object(0));
        for (int k : {6, 8, 10}) // (movables: not among the statics)
            removeStatic(Cache(1), Object(k));
        removeStatic(Cache(1), Object(5));
        for (int k : {1, 2, 3, 4}) // (statics: not among the movables)
            removeMovable(Cache(2), Object(k));
        removeMovable(Cache(1), Object(6));
        removeMovable(Cache(1), Object(10));
        CacheFn<void(__cdecl*)(void*)>(0x0040AA80)(Object(7));
        CacheFn<void(__cdecl*)(void*)>(0x0040AAC0)(Object(2));
        CacheFn<void(__cdecl*)(void*)>(0x0040AAC0)(Object(8));
        auto unreg = CacheFn<void(__cdecl*)(Nx::CCollCache*)>(0x0040AA40);
        unreg(Cache(5));
        unreg(Cache(9)); // (not registered)
        reg(Cache(6));   // (twice: into the slot just freed, before its own)
        unreg(Cache(6));
        unreg(Cache(6)); // (the stale pointer in the freed slot doesn't count)
        CacheFn<void(__thiscall*)(Nx::CCollCache*)>(0x0040AB40)(Cache(0));
        reg(Cache(9));
        for (uint32_t shape : {0xE460ABDEu, 0xDD42AA66u, 0x2BA71070u, 0x6AADF154u, 0xDD42AA67u, 0x6AADF153u, 0u,
                               0xFFFFFFFFu, 0xDD42AA65u})
            Record({static_cast<uint32_t>(CacheFn<int(__cdecl*)(uint32_t)>(0x0040ADD0)(shape))});

        std::vector<uint32_t> trace = g_trace;
        const auto* words = reinterpret_cast<const uint32_t*>(&g_cache);
        trace.insert(trace.end(), words, words + sizeof(g_cache) / 4);
        const auto* registry = reinterpret_cast<const uint32_t*>(0x006918EC);
        trace.insert(trace.end(), registry, registry + 9);
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    delete &s;
    CompareTraces(expected, actual, "Nx::CCollCache (construct / update / remove / register)");
}
SELFTEST(Early, TestAnimCollCaches, TestStandIns::RemoveStandIns);

} // namespace
