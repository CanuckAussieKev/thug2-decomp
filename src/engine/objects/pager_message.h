#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>
#include "engine/lists/pointer_ring.h"

namespace script {
class Script;
class Struct;
} // namespace script

namespace objects {

// A pager message: its parameters (owned) and whether it has been read (8 bytes).
struct PagerMessage {
    script::Struct* params;
    bool read;
    std::array<uint8_t, 3> spare_slot5_;
};

// The pager message component ("pagermessage", type 0xD1515392, vtable 0x0064B6FC, 0x20 bytes; made by 0x004ECF20):
// the skater's pager - messages sent by scripts, kept in order, read or not. Its script member functions are the
// table at 0x006831A8 (filled at start-up). Every change runs the object's script 0xE8DA4931 afresh (one already
// running is killed).
class PagerMessageComponent final : public ObjectModule {
public:
    PagerMessageComponent(const PagerMessageComponent&) = delete;
    PagerMessageComponent& operator=(const PagerMessageComponent&) = delete;
    // 0x004ECF20: a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x004EC960 (not a Ghidra function): its script member functions (the table at 0x006831A8) registered.
    static void publish_method_table();
    // 0x004ECE10: its type, an empty list.
    PagerMessageComponent();
    // 0x004ECE90: the destructor body: every message deleted, the list freed, the base's.
    ~PagerMessageComponent() override;
    void tick_frame() override {} // (slot 1: nothing to update)
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x004ECC80: the oldest messages deleted down to the parameters' 0x340FB7F4 (none: all), then the script
    // 0xE8DA4931 run with 0x14C4B1CC.
    void clear_messages(script::Struct* params);

    // --- script member functions (the table at 0x006831A8). True. ---
    // 0x004EC9A0 [send_pager_message]: a new unread message (a copy of the parameters) at the end; the script
    // 0xE8DA4931 run with 0x34406F14 (and the message's flag 0x8E6DFA5D).
    bool send_pager_message(script::Struct* params, script::Script* script);
    // 0x004ECB50 [GetPagerMessage]: message 0x0A89DA7D (from 1; past the end: the last's next) into the script's
    // parameters as 0xD1515392; marked read.
    bool get_pager_message(script::Struct* params, script::Script* script);
    // 0x004EC980 [GetPagerMessageCount]: 0xC4D8D79A = how many.
    bool get_pager_message_count(script::Struct* params, script::Script* script);
    // 0x004ECBD0 [GetFirstUnreadMessageNum]: 0x8FD8B4C3 = the first unread one's number (from 1; none: the last).
    bool get_first_unread_message_num(script::Struct* params, script::Script* script);
    // 0x004ECDF0 [ClearPagerMessages]
    bool clear_pager_messages(script::Struct* params, script::Script* script);

    lists::PointerRing<PagerMessage> messages_{}; // +0x18
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<script::MemberFunctionEntry, 6> g_pager_message_member_functions;
} // namespace objects
