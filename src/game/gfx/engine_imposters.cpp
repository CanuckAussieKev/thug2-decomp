// The engine's imposter manager: see engine_imposters.h.

#include "game/gfx/engine_imposters.h"

#include <cstdint>
#include <new>

#include "game/core/mem.h"
#include "game/gfx/primitive_states.h"
#include "game/lst/key_map.h"

namespace Engine3D {

namespace {

// The groups' slots.
template <typename R = void>
R Virtual(void* group, int slot)
{
    return reinterpret_cast<R(__thiscall*)(void*)>((*static_cast<void***>(group))[slot])(group);
}
float SpanTo(void* group) { return reinterpret_cast<float(__thiscall*)(void*)>((*static_cast<void***>(group))[5])(group); }
float& NearDistance(void* group) { return *reinterpret_cast<float*>(static_cast<uint8_t*>(group) + 4); }
int& FrameCount(void* group) { return *reinterpret_cast<int*>(static_cast<uint8_t*>(group) + 0x58); }
bool HasImposter(void* group) { return *(static_cast<uint8_t*>(group) + 0x5C) != 0; }

// The table's iteration, inlined in the original: the first / next value (null at the end).
void* NextGroup(ListKit::KeyMap* table)
{
    if (!table->StepIterator() || !table->m_iterItem)
        return nullptr;
    return table->m_iterItem->value;
}
void* FirstGroup(ListKit::KeyMap* table)
{
    table->m_iterBucket = -1;
    table->m_iterItem = nullptr;
    return NextGroup(table);
}

} // namespace

// 0x0048E3E0
CImposterManager* CImposterManager::MakeNew()
{
    void* memory = MemSys::Alloc(sizeof(ListKit::KeyMap));
    mp_groups = memory ? new (memory) ListKit::KeyMap(8) : nullptr;
    m_maxPerFrame = 1;
    return this;
}

// 0x0048E260
void CImposterManager::DeleteAll()
{
    for (void* group = FirstGroup(mp_groups); group;) {
        void* next = NextGroup(mp_groups);
        reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>((*static_cast<void***>(group))[0])(group, 1);
        group = next;
    }
    mp_groups->EmptyTable();
}

// 0x0048E2D0
void CImposterManager::TickFrame()
{
    int done = 0;
    for (void* group = FirstGroup(mp_groups); group; group = NextGroup(mp_groups)) {
        Virtual(group, 6);
        float distance = SpanTo(group);
        if (distance <= NearDistance(group)) { // (NaN: as beyond it)
            Virtual(group, 2);
        } else if (!HasImposter(group)) {
            if (done < m_maxPerFrame) {
                Virtual(group, 1);
                ++done;
            }
        } else if (done < m_maxPerFrame) {
            ++FrameCount(group);
            if (Virtual<bool>(group, 3))
                ++done;
        }
    }
}

// 0x0048E370
void CImposterManager::Render()
{
    SetPrimitiveStates();
    for (void* group = FirstGroup(mp_groups); group; group = NextGroup(mp_groups)) {
        if (HasImposter(group))
            Virtual(group, 4);
    }
    ResetPrimitiveStates();
}

} // namespace Nx
