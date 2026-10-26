// Self-tests for AN-21: the orthonormal inverse (src/game/mth/matrix.cpp), the sector's element constructor
// (src/game/nx/coll_static.cpp), the ride tracker (src/game/obj/ride_tracker.cpp) and the feeler's hit vertex value
// (src/game/nx/feeler.cpp) - the original and ours on the same inputs, results compared bit for bit.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "game/obj/ride_tracker.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Matrix;
using Mth::Vector;

float MvSpread(Lcg& random, float range)
{
    return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range;
}
void MvWords(std::vector<uint32_t>& trace, const void* p, size_t bytes, uint32_t base = 0, uint32_t size = 0)
{
    const auto* w = static_cast<const uint32_t*>(p);
    for (size_t i = 0; i < bytes / 4; ++i) {
        uint32_t word = w[i];
        if (size && word >= base && word < base + size)
            word = 0xA0000000u + (word - base);
        if ((word & 0x7F800000u) == 0x7F800000u && (word & 0x007FFFFFu))
            word = 0x7FC00000u; // (any NaN)
        trace.push_back(word);
    }
}
void MvDouble(std::vector<uint32_t>& trace, double d)
{
    if (d != d) {
        trace.push_back(0x7FF80000u);
        return;
    }
    uint32_t b[2];
    std::memcpy(b, &d, 8);
    trace.push_back(b[0]);
    trace.push_back(b[1]);
}
// A rotation (about a random axis) with a translation, sometimes scaled or skewed, sometimes with NaN.
Matrix MvMatrix(Lcg& random, int n)
{
    Matrix m;
    float a = MvSpread(random, 3.2f), b = MvSpread(random, 3.2f);
    float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b);
    m.row[0] = {ca, sa * cb, sa * sb, MvSpread(random, 1.0f)};
    m.row[1] = {-sa, ca * cb, ca * sb, MvSpread(random, 1.0f)};
    m.row[2] = {0.0f, -sb, cb, MvSpread(random, 1.0f)};
    m.row[3] = {MvSpread(random, 500.0f), MvSpread(random, 500.0f), MvSpread(random, 500.0f), MvSpread(random, 2.0f)};
    if (n % 5 == 0)
        for (Vector& r : m.row)
            r = {MvSpread(random, 10.0f), MvSpread(random, 10.0f), MvSpread(random, 10.0f), MvSpread(random, 10.0f)};
    if (n % 31 == 0)
        m.row[random.Below(4)].y = std::numeric_limits<float>::quiet_NaN();
    return m;
}

// The orthonormal inverse on rotations and arbitrary matrices; the sector's element constructor on dirty memory.
void TestAnimMatrixInvertOrthonormal()
{
    using InvertFn = Matrix*(__thiscall*)(Matrix*);
    using ConstructFn = void*(__thiscall*)(void*);
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto invert = reinterpret_cast<InvertFn>(Code(original, 0x00416DF0));
        auto construct = reinterpret_cast<ConstructFn>(Code(original, 0x00412730));
        Lcg random(0x416DF0);
        for (int n = 0; n < 3000; ++n) {
            Matrix m = MvMatrix(random, n);
            trace.push_back(invert(&m) == &m);
            MvWords(trace, &m, sizeof(m));
        }
        alignas(16) uint8_t sector[0x20];
        for (int n = 0; n < 4; ++n) {
            std::memset(sector, 0xA5 + n, sizeof(sector));
            trace.push_back(construct(sector) == sector);
            MvWords(trace, sector, sizeof(sector));
        }
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    CompareTraces(expected, actual, "Mth::Matrix::InvertOrthonormal and CCollStatic::Construct");
}
SELFTEST(Early, TestAnimMatrixInvertOrthonormal, TestStandIns::RemoveStandIns);

// --- the ride tracker ---

struct RideArena {
    alignas(16) uint8_t object[0x200]; // (a stand-in object: no smart pointers yet, its position and matrix)
    alignas(16) uint8_t trackers[3][sizeof(Obj::CRideTracker)];
};

// Trackers built on dirty memory, updated as their object moves and turns (or keeps its orientation exactly), on
// points near it; one whose object is gone.
void TestAnimRideTracker()
{
    using ConstructFn = void*(__thiscall*)(void*, void*);
    using UpdateFn = bool(__thiscall*)(void*, const Vector*);
    auto* arena = static_cast<RideArena*>(VirtualAlloc(nullptr, sizeof(RideArena), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    const uint32_t base = reinterpret_cast<uint32_t>(arena);
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto construct = reinterpret_cast<ConstructFn>(Code(original, 0x00416E70));
        auto update = reinterpret_cast<UpdateFn>(Code(original, 0x00416F00));
        std::memset(arena, 0xB7, sizeof(RideArena));
        std::memset(arena->object, 0, sizeof(arena->object));
        auto* object = reinterpret_cast<Obj::CCompositeObject*>(arena->object);
        Lcg random(0x416F00);
        auto place = [&](int n) {
            object->m_pos = {MvSpread(random, 300.0f), MvSpread(random, 300.0f), MvSpread(random, 300.0f),
                             n % 7 == 0 ? MvSpread(random, 2.0f) : 1.0f};
            object->m_matrix = MvMatrix(random, n);
        };
        place(1);
        for (int k = 0; k < 3; ++k) {
            trace.push_back(construct(arena->trackers[k], object) == arena->trackers[k]);
            MvWords(trace, arena->trackers[k], sizeof(Obj::CRideTracker), base, sizeof(RideArena));
        }
        MvWords(trace, arena->object, 16, base, sizeof(RideArena));
        for (int n = 0; n < 2000; ++n) {
            int k = static_cast<int>(random.Below(3));
            auto* tracker = reinterpret_cast<Obj::CRideTracker*>(arena->trackers[k]);
            if (n % 3 == 0) {
                // only moved: the same orientation
                object->m_pos.x += MvSpread(random, 5.0f);
                object->m_pos.z += MvSpread(random, 5.0f);
            } else if (n % 3 == 1) {
                place(n);
            } else {
                object->m_pos.y += MvSpread(random, 5.0f);
                object->m_matrix.row[random.Below(3)].x += MvSpread(random, 0.01f);
            }
            Vector point{object->m_pos.x + MvSpread(random, 20.0f), object->m_pos.y + MvSpread(random, 20.0f),
                         object->m_pos.z + MvSpread(random, 20.0f), n % 4 == 0 ? MvSpread(random, 2.0f) : 1.0f};
            if (n == 1500)
                tracker->m_object.mp_object = nullptr; // (gone: nothing done from now on for this one)
            trace.push_back(update(tracker, &point));
            MvWords(trace, tracker, sizeof(Obj::CRideTracker), base, sizeof(RideArena));
        }
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    VirtualFree(arena, 0, MEM_RELEASE);
    CompareTraces(expected, actual, "Obj::CRideTracker");
}
SELFTEST(Early, TestAnimRideTracker, TestStandIns::RemoveStandIns);

// --- the feeler's hit vertex value ---

constexpr int kHvVertices = 30;
constexpr int kHvFaces = 40;

struct HitValueArena {
    alignas(16) uint8_t sector[0x18];
    Nx::CCollObjTriData geometry[2];
    float plain[kHvVertices][3];
    uint16_t packed[kHvVertices][3];
    uint8_t faces10[kHvFaces * 10];
    uint8_t faces8[kHvFaces * 8];
    uint8_t bytes[kHvVertices];
    uint8_t feeler[sizeof(Nx::CFeeler)];
};

// Feelers with and without a hit, on faces of plain and compressed geometry, the hit point inside the face, on an
// edge, at a corner and off it.
void TestAnimFeelerHitVertexValue()
{
    GameMemory memory({{0x006918EC, 0x24}});
    StandIns& s = *new StandIns();
    uint32_t closest = AddressOf(&Mth::ClosestPointsBetweenLines);
    s.Bytes(closest, Branch(0xE9, closest, OriginalFunction(0x00402F00)));
    using ValueFn = double(__thiscall*)(const void*);
    auto* arena = static_cast<HitValueArena*>(
        VirtualAlloc(nullptr, sizeof(HitValueArena), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    int checked = 0;
    auto runOnce = [&](bool original) {
        std::vector<uint32_t> trace;
        auto value = reinterpret_cast<ValueFn>(Code(original, 0x004174A0));
        std::memset(arena, 0xA7, sizeof(HitValueArena));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        if (!original)
            UseOurCollisionVtables();
        Lcg random(0x4174A0);
        for (int v = 0; v < kHvVertices; ++v) {
            for (int k = 0; k < 3; ++k) {
                arena->plain[v][k] = MvSpread(random, 40.0f);
                arena->packed[v][k] = static_cast<uint16_t>((arena->plain[v][k] + 50.0f) * 16.0f);
            }
            arena->bytes[v] = static_cast<uint8_t>(random.Next());
        }
        for (int f = 0; f < kHvFaces; ++f) {
            auto* wide = reinterpret_cast<uint16_t*>(arena->faces10 + f * 10);
            uint8_t* small = arena->faces8 + f * 8;
            wide[0] = static_cast<uint16_t>(random.Next());
            wide[1] = static_cast<uint16_t>(random.Next());
            std::memcpy(small, wide, 4);
            uint16_t first = static_cast<uint16_t>(random.Below(kHvVertices - 2));
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(first + k); // (three different corners)
                wide[2 + k] = corner;
                small[4 + k] = static_cast<uint8_t>(corner);
            }
            small[7] = 0;
        }
        for (int g = 0; g < 2; ++g) {
            Nx::CCollObjTriData& geometry = arena->geometry[g];
            std::memset(&geometry, 0, sizeof(geometry));
            geometry.m_numVertices = kHvVertices;
            geometry.m_numFaces = kHvFaces;
            geometry.m_smallFaces = g == 1;
            geometry.m_compressed = g == 1;
            geometry.m_faces = g == 1 ? arena->faces8 : arena->faces10;
            geometry.m_min = {-50, -50, -50, 1};
            geometry.m_max = {50, 50, 50, 1};
            geometry.m_vertices = g == 1 ? static_cast<void*>(arena->packed) : static_cast<void*>(arena->plain);
            geometry.m_vertexBytes = arena->bytes;
        }
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(arena->sector);
        std::memset(sector, 0, 0x18);
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        auto* feeler = reinterpret_cast<Nx::CFeeler*>(arena->feeler);
        for (int n = 0; n < 2000; ++n) {
            int g = n % 2;
            sector->m_geometry = &arena->geometry[g];
            feeler->m_hit.hit = n % 13 != 0;
            feeler->m_hit.object = sector;
            int face = static_cast<int>(random.Below(kHvFaces));
            feeler->m_hit.face = static_cast<uint32_t>(face);
            Vector v[3];
            for (int k = 0; k < 3; ++k)
                v[k] = sector->GetVertexPos(static_cast<int>(arena->geometry[g].FaceVertex(face, k)));
            float a = MvSpread(random, 0.5f) + 0.4f, b = MvSpread(random, 0.5f) + 0.3f;
            switch (n % 7) {
            case 1: b = 0.0f; break;      // (on the first edge)
            case 2: a = 1.0f - b; break;  // (on the opposite edge)
            case 3: a = 0.5f, b = 0.5f; break;
            case 4: a = MvSpread(random, 2.0f), b = MvSpread(random, 2.0f); break; // (off the face)
            default: break;
            }
            feeler->m_hit.point = {v[0].x + a * (v[1].x - v[0].x) + b * (v[2].x - v[0].x),
                                   v[0].y + a * (v[1].y - v[0].y) + b * (v[2].y - v[0].y),
                                   v[0].z + a * (v[1].z - v[0].z) + b * (v[2].z - v[0].z), 1.0f};
            MvDouble(trace, value(feeler));
            checked += original;
        }
        EndRunPatches();
        return trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    VirtualFree(arena, 0, MEM_RELEASE);
    delete &s;
    Log("feeler hit values: %d checked", checked);
    CompareTraces(expected, actual, "Nx::CFeeler::GetHitVertexValue");
}
SELFTEST(Early, TestAnimFeelerHitVertexValue, TestStandIns::RemoveStandIns);

} // namespace
