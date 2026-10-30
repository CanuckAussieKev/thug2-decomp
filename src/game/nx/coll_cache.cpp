// Collision caches.

#include "game/nx/coll_cache.h"
#include "game/nx/coll_grid.h"

#include <new>

#include "game/core/alloc.h"
#include "game/gfx/engine_scene_list.h"
#include "game/nx/coll_world.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

namespace {

GAME_GLOBAL(CCollCache*[8], g_caches, 0x006918EC);

CCollCache** Caches() { return g_caches; }

GAME_GLOBAL(uint32_t, g_cachesInUse, 0x0069190C);

uint32_t& CachesInUse() { return g_cachesInUse; }

// The movable collision objects (the first node of their list: 0x00691984).
Mem::ListNode* FirstMovable() { return g_movables.next; }

// The collision grid lists the line from the box's min to its max passes through (null-terminated; null: none),
// and one list's collision objects near the line (null-terminated). 0x0048C980 / 0x004188C0.
void** GridListsAlong(Mth::Line* line) { return reinterpret_cast<void**>(GetCollGridsOnLine(line)); }
CCollObj** ObjectsAlong(void* list, Mth::Line* line)
{
    return static_cast<CCollGrid*>(list)->ObjectsAlongLine(line, nullptr);
}

// The collision objects' virtual methods the cache uses.
bool TouchesBox(CCollObj* object, const Mth::BBox* box)
{
    return reinterpret_cast<bool(__thiscall*)(CCollObj*, const Mth::BBox*)>((*reinterpret_cast<void***>(object))[13])(
        object, box);
}
void* Geometry(CCollObj* object, int slot)
{
    return reinterpret_cast<void*(__thiscall*)(CCollObj*)>((*reinterpret_cast<void***>(object))[slot])(object);
}
uint32_t ObjectFlags(CCollObj* object) { return reinterpret_cast<uint32_t*>(object)[2]; }

void Clear(CCollCache* cache)
{
    cache->m_box.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    cache->m_box.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    cache->m_count = 0;
    cache->m_statics = 0;
    cache->m_movables = 0;
}

} // namespace

// 0x0040AB50
CCollCache* CCollCache::Construct()
{
    new (&m_box) Mth::BBox();
    RegisterCollCache(this);
    Clear(this);
    return this;
}

// 0x0040AB40
void CCollCache::Destruct()
{
    UnregisterCollCache(this);
}

// 0x0040ABA0
void CCollCache::Update(const Mth::BBox& box)
{
    Clear(this);
    Mth::Line line(box.min, box.max);
    void** lists = GridListsAlong(&line);
    if (!lists)
        return;
    m_box = box;
    // The box the objects are tested against: grown by half a unit each way.
    Mth::BBox grown;
    grown.AddPoint({box.max.x + 0.5f, box.max.y + 0.5f, box.max.z + 0.5f, box.max.w});
    grown.AddPoint({box.min.x - 0.5f, box.min.y - 0.5f, box.min.z - 0.5f, box.min.w});
    for (; *lists; ++lists) {
        for (CCollObj** object = ObjectsAlong(*lists, &line); *object; ++object) {
            if (!TouchesBox(*object, &grown))
                continue;
            m_entries[m_count].geometry = Geometry(*object, 21);
            m_entries[m_count].object = *object;
            ++m_count;
            ++m_statics;
        }
    }
    for (Mem::ListNode* node = FirstMovable(); node->data != reinterpret_cast<void*>(-1) && node; node = node->next) {
        auto* object = static_cast<CCollObj*>(node->data);
        if (!object || (ObjectFlags(object) & 6) || !TouchesBox(object, &grown))
            continue;
        m_entries[m_count].geometry = Geometry(object, 20);
        m_entries[m_count].object = object;
        ++m_count;
        ++m_movables;
    }
}

// 0x0040A910
void CCollCache::RemoveStatic(CCollObj* object)
{
    for (int i = 0; i < m_statics; ++i) {
        if (m_entries[i].object != object)
            continue;
        for (int k = i + 1; k < m_count; ++k)
            m_entries[k - 1] = m_entries[k];
        --m_statics;
        --m_count;
        return;
    }
}

// 0x0040A990
void CCollCache::RemoveMovable(CCollObj* object)
{
    for (int i = m_statics; i < m_count; ++i) {
        if (m_entries[i].object != object)
            continue;
        --m_count;
        m_entries[i] = m_entries[m_count];
        --m_movables;
        return;
    }
}

// 0x0040AA00
void RegisterCollCache(CCollCache* cache)
{
    for (int i = 0; i < 8; ++i) {
        if (!(CachesInUse() & (1u << i))) {
            Caches()[i] = cache;
            CachesInUse() |= 1u << i;
            return;
        }
    }
}

// 0x0040AA40
void UnregisterCollCache(CCollCache* cache)
{
    for (int i = 0; i < 8; ++i) {
        if ((CachesInUse() & (1u << i)) && Caches()[i] == cache) {
            CachesInUse() &= ~(1u << i);
            return;
        }
    }
}

// 0x0040AA80
void RemoveMovableFromCaches(CCollObj* object)
{
    for (int i = 0; i < 8; ++i) {
        if (CachesInUse() & (1u << i))
            Caches()[i]->RemoveMovable(object);
    }
}

// 0x0040AAC0
void RemoveFromCaches(CCollObj* object)
{
    for (int i = 0; i < 8; ++i) {
        if (CachesInUse() & (1u << i)) {
            Caches()[i]->RemoveStatic(object);
            Caches()[i]->RemoveMovable(object);
        }
    }
}

// 0x0040ADD0
int ShapeTypeFromChecksum(uint32_t shape)
{
    switch (shape) {
    case 0xDD42AA66:
    case 0x2BA71070:
    case 0xE460ABDE: // box
        return 1;
    case 0x6AADF154: // sector
        return 4;
    default:
        return 0;
    }
}

} // namespace Nx

REPLACE(0x0040AB50, &Nx::CCollCache::Construct);
REPLACE(0x0040AB40, &Nx::CCollCache::Destruct);
REPLACE(0x0040ABA0, &Nx::CCollCache::Update);
REPLACE(0x0040A910, &Nx::CCollCache::RemoveStatic);
REPLACE(0x0040A990, &Nx::CCollCache::RemoveMovable);
REPLACE(0x0040AA00, Nx::RegisterCollCache);
REPLACE(0x0040AA40, Nx::UnregisterCollCache);
REPLACE(0x0040AA80, Nx::RemoveMovableFromCaches);
REPLACE(0x0040AAC0, Nx::RemoveFromCaches);
REPLACE(0x0040ADD0, Nx::ShapeTypeFromChecksum);
