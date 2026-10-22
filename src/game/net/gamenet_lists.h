#pragma once

// The network manager's list types (Neversoft's Lst::Node<T> / Lst::Head<T> instances for GameNet's records): their
// destructors. Each node type has its own vtable holding only its scalar deleting destructor; a head type's vtable
// holds the head's (the same body, then freed). See net_lists.h for the net library's.

#include <cstdint>

#include "game/core/alloc.h"

namespace GameNet {

inline void** const kPlayerNodeVtable = reinterpret_cast<void**>(0x0064C9E8);     // Lst::Node<PlayerInfo>
inline void** const kNewPlayerNodeVtable = reinterpret_cast<void**>(0x0064C9E4);  // Lst::Node<NewPlayerInfo>
inline void** const kServerNodeVtable = reinterpret_cast<void**>(0x0064C9EC);     // Lst::Node<ServerInfo>
inline void** const kQueuedScriptNodeVtable = reinterpret_cast<void**>(0x0064C8EC); // Lst::Node<QueuedScript>
inline void** const kQueuedScriptVtable = reinterpret_cast<void**>(0x0064C9F8);     // QueuedScript (and its head)
inline void** const kPlayerHeadVtable = reinterpret_cast<void**>(0x0064C9F0);     // Lst::Head<PlayerInfo>
inline void** const kNewPlayerHeadVtable = reinterpret_cast<void**>(0x0064C9F4);  // Lst::Head<NewPlayerInfo>
inline void** const kServerHeadVtable = reinterpret_cast<void**>(0x0064C9FC);     // Lst::Head<ServerInfo>

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
