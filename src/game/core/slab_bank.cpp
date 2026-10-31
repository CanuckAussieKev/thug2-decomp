#include "game/core/slab_bank.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>

#include "game/core/mem.h"
#include "game/sys/run_log.h"

namespace MemSys {

static_assert(offsetof(SlabBank, m_freeList) == 0x14 && offsetof(SlabBank, m_label) == 0x18 &&
                  offsetof(SlabBank, m_peakUsed) == 0x58,
              "layout must match the original");

// 0x005BA420
SlabBank::SlabBank(int item_size, int count, const char* name)
{
    m_slotBytes = item_size;
    m_num = count;
    auto* block = static_cast<uint8_t*>(Alloc(static_cast<unsigned int>(item_size * count)));
    m_finish = block + m_num * m_slotBytes;
    m_outset = block;
    m_takenCount = 0;
    m_peakUsed = 0;
    m_freeList = block;

    // Link every item to the next; the last one ends the list.
    uint8_t* item = block;
    for (int i = 0; i < m_num; ++i, item += m_slotBytes)
        *reinterpret_cast<void**>(item) = i < m_num - 1 ? item + m_slotBytes : nullptr;

    // The original copies with no length limit, like strcpy.
    const char* source = name ? name : "unnamed";
    char* out = m_label;
    while ((*out++ = *source++) != '\0') {
    }
}

// 0x005BA3D0
void* SlabBank::Carve()
{
    void* item = m_freeList;
    if (!item) {
        // Not original - the log names a pool that ran dry, once per pool (most callers don't check for null).
        static const SlabBank* reported[16];
        static int reportedCount;
        bool seen = false;
        for (int i = 0; i < reportedCount; ++i)
            seen = seen || reported[i] == this;
        // (a pool without a name is a self-test's: the game names every pool, "unnamed" for none)
        if (!seen && reportedCount < 16 && m_label[0] != '\0') {
            reported[reportedCount++] = this;
            Log("pool %s full: all %d items (%d bytes each) in use", m_label, m_num, m_slotBytes);
        }
        return nullptr;
    }
    ++m_takenCount;
    if (m_takenCount > m_peakUsed) {
        m_peakUsed = m_takenCount;
        // Not original either - a pool reaching 80 % is logged once per pool (which pools a big level comes near:
        // the script pools' sizes are ours, game/core/memory_sizes.h).
        if (static_cast<int64_t>(m_takenCount) * 5 == static_cast<int64_t>(m_num) * 4 && m_num >= 50 &&
            m_label[0] != '\0') {
            static const SlabBank* nearlyFull[32];
            static int nearlyFullCount;
            bool seen = false;
            for (int i = 0; i < nearlyFullCount; ++i)
                seen = seen || nearlyFull[i] == this;
            if (!seen && nearlyFullCount < 32) {
                nearlyFull[nearlyFullCount++] = this;
                Log("pool %s 80%% full: %d of %d items in use", m_label, m_takenCount, m_num);
            }
        }
    }
    m_freeList = *static_cast<void**>(item);
    return item;
}

// 0x005BA400
void SlabBank::Dealloc(void* item)
{
    --m_takenCount;
    *static_cast<void**>(item) = m_freeList;
    m_freeList = item;
}

// 0x005BA3B0
bool SlabBank::Holds(const void* pointer) const
{
    auto* p = static_cast<const uint8_t*>(pointer);
    return m_outset <= p && p < m_finish;
}

namespace SlotPoolSetup {

SlabBank* g_pools[2]; // (the memory card's path buffer follows)

static SlabBank*& PoolAt(int index) { return g_pools[index]; }

// 0x005BC3C0
void SetUp(int index, int count)
{
    if (index != 0)
        return;
    void* memory = Alloc(sizeof(SlabBank));
    PoolAt(0) = memory ? new (memory) SlabBank(12, count, "HashItem") : nullptr;
}

// 0x005BC450
void* CreateItem(int index)
{
    if (SlabBank* pool = PoolAt(index))
        return pool->Carve();
    // Only pool 0 has a known item size; the original asks for 0 bytes for the others.
    return Alloc(index == 0 ? 12 : 0);
}

// 0x005BC3B0
SlabBank* GetPool(int index)
{
    return PoolAt(index);
}

// 0x005BC4A0
void ReturnEntry(int index, void* item)
{
    if (SlabBank* pool = PoolAt(index))
        pool->Dealloc(item);
    else
        Dealloc(item);
}

} // namespace SlotPoolSetup

void* PoolPair::Carve(unsigned int size)
{
    if (SlabBank* pool = pools[current])
        return pool->Carve();
    return Alloc(size);
}

void PoolPair::Dealloc(void* item)
{
    for (int which : {current, current ^ 1}) {
        SlabBank* pool = pools[which];
        if (pool && pool->Holds(item)) {
            pool->Dealloc(item);
            return;
        }
    }
    MemSys::Dealloc(item);
}

} // namespace Mem

namespace {

struct PoolAdapter {
    MemSys::SlabBank* MakeNew(int item_size, int count, const char* name)
    {
        return new (this) MemSys::SlabBank(item_size, count, name);
    }
};

} // namespace
