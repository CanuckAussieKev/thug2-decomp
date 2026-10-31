// The pager message component (Obj::CPagerMessageComponent) and the game's std::list<T*>::erase: see
// pager_message.h. From the disassembly of 0x004EC960-0x004ECF9E (the member functions 0x004EC980, 0x004ECB50,
// 0x004ECBD0 and 0x004ECDF0 aren't Ghidra functions).

#include "game/obj/pager_message.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

uint32_t g_pagerMessageMemberFunctions[36]{
            0x4824484A, 0xD1515392, 0, 0, 0, 0,
            0xD65B2BB5, 0xD1515392, 0, 0, 0, 0,
            0x5B64BFEC, 0xD1515392, 0, 0, 0, 0,
            0xAF88BE7A, 0xD1515392, 0, 0, 0, 0,
            0xE7DF996D, 0xD1515392, 0, 0, 0, 0,
            0, 0xD1515392, 0, 0, 0, 0};

namespace {

extern void** const kVtable;
constexpr uint32_t kPagerMessage = 0xD1515392;
constexpr uint32_t kPagerScript = 0xE8DA4931;

void Free(void* block) { Mem::Manager::Get()->Free(block); }

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}
void DeleteStructure(Script::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Script::DeleteStruct(structure);
    }
}

// std::list<PagerMessage*>::push_back (0x004964A0, the game's: inserts before the sentinel).
void PushBack(PointerList* list, PagerMessage** message)
{
    list->Insert(list->head, reinterpret_cast<void* const*>(message));
}

} // namespace

// 0x004ECC30
PointerListNode** PointerList::Erase(PointerListNode** out, PointerListNode* first, PointerListNode* last)
{
    while (first != last) {
        PointerListNode* node = first;
        first = node->next;
        if (node != head) {
            node->prev->next = first;
            first->prev = node->prev;
            Free(node);
            --size;
        }
    }
    *out = first;
    return out;
}

// 0x004ECF20
CBaseComponent* __cdecl CPagerMessageComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CPagerMessageComponent));
    return memory ? static_cast<CPagerMessageComponent*>(memory)->Construct() : nullptr;
}

// 0x004EC960
void __cdecl CPagerMessageComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kPagerMessage), g_pagerMessageMemberFunctions, 0x18);
}

// 0x004ECE10
CPagerMessageComponent* CPagerMessageComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_messages.size = 0;
    auto* sentinel = static_cast<PointerListNode*>(Mem::New(sizeof(PointerListNode)));
    if (sentinel) {
        sentinel->next = sentinel;
        sentinel->prev = sentinel;
    }
    m_messages.head = sentinel;
    m_type = kPagerMessage;
    return this;
}

// 0x004ECE90
void CPagerMessageComponent::DestroyBody()
{
    m_vtable = kVtable;
    ClearMessages(nullptr);
    PointerListNode* head = m_messages.head;
    PointerListNode* end;
    m_messages.Erase(&end, head ? head->next : nullptr, head);
    Free(m_messages.head);
    CBaseComponent::DestroyBody();
}

// 0x004ECF80
CPagerMessageComponent* CPagerMessageComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

// 0x004ECC80
void CPagerMessageComponent::ClearMessages(Script::CStruct* params)
{
    int keep = 0;
    if (params)
        params->GetInteger(0x340FB7F4, &keep, 0);
    PointerListNode* node = m_messages.head ? m_messages.head->next : nullptr;
    while (m_messages.size > keep) {
        if (auto* message = static_cast<PagerMessage*>(node->data)) {
            DeleteStructure(message->params);
            Free(message);
            node->data = nullptr;
        }
        PointerListNode* next = node->next;
        if (node != m_messages.head) {
            node->prev->next = next;
            next->prev = node->prev;
            Free(node);
            --m_messages.size;
        }
        node = next;
    }
    Script::CStruct* data = NewStructure();
    data->AddChecksum(0x14C4B1CC, 1u);
    Script::KillSpawnedScriptsWithObjectAndName(m_object, kPagerScript, nullptr);
    m_object->SpawnScript(kPagerScript, data, 0, false);
    DeleteStructure(data);
}

// 0x004EC9A0
bool CPagerMessageComponent::SendPagerMessage(Script::CStruct* params, Script::CScript* /*script*/)
{
    auto* message = static_cast<PagerMessage*>(Mem::New(sizeof(PagerMessage)));
    if (message)
        message->params = nullptr;
    message->params = NewStructure();
    message->params->AppendStructure(params);
    message->read = false;
    PushBack(&m_messages, &message);
    Script::CStruct* data = NewStructure();
    data->AddChecksum(0x34406F14, 1u);
    if (message->params->ContainsFlag(0x8E6DFA5D))
        data->AddChecksum(0u, 0x8E6DFA5Du);
    Script::KillSpawnedScriptsWithObjectAndName(m_object, kPagerScript, nullptr);
    m_object->SpawnScript(kPagerScript, data, 0, false);
    DeleteStructure(data);
    return true;
}

// 0x004ECB50
bool CPagerMessageComponent::GetPagerMessage(Script::CStruct* params, Script::CScript* script)
{
    int number = 1;
    params->GetInteger(0x0A89DA7D, &number, 0);
    int index = number - 1;
    PointerListNode* node = m_messages.head ? m_messages.head->next : nullptr;
    for (int i = 0; i < index && i < m_messages.size; ++i)
        node = node->next;
    auto* message = static_cast<PagerMessage*>(node->data);
    script->m_params->AddStructure(kPagerMessage, message->params);
    static_cast<PagerMessage*>(node->data)->read = true;
    return true;
}

// 0x004EC980
bool CPagerMessageComponent::GetPagerMessageCount(Script::CStruct* /*params*/, Script::CScript* script)
{
    script->m_params->AddInteger(0xC4D8D79A, m_messages.size);
    return true;
}

// 0x004ECBD0
bool CPagerMessageComponent::GetFirstUnreadMessageNum(Script::CStruct* /*params*/, Script::CScript* script)
{
    PointerListNode* node = m_messages.head ? m_messages.head->next : nullptr;
    int count = m_messages.size;
    int index = 0;
    if (count != 0) {
        for (; index < count; ++index) {
            if (!static_cast<PagerMessage*>(node->data)->read)
                break;
            node = node->next;
        }
        if (index == count)
            --index;
    }
    script->m_params->AddInteger(0x8FD8B4C3, index + 1);
    return true;
}

// 0x004ECDF0
bool CPagerMessageComponent::ClearPagerMessages(Script::CStruct* params, Script::CScript* /*script*/)
{
    ClearMessages(params);
    return true;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CPagerMessageComponent::Destroy,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &Obj::CBaseComponent::RunMemberFunction,
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
