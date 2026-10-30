// The park's rail sets, strings and points (0x0058AFC0-0x0058B7D5). See park_rails.h.

#include "game/ed/park_rails.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Ed {

GAME_GLOBAL(Mem::PoolPair, g_parkStringPools, 0x007CE594);
GAME_GLOBAL(Mem::PoolPair, g_parkPointPools, 0x007CE5A4);

namespace {

// The classes' pool pairs (CPoolable): a set's pool is lent to them while one of its items is made or freed.
Mem::PoolPair& StringPools() { return g_parkStringPools; }
Mem::PoolPair& PointPools() { return g_parkPointPools; }

// Whether the pair's current pool was made by it (and so is deleted when it's taken away): a byte per pool at +4.
uint8_t& PoolOwned(Mem::PoolPair& pair) { return reinterpret_cast<uint8_t*>(&pair.unknown4)[pair.current]; }

// A pool deleted: its block (0x005BA4E0, ~Pool), then the pool.
void DeletePool(Mem::Pool* pool)
{
    Mem::Free(pool->m_start);
    Mem::Manager::Get()->Free(pool);
}

// `pool` lent to `pair` as its current pool (not owned).
void LendPool(Mem::PoolPair& pair, Mem::Pool* pool)
{
    pair.pools[pair.current] = pool;
    PoolOwned(pair) = 0;
}
// The current pool taken away again (deleted if the pair made it).
void TakeBackPool(Mem::PoolPair& pair)
{
    if (Mem::Pool* pool = pair.pools[pair.current]) {
        if (PoolOwned(pair))
            DeletePool(pool);
        pair.pools[pair.current] = nullptr;
    }
}

// A climbing string's heading turned by a quarter, half and three-quarter turn (0x00645484, 0x0064F1B8, 0x0064B76C).
const float kQuarterTurn = std::bit_cast<float>(0x3FC90FDBu);
const float kHalfTurn = std::bit_cast<float>(0x40490FDBu);
const float kThreeQuarterTurn = std::bit_cast<float>(0x4096CBE4u);

} // namespace

// 0x0058AFC0
void CParkRailString::AddPoint(CParkRailPoint* point)
{
    if (!mp_points) {
        ++m_numPoints;
        mp_points = point;
        mp_last = point;
        return;
    }
    mp_last->mp_next = point;
    ++m_numPoints;
    mp_last = point;
}

// 0x0058B040
int CParkRailString::CountSegments() const
{
    if (m_flags & kLoop)
        return m_numPoints;
    int points = 0;
    for (const CParkRailPoint* point = mp_points; point; point = point->mp_next)
        ++points;
    return points > 1 ? points - 1 : 0;
}

// 0x0058B220
void __cdecl CParkRailString::Free(void* item) { StringPools().Free(item); }

// 0x0058B420
void CParkRailString::CopyFrom(const CParkRailString* source, const Mth::Vector* offset, int rot, uint32_t cluster,
                               float angle)
{
    const bool climbing = source->m_class == kClimbingNode;
    for (const CParkRailPoint* from = source->mp_points; from; from = from->mp_next) {
        CParkRailPoint* point = mp_set->NewPoint(); // (not null-checked: as the original)
        std::memcpy(point, from, offsetof(CParkRailPoint, mp_next));
        // Turned about y: what's added to the offset's x and z.
        float x, z;
        switch (rot) {
        case 1:
            x = from->m_pos.z;
            z = -from->m_pos.x;
            if (climbing)
                point->m_angles.y = angle + kQuarterTurn;
            break;
        case 2:
            x = -from->m_pos.x;
            z = -from->m_pos.z;
            if (climbing)
                point->m_angles.y = angle + kHalfTurn;
            break;
        case 3:
            x = -from->m_pos.z;
            z = from->m_pos.x;
            if (climbing)
                point->m_angles.y = angle + kThreeQuarterTurn;
            break;
        default:
            x = from->m_pos.x;
            z = from->m_pos.z;
            if (climbing)
                point->m_angles.y = angle;
            break;
        }
        point->m_pos.w = 1.0f;
        point->m_pos.x = x + offset->x;
        point->m_pos.y = offset->y + from->m_pos.y;
        point->m_pos.z = offset->z + z;
        point->m_cluster = cluster;
        AddPoint(point);
    }
    m_class = source->m_class;
    m_name = source->m_name;
    m_flags = static_cast<uint8_t>((m_flags & ~(kLoop | kClimbable)) | (source->m_flags & (kLoop | kClimbable)));
}

// 0x0058B620
void CParkRailString::DeletePoints()
{
    Mem::PoolPair& pair = PointPools();
    for (CParkRailPoint* point = mp_points; point;) {
        CParkRailPoint* next = point->mp_next;
        LendPool(pair, mp_set->mp_pointPool);
        pair.Free(point);
        TakeBackPool(pair);
        point = next;
    }
    mp_points = nullptr;
    mp_last = nullptr;
    m_numPoints = 0;
}

// 0x0058AFF0
CParkRailSet* CParkRailSet::Construct()
{
    mp_rails = nullptr;
    m_numRails = 0;
    mp_pointPool = nullptr;
    mp_railPool = nullptr;
    return this;
}

// 0x0058B010
void CParkRailSet::AddRail(CParkRailString* string)
{
    if (!mp_rails) {
        ++m_numRails;
        mp_rails = string;
        mp_last = string;
        return;
    }
    mp_last->mp_next = string;
    ++m_numRails;
    mp_last = string;
}

// 0x0058B070
int CParkRailSet::CountPoints() const
{
    int points = 0;
    for (const CParkRailString* string = mp_rails; string; string = string->mp_next)
        points += string->m_numPoints;
    return points;
}

// 0x0058B090
CParkRailString* CParkRailSet::FindRail(uint32_t name, int index)
{
    int found = 0;
    for (CParkRailString* string = mp_rails; string; string = string->mp_next) {
        if (string->m_name == name) {
            if (found == index)
                return string;
            ++found;
        }
    }
    return nullptr;
}

// 0x0058B0C0
void CParkRailSet::Init(int points, int strings, bool in)
{
    if (in)
        std::memcpy(m_name, "In set", sizeof("In set"));
    else
        std::memcpy(m_name, "Out set", sizeof("Out set"));
    char pointName[64], stringName[64];
    std::snprintf(pointName, sizeof(pointName), "%s RailPoint", m_name);
    std::snprintf(stringName, sizeof(stringName), "%s RailString", m_name);
    void* memory = Mem::New(sizeof(Mem::Pool));
    mp_pointPool = memory ? new (memory) Mem::Pool(sizeof(CParkRailPoint), points, pointName) : nullptr;
    memory = Mem::New(sizeof(Mem::Pool));
    mp_railPool = memory ? new (memory) Mem::Pool(sizeof(CParkRailString), strings, stringName) : nullptr;
}

// 0x0058B280
void CParkRailSet::FreePools()
{
    if (mp_railPool) {
        DeletePool(mp_railPool);
        mp_railPool = nullptr;
    }
    if (mp_pointPool) {
        DeletePool(mp_pointPool);
        mp_pointPool = nullptr;
    }
}

// 0x0058B2D0
CParkRailPoint* CParkRailSet::NewPoint()
{
    Mem::PoolPair& pair = PointPools();
    LendPool(pair, mp_pointPool);
    auto* point = static_cast<CParkRailPoint*>(pair.Allocate(sizeof(CParkRailPoint)));
    if (point) {
        point->m_pos = {0.0f, 0.0f, 0.0f, 0.0f};
        point->mp_next = nullptr;
    }
    TakeBackPool(pair);
    return point;
}

// 0x0058B370
CParkRailString* CParkRailSet::NewRail()
{
    Mem::PoolPair& pair = StringPools();
    LendPool(pair, mp_railPool);
    auto* string = static_cast<CParkRailString*>(pair.Allocate(sizeof(CParkRailString)));
    if (string) {
        string->m_flags = static_cast<uint8_t>((string->m_flags & ~CParkRailString::kLoop) | CParkRailString::kClimbable);
        string->mp_points = nullptr;
        string->m_numPoints = 0;
        string->mp_next = nullptr;
    }
    TakeBackPool(pair);
    string->mp_set = this; // (not null-checked: as the original)
    return string;
}

// 0x0058B5B0
void CParkRailSet::FreeRail(CParkRailString* string)
{
    Mem::PoolPair& pair = StringPools();
    LendPool(pair, mp_railPool);
    if (string)
        CParkRailString::Free(string);
    TakeBackPool(pair);
}

// 0x0058B700
void CParkRailSet::Clear()
{
    Mem::PoolPair& pair = StringPools();
    for (CParkRailString* string = mp_rails; string;) {
        CParkRailString* next = string->mp_next;
        string->DeletePoints();
        LendPool(pair, mp_railPool);
        pair.Free(string);
        TakeBackPool(pair);
        string = next;
    }
    mp_rails = nullptr;
    mp_last = nullptr;
    m_numRails = 0;
}

} // namespace Ed

REPLACE(0x0058AFC0, &Ed::CParkRailString::AddPoint);
REPLACE(0x0058AFF0, &Ed::CParkRailSet::Construct);
REPLACE(0x0058B010, &Ed::CParkRailSet::AddRail);
REPLACE(0x0058B040, &Ed::CParkRailString::CountSegments);
REPLACE(0x0058B070, &Ed::CParkRailSet::CountPoints);
REPLACE(0x0058B090, &Ed::CParkRailSet::FindRail);
REPLACE(0x0058B0C0, &Ed::CParkRailSet::Init);
REPLACE(0x0058B220, Ed::CParkRailString::Free);
REPLACE(0x0058B280, &Ed::CParkRailSet::FreePools);
REPLACE(0x0058B2D0, &Ed::CParkRailSet::NewPoint);
REPLACE(0x0058B370, &Ed::CParkRailSet::NewRail);
REPLACE(0x0058B420, &Ed::CParkRailString::CopyFrom);
REPLACE(0x0058B5B0, &Ed::CParkRailSet::FreeRail);
REPLACE(0x0058B620, &Ed::CParkRailString::DeletePoints);
REPLACE(0x0058B700, &Ed::CParkRailSet::Clear);
