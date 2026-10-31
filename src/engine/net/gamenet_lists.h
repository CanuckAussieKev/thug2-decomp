#pragma once

// The network manager's list types (Neversoft's lists::Elem<T> / lists::Foremost<T> instances for online's records): their
// destructors. Each node type has its own vtable holding only its scalar deleting destructor; a head type's vtable
// holds the head's (the same body, then freed). See net_lists.h for the net library's.

#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace online {

extern void** const kPlayerNodeVtable;       // lists::Elem<PlayerRecord>
extern void** const kNewPlayerNodeVtable;    // lists::Elem<JoinerRecord>
extern void** const kServerNodeVtable;       // lists::Elem<HostListing>
extern void** const kQueuedScriptNodeVtable; // lists::Elem<PendingScriptItem>
extern void** const kQueuedScriptVtable;     // PendingScriptItem (and its head)
extern void** const kPlayerHeadVtable;       // lists::Foremost<PlayerRecord>
extern void** const kNewPlayerHeadVtable;    // lists::Foremost<JoinerRecord>
extern void** const kServerHeadVtable;       // lists::Foremost<HostListing>

// The manager's lists: +0x11C the players, +0x130 the players joining, +0x144 the scripts queued to run once the level
// is up (PendingScriptItem, LaunchQueuedScripts), +0x158 / +0x16C the servers found (the server list's; a HostListing, vtable 0x0064CDFC, is
// itself a node of them, with a player list of its own at +0x94).
struct PlayerNode : memory::ListNode {
    // 0x005366A0: the destructor body: out of its list (and linked to itself).
    void destroy_body();
    // 0x00536760: the scalar deleting destructor.
    PlayerNode* teardown(unsigned int flags);
    // 0x00536CE0: the head's scalar deleting destructor.
    PlayerNode* destroy_head(unsigned int flags);
};
struct NewPlayerNode : memory::ListNode {
    // 0x005366E0 / 0x005367E0 / 0x00536D10 (head)
    void destroy_body();
    NewPlayerNode* teardown(unsigned int flags);
    NewPlayerNode* destroy_head(unsigned int flags);
};
struct ServerNode : memory::ListNode {
    // 0x005366C0 / 0x005367A0 / 0x00536D40 (head)
    void destroy_body();
    ServerNode* teardown(unsigned int flags);
    ServerNode* destroy_head(unsigned int flags);
};
struct QueuedScriptNode : memory::ListNode {
    // 0x00536700 / 0x00536720
    void destroy_body();
    QueuedScriptNode* teardown(unsigned int flags);
};
// A script to spawn later (0x20 bytes; Hub::queue_script): a node whose data is itself.
struct PendingScriptItem : QueuedScriptNode {
    int list_link_;       // +0x14 the node it runs at
    uint32_t entity_key_; // +0x18 the object it runs on
    uint32_t own_script_; // +0x1C
    // 0x005371F0: the scalar deleting destructor (the head's at +0x144 too).
    PendingScriptItem* destroy_record(unsigned int flags);
};
static_assert(sizeof(PendingScriptItem) == 0x20, "layout must match the original");

} // namespace online
