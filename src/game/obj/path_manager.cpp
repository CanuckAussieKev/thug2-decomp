// The path manager and its object groups (0x004694A0-0x00469AFE). See path_manager.h.

#include "game/obj/path_manager.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/entity.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"

namespace GameObjects {

CPathManager* g_pathManager;

namespace {

constexpr uint32_t kPathNum = 0x9C91C3CA;   // PathNum
constexpr uint32_t kClass = 0x12B4E660;     // class
constexpr uint32_t kType = 0x7321A8D6;      // type

CPathManager*& InstancePointer() { return g_pathManager; }

// An array new[] of `count` elements of `size` bytes from the current heap: the count in the word before them.
void* AllocScriptArray(int count, unsigned int size)
{
    auto* block = static_cast<int*>(MemSys::Alloc(static_cast<unsigned int>(count) * size + 4));
    if (!block)
        return nullptr;
    block[0] = count;
    return block + 1;
}
void DeleteArray(void* elements) { MemSys::Hub::Pull()->Dealloc(static_cast<int*>(elements) - 1); }
int ElementTotal(const void* elements) { return static_cast<const int*>(elements)[-1]; }

// A slot let go (and cleared again, as the original).
void ReleaseSlot(CSharedRef<CEntity>* slot)
{
    slot->Release();
    slot->m_ownerObject = nullptr;
}

} // namespace

// 0x004694A0
void CObjectGroup::Destruct()
{
    if (!m_slots)
        return;
    for (int i = ElementTotal(m_slots) - 1; i >= 0; --i)
        m_slots[i].Release();
    DeleteArray(m_slots);
    m_slots = nullptr;
}

// 0x004698C0
void CObjectGroup::PrepareAll(int size)
{
    auto* slots = static_cast<CSharedRef<CEntity>*>(AllocScriptArray(size, sizeof(CSharedRef<CEntity>)));
    if (slots) {
        for (int i = 0; i < size; ++i)
            new (&slots[i]) CSharedRef<CEntity>(); // (0x0042B580: all three words cleared)
    }
    m_slots = slots;
    m_extent = size;
    ReleaseAll();
}

// 0x00469540
void CObjectGroup::ReleaseAll()
{
    for (int i = 0; i < m_extent; ++i)
        ReleaseSlot(&m_slots[i]);
}

// 0x00469580
void CObjectGroup::DropOut(CEntity* object)
{
    for (int i = 0; i < m_extent; ++i) {
        if (m_slots[i].m_ownerObject == object)
            ReleaseSlot(&m_slots[i]);
    }
}

// 0x004695D0
void CObjectGroup::AppendOne(CEntity* object)
{
    for (int i = 0; i < m_extent; ++i) {
        if (!m_slots[i].m_ownerObject) {
            m_slots[i].AssignValue(object);
            return;
        }
    }
}

// 0x004694E0
CPathManager* CPathManager::Sole()
{
    if (!InstancePointer()) {
        auto* manager = static_cast<CPathManager*>(MemSys::Alloc(sizeof(CPathManager)));
        if (manager) {
            manager->m_pathCount = 0;
            manager->m_groupCount = 0;
            manager->m_groups = nullptr;
            manager->m_extra = nullptr;
        }
        InstancePointer() = manager;
    }
    return InstancePointer();
}

// 0x00469620
CObjectGroup* CPathManager::AddToPath(CEntity* object, uint32_t node)
{
    Scripts::CStruct* params = LevelNodes::FetchVertex(node, true);
    int path = 0;
    params->FetchIntValue(kPathNum, &path, 0);
    m_groups[path].AppendOne(object);
    return &m_groups[path];
}

// 0x00469670
CObjectGroup* CPathManager::AddToExtraGroup(CEntity* object, uint32_t node)
{
    Scripts::CStruct* params = LevelNodes::FetchVertex(node, true);
    int path = 0;
    params->FetchIntValue(kPathNum, &path, 0); // (not used)
    m_extra->AppendOne(object);
    return m_extra;
}

// 0x004696C0
void CPathManager::NumberPaths()
{
    if (!m_groups)
        return;
    for (int i = 0; i < m_groupCount; ++i)
        m_groups[i].ReleaseAll();
    m_pathCount = 0;
    LevelNodes::StartNodeWalk(0);
    for (Scripts::CStruct* start = LevelNodes::NextNode(); start; start = LevelNodes::NextNode()) {
        uint32_t nodeClass = 0;
        start->ReadHashValue(kClass, &nodeClass, 0);
        if (nodeClass == 0x8E6B02AD || nodeClass == 0x30C19600)
            continue;
        uint32_t type;
        if (nodeClass == 0x4C23A77E && start->ReadHashValue(kType, &type, 0) && type == 0xCBA10FFA)
            continue;
        if (LevelNodes::GetLinkCount(start) == 0 && nodeClass != 0x4C23A77E)
            continue;
        if (start->HasEntryNamed(kPathNum))
            continue;
        // down the first links, numbering; a node numbered already gives its number to the chain, from the start
        int path = m_pathCount;
        Scripts::CStruct* node = start;
        for (;;) {
            int existing;
            if (node->FetchIntValue(kPathNum, &existing, 0)) {
                if (existing == path)
                    break;
                path = existing;
                node = start;
            }
            node->PutInt(kPathNum, path);
            if (LevelNodes::GetLinkCount(node) == 0)
                break;
            node = LevelNodes::FetchNeighbourNode(node, 0);
        }
        if (path == m_pathCount)
            ++m_pathCount;
    }
    LevelNodes::EndNodeWalk();
}

// 0x00469820
void CPathManager::Dealloc()
{
    if (!m_groups)
        return;
    for (int i = 0; i < m_groupCount; ++i)
        m_groups[i].ReleaseAll();
    if (CObjectGroup* groups = m_groups) {
        for (int i = ElementTotal(groups) - 1; i >= 0; --i)
            groups[i].Destruct();
        DeleteArray(groups);
    }
    CObjectGroup* extra = m_extra;
    m_groups = nullptr;
    if (extra) {
        if (extra->m_slots) {
            for (int i = ElementTotal(extra->m_slots) - 1; i >= 0; --i)
                extra->m_slots[i].Release();
            DeleteArray(extra->m_slots);
            extra->m_slots = nullptr;
        }
        MemSys::Hub::Pull()->Dealloc(extra);
    }
    m_extra = nullptr;
}

// 0x00469960
void CPathManager::Carve(int groups, int groupSize, int extraSize)
{
    m_groupCount = groups;
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_bottomUpHeap);
    auto* made = static_cast<CObjectGroup*>(AllocScriptArray(m_groupCount, sizeof(CObjectGroup)));
    if (made) {
        for (int i = 0; i < m_groupCount; ++i) {
            made[i].m_slots = nullptr; // (0x00414D70)
            made[i].m_extent = 0;
        }
    }
    m_groups = made;
    for (int i = 0; i < m_groupCount; ++i)
        m_groups[i].PrepareAll(groupSize);
    auto* extra = static_cast<CObjectGroup*>(MemSys::Alloc(sizeof(CObjectGroup)));
    if (extra) {
        extra->m_slots = nullptr;
        extra->m_extent = 0;
    }
    m_extra = extra;
    extra->PrepareAll(extraSize);
    MemSys::Hub::Pull()->LeaveHeapScope();
}

// 0x00469A80
bool CFunc_AllocatePathManMemory(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    CPathManager* manager = CPathManager::Sole();
    int groups = 200;
    params->FetchIntValue(0x0C230B04, &groups, 0);
    int groupSize = 20;
    params->FetchIntValue(0xF6A4081D, &groupSize, 0);
    int extraSize = 200;
    params->FetchIntValue(0x13AFFFF9, &extraSize, 0);
    manager->Carve(groups, groupSize, extraSize);
    return true;
}

} // namespace Obj
