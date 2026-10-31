#pragma once

#include <cstdint>

#include "game/core/alloc.h"

namespace Scripts {
class CScript;
class CStruct;
} // namespace Script

// The buddy list's part before its own code (0x00548100-0x00548560: FriendsManager in buddy_man.h): GP's buddy request
// and login, buddy messages sent, the presence status, and the buddy lists' list types; with the getters the linker
// folded at 0x005480B0-0x005480F0.
namespace OnlinePlay {

extern void** const kBuddyNodeVtable;   // 0x0064CF60: ListKit::Elem<Buddy> / ListKit::Foremost<Buddy>
extern void** const kMessageNodeVtable; // 0x0064CF64

// The buddy lists' list types (their vtables hold only the scalar deleting destructor):
// ListKit::Hunt<Buddy> (0x0064CECC): 0x005480D0 the body (the vtable reset), 0x00548440 the scalar deleting one;
// ListKit::Hunt<BuddyMessage> (0x0064CED0): 0x005480E0 / 0x00548470;
// ListKit::Elem<Buddy> (0x0064CF60): 0x005484A0 the body (out of its list), 0x005484E0 the scalar deleting one;
// ListKit::Elem<BuddyMessage> (0x0064CF64): 0x005484C0 / 0x00548520.
struct BuddySearch {
    void** m_vtable;
    MemSys::ListNode* m_listLink;
    void DestroyBody();
    BuddySearch* Teardown(unsigned int flags);
};
struct BuddyMessageSearch {
    void** m_vtable;
    MemSys::ListNode* m_listLink;
    void DestroyBody();
    BuddyMessageSearch* Teardown(unsigned int flags);
};
struct BuddyNode : MemSys::ListNode {
    void DestroyBody();
    BuddyNode* Teardown(unsigned int flags);
};
struct BuddyMessageNode : MemSys::ListNode {
    void DestroyBody();
    BuddyMessageNode* Teardown(unsigned int flags);
};

// The getters folded at one address each (4 bytes: source only), named for an object with the field.
struct FoldedGetters {
    // 0x005480B0: the byte at +0x10 (FriendsManager's logged-in flag, ...).
    bool Byte10();
    // 0x005480C0: the word at +0x04 (FriendsManager's profile, the network manager's server app, a park's ..., 203 callers).
    uint32_t Word04();
    // 0x005480F0: the word at +0x18 (a skeleton component's skeleton, a node component's node array, a sector's
    // collision, ...).
    uint32_t Word18();
};

// The script commands.
bool __cdecl ProfileLoggedIn(Scripts::CStruct* params, Scripts::CScript* script);   // 0x00548360
bool __cdecl SendBuddyMessage(Scripts::CStruct* params, Scripts::CScript* script);  // 0x00548370
bool __cdecl SetLobbyStatus(Scripts::CStruct* params, Scripts::CScript* script);    // 0x005483E0

} // namespace OnlinePlay
