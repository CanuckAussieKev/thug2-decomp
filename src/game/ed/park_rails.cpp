// The park's rail sets, strings and points (0x0058AFC0-0x0058B7D5). See park_rails.h.

#include "game/ed/park_rails.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

// The classes' pool pairs (CPoolable): a set's pool is lent to them while one of its items is made or freed.
Mem::PoolPair& StringPools() { return GameVariable<Mem::PoolPair>(0x007CE594); }
Mem::PoolPair& PointPools() { return GameVariable<Mem::PoolPair>(0x007CE5A4); }

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
    if (!mp_first) {
        ++m_count;
        mp_first = point;
        mp_last = point;
        return;
    }
    mp_last->mp_next = point;
    ++m_count;
    mp_last = point;
}

// 0x0058B040
int CParkRailString::GetSegmentCount() const
{
    if (m_flags & kLoop)
        return m_count;
    int points = 0;
    for (const CParkRailPoint* point = mp_first; point; point = point->mp_next)
        ++points;
    return points > 1 ? points - 1 : 0;
}

// 0x0058B220
void __cdecl CParkRailString::Free(void* item) { StringPools().Free(item); }

// 0x0058B420
void CParkRailString::CopyFrom(const CParkRailString* source, const Mth::Vector* offset, int rot, uint32_t owner,
                               float angle)
{
    const bool climbing = source->m_type == kClimbingNode;
    for (const CParkRailPoint* from = source->mp_first; from; from = from->mp_next) {
        CParkRailPoint* point = mp_set->NewPoint(); // (not null-checked: as the original)
        std::memcpy(point, from, offsetof(CParkRailPoint, mp_next));
        // Turned about y: what's added to the offset's x and z.
        float x, z;
        switch (rot) {
        case 1:
            x = from->m_pos.z;
            z = -from->m_pos.x;
            if (climbing)
                point->m_angle = angle + kQuarterTurn;
            break;
        case 2:
            x = -from->m_pos.x;
            z = -from->m_pos.z;
            if (climbing)
                point->m_angle = angle + kHalfTurn;
            break;
        case 3:
            x = -from->m_pos.z;
            z = from->m_pos.x;
            if (climbing)
                point->m_angle = angle + kThreeQuarterTurn;
            break;
        default:
            x = from->m_pos.x;
            z = from->m_pos.z;
            if (climbing)
                point->m_angle = angle;
            break;
        }
        point->m_pos.w = 1.0f;
        point->m_pos.x = x + offset->x;
        point->m_pos.y = offset->y + from->m_pos.y;
        point->m_pos.z = offset->z + z;
        point->m_owner = owner;
        AddPoint(point);
    }
    m_type = source->m_type;
    m_id = source->m_id;
    m_flags = static_cast<uint8_t>((m_flags & ~(kLoop | kFlag2)) | (source->m_flags & (kLoop | kFlag2)));
}

// 0x0058B620
void CParkRailString::DeletePoints()
{
    Mem::PoolPair& pair = PointPools();
    for (CParkRailPoint* point = mp_first; point;) {
        CParkRailPoint* next = point->mp_next;
        LendPool(pair, mp_set->mp_pointPool);
        pair.Free(point);
        TakeBackPool(pair);
        point = next;
    }
    mp_first = nullptr;
    mp_last = nullptr;
    m_count = 0;
}

// 0x0058AFF0
CParkRailSet* CParkRailSet::Construct()
{
    mp_first = nullptr;
    m_count = 0;
    mp_pointPool = nullptr;
    mp_stringPool = nullptr;
    return this;
}

// 0x0058B010
void CParkRailSet::AddString(CParkRailString* string)
{
    if (!mp_first) {
        ++m_count;
        mp_first = string;
        mp_last = string;
        return;
    }
    mp_last->mp_next = string;
    ++m_count;
    mp_last = string;
}

// 0x0058B070
int CParkRailSet::CountPoints() const
{
    int points = 0;
    for (const CParkRailString* string = mp_first; string; string = string->mp_next)
        points += string->m_count;
    return points;
}

// 0x0058B090
CParkRailString* CParkRailSet::FindString(uint32_t id, int index)
{
    int found = 0;
    for (CParkRailString* string = mp_first; string; string = string->mp_next) {
        if (string->m_id == id) {
            if (found == index)
                return string;
            ++found;
        }
    }
    return nullptr;
}

// 0x0058B0C0
void CParkRailSet::CreatePools(int points, int strings, bool in)
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
    mp_stringPool = memory ? new (memory) Mem::Pool(sizeof(CParkRailString), strings, stringName) : nullptr;
}

// 0x0058B280
void CParkRailSet::DestroyPools()
{
    if (mp_stringPool) {
        DeletePool(mp_stringPool);
        mp_stringPool = nullptr;
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
CParkRailString* CParkRailSet::NewString()
{
    Mem::PoolPair& pair = StringPools();
    LendPool(pair, mp_stringPool);
    auto* string = static_cast<CParkRailString*>(pair.Allocate(sizeof(CParkRailString)));
    if (string) {
        string->m_flags = static_cast<uint8_t>((string->m_flags & ~CParkRailString::kLoop) | CParkRailString::kFlag2);
        string->mp_first = nullptr;
        string->m_count = 0;
        string->mp_next = nullptr;
    }
    TakeBackPool(pair);
    string->mp_set = this; // (not null-checked: as the original)
    return string;
}

// 0x0058B5B0
void CParkRailSet::DeleteString(CParkRailString* string)
{
    Mem::PoolPair& pair = StringPools();
    LendPool(pair, mp_stringPool);
    if (string)
        CParkRailString::Free(string);
    TakeBackPool(pair);
}

// 0x0058B700
void CParkRailSet::DeleteStrings()
{
    Mem::PoolPair& pair = StringPools();
    for (CParkRailString* string = mp_first; string;) {
        CParkRailString* next = string->mp_next;
        string->DeletePoints();
        LendPool(pair, mp_stringPool);
        pair.Free(string);
        TakeBackPool(pair);
        string = next;
    }
    mp_first = nullptr;
    mp_last = nullptr;
    m_count = 0;
}

} // namespace Ed

REPLACE(0x0058AFC0, &Ed::CParkRailString::AddPoint);
REPLACE(0x0058AFF0, &Ed::CParkRailSet::Construct);
REPLACE(0x0058B010, &Ed::CParkRailSet::AddString);
REPLACE(0x0058B040, &Ed::CParkRailString::GetSegmentCount);
REPLACE(0x0058B070, &Ed::CParkRailSet::CountPoints);
REPLACE(0x0058B090, &Ed::CParkRailSet::FindString);
REPLACE(0x0058B0C0, &Ed::CParkRailSet::CreatePools);
REPLACE(0x0058B220, Ed::CParkRailString::Free);
REPLACE(0x0058B280, &Ed::CParkRailSet::DestroyPools);
REPLACE(0x0058B2D0, &Ed::CParkRailSet::NewPoint);
REPLACE(0x0058B370, &Ed::CParkRailSet::NewString);
REPLACE(0x0058B420, &Ed::CParkRailString::CopyFrom);
REPLACE(0x0058B5B0, &Ed::CParkRailSet::DeleteString);
REPLACE(0x0058B620, &Ed::CParkRailString::DeletePoints);
REPLACE(0x0058B700, &Ed::CParkRailSet::DeleteStrings);
