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

// (The getters folded at 0x005480B0 / 0x005480C0 / 0x005480F0 / 0x00553E90 - the byte at +0x10, the words at +0x04,
// +0x18, +0x1C of whatever object called them - are each object's member read directly.)

// The script commands.
bool profile_logged_in(script::Struct* params, script::Script* script);  // 0x00548360
bool send_buddy_message(script::Struct* params, script::Script* script); // 0x00548370
bool set_lobby_status(script::Struct* params, script::Script* script);   // 0x005483E0

} // namespace online
