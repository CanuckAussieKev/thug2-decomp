#pragma once

// The network manager's list types (Neversoft's Lst::Node<T> / Lst::Head<T> instances for GameNet's records): their
// destructors. Each node type has its own vtable holding only its scalar deleting destructor; a head type's vtable
// holds the head's (the same body, then freed). See net_lists.h for the net library's.

#include <cstdint>

#include "game/core/alloc.h"

namespace GameNet {

extern void** const kPlayerNodeVtable;     // Lst::Node<PlayerInfo>
extern void** const kNewPlayerNodeVtable;  // Lst::Node<NewPlayerInfo>
extern void** const kServerNodeVtable;     // Lst::Node<ServerInfo>
extern void** const kQueuedScriptNodeVtable; // Lst::Node<QueuedScript>
extern void** const kQueuedScriptVtable;     // QueuedScript (and its head)
extern void** const kPlayerHeadVtable;     // Lst::Head<PlayerInfo>
extern void** const kNewPlayerHeadVtable;  // Lst::Head<NewPlayerInfo>
extern void** const kServerHeadVtable;     // Lst::Head<ServerInfo>

// The manager's lists: +0x11C the players, +0x130 the players joining, +0x144 the scripts queued to run once the level
// is up (QueuedScript, LaunchQueuedScripts), +0x158 / +0x16C the servers found (the server list's; a ServerInfo, vtable 0x0064CDFC, is
// itself a node of them, with a player list of its own at +0x94).
struct PlayerNode : Mem::ListNode {
    // 0x005366A0: the destructor body: out of its list (and linked to itself).
    void DestroyBody();
    // 0x00536760: the scalar deleting destructor.
    PlayerNode* Destroy(unsigned int flags);
    // 0x00536CE0: the head's scalar deleting destructor.
    PlayerNode* DestroyHead(unsigned int flags);
};
struct NewPlayerNode : Mem::ListNode {
    // 0x005366E0 / 0x005367E0 / 0x00536D10 (head)
    void DestroyBody();
    NewPlayerNode* Destroy(unsigned int flags);
    NewPlayerNode* DestroyHead(unsigned int flags);
};
struct ServerNode : Mem::ListNode {
    // 0x005366C0 / 0x005367A0 / 0x00536D40 (head)
    void DestroyBody();
    ServerNode* Destroy(unsigned int flags);
    ServerNode* DestroyHead(unsigned int flags);
};
struct QueuedScriptNode : Mem::ListNode {
    // 0x00536700 / 0x00536720
    void DestroyBody();
    QueuedScriptNode* Destroy(unsigned int flags);
};
// A script to spawn later (0x20 bytes; Manager::QueueScript): a node whose data is itself.
struct QueuedScript : QueuedScriptNode {
    int m_node;           // +0x14 the node it runs at
    uint32_t m_objectId;  // +0x18 the object it runs on
    uint32_t m_script;    // +0x1C
    // 0x005371F0: the scalar deleting destructor (the head's at +0x144 too).
    QueuedScript* DestroyRecord(unsigned int flags);
};
static_assert(sizeof(QueuedScript) == 0x20, "layout must match the original");

} // namespace GameNet
