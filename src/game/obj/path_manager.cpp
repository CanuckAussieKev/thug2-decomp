// The path manager and its object groups (0x004694A0-0x00469AFE). See path_manager.h.

#include "game/obj/path_manager.h"

#include <cstdint>
#include <new>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/object.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"

namespace Obj {

CPathManager* g_pathManager;

namespace {

constexpr uint32_t kPathNum = 0x9C91C3CA;   // PathNum
constexpr uint32_t kClass = 0x12B4E660;     // class
constexpr uint32_t kType = 0x7321A8D6;      // type

CPathManager*& InstancePointer() { return g_pathManager; }

// An array new[] of `count` elements of `size` bytes from the current heap: the count in the word before them.
void* NewArray(int count, unsigned int size)
{
    auto* block = static_cast<int*>(Mem::New(static_cast<unsigned int>(count) * size + 4));
    if (!block)
        return nullptr;
    block[0] = count;
    return block + 1;
}
void DeleteArray(void* elements) { Mem::Manager::Get()->Free(static_cast<int*>(elements) - 1); }
int ArrayCount(const void* elements) { return static_cast<const int*>(elements)[-1]; }

// A slot let go (and cleared again, as the original).
void ReleaseSlot(CSmtPtr<CObject>* slot)
{
    slot->Release();
    slot->mp_object = nullptr;
}

} // namespace

// 0x004694A0
void CObjectGroup::Destruct()
{
    if (!m_slots)
        return;
    for (int i = ArrayCount(m_slots) - 1; i >= 0; --i)
        m_slots[i].Release();
    DeleteArray(m_slots);
    m_slots = nullptr;
}

// 0x004698C0
void CObjectGroup::Init(int size)
{
    auto* slots = static_cast<CSmtPtr<CObject>*>(NewArray(size, sizeof(CSmtPtr<CObject>)));
    if (slots) {
        for (int i = 0; i < size; ++i)
            new (&slots[i]) CSmtPtr<CObject>(); // (0x0042B580: all three words cleared)
    }
    m_slots = slots;
    m_size = size;
    ReleaseAll();
}

// 0x00469540
void CObjectGroup::ReleaseAll()
{
    for (int i = 0; i < m_size; ++i)
        ReleaseSlot(&m_slots[i]);
}

// 0x00469580
void CObjectGroup::Remove(CObject* object)
{
    for (int i = 0; i < m_size; ++i) {
        if (m_slots[i].mp_object == object)
            ReleaseSlot(&m_slots[i]);
    }
}

// 0x004695D0
void CObjectGroup::Add(CObject* object)
{
    for (int i = 0; i < m_size; ++i) {
        if (!m_slots[i].mp_object) {
            m_slots[i].Set(object);
            return;
        }
    }
}

// 0x004694E0
CPathManager* CPathManager::Instance()
{
    if (!InstancePointer()) {
        auto* manager = static_cast<CPathManager*>(Mem::New(sizeof(CPathManager)));
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
CObjectGroup* CPathManager::AddToPath(CObject* object, uint32_t node)
{
    Script::CStruct* params = SkateScript::GetNode(node, true);
    int path = 0;
    params->GetInteger(kPathNum, &path, 0);
    m_groups[path].Add(object);
    return &m_groups[path];
}

// 0x00469670
CObjectGroup* CPathManager::AddToExtraGroup(CObject* object, uint32_t node)
{
    Script::CStruct* params = SkateScript::GetNode(node, true);
    int path = 0;
    params->GetInteger(kPathNum, &path, 0); // (not used)
    m_extra->Add(object);
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
    SkateScript::StartNodeWalk(0);
    for (Script::CStruct* start = SkateScript::NextNode(); start; start = SkateScript::NextNode()) {
        uint32_t nodeClass = 0;
        start->GetChecksum(kClass, &nodeClass, 0);
        if (nodeClass == 0x8E6B02AD || nodeClass == 0x30C19600)
            continue;
        uint32_t type;
        if (nodeClass == 0x4C23A77E && start->GetChecksum(kType, &type, 0) && type == 0xCBA10FFA)
            continue;
        if (SkateScript::GetLinkCount(start) == 0 && nodeClass != 0x4C23A77E)
            continue;
        if (start->ContainsComponentNamed(kPathNum))
            continue;
        // down the first links, numbering; a node numbered already gives its number to the chain, from the start
        int path = m_pathCount;
        Script::CStruct* node = start;
        for (;;) {
            int existing;
            if (node->GetInteger(kPathNum, &existing, 0)) {
                if (existing == path)
                    break;
                path = existing;
                node = start;
            }
            node->AddInteger(kPathNum, path);
            if (SkateScript::GetLinkCount(node) == 0)
                break;
            node = SkateScript::GetLinkedNode(node, 0);
        }
        if (path == m_pathCount)
            ++m_pathCount;
    }
    SkateScript::EndNodeWalk();
}

// 0x00469820
void CPathManager::Free()
{
    if (!m_groups)
        return;
    for (int i = 0; i < m_groupCount; ++i)
        m_groups[i].ReleaseAll();
    if (CObjectGroup* groups = m_groups) {
        for (int i = ArrayCount(groups) - 1; i >= 0; --i)
            groups[i].Destruct();
        DeleteArray(groups);
    }
    CObjectGroup* extra = m_extra;
    m_groups = nullptr;
    if (extra) {
        if (extra->m_slots) {
            for (int i = ArrayCount(extra->m_slots) - 1; i >= 0; --i)
                extra->m_slots[i].Release();
            DeleteArray(extra->m_slots);
            extra->m_slots = nullptr;
        }
        Mem::Manager::Get()->Free(extra);
    }
    m_extra = nullptr;
}

// 0x00469960
void CPathManager::Allocate(int groups, int groupSize, int extraSize)
{
    m_groupCount = groups;
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_bottomUpHeap);
    auto* made = static_cast<CObjectGroup*>(NewArray(m_groupCount, sizeof(CObjectGroup)));
    if (made) {
        for (int i = 0; i < m_groupCount; ++i) {
            made[i].m_slots = nullptr; // (0x00414D70)
            made[i].m_size = 0;
        }
    }
    m_groups = made;
    for (int i = 0; i < m_groupCount; ++i)
        m_groups[i].Init(groupSize);
    auto* extra = static_cast<CObjectGroup*>(Mem::New(sizeof(CObjectGroup)));
    if (extra) {
        extra->m_slots = nullptr;
        extra->m_size = 0;
    }
    m_extra = extra;
    extra->Init(extraSize);
    Mem::Manager::Get()->PopContext();
}

// 0x00469A80
bool CFunc_AllocatePathManMemory(Script::CStruct* params, Script::CScript* /*script*/)
{
    CPathManager* manager = CPathManager::Instance();
    int groups = 200;
    params->GetInteger(0x0C230B04, &groups, 0);
    int groupSize = 20;
    params->GetInteger(0xF6A4081D, &groupSize, 0);
    int extraSize = 200;
    params->GetInteger(0x13AFFFF9, &extraSize, 0);
    manager->Allocate(groups, groupSize, extraSize);
    return true;
}

} // namespace Obj
