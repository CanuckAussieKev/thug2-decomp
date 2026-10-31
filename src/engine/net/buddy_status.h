#pragma once

#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The buddy list's part before its own code (0x00548100-0x00548560: FriendsManager in buddy_man.h): GP's buddy request
// and login, buddy messages sent, the presence status, and the buddy lists' list types; with the getters the linker
// folded at 0x005480B0-0x005480F0.
namespace online {

extern void** const kBuddyNodeVtable;   // 0x0064CF60: lists::Elem<Buddy> / lists::Foremost<Buddy>
extern void** const kMessageNodeVtable; // 0x0064CF64

// The buddy lists' list types (their vtables hold only the scalar deleting destructor):
// lists::Hunt<Buddy> (0x0064CECC): 0x005480D0 the body (the vtable reset), 0x00548440 the scalar deleting one;
// lists::Hunt<FriendNote> (0x0064CED0): 0x005480E0 / 0x00548470 (the searches' are not kept);
// lists::Elem<Buddy> (0x0064CF60): 0x005484A0 the body (out of its list), 0x005484E0 the scalar deleting one;
// lists::Elem<FriendNote> (0x0064CF64): 0x005484C0 / 0x00548520.
struct BuddyNode : memory::ListNode {
    void destroy_body();
    BuddyNode* teardown(unsigned int flags);
};
struct BuddyMessageNode : memory::ListNode {
    void destroy_body();
    BuddyMessageNode* teardown(unsigned int flags);
};

// The getters folded at one address each (4 bytes: source only), named for an object with the field.
struct FoldedGetters {
    // 0x005480B0: the byte at +0x10 (FriendsManager's logged-in flag, ...).
    bool byte10();
    // 0x005480C0: the word at +0x04 (FriendsManager's profile, the network manager's server app, a park's ..., 203 callers).
    uint32_t word04();
    // 0x005480F0: the word at +0x18 (a skeleton component's skeleton, a node component's node array, a sector's
    // collision, ...).
    uint32_t word18();
    // 0x00553E90 (folded with goals::Objective::fetch_args): the word at +0x1C (the profile roster's current skater,
    // a model component's texture, the tagging goal's ...).
    uint32_t word1c();
};

// The script commands.
bool __cdecl profile_logged_in(script::Struct* params, script::Script* script);  // 0x00548360
bool __cdecl send_buddy_message(script::Struct* params, script::Script* script); // 0x00548370
bool __cdecl set_lobby_status(script::Struct* params, script::Script* script);   // 0x005483E0

} // namespace online
