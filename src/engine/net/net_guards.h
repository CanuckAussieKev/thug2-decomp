#pragma once

#include <cstdint>

// NOT ORIGINAL - guards on received network messages, for playing with THUGPRO (Morten1337's THUG2 mod) players
// (register: tools/pipeline/compat/thugpro/README.md "Run time", TP-R8). A THUGPRO
// player can name things THUG2 hasn't - an animation from THUGPRO's own set, a special item, a game mode, a level -
// and THUG2's handlers look them up without checking. Checked here before the dispatcher runs a message's handlers
// (NetLink::Router::dispatch_msg, ours), as THUGPRO's own DLL does in its DispatchMessage hook (the special item and
// animation checks are its "BUGFIX(MORTEN)"s - idea from Morten1337's thugpro_proxy, written here in our own way):
//   - 0x29 a skater's primary animation: one that isn't loaded is played as `default` (THUGPRO's choice).
//   - 0x77 / 0x78 a special item made / destroyed: dropped when its details structure doesn't exist.
//   - 0x33 the start info / 0x34 the game info: a game mode not in mode_info (THUGPRO's race, gold) is played as
//     free skate (netlobby) - THUG2's apply_match_style would leave the game mode empty.
//   - 0x33 the start info / 0x46 a change of level: a level whose load script doesn't exist (THUGPRO's own levels)
//     leaves the server as "the server moved on" does (THUG2 would run a load that can't happen).
// A message shorter than a check reads isn't checked (nothing read past its end) and goes to THUG2's handlers as it
// is. Everything is logged once per name.
namespace net {
struct InboundMsgCtx;
}

namespace online::guards {

// -1: dispatch the message as usual (it may have been changed in place); otherwise the dispatch's result, nothing run.
int check_received_message(net::InboundMsgCtx* context);

// What the checks look things up with (the self-tests swap them).
struct Lookups {
    bool (*animation_loaded_)(uint32_t animation); // the skater animation set's (THPS6_human) animation is cached
    bool (*structure_exists_)(uint32_t name);      // a global structure
    bool (*game_mode_exists_)(uint32_t mode);       // in mode_info
    bool (*script_exists_)(uint32_t name);         // a global script
    int (*leave_server_)(net::InboundMsgCtx* context); // the "server moved on" path; its handler result
};
const Lookups& game_lookups();
void set_lookups(const Lookups& lookups);

} // namespace online::Guards
