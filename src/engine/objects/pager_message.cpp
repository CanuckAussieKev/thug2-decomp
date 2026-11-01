// The pager message component (GameObjects::PagerMessageComponent) and the game's std::list<T*>::erase: see
// pager_message.h. From the disassembly of 0x004EC960-0x004ECF9E (the member functions 0x004EC980, 0x004ECB50,
// 0x004ECBD0 and 0x004ECDF0 aren't Ghidra functions).

#include "engine/objects/pager_message.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace objects {

uint32_t g_pager_message_member_functions[36]{
    0x4824484A, 0xD1515392, 0, 0, 0, 0, 0xD65B2BB5, 0xD1515392, 0, 0, 0, 0, 0x5B64BFEC, 0xD1515392, 0, 0, 0, 0,
    0xAF88BE7A, 0xD1515392, 0, 0, 0, 0, 0xE7DF996D, 0xD1515392, 0, 0, 0, 0, 0,          0xD1515392, 0, 0, 0, 0};

namespace {

constexpr uint32_t kPagerMessage = 0xD1515392;
constexpr uint32_t kPagerScript = 0xE8DA4931;

void dealloc(void* block) { memory::Hub::get_pull()->dealloc(block); }

script::Struct* spawn_param_table()
{
    script::Struct* structure = script::spawn_param_bag();
    return structure ? memory::construct_in<script::Struct>(structure) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

// std::list<PagerMessage*>::push_back (0x004964A0, the game's: inserts before the sentinel).
void get_push_back(PointerList* list, PagerMessage** message)
{
    list->splice(list->head, raw::as<void* const>(message));
}

} // namespace

// 0x004ECC30
PointerListNode** PointerList::get_erase(PointerListNode** out, PointerListNode* first, PointerListNode* last)
{
    while (first != last) {
        PointerListNode* node = first;
        first = node->next;
        if (node != head) {
            node->prev->next = first;
            first->prev = node->prev;
            dealloc(node);
            --size;
        }
    }
    *out = first;
    return out;
}

// 0x004ECF20
memory::owner<ObjectModule*> PagerMessageComponent::fabricate() { return new PagerMessageComponent; }

// 0x004EC960
void PagerMessageComponent::publish_method_table()
{
    script::register_member_function_table(cell::pointer<void>(kPagerMessage), g_pager_message_member_functions,
                                           0x18);
}

// 0x004ECE10
PagerMessageComponent::PagerMessageComponent()
{
    messages_.size = 0;
    auto* sentinel = static_cast<PointerListNode*>(memory::alloc(sizeof(PointerListNode)));
    if (sentinel) {
        sentinel->next = sentinel;
        sentinel->prev = sentinel;
    }
    messages_.head = sentinel;
    kind_tag_ = kPagerMessage;
}

// 0x004ECE90 (0x004ECF80 the deleting one)
PagerMessageComponent::~PagerMessageComponent()
{
    clear_messages(nullptr);
    PointerListNode* head = messages_.head;
    PointerListNode* end = nullptr;
    messages_.get_erase(&end, head ? head->next : nullptr, head);
    dealloc(messages_.head);
}

// 0x004ECC80
void PagerMessageComponent::clear_messages(script::Struct* params)
{
    int keep = 0;
    if (params)
        params->fetch_int_value(0x340FB7F4, &keep, 0);
    PointerListNode* node = messages_.head ? messages_.head->next : nullptr;
    while (messages_.size > keep) {
        if (auto* message = static_cast<PagerMessage*>(node->data)) {
            delete_structure(message->params);
            dealloc(message);
            node->data = nullptr;
        }
        PointerListNode* next = node->next;
        if (node != messages_.head) {
            node->prev->next = next;
            next->prev = node->prev;
            dealloc(node);
            --messages_.size;
        }
        node = next;
    }
    script::Struct* data = spawn_param_table();
    data->put_name_key(0x14C4B1CC, 1u);
    script::end_child_scripts_of_entity_by_name(entity_, kPagerScript, nullptr);
    entity_->spawn_script(kPagerScript, data, 0, false);
    delete_structure(data);
}

// 0x004EC9A0
bool PagerMessageComponent::send_pager_message(script::Struct* params, script::Script* /*script*/)
{
    auto* message = static_cast<PagerMessage*>(memory::alloc(sizeof(PagerMessage)));
    if (message)
        message->params = nullptr;
    message->params = spawn_param_table();
    message->params->merge_structure(params);
    message->read = false;
    get_push_back(&messages_, &message);
    script::Struct* data = spawn_param_table();
    data->put_name_key(0x34406F14, 1u);
    if (message->params->has_bare_flag(0x8E6DFA5D))
        data->put_name_key(0u, 0x8E6DFA5Du);
    script::end_child_scripts_of_entity_by_name(entity_, kPagerScript, nullptr);
    entity_->spawn_script(kPagerScript, data, 0, false);
    delete_structure(data);
    return true;
}

// 0x004ECB50
bool PagerMessageComponent::get_pager_message(script::Struct* params, script::Script* script)
{
    int number = 1;
    params->fetch_int_value(0x0A89DA7D, &number, 0);
    const int index = number - 1;
    const PointerListNode* node = messages_.head ? messages_.head->next : nullptr;
    for (int i = 0; i < index && i < messages_.size; ++i)
        node = node->next;
    auto* message = static_cast<PagerMessage*>(node->data);
    script->args_->put_sub_struct(kPagerMessage, message->params);
    static_cast<PagerMessage*>(node->data)->read = true;
    return true;
}

// 0x004EC980
bool PagerMessageComponent::get_pager_message_count(script::Struct* /*params*/, script::Script* script)
{
    script->args_->put_int(0xC4D8D79A, messages_.size);
    return true;
}

// 0x004ECBD0
bool PagerMessageComponent::get_first_unread_message_num(script::Struct* /*params*/, script::Script* script)
{
    const PointerListNode* node = messages_.head ? messages_.head->next : nullptr;
    const int count = messages_.size;
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
    script->args_->put_int(0x8FD8B4C3, index + 1);
    return true;
}

// 0x004ECDF0
bool PagerMessageComponent::clear_pager_messages(script::Struct* params, script::Script* /*script*/)
{
    clear_messages(params);
    return true;
}

} // namespace objects
