#pragma once

// The network manager's list types (Neversoft's ListKit::Elem<T> / ListKit::Foremost<T> instances for OnlinePlay's records): their
// destructors. Each node type has its own vtable holding only its scalar deleting destructor; a head type's vtable
// holds the head's (the same body, then freed). See net_lists.h for the net library's.

#include <cstdint>

#include "game/core/arena_alloc.h"

namespace OnlinePlay {

extern void** const kPlayerNodeVtable;     // ListKit::Elem<PlayerRecord>
extern void** const kNewPlayerNodeVtable;  // ListKit::Elem<JoinerRecord>
extern void** const kServerNodeVtable;     // ListKit::Elem<HostListing>
extern void** const kQueuedScriptNodeVtable; // ListKit::Elem<PendingScriptItem>
extern void** const kQueuedScriptVtable;     // PendingScriptItem (and its head)
extern void** const kPlayerHeadVtable;     // ListKit::Foremost<PlayerRecord>
extern void** const kNewPlayerHeadVtable;  // ListKit::Foremost<JoinerRecord>
extern void** const kServerHeadVtable;     // ListKit::Foremost<HostListing>

// The manager's lists: +0x11C the players, +0x130 the players joining, +0x144 the scripts queued to run once the level
// is up (PendingScriptItem, LaunchQueuedScripts), +0x158 / +0x16C the servers found (the server list's; a HostListing, vtable 0x0064CDFC, is
// itself a node of them, with a player list of its own at +0x94).
struct PlayerNode : MemSys::ListNode {
    // 0x005366A0: the destructor body: out of its list (and linked to itself).
    void DestroyBody();
    // 0x00536760: the scalar deleting destructor.
    PlayerNode* Teardown(unsigned int flags);
    // 0x00536CE0: the head's scalar deleting destructor.
    PlayerNode* DestroyHead(unsigned int flags);
};
struct NewPlayerNode : MemSys::ListNode {
    // 0x005366E0 / 0x005367E0 / 0x00536D10 (head)
    void DestroyBody();
    NewPlayerNode* Teardown(unsigned int flags);
    NewPlayerNode* DestroyHead(unsigned int flags);
};
struct ServerNode : MemSys::ListNode {
    // 0x005366C0 / 0x005367A0 / 0x00536D40 (head)
    void DestroyBody();
    ServerNode* Teardown(unsigned int flags);
    ServerNode* DestroyHead(unsigned int flags);
};
struct QueuedScriptNode : MemSys::ListNode {
    // 0x00536700 / 0x00536720
    void DestroyBody();
    QueuedScriptNode* Teardown(unsigned int flags);
};
// A script to spawn later (0x20 bytes; Hub::QueueScript): a node whose data is itself.
struct PendingScriptItem : QueuedScriptNode {
    int m_listLink;           // +0x14 the node it runs at
    uint32_t m_entityKey;  // +0x18 the object it runs on
    uint32_t m_ownScript;    // +0x1C
    // 0x005371F0: the scalar deleting destructor (the head's at +0x144 too).
    PendingScriptItem* DestroyRecord(unsigned int flags);
};
static_assert(sizeof(PendingScriptItem) == 0x20, "layout must match the original");

} // namespace OnlinePlay
