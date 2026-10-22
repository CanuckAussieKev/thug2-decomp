#pragma once

#include <cstdint>

#include "game/core/alloc.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

// The buddy list's part before its own code (0x00548100-0x00548560: BuddyMan in buddy_man.h): GP's buddy request
// and login, buddy messages sent, the presence status, and the buddy lists' list types; with the getters the linker
// folded at 0x005480B0-0x005480F0.
namespace GameNet {

// The buddy lists' list types (their vtables hold only the scalar deleting destructor):
// Lst::Search<Buddy> (0x0064CECC): 0x005480D0 the body (the vtable reset), 0x00548440 the scalar deleting one;
// Lst::Search<BuddyMessage> (0x0064CED0): 0x005480E0 / 0x00548470;
// Lst::Node<Buddy> (0x0064CF60): 0x005484A0 the body (out of its list), 0x005484E0 the scalar deleting one;
// Lst::Node<BuddyMessage> (0x0064CF64): 0x005484C0 / 0x00548520.
struct BuddySearch {
    void** m_vtable;
    Mem::ListNode* m_node;
    void DestroyBody();
    BuddySearch* Destroy(unsigned int flags);
};
struct BuddyMessageSearch {
    void** m_vtable;
    Mem::ListNode* m_node;
    void DestroyBody();
    BuddyMessageSearch* Destroy(unsigned int flags);
};
struct BuddyNode : Mem::ListNode {
    void DestroyBody();
    BuddyNode* Destroy(unsigned int flags);
};
struct BuddyMessageNode : Mem::ListNode {
    void DestroyBody();
    BuddyMessageNode* Destroy(unsigned int flags);
};

// The getters folded at one address each (4 bytes: source only), named for an object with the field.
struct FoldedGetters {
    // 0x005480B0: the byte at +0x10 (BuddyMan's logged-in flag, ...).
    bool Byte10();
    // 0x005480C0: the word at +0x04 (BuddyMan's profile, the network manager's server app, a park's ..., 203 callers).
    uint32_t Word04();
    // 0x005480F0: the word at +0x18 (a skeleton component's skeleton, a node component's node array, a sector's
    // collision, ...).
    uint32_t Word18();
};

// The script commands.
bool __cdecl ProfileLoggedIn(Script::CStruct* params, Script::CScript* script);   // 0x00548360
bool __cdecl SendBuddyMessage(Script::CStruct* params, Script::CScript* script);  // 0x00548370
bool __cdecl SetLobbyStatus(Script::CStruct* params, Script::CScript* script);    // 0x005483E0

} // namespace GameNet
