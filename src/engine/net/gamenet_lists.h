#pragma once

// The network manager's list records. (The original's node and head types each had a table holding only their
// scalar deleting destructor: out of the list, freed. The lists here use memory::ListNode; the records keep the
// original's layout.)

#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace online {

// The manager's lists: +0x11C the players, +0x130 the players joining, +0x144 the scripts queued to run once the level
// is up (PendingScriptItem, LaunchQueuedScripts), +0x158 / +0x16C the servers found (the server list's; a HostListing
// is itself a node of them, with a player list of its own at +0x94). Each head keeps the original's 0x14 bytes
// (PaddedListNode).

// A script to spawn later (0x20 bytes; Hub::queue_script): a node whose data is itself.
struct PendingScriptItem : memory::PaddedListNode {
    int list_link_;       // +0x14 the node it runs at
    uint32_t entity_key_; // +0x18 the object it runs on
    uint32_t own_script_; // +0x1C
    // 0x005371F0: the scalar deleting destructor (the head's at +0x144 too; the node's 0x00536700: out of its list).
    PendingScriptItem* destroy_record(unsigned int flags);
};
static_assert(sizeof(PendingScriptItem) == 0x20, "layout must match the original");

} // namespace online
