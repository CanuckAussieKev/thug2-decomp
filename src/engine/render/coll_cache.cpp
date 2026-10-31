// Collision caches.

#include "engine/render/coll_cache.h"
#include "engine/render/coll_grid.h"

#include <cstdint>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/render/coll_world.h"

namespace Engine3D {

namespace {

CHitMeshCache* g_caches[8];

CHitMeshCache** Caches() { return g_caches; }

uint32_t g_cachesInUse;

uint32_t& CachesInUse() { return g_cachesInUse; }

// The movable collision objects (the first node of their list: 0x00691984).
MemSys::ListNode* FirstMovable() { return g_movables.next; }

// The collision grid lists the line from the box's min to its max passes through (null-terminated; null: none),
// and one list's collision objects near the line (null-terminated). 0x0048C980 / 0x004188C0.
void** GridListsAlong(Maths::Seg* line) { return reinterpret_cast<void**>(GetCollGridsOnLine(line)); }
CHitMesh** ObjectsAlong(void* list, Maths::Seg* line)
{
    return static_cast<CCollGrid*>(list)->ObjectsAlongLine(line, nullptr);
}

// The collision objects' virtual methods the cache uses.
bool TouchesBox(CHitMesh* object, const Maths::BBox* box)
{
    return reinterpret_cast<bool(__thiscall*)(CHitMesh*, const Maths::BBox*)>((*reinterpret_cast<void***>(object))[13])(
        object, box);
}
void* Geometry(CHitMesh* object, int slot)
{
    return reinterpret_cast<void*(__thiscall*)(CHitMesh*)>((*reinterpret_cast<void***>(object))[slot])(object);
}
uint32_t ObjectFlags(CHitMesh* object) { return reinterpret_cast<uint32_t*>(object)[2]; }

void Clear(CHitMeshCache* cache)
{
    cache->m_box.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    cache->m_box.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    cache->m_num = 0;
    cache->m_statics = 0;
    cache->m_movables = 0;
}

} // namespace

// 0x0040AB50
CHitMeshCache* CHitMeshCache::MakeNew()
{
    new (&m_box) Maths::BBox();
    RegisterCollCache(this);
    Clear(this);
    return this;
}

// 0x0040AB40
void CHitMeshCache::Destruct()
{
    UnregisterCollCache(this);
}

// 0x0040ABA0
void CHitMeshCache::TickFrame(const Maths::BBox& box)
{
    Clear(this);
    Maths::Seg line(box.min, box.max);
    void** lists = GridListsAlong(&line);
    if (!lists)
        return;
    m_box = box;
    // The box the objects are tested against: grown by half a unit each way.
    Maths::BBox grown;
    grown.AppendSpot({box.max.x + 0.5f, box.max.y + 0.5f, box.max.z + 0.5f, box.max.w});
    grown.AppendSpot({box.min.x - 0.5f, box.min.y - 0.5f, box.min.z - 0.5f, box.min.w});
    for (; *lists; ++lists) {
        for (CHitMesh** object = ObjectsAlong(*lists, &line); *object; ++object) {
            if (!TouchesBox(*object, &grown))
                continue;
            m_entries[m_num].geometry = Geometry(*object, 21);
            m_entries[m_num].object = *object;
            ++m_num;
            ++m_statics;
        }
    }
    for (MemSys::ListNode* node = FirstMovable(); node->data != reinterpret_cast<void*>(-1) && node; node = node->next) {
        auto* object = static_cast<CHitMesh*>(node->data);
        if (!object || (ObjectFlags(object) & 6) || !TouchesBox(object, &grown))
            continue;
        m_entries[m_num].geometry = Geometry(object, 20);
        m_entries[m_num].object = object;
        ++m_num;
        ++m_movables;
    }
}

// 0x0040A910
void CHitMeshCache::RemoveStatic(CHitMesh* object)
{
    for (int i = 0; i < m_statics; ++i) {
        if (m_entries[i].object != object)
            continue;
        for (int k = i + 1; k < m_num; ++k)
            m_entries[k - 1] = m_entries[k];
        --m_statics;
        --m_num;
        return;
    }
}

// 0x0040A990
void CHitMeshCache::RemoveMovable(CHitMesh* object)
{
    for (int i = m_statics; i < m_num; ++i) {
        if (m_entries[i].object != object)
            continue;
        --m_num;
        m_entries[i] = m_entries[m_num];
        --m_movables;
        return;
    }
}

// 0x0040AA00
void RegisterCollCache(CHitMeshCache* cache)
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
void UnregisterCollCache(CHitMeshCache* cache)
{
    for (int i = 0; i < 8; ++i) {
        if ((CachesInUse() & (1u << i)) && Caches()[i] == cache) {
            CachesInUse() &= ~(1u << i);
            return;
        }
    }
}

// 0x0040AA80
void RemoveMovableFromCaches(CHitMesh* object)
{
    for (int i = 0; i < 8; ++i) {
        if (CachesInUse() & (1u << i))
            Caches()[i]->RemoveMovable(object);
    }
}

// 0x0040AAC0
void RemoveFromCaches(CHitMesh* object)
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
