#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The game's std::list of pointers (its allocator: the memory manager): a sentinel node and a count.
struct PointerListNode {
    void* data;              // +0x0
    PointerListNode* next;   // +0x4
    PointerListNode* prev;   // +0x8
};
struct PointerList {
    PointerListNode* head;   // +0x0 the sentinel
    int size;                // +0x4
    // 0x004ECC30 (shared by every such list): the nodes from `first` up to `last` unlinked and freed (the sentinel
    // never); `*out` = `last`. Returns out.
    PointerListNode** Erase(PointerListNode** out, PointerListNode* first, PointerListNode* last);
    // 0x004964A0 (shared by every such list; gfx/particles.cpp): a new node holding *value before `position`. Out of
    // memory, the original goes on through the null node (and crashes) - kept.
    void Insert(PointerListNode* position, void* const* value);
};

// A pager message: its parameters (owned) and whether it has been read (8 bytes).
struct PagerMessage {
    Script::CStruct* params;
    bool read;
    uint8_t pad5[3];
};

// The pager message component ("pagermessage", type 0xD1515392, vtable 0x0064B6FC, 0x20 bytes; made by 0x004ECF20):
// the skater's pager - messages sent by scripts, kept in order, read or not. Its script member functions are the
// table at 0x006831A8 (filled at start-up). Every change runs the object's script 0xE8DA4931 afresh (one already
// running is killed).
class CPagerMessageComponent : public CBaseComponent {
public:
    // 0x004ECF20: a new one (0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x004EC960 (not a Ghidra function): its script member functions (the table at 0x006831A8) registered.
    static void __cdecl RegisterMemberFunctions();
    // 0x004ECE10: the base's constructor, its vtable and type, an empty list. Returns this.
    CPagerMessageComponent* Construct();
    // 0x004ECE90: the destructor body: every message deleted, the list freed, the base's.
    void DestroyBody();
    // 0x004ECF80 (vtable slot 0)
    CPagerMessageComponent* Destroy(unsigned int flags);
    // 0x004ECC80: the oldest messages deleted down to the parameters' 0x340FB7F4 (none: all), then the script
    // 0xE8DA4931 run with 0x14C4B1CC.
    void ClearMessages(Script::CStruct* params);

    // --- script member functions (the table at 0x006831A8). True. ---
    // 0x004EC9A0 [SendPagerMessage]: a new unread message (a copy of the parameters) at the end; the script
    // 0xE8DA4931 run with 0x34406F14 (and the message's flag 0x8E6DFA5D).
    bool SendPagerMessage(Script::CStruct* params, Script::CScript* script);
    // 0x004ECB50 [GetPagerMessage]: message 0x0A89DA7D (from 1; past the end: the last's next) into the script's
    // parameters as 0xD1515392; marked read.
    bool GetPagerMessage(Script::CStruct* params, Script::CScript* script);
    // 0x004EC980 [GetPagerMessageCount]: 0xC4D8D79A = how many.
    bool GetPagerMessageCount(Script::CStruct* params, Script::CScript* script);
    // 0x004ECBD0 [GetFirstUnreadMessageNum]: 0x8FD8B4C3 = the first unread one's number (from 1; none: the last).
    bool GetFirstUnreadMessageNum(Script::CStruct* params, Script::CScript* script);
    // 0x004ECDF0 [ClearPagerMessages]
    bool ClearPagerMessages(Script::CStruct* params, Script::CScript* script);

    PointerList m_messages; // +0x18 (PagerMessage*)
};
static_assert(offsetof(CPagerMessageComponent, m_messages) == 0x18 && sizeof(CPagerMessageComponent) == 0x20,
              "layout must match the original");

} // namespace Obj
