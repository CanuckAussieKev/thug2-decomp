// The pager message component (GameObjects::CPagerMessageComponent) and the game's std::list<T*>::erase: see
// pager_message.h. From the disassembly of 0x004EC960-0x004ECF9E (the member functions 0x004EC980, 0x004ECB50,
// 0x004ECBD0 and 0x004ECDF0 aren't Ghidra functions).

#include "game/obj/pager_message.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

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

void Dealloc(void* block) { MemSys::Hub::Pull()->Dealloc(block); }

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->PrepareAll() : nullptr;
}
void DeleteStructure(Scripts::CStruct* structure)
{
    if (structure) {
        structure->Clear();
        Scripts::DeleteStruct(structure);
    }
}

// std::list<PagerMessage*>::push_back (0x004964A0, the game's: inserts before the sentinel).
void PushBack(PointerList* list, PagerMessage** message)
{
    list->Splice(list->head, reinterpret_cast<void* const*>(message));
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
            Dealloc(node);
            --size;
        }
    }
    *out = first;
    return out;
}

// 0x004ECF20
CObjectModule* __cdecl CPagerMessageComponent::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CPagerMessageComponent));
    return memory ? static_cast<CPagerMessageComponent*>(memory)->MakeNew() : nullptr;
}

// 0x004EC960
void __cdecl CPagerMessageComponent::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kPagerMessage), g_pagerMessageMemberFunctions, 0x18);
}

// 0x004ECE10
CPagerMessageComponent* CPagerMessageComponent::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_messages.size = 0;
    auto* sentinel = static_cast<PointerListNode*>(MemSys::Alloc(sizeof(PointerListNode)));
    if (sentinel) {
        sentinel->next = sentinel;
        sentinel->prev = sentinel;
    }
    m_messages.head = sentinel;
    m_kindTag = kPagerMessage;
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
    Dealloc(m_messages.head);
    CObjectModule::DestroyBody();
}

// 0x004ECF80
CPagerMessageComponent* CPagerMessageComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Dealloc(this);
    return this;
}

// 0x004ECC80
void CPagerMessageComponent::ClearMessages(Scripts::CStruct* params)
{
    int keep = 0;
    if (params)
        params->FetchIntValue(0x340FB7F4, &keep, 0);
    PointerListNode* node = m_messages.head ? m_messages.head->next : nullptr;
    while (m_messages.size > keep) {
        if (auto* message = static_cast<PagerMessage*>(node->data)) {
            DeleteStructure(message->params);
            Dealloc(message);
            node->data = nullptr;
        }
        PointerListNode* next = node->next;
        if (node != m_messages.head) {
            node->prev->next = next;
            next->prev = node->prev;
            Dealloc(node);
            --m_messages.size;
        }
        node = next;
    }
    Scripts::CStruct* data = NewStructure();
    data->PutNameKey(0x14C4B1CC, 1u);
    Scripts::EndChildScriptsOfEntityByName(m_entity, kPagerScript, nullptr);
    m_entity->SpawnScript(kPagerScript, data, 0, false);
    DeleteStructure(data);
}

// 0x004EC9A0
bool CPagerMessageComponent::SendPagerMessage(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    auto* message = static_cast<PagerMessage*>(MemSys::Alloc(sizeof(PagerMessage)));
    if (message)
        message->params = nullptr;
    message->params = NewStructure();
    message->params->MergeStructure(params);
    message->read = false;
    PushBack(&m_messages, &message);
    Scripts::CStruct* data = NewStructure();
    data->PutNameKey(0x34406F14, 1u);
    if (message->params->HasBareFlag(0x8E6DFA5D))
        data->PutNameKey(0u, 0x8E6DFA5Du);
    Scripts::EndChildScriptsOfEntityByName(m_entity, kPagerScript, nullptr);
    m_entity->SpawnScript(kPagerScript, data, 0, false);
    DeleteStructure(data);
    return true;
}

// 0x004ECB50
bool CPagerMessageComponent::GetPagerMessage(Scripts::CStruct* params, Scripts::CScript* script)
{
    int number = 1;
    params->FetchIntValue(0x0A89DA7D, &number, 0);
    int index = number - 1;
    PointerListNode* node = m_messages.head ? m_messages.head->next : nullptr;
    for (int i = 0; i < index && i < m_messages.size; ++i)
        node = node->next;
    auto* message = static_cast<PagerMessage*>(node->data);
    script->m_args->PutSubStruct(kPagerMessage, message->params);
    static_cast<PagerMessage*>(node->data)->read = true;
    return true;
}

// 0x004EC980
bool CPagerMessageComponent::GetPagerMessageCount(Scripts::CStruct* /*params*/, Scripts::CScript* script)
{
    script->m_args->PutInt(0xC4D8D79A, m_messages.size);
    return true;
}

// 0x004ECBD0
bool CPagerMessageComponent::GetFirstUnreadMessageNum(Scripts::CStruct* /*params*/, Scripts::CScript* script)
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
    script->m_args->PutInt(0x8FD8B4C3, index + 1);
    return true;
}

// 0x004ECDF0
bool CPagerMessageComponent::ClearPagerMessages(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    ClearMessages(params);
    return true;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CPagerMessageComponent::Teardown,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
