// The pager message component (GameObjects::PagerMessageComponent) and the game's std::list<T*>::erase: see
// pager_message.h. From the disassembly of 0x004EC960-0x004ECF9E (the member functions 0x004EC980, 0x004ECB50,
// 0x004ECBD0 and 0x004ECDF0 aren't Ghidra functions).

#include "engine/objects/pager_message.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

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
namespace objects {

std::array<script::MemberFunctionEntry, 6>& pager_message_member_functions()
{
    static std::array<script::MemberFunctionEntry, 6> pager_message_member_functions{{
        {.callsign_ = 0x4824484A, .owner_kind_ = 0xD1515392}, {.callsign_ = 0xD65B2BB5, .owner_kind_ = 0xD1515392}, {.callsign_ = 0x5B64BFEC, .owner_kind_ = 0xD1515392}, {.callsign_ = 0xAF88BE7A, .owner_kind_ = 0xD1515392},
        {.callsign_ = 0xE7DF996D, .owner_kind_ = 0xD1515392}, {.callsign_ = 0, .owner_kind_ = 0xD1515392}}};
    return pager_message_member_functions;
}

namespace {

constexpr uint32_t kPagerMessage = 0xD1515392;
constexpr uint32_t kPagerScript = 0xE8DA4931;

void dealloc(void* block) { memory::Hub::get_pull()->dealloc(block); }

} // namespace

// 0x004ECF20
gsl::owner<ObjectModule*> PagerMessageComponent::fabricate() { return new PagerMessageComponent; }

// 0x004EC960
void PagerMessageComponent::publish_method_table()
{
    script::register_member_function_table(pager_message_member_functions());
}

// 0x004ECE10
PagerMessageComponent::PagerMessageComponent()
{
    messages_.make();
    kind_tag_ = kPagerMessage;
}

// 0x004ECE90 (0x004ECF80 the deleting one)
PagerMessageComponent::~PagerMessageComponent()
{
    clear_messages(nullptr);
    messages_.destroy();
}

// 0x004ECC80
void PagerMessageComponent::clear_messages(script::Struct* params)
{
    int keep = 0;
    if (params)
        params->fetch_int_value(0x340FB7F4, &keep, 0);
    auto* node = messages_.first();
    while (messages_.size > keep) {
        if (PagerMessage* message = node->value) {
            script::free_struct(memory::owned_element(message->params)); // (the message's)
            dealloc(message);
            node->value = nullptr;
        }
        auto* next = node->next;
        if (node != messages_.head) {
            node->prev->next = next;
            next->prev = node->prev;
            dealloc(node);
            --messages_.size;
        }
        node = next;
    }
    const script::StructPtr data = script::make_struct();
    data->put_name_key(0x14C4B1CC, 1u);
    script::end_child_scripts_of_entity_by_name(entity_, kPagerScript, nullptr);
    entity_->spawn_script(kPagerScript, data.get(), 0, false);
}

// 0x004EC9A0
bool PagerMessageComponent::send_pager_message(script::Struct* params, script::Script* /*script*/)
{
    auto* message = static_cast<PagerMessage*>(memory::alloc(sizeof(PagerMessage)));
    if (message)
        message->params = nullptr;
    gsl::owner<script::Struct*> made = script::new_struct(); // (the message's: freed with it)
    message->params = made;
    message->params->merge_structure(params);
    message->read = false;
    messages_.push_back(message);
    script::StructPtr data = script::make_struct();
    data->put_name_key(0x34406F14, 1u);
    if (message->params->has_bare_flag(0x8E6DFA5D))
        data->put_name_key(0u, 0x8E6DFA5Du);
    script::end_child_scripts_of_entity_by_name(entity_, kPagerScript, nullptr);
    entity_->spawn_script(kPagerScript, data.get(), 0, false);
    data.reset();
    return true;
}

// 0x004ECB50
bool PagerMessageComponent::get_pager_message(script::Struct* params, script::Script* script)
{
    int number = 1;
    params->fetch_int_value(0x0A89DA7D, &number, 0);
    const int index = number - 1;
    const auto* node = messages_.first();
    for (int i = 0; i < index && i < messages_.size; ++i)
        node = node->next;
    PagerMessage* message = node->value;
    script->args()->put_sub_struct(kPagerMessage, message->params);
    message->read = true;
    return true;
}

// 0x004EC980
bool PagerMessageComponent::get_pager_message_count(script::Struct* /*params*/, script::Script* script)
{
    script->args()->put_int(0xC4D8D79A, messages_.size);
    return true;
}

// 0x004ECBD0
bool PagerMessageComponent::get_first_unread_message_num(script::Struct* /*params*/, script::Script* script)
{
    const auto* node = messages_.first();
    const int count = messages_.size;
    int index = 0;
    if (count != 0) {
        for (; index < count; ++index) {
            if (!node->value->read)
                break;
            node = node->next;
        }
        if (index == count)
            --index;
    }
    script->args()->put_int(0x8FD8B4C3, index + 1);
    return true;
}

// 0x004ECDF0
bool PagerMessageComponent::clear_pager_messages(script::Struct* params, script::Script* /*script*/)
{
    clear_messages(params);
    return true;
}

} // namespace objects
