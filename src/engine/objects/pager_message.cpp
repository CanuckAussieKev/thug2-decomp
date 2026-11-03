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
namespace {
constexpr uint32_t kSendPagerMessage = 0x4824484A; // SendPagerMessage
constexpr uint32_t kPagerMessageName = 0xD1515392; // PagerMessage
constexpr uint32_t kGetPagerMessage = 0xD65B2BB5; // GetPagerMessage
constexpr uint32_t kGetPagerMessageCount = 0x5B64BFEC; // GetPagerMessageCount
constexpr uint32_t kGetFirstUnreadMessageNum = 0xAF88BE7A; // GetFirstUnreadMessageNum
constexpr uint32_t kClearPagerMessages = 0xE7DF996D; // ClearPagerMessages
constexpr uint32_t kKeep = 0x340FB7F4; // Keep
constexpr uint32_t kDeleted = 0x14C4B1CC; // Deleted
constexpr uint32_t kAdded = 0x34406F14; // Added
constexpr uint32_t kPriorityMessage = 0x8E6DFA5D; // priority_message
constexpr uint32_t kPriorityMessageName = 0x8E6DFA5Du; // priority_message
constexpr uint32_t kMessageNum = 0x0A89DA7D; // message_num
constexpr uint32_t kPagerMessageCount = 0xC4D8D79A; // PagerMessageCount
constexpr uint32_t kFirstUnreadMessageNum = 0x8FD8B4C3; // FirstUnreadMessageNum
} // namespace

std::array<script::MemberFunctionEntry, kPagerMessageMemberFunctionCount>& pager_message_member_functions()
{
    static std::array<script::MemberFunctionEntry, kPagerMessageMemberFunctionCount> pager_message_member_functions{{
        {.callsign_ = kSendPagerMessage, .owner_kind_ = kPagerMessageName}, {.callsign_ = kGetPagerMessage, .owner_kind_ = kPagerMessageName}, {.callsign_ = kGetPagerMessageCount, .owner_kind_ = kPagerMessageName}, {.callsign_ = kGetFirstUnreadMessageNum, .owner_kind_ = kPagerMessageName},
        {.callsign_ = kClearPagerMessages, .owner_kind_ = kPagerMessageName}, {.callsign_ = 0, .owner_kind_ = kPagerMessageName}}};
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
    set_kind_tag(kPagerMessage);
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
        params->fetch_int_value(kKeep, &keep, 0);
    auto* node = messages_.first();
    while (messages_.size() > keep) {
        if (PagerMessage* message = node->value) {
            script::free_struct(memory::owned_element(message->params)); // (the message's)
            dealloc(message);
            node->value = nullptr;
        }
        auto* next = node->next;
        if (node != messages_.head()) {
            node->prev->next = next;
            next->prev = node->prev;
            dealloc(node);
            --messages_.size();
        }
        node = next;
    }
    const script::StructPtr data = script::make_struct();
    data->put_name_key(checksums::Name{kDeleted}, 1u);
    script::end_child_scripts_of_entity_by_name(entity(), kPagerScript, nullptr);
    entity()->spawn_script(kPagerScript, data.get(), 0, false);
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
    data->put_name_key(checksums::Name{kAdded}, 1u);
    if (message->params->has_bare_flag(kPriorityMessage))
        data->put_name_key(checksums::Name{0u}, kPriorityMessageName);
    script::end_child_scripts_of_entity_by_name(entity(), kPagerScript, nullptr);
    entity()->spawn_script(kPagerScript, data.get(), 0, false);
    data.reset();
    return true;
}

// 0x004ECB50
bool PagerMessageComponent::get_pager_message(script::Struct* params, script::Script* script)
{
    int number = 1;
    params->fetch_int_value(kMessageNum, &number, 0);
    const int index = number - 1;
    const auto* node = messages_.first();
    for (int i = 0; i < index && i < messages_.size(); ++i)
        node = node->next;
    PagerMessage* message = node->value;
    script->args()->put_sub_struct(kPagerMessage, message->params);
    message->read = true;
    return true;
}

// 0x004EC980
bool PagerMessageComponent::get_pager_message_count(script::Struct* /*params*/, script::Script* script)
{
    script->args()->put_int(checksums::Name{kPagerMessageCount}, messages_.size());
    return true;
}

// 0x004ECBD0
bool PagerMessageComponent::get_first_unread_message_num(script::Struct* /*params*/, script::Script* script)
{
    const auto* node = messages_.first();
    const int count = messages_.size();
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
    script->args()->put_int(checksums::Name{kFirstUnreadMessageNum}, index + 1);
    return true;
}

// 0x004ECDF0
bool PagerMessageComponent::clear_pager_messages(script::Struct* params, script::Script* /*script*/)
{
    clear_messages(params);
    return true;
}

} // namespace objects
