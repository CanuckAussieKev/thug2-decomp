// Self-tests for the rail editor's sectors and checks (rail_editor_draw.cpp): original vs ours on the rail editor kit's
// world (rail_editor_testkit.h) with stand-in sectors whose geometries keep their vertices outside the arena, every
// callee into game code stood in for (the script values, the sector and collision calls, the clone, the feeler's
// query); the register-argument functions called through their stubs as the original's callers call them.
//
// Three places where the original reads an unset local (garbage) are made to read 0 for the test, as ours does:
// 0x004F3D80's and 0x004F4370's w (an FLD replaced by FLDZ) and 0x004F4B90's matrix's last row (its multiply called
// through a wrapper that clears it).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/rail_editor.h"
#include "game/mth/vector.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "game/obj/smtptr.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/rail_editor_testkit.h"
#include "hook/selftest.h"

namespace {

using namespace RailEditorKit;
using Mth::Vector;

template <typename F>
F Orig(uint32_t address)
{
    return Original<F>(address);
}

// The geometries' vertices (outside the arena: 48 geometries of up to 24).
constexpr int kVertices = 24;
Vector g_store[kSectorCount][kVertices];
Vector g_storeStart[kSectorCount][kVertices];
int g_storeCount[kSectorCount];
// The collision's raw vertices (one collision object for every geometry).
constexpr uint32_t kCollision = kScratch;           // +6 the vertex count
Vector g_raw[32];
int g_rawCount;
int g_clones;

int GeometryIndex(uint32_t geometry)
{
    if (geometry < reinterpret_cast<uint32_t>(ArenaPtr(kGeometries)) ||
        geometry >= reinterpret_cast<uint32_t>(ArenaPtr(kGeometries + kSectorCount * 8)))
        return kSectorCount - 1;
    return static_cast<int>((geometry - reinterpret_cast<uint32_t>(ArenaPtr(kGeometries))) / 8);
}

constexpr uint32_t kRailSource = 0x66FE99BF, kPostSource = 0x2756C52D, kGreen = 0x5DC3C690, kRed = 0x8DDA17D6;

float g_scriptFloats[4]; // max join, corner leave, minimum separation, max slope
int g_scriptInts[8];

std::vector<StandIn> DrawStandIns()
{
    return {
        {0x00595200, 0, 0, Ret::Int, false, 0, {Ours(&Ed::CParkManager::sInstance)},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kManager)); }},
        {0x00591DD0, 8, 2, Ret::Bool, true, 0, {Ours(&Ed::CParkGenerator::DeleteSector)}},
        {0x0059E170, 0, 0, Ret::None, false, 0, {Ours(Ed::SetCountsDirty)}},
        {0x00592090, 0, 0, Ret::None, true, 0, {Ours(&Ed::CParkGenerator::RebuildCollision)}},
        {0x00592320, 16, 4, Ret::Int, true, 0, {Ours(&Ed::CParkGenerator::CloneSector)},
         [](Frame&, Result& r) { r.eax = 0xC10E0000u + static_cast<uint32_t>(g_clones++); }},
        {0x0049E670, 0, 0, Ret::None, true},
        {0x0049E680, 4, 1, Ret::None, true},
        {0x0049E9B0, 4, 1, Ret::None, true, 1, {Ours(&Nx::CSector::SetActive)}},
        {0x0048C510, 0, 1, Ret::Int, false, 0, {Ours(Nx::Engine::GetSector)}, [](Frame& f, Result& r) {
             uint32_t name = f.args[0];
             int index = name == kRailSource ? 40 : name == kPostSource ? 41 : name == kGreen ? 42 : name == kRed ? 43
                                                                                                       : 44 + (name & 3);
             r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kSectors + index * 0x20));
         }},
        {0x0048DF70, 0, 0, Ret::Int, true, 0, {},
         [](Frame& f, Result& r) { r.eax = static_cast<uint32_t>(g_storeCount[GeometryIndex(f.ecx)]); }},
        {0x0048DF80, 4, 1, Ret::None, true, 0, {}, [](Frame& f, Result&) {
             int g = GeometryIndex(f.ecx);
             std::memcpy(reinterpret_cast<void*>(f.args[0]), g_store[g], g_storeCount[g] * sizeof(Vector));
         }},
        {0x0048DF90, 4, 1, Ret::None, true, 0, {}, [](Frame& f, Result&) {
             int g = GeometryIndex(f.ecx);
             const auto* v = reinterpret_cast<const Vector*>(f.args[0]);
             for (int i = 0; i < g_storeCount[g]; ++i) {
                 RecordFloat(v[i].x);
                 RecordFloat(v[i].y);
                 RecordFloat(v[i].z);
             }
             std::memcpy(g_store[g], v, g_storeCount[g] * sizeof(Vector));
         }},
        {0x005BBAE0, 0, 1, Ret::Int, false, 0, {Ours(Mem::Malloc)},
         [](Frame& f, Result& r) { r.eax = reinterpret_cast<uint32_t>(HeapAllocate(f.args[0])); }},
        {0x005BBB60, 0, 1, Ret::None, false, 0, {Ours(Mem::FreeMalloc)}},
        {0x0049E810, 0, 0, Ret::Int, true, 0, {},
         [](Frame&, Result& r) { r.eax = reinterpret_cast<uint32_t>(ArenaPtr(kCollision)); }},
        {0x00414F90, 4, 1, Ret::None, true, 0, {Ours(&Nx::CCollObjTriData::GetRawVertices)}, [](Frame& f, Result&) {
             std::memcpy(reinterpret_cast<void*>(f.args[0]), g_raw, g_rawCount * sizeof(Vector));
         }},
        {0x0049EA00, 4, 1, Ret::None, true, 0, {}, [](Frame& f, Result&) {
             const auto* v = reinterpret_cast<const Vector*>(f.args[0]);
             for (int i = 0; i < g_rawCount; ++i) {
                 RecordFloat(v[i].x);
                 RecordFloat(v[i].y);
                 RecordFloat(v[i].z);
                 RecordFloat(v[i].w);
             }
         }},
        {0x00405FA0, 0, 0, Ret::None, true, 0, {Ours(&Vector::Print)}, [](Frame& f, Result&) {
             const auto* v = reinterpret_cast<const Vector*>(f.ecx);
             RecordFloat(v->x);
             RecordFloat(v->y);
         }},
        {0x00478F00, 0, 1, Ret::Float, false, 0, {Ours(static_cast<float (*)(uint32_t)>(&Script::GetFloat))},
         [](Frame& f, Result& r) {
             switch (f.args[0]) {
             case 0xB1459A78: r.st0 = g_scriptFloats[0]; break;
             case 0x76C1DA15: r.st0 = g_scriptFloats[1]; break;
             case 0x8BEA1C03: r.st0 = g_scriptFloats[2]; break;
             default: r.st0 = g_scriptFloats[3]; break;
             }
         }},
        {0x00478F60, 0, 1, Ret::Int, false, 0, {Ours(static_cast<int (*)(uint32_t)>(&Script::GetInteger))},
         [](Frame& f, Result& r) {
             static const uint32_t names[8] = {0x4551E901, 0xEEBE4394, 0xAD28B18B, 0xC0F55560,
                                               0xB09FA1EF, 0x89B66FB9, 0xE46B8B52, 0x94017FDD};
             for (int i = 0; i < 8; ++i) {
                 if (f.args[0] == names[i])
                     r.eax = static_cast<uint32_t>(g_scriptInts[i]);
             }
         }},
        {0x0058DEC0, 0, 5, Ret::Bool, false, 0, {Ours(Ed::CParkEditor::IsInsidePark)}},
        {0x004172F0, 8, 2, Ret::Bool, true, 0, {Ours(&Nx::CFeeler::GetCollision)}, [](Frame& f, Result& r) {
             auto* feeler = reinterpret_cast<Nx::CFeeler*>(f.ecx);
             for (int i = 0; i < 8; ++i)
                 RecordFloat(reinterpret_cast<const float*>(&feeler->m_line)[i]);
             feeler->m_point.y = RandomFloat(40.0f);
             if (Next() % 4 == 0)
                 *reinterpret_cast<void**>(f.ecx + 0x98) = ArenaPtr(kScratch + 0x40);
             r.eax = Next() % 5 != 0;
         }},
        {0x004637E0, 4, 1, Ret::None, true, 0, {Ours(&Obj::CRefCounted::RemoveSmartPointer)}},
    };
}

// The original's unset locals read as 0 (see the top).
__declspec(naked) void MultiplyClearingLastRow()
{
    __asm {
        mov eax, [esp + 0xc]
        mov dword ptr [eax + 0x30], 0
        mov dword ptr [eax + 0x34], 0
        mov dword ptr [eax + 0x38], 0
        mov eax, 0x0041F470
        jmp eax
    }
}
struct GarbageAsZero {
    CodePatch transformW{0x004F3DCE, {0xD9, 0xEE, 0x90, 0x90}};
    CodePatch railW{0x004F43EC, {0xD9, 0xEE, 0x90, 0x90}};
    CodePatch matrix{0x004F4DFB, CallTo(0x004F4DFB)};
    static std::vector<uint8_t> CallTo(uint32_t from)
    {
        std::vector<uint8_t> bytes = JumpTo(from, Resolve(reinterpret_cast<const void*>(&MultiplyClearingLastRow)));
        bytes[0] = 0xE8;
        return bytes;
    }
};

// The register-argument functions called as their callers call them.
uint32_t CallTransform(uint32_t code, Vector* target, const Vector* point, const Vector* origin, const Vector* position,
                       float angle, uint32_t* kept)
{
    uint32_t result, keptEcx, keptEdx;
    __asm {
        push esi
        mov eax, target
        mov ecx, position
        mov edx, origin
        mov esi, point
        push angle
        call code
        add esp, 4
        mov result, eax
        mov keptEcx, ecx
        mov keptEdx, edx
        pop esi
    }
    kept[0] = keptEcx;
    kept[1] = keptEdx;
    return result;
}
uint32_t CallThreePoints(uint32_t code, const Vector* a, const Vector* b, const Vector* c)
{
    uint32_t result;
    __asm {
        mov eax, a
        mov ecx, b
        mov edx, c
        call code
        movzx eax, al
        mov result, eax
    }
    return result;
}
uint32_t CallEcxEax(uint32_t code, const Vector* a, const Vector* b, uint32_t* keptEcx)
{
    uint32_t result, kept;
    __asm {
        mov ecx, a
        mov eax, b
        call code
        mov result, eax
        mov kept, ecx
    }
    *keptEcx = kept;
    return result;
}
uint32_t CallEcxEdx(uint32_t code, const Vector* a, const Vector* b)
{
    uint32_t result;
    __asm {
        mov ecx, a
        mov edx, b
        call code
        movzx eax, al
        mov result, eax
    }
    return result;
}
void CallOrient(uint32_t code, const Vector* direction, const Vector* translation, Vector* vertices, const int* indices,
                int count)
{
    __asm {
        push edi
        mov ecx, direction
        mov edi, translation
        push count
        push indices
        push vertices
        call code
        add esp, 0xc
        pop edi
    }
}
void CallStretch(uint32_t code, uint32_t source, const Vector* from, const Vector* previous, const Vector* to,
                 uint8_t* sector, uint8_t* previousSector, uint32_t update)
{
    __asm {
        mov eax, source
        mov ecx, from
        push update
        push previousSector
        push sector
        push to
        push previous
        call code
        add esp, 0x14
    }
}
float CallGround(uint32_t code, const Vector* position)
{
    float result;
    __asm {
        mov ecx, position
        call code
        fstp result
    }
    return result;
}
uint32_t Code(const void* function) { return Resolve(function); }

// The world's extras: the geometries' vertices, the collision's, the script values.
void BuildGeometry(const std::vector<CEditedRail*>& rails)
{
    // A point far off now and then (segments too long either side of it).
    if (!rails.empty() && Next() % 3 == 0) {
        CEditedRailPoint* p = rails[Next() % rails.size()]->mp_points;
        for (uint32_t i = 0, n = Next() % 4; p && p->mp_next && i < n; ++i)
            p = p->mp_next;
        if (p)
            p->m_pos.x += Next() % 2 ? 4200.0f : 5000.0f;
    }
    // (one vertex count for all: a clone has its source's vertices)
    int count = 20 + static_cast<int>(Next() % 5);
    for (int g = 0; g < kSectorCount; ++g) {
        g_storeCount[g] = count;
        for (int i = 0; i < kVertices; ++i)
            g_storeStart[g][i] = {RandomFloat(6.0f), RandomFloat(6.0f), RandomFloat(6.0f), 1.0f};
    }
    for (int i = 40; i < kSectorCount; ++i)
        *At<uint8_t*>(kSectors + i * 0x20 + 0x14) = ArenaPtr(kGeometries + i * 8);
    g_rawCount = 16 + static_cast<int>(Next() % 8);
    *At<uint16_t>(kCollision + 6) = static_cast<uint16_t>(g_rawCount);
    for (int i = 0; i < g_rawCount; ++i) {
        const Vector& t = g_storeStart[40 + Next() % 2][Next() % 20];
        switch (Next() % 3) {
        case 0:
            g_raw[i] = t;
            break;
        case 1:
            g_raw[i] = {t.x, t.y + RandomFloat(2.0f), t.z, 0.0f};
            break;
        default:
            g_raw[i] = {RandomFloat(6.0f), RandomFloat(6.0f), RandomFloat(6.0f), 0.0f};
            break;
        }
    }
    static const float joins[] = {10.0f, 45.0f, 90.0f, 135.0f, 179.0f};
    g_scriptFloats[0] = joins[Next() % 5];
    g_scriptFloats[1] = joins[Next() % 5];
    g_scriptFloats[2] = Next() % 3 ? 2.0f : 60.0f;
    g_scriptFloats[3] = Next() % 3 ? 45.0f : 10.0f;
    g_scriptInts[0] = static_cast<int>(Next() % 9);
    g_scriptInts[1] = static_cast<int>(Next() % 9);
    for (int i = 2; i < 8; ++i)
        g_scriptInts[i] = static_cast<int>(Next() % 300);
}

// A position near a point of the world (or anywhere).
Vector Near(const std::vector<CEditedRail*>& rails)
{
    if (!rails.empty() && rails[0]->mp_points && Next() % 2) {
        Vector p = rails[0]->mp_points->m_pos;
        return {p.x + RandomFloat(50.0f), p.y + RandomFloat(20.0f), p.z + RandomFloat(50.0f), 1.0f};
    }
    return {Coordinate() * 30.0f, RandomFloat(30.0f), Coordinate() * 30.0f, 1.0f};
}

uint32_t RunDrawOp(int op, bool o, const std::vector<CEditedRail*>& rails, uint32_t pick)
{
    CRailEditorComponent* ed = Editor();
    CEditedRail* rail = rails.empty() ? nullptr : rails[pick % rails.size()];
    CEditedRailPoint* point = rail ? rail->mp_points : nullptr;
    for (uint32_t i = 0, n = Next() % 4; point && point->mp_next && i < n; ++i)
        point = point->mp_next;
    Vector* scratch = At<Vector>(kOut);
    Vector a = Near(rails), b = Near(rails), c = Near(rails);
    Vector* origin = At<Vector>(kOut + 0x40);
    Vector* position = At<Vector>(kOut + 0x50);
    *origin = {Coordinate(), RandomFloat(10.0f), Coordinate(), 1.0f};
    *position = {Coordinate(), RandomFloat(10.0f), Coordinate(), Next() % 2 ? 1.0f : 0.5f};
    float angle = static_cast<float>(static_cast<int>(Next() % 721) - 360) * 0.5f;
    if (Next() % 4 == 0) {
        *origin = GameVariable<Vector>(0x007CCD30);
        *position = *origin;
        angle = 0.0f;
    }
    switch (op) {
    case 0: {
        uint32_t kept[2];
        uint32_t r = CallTransform(o ? 0x004F3D80 : Code(&Ed::TransformRailPointStub), scratch, &a, origin, position,
                                   angle, kept);
        Record(kept[0]);
        Record(kept[1]);
        return r;
    }
    case 1: {
        // Sometimes a short leg (under 1), none at all, or the threshold right at the turn's cosine (to the bit).
        Vector* last = &c;
        switch (Next() % 5) {
        case 0:
            last = &b;
            break;
        case 1:
            c = {b.x + 0.25f, b.y, b.z - 0.5f, 1.0f};
            break;
        default:
            break;
        }
        if (Next() % 2) {
            double x1 = static_cast<double>(b.x) - a.x, z1 = static_cast<double>(b.z) - a.z;
            double x2 = static_cast<double>(last->x) - b.x, z2 = static_cast<double>(last->z) - b.z;
            double dot = (x1 * x2 + z1 * z2) / (std::sqrt(x1 * x1 + z1 * z1) * std::sqrt(x2 * x2 + z2 * z2) + 1e-30);
            float degrees = static_cast<float>(std::acos(dot < -1.0 ? -1.0 : dot > 1.0 ? 1.0 : dot) * 57.29577951308232);
            uint32_t bits;
            std::memcpy(&bits, &degrees, 4);
            bits += static_cast<uint32_t>(static_cast<int>(Next() % 7) - 3);
            std::memcpy(&g_scriptFloats[1], &bits, 4);
        }
        return CallThreePoints(o ? 0x004F3E40 : Code(&Ed::RailCornerOkStub), &a, &b, last);
    }
    case 2: {
        uint32_t kept;
        Vector distant = {a.x + (Next() % 2 ? 4000.0f : 4400.0f), a.y + RandomFloat(500.0f), a.z, 1.0f};
        const Vector* other = Next() % 2 ? &b : &distant;
        uint32_t r = CallEcxEax(o ? 0x004F3F30 : Code(&Ed::RailTooFarStub), &a, other, &kept);
        Record(kept);
        r = r * 2 + CallEcxEax(o ? 0x004F3F70 : Code(&Ed::RailOutOfReachStub), &a, other, &kept);
        Record(kept);
        return r;
    }
    case 3: {
        Vector steep = {a.x + RandomFloat(5.0f), a.y + RandomFloat(10.0f), a.z + RandomFloat(5.0f), 1.0f};
        const Vector* other = Next() % 2 ? &b : &steep;
        return CallEcxEdx(o ? 0x004F3FB0 : Code(&Ed::RailSegmentValidStub), &a, other);
    }
    case 4: {
        auto* vertices = At<Vector>(kScratch + 0x100);
        for (int i = 0; i < 20; ++i)
            vertices[i] = {RandomFloat(6.0f), RandomFloat(6.0f), RandomFloat(6.0f), Next() % 2 ? 1.0f : 0.0f};
        static const int indices[10] = {1, 9, 3, 10, 14, 19, 0, 13, 2, 16};
        Vector direction = a;
        switch (Next() % 4) {
        case 0:
            direction = {0.0f, Next() % 2 ? 3.0f : -2.0f, 0.0f, 0.5f};
            break;
        case 1:
            direction = {0.0f, 0.0f, 0.0f, 1.0f};
            break;
        default:
            break;
        }
        CallOrient(o ? 0x004F4B90 : Code(&Ed::OrientRailVerticesStub), &direction, &b, vertices, indices,
                   static_cast<int>(Next() % 11));
        return 1;
    }
    case 5: {
        uint8_t* sector = ArenaPtr(kSectors + (44 + Next() % 4) * 0x20);
        uint8_t* previous = Next() % 2 ? ArenaPtr(kSectors + (44 + Next() % 4) * 0x20) : nullptr;
        uint32_t source = Next() % 3 ? kRailSource : kGreen;
        CallStretch(o ? 0x004F4E70 : Code(&Ed::StretchRailSectorStub), source, &a, &c, &b, sector, previous,
                    Next() % 2);
        return 1;
    }
    case 6:
        if (!point)
            return 0;
        if (o)
            Orig<void(__thiscall*)(void*, const void*, const void*, float)>(0x004F5460)(point, origin, position, angle);
        else
            point->DrawSegment(origin, position, angle);
        return 1;
    case 7:
        if (!point)
            return 0;
        if (Next() % 2)
            point->m_height = Next() % 2 ? 2.625f : 2.5f;
        if (o)
            Orig<void(__thiscall*)(void*, const void*, const void*, float)>(0x004F56D0)(point, origin, position, angle);
        else
            point->DrawPost(origin, position, angle);
        return 1;
    case 8: {
        float h = o ? CallGround(0x004F5D10, &a) : CallGround(Code(&CEditedRailPoint::GroundHeight), &a);
        return Bits(h);
    }
    case 9: {
        if (!point)
            return 0;
        bool flash = Next() % 2, ends = Next() % 2;
        if (o)
            Orig<void(__thiscall*)(void*, bool, bool)>(0x004F5E30)(point, flash, ends);
        else
            point->Highlight(flash, ends);
        return 1;
    }
    case 10:
        if (!rail)
            return 0;
        if (o)
            Orig<void(__thiscall*)(void*, const void*, const void*, float)>(0x004F4370)(rail, origin, position, angle);
        else
            rail->Transform(origin, position, angle);
        return 1;
    case 11:
        if (!rail)
            return 0;
        if (Next() % 2) {
            if (o)
                Orig<void(__thiscall*)(void*, const void*, const void*, float)>(0x004F61C0)(rail, origin, position,
                                                                                            angle);
            else
                rail->DrawSegments(origin, position, angle);
        } else {
            if (o)
                Orig<void(__thiscall*)(void*, const void*, const void*, float)>(0x004F6210)(rail, origin, position,
                                                                                            angle);
            else
                rail->DrawPosts(origin, position, angle);
        }
        return 1;
    case 12: {
        if (!rail || !rail->mp_points)
            return 0;
        int index = static_cast<int>(Next() % static_cast<uint32_t>(rail->CountPoints()));
        CEditedRailPoint* moved = rail->GetPoint(index);
        Vector to = moved->m_pos;
        to = {to.x + RandomFloat(Next() % 4 ? 30.0f : 6000.0f), to.y + RandomFloat(5.0f), to.z + RandomFloat(30.0f),
              1.0f};
        CEditedRailPoint* neighbour = Next() % 2 ? moved->mp_next : moved->mp_prev;
        if (neighbour && Next() % 2) {
            // 4500 away along x to the bit, a hair off in y and z: the squares' order decides the reach check
            Vector n = neighbour->m_pos;
            to = {n.x + 4500.0f, n.y + 3.4e-5f * static_cast<float>(1 + Next() % 3),
                  n.z + 3.4e-5f * static_cast<float>(1 + Next() % 3), 1.0f};
        }
        *scratch = to;
        return o ? Orig<bool(__thiscall*)(void*, int, const void*, bool)>(0x004F63B0)(rail, index, scratch, true)
                 : rail->MovePoint(index, scratch, true);
    }
    case 13: {
        uint32_t which = Next() % 5;
        static const uint32_t calls[5] = {0x004F6510, 0x004F6560, 0x004F65A0, 0x004F6600, 0x004F6650};
        if (o) {
            Orig<void(__thiscall*)(void*)>(calls[which])(ed);
        } else {
            switch (which) {
            case 0:
                ed->RestoreHeights();
                break;
            case 1:
                ed->StoreHeights();
                break;
            case 2:
                ed->DrawAllSegments();
                break;
            case 3:
                ed->DrawAllPosts();
                break;
            default:
                ed->Rebuild();
                break;
            }
        }
        return which;
    }
    case 14: {
        Vector to = a;
        if (ed->mp_current && ed->mp_current->mp_points && Next() % 2) {
            Vector head = ed->mp_current->mp_points->m_pos;
            float reach[] = {30.0f, 3990.0f, 4100.0f, 4600.0f};
            to = {head.x + reach[Next() % 4], head.y + RandomFloat(20.0f), head.z + RandomFloat(30.0f), 1.0f};
        }
        if (Next() % 3 == 0)
            ed->m_dottedLineName = Next() % 2 ? kGreen : kRed;
        *scratch = to;
        if (o)
            Orig<void(__thiscall*)(void*, const void*)>(0x004F6AF0)(ed, scratch);
        else
            ed->DrawDottedLine(scratch);
        return 1;
    }
    default: {
        uint32_t source = Next() % 2 ? kRailSource : kPostSource;
        return reinterpret_cast<uint32_t>(o ? Orig<uint8_t*(__cdecl*)(uint32_t)>(0x004F3E00)(source)
                                            : Ed::CloneRailSector(source));
    }
    }
}

constexpr int kDrawOps = 16;

void TestRailEditorDraw()
{
    StandIns standIns(DrawStandIns());
    GarbageAsZero zeroed;
    Globals saved = Globals::Read();
    int savedCounter = GameVariable<int>(0x007CCD2C);
    Vector savedZero = GameVariable<Vector>(0x007CCD30);
    GameVariable<Vector>(0x007CCD30) = {0.0f, 0.0f, 0.0f, 0.0f};
    bool same = true;
    int counts[kDrawOps] = {};
    for (int variant = 0; variant < 8000 && same; ++variant) {
        g_rng = 0x2F6E2B1u * (variant + 1);
        std::vector<CEditedRail*> rails = BuildWorld(4, 5);
        BuildGeometry(rails);
        int op = static_cast<int>(Next() % kDrawOps);
        uint32_t pick = Next();
        int counter = static_cast<int>(Next() % 9);
        SaveStart();
        uint32_t seed = Next() | 1;
        auto run = [&](bool o) {
            return RunWorld(seed, [&] {
                std::memcpy(g_store, g_storeStart, sizeof(g_store));
                g_clones = 0;
                GameVariable<int>(0x007CCD2C) = counter;
                uint32_t r = RunDrawOp(op, o, rails, pick);
                Record(static_cast<uint32_t>(GameVariable<int>(0x007CCD2C)));
                for (int g = 40; g < kSectorCount; ++g) {
                    for (int i = 0; i < g_storeCount[g]; ++i) {
                        RecordFloat(g_store[g][i].x);
                        RecordFloat(g_store[g][i].y);
                        RecordFloat(g_store[g][i].z);
                    }
                }
                return r;
            });
        };
        auto expected = run(true);
        auto actual = run(false);
        char what[48];
        std::snprintf(what, sizeof(what), "rail editor draw op %d", op);
        same = Compare(expected, actual, what, variant);
        if (!same) {
            Log("  (its result: original %08X ours %08X)", expected[0], actual[0]);
            size_t first = 0;
            while (first < expected.size() && first < actual.size() && expected[first] == actual[first])
                ++first;
            for (size_t i = first; i-- > 0;) {
                uint32_t w = expected[i];
                if (w >= 0x00400000 && w < 0x00640000) {
                    Log("  last call before entry %u: %08X at entry %u (this %08X; geometry %d)", (unsigned)first, w,
                        (unsigned)i, expected[i + 1], GeometryIndex(expected[i + 1]));
                    break;
                }
            }
        }
        ++counts[op];
    }
    saved.Write();
    GameVariable<int>(0x007CCD2C) = savedCounter;
    GameVariable<Vector>(0x007CCD30) = savedZero;
    Check(same, "ed: the rail editor's sectors, highlights and checks (16 kinds of call)");
}

} // namespace

SELFTEST(Early, TestRailEditorDraw);
