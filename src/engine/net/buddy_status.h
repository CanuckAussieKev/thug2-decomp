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

// (The buddy lists' list types: lists::Hunt<Buddy>'s 0x005480D0 / 0x00548440 and lists::Hunt<FriendNote>'s 0x005480E0 /
// 0x00548470 only reset their tables - not kept; lists::Elem<Buddy>'s 0x005484A0 / 0x005484E0 and
// lists::Elem<FriendNote>'s 0x005484C0 / 0x00548520 take the node out of its list (and free it): memory::ListNode.)

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
