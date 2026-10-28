// The engine's spawned polygons: see spawned_polys.h. The maths follows the original's x87 code (53-bit precision:
// double where it keeps values in registers, float where it stores them).

#include "game/gfx/spawned_polys.h"

#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/pool.h"
#include "game/gfx/pc_primitive.h"
#include "game/lst/hashtable.h"
#include "game/script/cfuncs_engine.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

Lst::HashTable* SpawnedPolys() { return GameVariable<Lst::HashTable*>(0x007004AC); }

void* NextValue(Lst::HashTable* table)
{
    if (!table->IterateNext() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* FirstValue(Lst::HashTable* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextValue(table);
}

// The primitive's calls: its virtual slots ([0] destroyed, [7] a corner's position, [15] a corner's colour, [16] a
// corner's texture coordinates) and the engine's dispatchers for [1] (the texture, 0x004A1090), [3] (the layout, type
// and count, 0x004A10A0) and [4] (readied, then +0x15 set: 0x0049C6C0).
template <typename... A>
void PrimitiveVirtual(CPcPrimitive* primitive, int slot, A... args)
{
    reinterpret_cast<void(__thiscall*)(CPcPrimitive*, A...)>(primitive->vtable[slot])(primitive, args...);
}
void SetCorner(CPcPrimitive* primitive, int corner, const Mth::Vector& at)
{
    PrimitiveVirtual(primitive, 7, corner, at.x, at.y, at.z);
}
void SetCornerColour(CPcPrimitive* primitive, int corner, const uint8_t* colour)
{
    uint32_t rgba;
    std::memcpy(&rgba, colour, 4);
    PrimitiveVirtual(primitive, 15, corner, rgba);
}

// Four corners turned by the polygon's matrix and moved to its position.
void PlaceCorners(sSpawnedPoly* poly, Mth::Vector* corners)
{
    for (int i = 0; i < 4; ++i)
        corners[i].MultiplyBy(poly->matrix);
    for (int i = 0; i < 4; ++i) {
        corners[i].x = static_cast<float>(static_cast<double>(corners[i].x) + poly->pos.x);
        corners[i].y = static_cast<float>(static_cast<double>(corners[i].y) + poly->pos.y);
        corners[i].z = static_cast<float>(static_cast<double>(corners[i].z) + poly->pos.z);
    }
}

} // namespace

// 0x00491990
sSpawnedPoly* sSpawnedPoly::Construct()
{
    std::memset(this, 0, sizeof(*this));
    justX = 0.0f;
    justY = 0.0f;
    blendMode = 0x54628ED7;
    return this;
}

namespace Engine {

// 0x00492450
void __cdecl SpawnPoly(sSpawnedPoly* poly, Mth::Vector* direction, Mth::Vector* up)
{
    void* memory = Mem::New(0x158);
    poly->primitive = memory ? static_cast<CPcPrimitive*>(memory)->Construct(1) : nullptr;
    Mth::Matrix& m = poly->matrix;
    std::memset(&m, 0, sizeof(m));
    m.row[0].x = m.row[1].y = m.row[2].z = m.row[3].w = 1.0f;
    direction->Normalize();
    up->Normalize();
    m.row[0] = *direction;
    // Row 2 across the direction and up, row 1 across row 2 and the direction.
    const Mth::Vector& r0 = m.row[0];
    double s0 = static_cast<double>(r0.z) * up->y - static_cast<double>(r0.y) * up->z;
    double s1 = static_cast<double>(up->z) * r0.x - static_cast<double>(r0.z) * up->x;
    double s2 = static_cast<double>(r0.y) * up->x - static_cast<double>(up->y) * r0.x;
    m.row[2].w = 0.0f;
    m.row[2].z = static_cast<float>(s2);
    m.row[2].x = static_cast<float>(s0);
    m.row[2].y = static_cast<float>(s1);
    const Mth::Vector& r2 = m.row[2];
    double t0 = static_cast<double>(r2.y) * r0.z - static_cast<double>(r2.z) * r0.y;
    double t1 = static_cast<double>(r0.x) * r2.z - static_cast<double>(r2.x) * r0.z;
    double t2 = static_cast<double>(r2.x) * r0.y - static_cast<double>(r0.x) * r2.y;
    m.row[1].w = 0.0f;
    m.row[1].z = static_cast<float>(t2);
    m.row[1].x = static_cast<float>(t0);
    m.row[1].y = static_cast<float>(t1);
    uint32_t layout = 1; // (colours; 3: and texture coordinates)
    if (poly->texture) {
        GameFunction<void(__thiscall*)(CPcPrimitive*, uint8_t*, int, int)>(0x004A1090)(poly->primitive, poly->texture,
                                                                                      0, 0);
        layout = 3;
    }
    std::memcpy(poly->colours, poly->startColours, sizeof(poly->colours));
    float height = poly->startDims[1];
    poly->dims[1] = height;
    float width = poly->startDims[0];
    poly->dims[0] = width;
    poly->age = 0.0f;
    // The corners about the centre (w: see the header).
    double left = width * -0.5;
    double top = height * -0.5;
    double right = width * 0.5;
    double bottom = height * 0.5;
    Mth::Vector corners[4] = {{static_cast<float>(left), static_cast<float>(top), 0.0f, 0.0f},
                              {static_cast<float>(right), static_cast<float>(top), 0.0f, 0.0f},
                              {static_cast<float>(right), static_cast<float>(bottom), 0.0f, 0.0f},
                              {static_cast<float>(left), static_cast<float>(bottom), 0.0f, 0.0f}};
    PlaceCorners(poly, corners);
    CPcPrimitive* primitive = poly->primitive;
    primitive->blendMode = poly->blendMode;
    GameFunction<void(__thiscall*)(CPcPrimitive*, uint32_t, int, int)>(0x004A10A0)(primitive, layout, 4, 4);
    const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (int i = 0; i < 4; ++i) {
        SetCornerColour(poly->primitive, i, poly->colours[i]);
        if (layout & 2)
            PrimitiveVirtual(poly->primitive, 16, i, uvs[i][0], uvs[i][1]);
        SetCorner(poly->primitive, i, corners[i]);
    }
    reinterpret_cast<uint8_t*>(poly->primitive)[0x14] = (poly->flags >> 1) & 1;
    GameFunction<void(__thiscall*)(CPcPrimitive*)>(0x0049C6C0)(poly->primitive);
    uint32_t key = reinterpret_cast<uint32_t>(poly);
    SpawnedPolys()->PutItem(key, poly);
}

// 0x004919C0
void __cdecl UpdateSpawnedPolys()
{
    float elapsed = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    for (void* value = FirstValue(SpawnedPolys()); value;) {
        auto* poly = static_cast<sSpawnedPoly*>(value);
        if (poly->flags & 1) {
            value = NextValue(SpawnedPolys());
            continue;
        }
        double age = static_cast<double>(elapsed) + poly->age;
        poly->age = static_cast<float>(age);
        if (age >= poly->lifetime) {
            // Done: its primitive destroyed, the polygon taken out and freed.
            poly->flags |= 1;
            poly->age = 0.0f;
            value = NextValue(SpawnedPolys());
            if (poly->primitive)
                PrimitiveVirtual(poly->primitive, 0, 1u);
            poly->primitive = nullptr;
            uint32_t key = reinterpret_cast<uint32_t>(poly);
            SpawnedPolys()->FlushItem(key);
            CFuncs::FreeSpawnedPolyDetails(poly);
            continue;
        }
        double t = age / poly->lifetime;
        for (int c = 0; c < 4; ++c) {
            for (int k = 0; k < 4; ++k) {
                int change = static_cast<int>(static_cast<int64_t>(
                    static_cast<double>(poly->endColours[c][k] - poly->startColours[c][k]) * t));
                poly->colours[c][k] = static_cast<uint8_t>(poly->startColours[c][k] + change);
            }
        }
        for (int i = 0; i < 2; ++i)
            poly->dims[i] = static_cast<float>((static_cast<double>(poly->endDims[i]) - poly->startDims[i]) * t +
                                               poly->startDims[i]);
        for (int c = 0; c < 4; ++c)
            SetCornerColour(poly->primitive, c, poly->colours[c]);
        // Where the corners go, by the justification: right / bottom of the position, centred, or left / top.
        float xOffset = 0.0f;
        if (poly->justX > 0.0f)
            xOffset = poly->dims[0];
        else if (poly->justX == 0.0f)
            xOffset = static_cast<float>(poly->dims[0] * 0.5);
        double yOffset = 0.0;
        if (poly->justY > 0.0f)
            yOffset = poly->dims[1];
        else if (poly->justY == 0.0f)
            yOffset = poly->dims[1] * 0.5;
        float left = -xOffset;
        float top = static_cast<float>(-yOffset);
        float right = static_cast<float>(static_cast<double>(left) + poly->dims[0]);
        float bottom = static_cast<float>(static_cast<double>(top) + poly->dims[1]);
        Mth::Vector corners[4] = {{left, top, 0.0f, 0.0f},
                                  {right, top, 0.0f, 0.0f},
                                  {right, bottom, 0.0f, 0.0f},
                                  {left, bottom, 0.0f, 0.0f}};
        PlaceCorners(poly, corners);
        for (int c = 0; c < 4; ++c)
            SetCorner(poly->primitive, c, corners[c]);
        value = NextValue(SpawnedPolys());
    }
}

// 0x00491E10
void __cdecl KillAllSpawnedPolys()
{
    for (void* value = FirstValue(SpawnedPolys()); value;) {
        auto* poly = static_cast<sSpawnedPoly*>(value);
        value = NextValue(SpawnedPolys());
        if (poly->primitive)
            PrimitiveVirtual(poly->primitive, 0, 1u);
        poly->primitive = nullptr;
        uint32_t key = reinterpret_cast<uint32_t>(poly);
        SpawnedPolys()->FlushItem(key);
        GameVariable<Mem::PoolPair>(0x00700500).Free(poly);
    }
}

} // namespace Engine

} // namespace Nx

REPLACE(0x00491990, &Nx::sSpawnedPoly::Construct);
REPLACE(0x00492450, Nx::Engine::SpawnPoly);
REPLACE(0x004919C0, Nx::Engine::UpdateSpawnedPolys);
REPLACE(0x00491E10, Nx::Engine::KillAllSpawnedPolys);
