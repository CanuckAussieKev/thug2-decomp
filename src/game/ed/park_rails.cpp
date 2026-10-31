// The park's rail sets, strings and points (0x0058AFC0-0x0058B7D5). See park_rails.h.

#include "game/ed/park_rails.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"

namespace ParkEditing {

MemSys::PoolPair g_parkStringPools;
MemSys::PoolPair g_parkPointPools;

namespace {

// The classes' pool pairs (CPoolable): a set's pool is lent to them while one of its items is made or freed.
MemSys::PoolPair& StringPools() { return g_parkStringPools; }
MemSys::PoolPair& PointPools() { return g_parkPointPools; }

// Whether the pair's current pool was made by it (and so is deleted when it's taken away): a byte per pool at +4.
uint8_t& PoolOwned(MemSys::PoolPair& pair) { return reinterpret_cast<uint8_t*>(&pair.unknown4)[pair.current]; }

// A pool deleted: its block (0x005BA4E0, ~Pool), then the pool.
void DeletePool(MemSys::Pool* pool)
{
    MemSys::Free(pool->m_start);
    MemSys::Manager::Get()->Free(pool);
}

// `pool` lent to `pair` as its current pool (not owned).
void LendPool(MemSys::PoolPair& pair, MemSys::Pool* pool)
{
    pair.pools[pair.current] = pool;
    PoolOwned(pair) = 0;
}
// The current pool taken away again (deleted if the pair made it).
void TakeBackPool(MemSys::PoolPair& pair)
{
    if (MemSys::Pool* pool = pair.pools[pair.current]) {
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
        ++m_vertexTotal;
        mp_points = point;
        m_tailNode = point;
        return;
    }
    m_tailNode->m_following = point;
    ++m_vertexTotal;
    m_tailNode = point;
}

// 0x0058B040
int CParkRailString::CountSegments() const
{
    if (m_flags & kLoop)
        return m_vertexTotal;
    int points = 0;
    for (const CParkRailPoint* point = mp_points; point; point = point->m_following)
        ++points;
    return points > 1 ? points - 1 : 0;
}

// 0x0058B220
void __cdecl CParkRailString::Free(void* item) { StringPools().Free(item); }

// 0x0058B420
void CParkRailString::CopyFrom(const CParkRailString* source, const Maths::Vector* offset, int rot, uint32_t cluster,
                               float angle)
{
    const bool climbing = source->m_class == kClimbingNode;
    for (const CParkRailPoint* from = source->mp_points; from; from = from->m_following) {
        CParkRailPoint* point = mp_set->NewPoint(); // (not null-checked: as the original)
        std::memcpy(point, from, offsetof(CParkRailPoint, m_following));
        // Turned about y: what's added to the offset's x and z.
        float x, z;
        switch (rot) {
        case 1:
            x = from->m_pos.z;
            z = -from->m_pos.x;
            if (climbing)
                point->m_headings.y = angle + kQuarterTurn;
            break;
        case 2:
            x = -from->m_pos.x;
            z = -from->m_pos.z;
            if (climbing)
                point->m_headings.y = angle + kHalfTurn;
            break;
        case 3:
            x = -from->m_pos.z;
            z = from->m_pos.x;
            if (climbing)
                point->m_headings.y = angle + kThreeQuarterTurn;
            break;
        default:
            x = from->m_pos.x;
            z = from->m_pos.z;
            if (climbing)
                point->m_headings.y = angle;
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
    MemSys::PoolPair& pair = PointPools();
    for (CParkRailPoint* point = mp_points; point;) {
        CParkRailPoint* next = point->m_following;
        LendPool(pair, mp_set->mp_pointPool);
        pair.Free(point);
        TakeBackPool(pair);
        point = next;
    }
    mp_points = nullptr;
    m_tailNode = nullptr;
    m_vertexTotal = 0;
}

// 0x0058AFF0
CParkRailSet* CParkRailSet::Construct()
{
    m_railGroups = nullptr;
    m_numRails = 0;
    mp_pointPool = nullptr;
    mp_railPool = nullptr;
    return this;
}

// 0x0058B010
void CParkRailSet::AddRail(CParkRailString* string)
{
    if (!m_railGroups) {
        ++m_numRails;
        m_railGroups = string;
        m_tailNode = string;
        return;
    }
    m_tailNode->m_following = string;
    ++m_numRails;
    m_tailNode = string;
}

// 0x0058B070
int CParkRailSet::RailPointTotal() const
{
    int points = 0;
    for (const CParkRailString* string = m_railGroups; string; string = string->m_following)
        points += string->m_vertexTotal;
    return points;
}

// 0x0058B090
CParkRailString* CParkRailSet::FindRail(uint32_t name, int index)
{
    int found = 0;
    for (CParkRailString* string = m_railGroups; string; string = string->m_following) {
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
    void* memory = MemSys::New(sizeof(MemSys::Pool));
    mp_pointPool = memory ? new (memory) MemSys::Pool(sizeof(CParkRailPoint), points, pointName) : nullptr;
    memory = MemSys::New(sizeof(MemSys::Pool));
    mp_railPool = memory ? new (memory) MemSys::Pool(sizeof(CParkRailString), strings, stringName) : nullptr;
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
    MemSys::PoolPair& pair = PointPools();
    LendPool(pair, mp_pointPool);
    auto* point = static_cast<CParkRailPoint*>(pair.Allocate(sizeof(CParkRailPoint)));
    if (point) {
        point->m_pos = {0.0f, 0.0f, 0.0f, 0.0f};
        point->m_following = nullptr;
    }
    TakeBackPool(pair);
    return point;
}

// 0x0058B370
CParkRailString* CParkRailSet::NewRail()
{
    MemSys::PoolPair& pair = StringPools();
    LendPool(pair, mp_railPool);
    auto* string = static_cast<CParkRailString*>(pair.Allocate(sizeof(CParkRailString)));
    if (string) {
        string->m_flags = static_cast<uint8_t>((string->m_flags & ~CParkRailString::kLoop) | CParkRailString::kClimbable);
        string->mp_points = nullptr;
        string->m_vertexTotal = 0;
        string->m_following = nullptr;
    }
    TakeBackPool(pair);
    string->mp_set = this; // (not null-checked: as the original)
    return string;
}

// 0x0058B5B0
void CParkRailSet::FreeRail(CParkRailString* string)
{
    MemSys::PoolPair& pair = StringPools();
    LendPool(pair, mp_railPool);
    if (string)
        CParkRailString::Free(string);
    TakeBackPool(pair);
}

// 0x0058B700
void CParkRailSet::Clear()
{
    MemSys::PoolPair& pair = StringPools();
    for (CParkRailString* string = m_railGroups; string;) {
        CParkRailString* next = string->m_following;
        string->DeletePoints();
        LendPool(pair, mp_railPool);
        pair.Free(string);
        TakeBackPool(pair);
        string = next;
    }
    m_railGroups = nullptr;
    m_tailNode = nullptr;
    m_numRails = 0;
}

} // namespace Ed
