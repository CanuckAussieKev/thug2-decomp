// The park's rail sets, strings and points (0x0058AFC0-0x0058B7D5). See park_rails.h.

#include "game/park_editing/park_rails.h"

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
void DeletePool(MemSys::SlabBank* pool)
{
    MemSys::Dealloc(pool->m_outset);
    MemSys::Hub::Pull()->Dealloc(pool);
}

// `pool` lent to `pair` as its current pool (not owned).
void LendPool(MemSys::PoolPair& pair, MemSys::SlabBank* pool)
{
    pair.pools[pair.current] = pool;
    PoolOwned(pair) = 0;
}
// The current pool taken away again (deleted if the pair made it).
void TakeBackPool(MemSys::PoolPair& pair)
{
    if (MemSys::SlabBank* pool = pair.pools[pair.current]) {
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
void CParkRailString::AppendSpot(CParkRailPoint* point)
{
    if (!m_pointChain) {
        ++m_vertexTotal;
        m_pointChain = point;
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
    if (m_flagBits & kLoop)
        return m_vertexTotal;
    int points = 0;
    for (const CParkRailPoint* point = m_pointChain; point; point = point->m_following)
        ++points;
    return points > 1 ? points - 1 : 0;
}

// 0x0058B220
void __cdecl CParkRailString::Dealloc(void* item) { StringPools().Dealloc(item); }

// 0x0058B420
void CParkRailString::CopyFrom(const CParkRailString* source, const Maths::Vec4* offset, int rot, uint32_t cluster,
                               float angle)
{
    const bool climbing = source->m_class == kClimbingNode;
    for (const CParkRailPoint* from = source->m_pointChain; from; from = from->m_following) {
        CParkRailPoint* point = m_pointGroup->AppendRailVertex(); // (not null-checked: as the original)
        std::memcpy(point, from, offsetof(CParkRailPoint, m_following));
        // Turned about y: what's added to the offset's x and z.
        float x, z;
        switch (rot) {
        case 1:
            x = from->m_atPoint.z;
            z = -from->m_atPoint.x;
            if (climbing)
                point->m_headings.y = angle + kQuarterTurn;
            break;
        case 2:
            x = -from->m_atPoint.x;
            z = -from->m_atPoint.z;
            if (climbing)
                point->m_headings.y = angle + kHalfTurn;
            break;
        case 3:
            x = -from->m_atPoint.z;
            z = from->m_atPoint.x;
            if (climbing)
                point->m_headings.y = angle + kThreeQuarterTurn;
            break;
        default:
            x = from->m_atPoint.x;
            z = from->m_atPoint.z;
            if (climbing)
                point->m_headings.y = angle;
            break;
        }
        point->m_atPoint.w = 1.0f;
        point->m_atPoint.x = x + offset->x;
        point->m_atPoint.y = offset->y + from->m_atPoint.y;
        point->m_atPoint.z = offset->z + z;
        point->m_cluster = cluster;
        AppendSpot(point);
    }
    m_class = source->m_class;
    m_label = source->m_label;
    m_flagBits = static_cast<uint8_t>((m_flagBits & ~(kLoop | kClimbable)) | (source->m_flagBits & (kLoop | kClimbable)));
}

// 0x0058B620
void CParkRailString::DeletePoints()
{
    MemSys::PoolPair& pair = PointPools();
    for (CParkRailPoint* point = m_pointChain; point;) {
        CParkRailPoint* next = point->m_following;
        LendPool(pair, m_pointGroup->m_pointPool);
        pair.Dealloc(point);
        TakeBackPool(pair);
        point = next;
    }
    m_pointChain = nullptr;
    m_tailNode = nullptr;
    m_vertexTotal = 0;
}

// 0x0058AFF0
CParkRailSet* CParkRailSet::MakeNew()
{
    m_railGroups = nullptr;
    m_railTotal = 0;
    m_pointPool = nullptr;
    m_railPool = nullptr;
    return this;
}

// 0x0058B010
void CParkRailSet::AddRail(CParkRailString* string)
{
    if (!m_railGroups) {
        ++m_railTotal;
        m_railGroups = string;
        m_tailNode = string;
        return;
    }
    m_tailNode->m_following = string;
    ++m_railTotal;
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
        if (string->m_label == name) {
            if (found == index)
                return string;
            ++found;
        }
    }
    return nullptr;
}

// 0x0058B0C0
void CParkRailSet::PrepareAll(int points, int strings, bool in)
{
    if (in)
        std::memcpy(m_label, "In set", sizeof("In set"));
    else
        std::memcpy(m_label, "Out set", sizeof("Out set"));
    char pointName[64], stringName[64];
    std::snprintf(pointName, sizeof(pointName), "%s RailPoint", m_label);
    std::snprintf(stringName, sizeof(stringName), "%s RailString", m_label);
    void* memory = MemSys::Alloc(sizeof(MemSys::SlabBank));
    m_pointPool = memory ? new (memory) MemSys::SlabBank(sizeof(CParkRailPoint), points, pointName) : nullptr;
    memory = MemSys::Alloc(sizeof(MemSys::SlabBank));
    m_railPool = memory ? new (memory) MemSys::SlabBank(sizeof(CParkRailString), strings, stringName) : nullptr;
}

// 0x0058B280
void CParkRailSet::FreePools()
{
    if (m_railPool) {
        DeletePool(m_railPool);
        m_railPool = nullptr;
    }
    if (m_pointPool) {
        DeletePool(m_pointPool);
        m_pointPool = nullptr;
    }
}

// 0x0058B2D0
CParkRailPoint* CParkRailSet::AppendRailVertex()
{
    MemSys::PoolPair& pair = PointPools();
    LendPool(pair, m_pointPool);
    auto* point = static_cast<CParkRailPoint*>(pair.Carve(sizeof(CParkRailPoint)));
    if (point) {
        point->m_atPoint = {0.0f, 0.0f, 0.0f, 0.0f};
        point->m_following = nullptr;
    }
    TakeBackPool(pair);
    return point;
}

// 0x0058B370
CParkRailString* CParkRailSet::NewRail()
{
    MemSys::PoolPair& pair = StringPools();
    LendPool(pair, m_railPool);
    auto* string = static_cast<CParkRailString*>(pair.Carve(sizeof(CParkRailString)));
    if (string) {
        string->m_flagBits = static_cast<uint8_t>((string->m_flagBits & ~CParkRailString::kLoop) | CParkRailString::kClimbable);
        string->m_pointChain = nullptr;
        string->m_vertexTotal = 0;
        string->m_following = nullptr;
    }
    TakeBackPool(pair);
    string->m_pointGroup = this; // (not null-checked: as the original)
    return string;
}

// 0x0058B5B0
void CParkRailSet::FreeRail(CParkRailString* string)
{
    MemSys::PoolPair& pair = StringPools();
    LendPool(pair, m_railPool);
    if (string)
        CParkRailString::Dealloc(string);
    TakeBackPool(pair);
}

// 0x0058B700
void CParkRailSet::Clear()
{
    MemSys::PoolPair& pair = StringPools();
    for (CParkRailString* string = m_railGroups; string;) {
        CParkRailString* next = string->m_following;
        string->DeletePoints();
        LendPool(pair, m_railPool);
        pair.Dealloc(string);
        TakeBackPool(pair);
        string = next;
    }
    m_railGroups = nullptr;
    m_tailNode = nullptr;
    m_railTotal = 0;
}

} // namespace Ed
