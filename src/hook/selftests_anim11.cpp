// Self-tests for AN-13: the hit record's report (Nx::ReportHit) and the movable box's line test
// (src/game/nx/coll_line.cpp) - the original and ours on identical objects, lines and records; the records, the
// callback's calls and the results compared.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/nx/coll_line.h"
#include "game/nx/coll_movable.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_tri.h"
#include "hook/anim_testkit.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"

namespace {

using namespace AnimTests;
using Mth::Matrix;
using Mth::Vector;
using Nx::CollisionHit;

struct LineArena {
    alignas(16) uint8_t box[0xD0];
    uint8_t sector[0x18];
    Nx::CCollObjTriData geometry;
    float vertices[4][3];
    uint8_t faces[4 * 10];
    CollisionHit hit;
};
LineArena g_line;
bool g_lineOriginal;

template <typename F>
F LineFn(uint32_t address)
{
    return reinterpret_cast<F>(Code(g_lineOriginal, address));
}
float LRand(Lcg& random, float range) { return (static_cast<float>(random.Below(20001)) - 10000.0f) / 10000.0f * range; }

// The record's words, the padding and (for the box: the original leaves it unset) the face index left out.
void RecordHit(const CollisionHit& hit, bool face)
{
    const auto* w = reinterpret_cast<const uint32_t*>(&hit);
    for (int k = 0; k < 0x50 / 4; ++k) {
        uint32_t word = w[k];
        if (k == 0x20 / 4 && !face)
            continue;
        if (k == 0x2C / 4)
            word &= 0x00FFFFFF; // +0x2F padding
        if (k == 0x48 / 4)
            word &= 0x0000FFFF; // +0x4A padding
        g_trace.push_back(word);
    }
}

void __cdecl HitCallback(CollisionHit* hit)
{
    Record({0xCA11BAC0u, hit->callbackData});
    RecordHit(*hit, hit->object != reinterpret_cast<Nx::CCollObj*>(g_line.box));
    // a callback may change the record (when the hit isn't the better one, all of it is put back)
    hit->owner += 1;
    hit->unknown38 ^= 0x10;
    hit->scene += 3;
    hit->requireFlags ^= 0x0100;
    hit->ignoreFlags ^= 0x0200;
    hit->callbackData += 7;
}

// The box turned and placed at random, lines through it, beside it, starting or ending inside it, along it; the
// record's distance to beat below, at and above the hit's, its flags ignoring or requiring the box's, with and
// without the callback, nearest and farthest wanted. Then ReportHit directly on a sector object's faces (each
// face's own flags and terrain).
void TestAnimCollLineReport()
{
    GameMemory memory({{0x006918EC, 0x24}, {0x00691910, 1}});
    auto runOnce = [&](bool original) {
        g_lineOriginal = original;
        g_trace.clear();
        std::memset(&g_line, 0xA3, sizeof(g_line));
        std::memset(reinterpret_cast<void*>(0x006918EC), 0, 0x24);
        if (!original)
            UseOurCollisionVtables();
        Lcg random(0x4131F0);
        auto* box = reinterpret_cast<Nx::CCollMovBBox*>(g_line.box);
        LineFn<void*(__thiscall*)(void*)>(0x00412550)(box);
        auto collide = LineFn<bool(__thiscall*)(void*, const Mth::Line*, const Vector*, CollisionHit*, const Mth::BBox*)>(
            0x004137B0);
        for (int n = 0; n < 3000; ++n) {
            if (n % 50 == 0) {
                // a new placement: a rotation (yaw, pitch, roll) and a position
                float yaw = LRand(random, 3.1f), pitch = LRand(random, 1.5f), roll = LRand(random, 3.1f);
                float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch),
                      cr = std::cos(roll), sr = std::sin(roll);
                Matrix m;
                m.row[0] = {cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr, 0.0f};
                m.row[1] = {-cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr, 0.0f};
                m.row[2] = {sy * cp, -sp, cy * cp, 0.0f};
                m.row[3] = {0, 0, 0, 1};
                LineFn<void(__thiscall*)(void*, const Matrix*)>(0x00412070)(box, &m);
                box->m_position = {LRand(random, 500.0f), LRand(random, 500.0f), LRand(random, 500.0f), 1.0f};
                box->m_faceFlags = static_cast<uint32_t>(random.Next()) & 0xFFFF;
                box->m_terrain = static_cast<uint16_t>(random.Next());
                box->m_checksum = static_cast<uint32_t>(random.Next());
            }
            // a line through a point near the box
            Vector target{box->m_position.x + LRand(random, 80.0f), box->m_position.y + LRand(random, 50.0f),
                          box->m_position.z + LRand(random, 110.0f), 1.0f};
            Vector way{LRand(random, 1.0f), LRand(random, 1.0f), LRand(random, 1.0f), 0.0f};
            float before = LRand(random, 300.0f) + (n % 4 == 0 ? 0.0f : 150.0f);
            float after = LRand(random, 300.0f) + (n % 5 == 0 ? -100.0f : 150.0f);
            Mth::Line line;
            line.start = {target.x - way.x * before, target.y - way.y * before, target.z - way.z * before, 1.0f};
            line.end = {target.x + way.x * after, target.y + way.y * after, target.z + way.z * after, 1.0f};
            if (n % 37 == 0)
                line.end = line.start;
            Vector direction{line.end.x - line.start.x, line.end.y - line.start.y, line.end.z - line.start.z, 0.0f};
            CollisionHit& hit = g_line.hit;
            std::memset(&hit, 0x5C, sizeof(hit));
            hit.distance = n % 3 == 0 ? 2.0f : LRand(random, 1.0f) + 0.5f;
            hit.ignoreFlags = n % 4 == 1 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
            hit.requireFlags = n % 4 == 2 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
            hit.hit = false;
            hit.callback = n % 2 ? HitCallback : nullptr;
            hit.callbackData = static_cast<uint32_t>(n);
            GameVariable<bool>(0x00691910) = n % 7 == 0;
            bool result = collide(box, &line, &direction, &hit, nullptr);
            Record({result});
            RecordHit(hit, false);
            // again with the distance to beat at the one found
            if (result) {
                hit.hit = false;
                Record({collide(box, &line, &direction, &hit, nullptr)});
                RecordHit(hit, false);
            }
        }
        // ReportHit on a sector object's faces: their own flags and terrain
        auto* sector = reinterpret_cast<Nx::CCollStatic*>(g_line.sector);
        std::memset(sector, 0, sizeof(g_line.sector));
        sector->m_vtable = reinterpret_cast<void**>(0x00645C18);
        sector->m_checksum = 0x5EC7;
        sector->m_geometry = &g_line.geometry;
        std::memset(&g_line.geometry, 0, sizeof(g_line.geometry));
        g_line.geometry.m_faces = g_line.faces;
        g_line.geometry.m_numFaces = 4;
        for (int f = 0; f < 4; ++f) {
            auto* face = reinterpret_cast<uint16_t*>(g_line.faces + f * 10);
            face[0] = static_cast<uint16_t>(random.Next() | (f == 1 ? 0x40 : 0));
            face[1] = static_cast<uint16_t>(random.Next());
        }
        auto report = LineFn<bool(__cdecl*)(const Mth::Line*, void*, const Nx::FaceHit*, float, CollisionHit*)>(
            0x004131F0);
        for (int n = 0; n < 400; ++n) {
            Mth::Line line;
            line.start = {LRand(random, 1e3f), LRand(random, 1e3f), LRand(random, 1e3f), 1.0f};
            line.end = {LRand(random, 1e3f), LRand(random, 1e3f), LRand(random, 1e3f), 1.0f};
            Nx::FaceHit face;
            face.normal = {LRand(random, 1.0f), LRand(random, 1.0f), LRand(random, 1.0f), LRand(random, 1.0f)};
            face.point = {LRand(random, 9.0f), LRand(random, 9.0f), LRand(random, 9.0f), LRand(random, 9.0f)};
            face.face = static_cast<uint32_t>(n % 4);
            CollisionHit& hit = g_line.hit;
            std::memset(&hit, 0x6D, sizeof(hit));
            float distance = LRand(random, 1.0f) + 0.5f;
            hit.distance = n % 5 == 0 ? distance : LRand(random, 1.0f) + 0.5f;
            hit.ignoreFlags = n % 3 == 1 ? static_cast<uint16_t>(random.Next()) : 0;
            hit.requireFlags = n % 3 == 2 ? static_cast<uint16_t>(1u << random.Below(16)) : 0;
            hit.callback = n % 2 ? HitCallback : nullptr;
            hit.callbackData = 0x1000u + n;
            GameVariable<bool>(0x00691910) = n % 6 < 2;
            Record({report(&line, sector, &face, distance, &hit)});
            RecordHit(hit, true);
        }
        EndRunPatches();
        return g_trace;
    };
    std::vector<uint32_t> expected = runOnce(true);
    std::vector<uint32_t> actual = runOnce(false);
    Log("line report: %u trace words", static_cast<unsigned>(expected.size()));
    CompareTraces(expected, actual, "Nx line tests: ReportHit and the movable box");
}
SELFTEST(Early, TestAnimCollLineReport, TestStandIns::RemoveStandIns);

} // namespace
