#include "game/core/pool.h"

#include <cstddef>
#include <new>

#include "game/core/mem.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/shadow.h"

namespace Mem {

static_assert(offsetof(Pool, m_freeList) == 0x14 && offsetof(Pool, m_name) == 0x18 &&
                  offsetof(Pool, m_peakUsed) == 0x58,
              "layout must match the original");

// 0x005BA420
Pool::Pool(int item_size, int count, const char* name)
{
    m_itemSize = item_size;
    m_count = count;
    auto* block = static_cast<uint8_t*>(New(static_cast<unsigned int>(item_size * count)));
    m_end = block + m_count * m_itemSize;
    m_start = block;
    m_used = 0;
    m_peakUsed = 0;
    m_freeList = block;

    // Link every item to the next; the last one ends the list.
    uint8_t* item = block;
    for (int i = 0; i < m_count; ++i, item += m_itemSize)
        *reinterpret_cast<void**>(item) = i < m_count - 1 ? item + m_itemSize : nullptr;

    // The original copies with no length limit, like strcpy.
    const char* source = name ? name : "unnamed";
    char* out = m_name;
    while ((*out++ = *source++) != '\0') {
    }
}

// 0x005BA3D0
void* Pool::Allocate()
{
    void* item = m_freeList;
    if (!item)
        return nullptr;
    ++m_used;
    if (m_used > m_peakUsed)
        m_peakUsed = m_used;
    m_freeList = *static_cast<void**>(item);
    return item;
}

// 0x005BA400
void Pool::Free(void* item)
{
    --m_used;
    *static_cast<void**>(item) = m_freeList;
    m_freeList = item;
}

// 0x005BA3B0
bool Pool::Contains(const void* pointer) const
{
    auto* p = static_cast<const uint8_t*>(pointer);
    return m_start <= p && p < m_end;
}

namespace PoolManager {

static Pool*& PoolAt(int index) { return GameVariable<Pool*[4]>(0x007D1808)[index]; }

// 0x005BC3C0
void SetUp(int index, int count)
{
    if (index != 0)
        return;
    void* memory = New(sizeof(Pool));
    PoolAt(0) = memory ? new (memory) Pool(12, count, "HashItem") : nullptr;
}

// 0x005BC450
void* CreateItem(int index)
{
    if (Pool* pool = PoolAt(index))
        return pool->Allocate();
    // Only pool 0 has a known item size; the original asks for 0 bytes for the others.
    return New(index == 0 ? 12 : 0);
}

// 0x005BC3B0
Pool* GetPool(int index)
{
    return PoolAt(index);
}

// 0x005BC4A0
void FreeItem(int index, void* item)
{
    if (Pool* pool = PoolAt(index))
        pool->Free(item);
    else
        Free(item);
}

} // namespace PoolManager

void* PoolPair::Allocate(unsigned int size)
{
    if (Pool* pool = pools[current])
        return pool->Allocate();
    return New(size);
}

void PoolPair::Free(void* item)
{
    for (int which : {current, current ^ 1}) {
        Pool* pool = pools[which];
        if (pool && pool->Contains(item)) {
            pool->Free(item);
            return;
        }
    }
    Mem::Free(item);
}

} // namespace Mem

namespace {

struct PoolAdapter {
    Mem::Pool* Construct(int item_size, int count, const char* name)
    {
        return new (this) Mem::Pool(item_size, count, name);
    }
};

} // namespace

REPLACE_SHADOW(0x005BA3B0, &Mem::Pool::Contains);
REPLACE(0x005BA3D0, &Mem::Pool::Allocate);
REPLACE(0x005BA400, &Mem::Pool::Free);
REPLACE(0x005BA420, &PoolAdapter::Construct);
REPLACE(0x005BC3C0, Mem::PoolManager::SetUp);
REPLACE(0x005BC450, Mem::PoolManager::CreateItem);
REPLACE(0x005BC4A0, Mem::PoolManager::FreeItem);
REPLACE(0x005BC3B0, Mem::PoolManager::GetPool);
