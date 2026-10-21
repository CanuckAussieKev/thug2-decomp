#pragma once

#include <cstdint>

// NOT ORIGINAL - guards on received network messages, for playing with THUGPRO (Morten1337's THUG2 mod) players
// (register: tools/compat/thugpro/README.md "Run time", TP-R4; record: docs/pending/thugpro-guards.md). A THUGPRO
// player can name things THUG2 hasn't - an animation from THUGPRO's own set, a special item, a game mode, a level -
// and THUG2's handlers look them up without checking. Checked here before the dispatcher runs a message's handlers
// (Net::Dispatcher::DispatchMsg, ours), as THUGPRO's own DLL does in its DispatchMessage hook (the special item and
// animation checks are its "BUGFIX(MORTEN)"s - idea from Morten1337's thugpro_proxy, written here in our own way):
//   - 0x29 a skater's primary animation: one that isn't loaded is played as `default` (THUGPRO's choice).
//   - 0x77 / 0x78 a special item made / destroyed: dropped when its details structure doesn't exist.
//   - 0x33 the start info / 0x34 the game info: a game mode not in mode_info (THUGPRO's race, gold) is played as
//     free skate (netlobby) - THUG2's LoadGameType would leave the game mode empty.
//   - 0x33 the start info / 0x46 a change of level: a level whose load script doesn't exist (THUGPRO's own levels)
//     leaves the server as "the server moved on" does (THUG2 would run a load that can't happen).
// A message shorter than a check reads isn't checked (nothing read past its end) and goes to THUG2's handlers as it
// is. Everything is logged once per name.
namespace Net {
struct MsgHandlerContext;
}

namespace GameNet::Guards {

// -1: dispatch the message as usual (it may have been changed in place); otherwise the dispatch's result, nothing run.
int CheckReceivedMessage(Net::MsgHandlerContext* context);

// What the checks look things up with (the self-tests swap them).
struct Lookups {
    bool (*animationLoaded)(uint32_t animation); // the skater animation set's (THPS6_human) animation is cached
    bool (*structureExists)(uint32_t name);      // a global structure
    bool (*gameModeExists)(uint32_t mode);       // in mode_info
    bool (*scriptExists)(uint32_t name);         // a global script
    int (*leaveServer)(Net::MsgHandlerContext* context); // the "server moved on" path; its handler result
};
const Lookups& GameLookups();
void SetLookups(const Lookups& lookups);

} // namespace GameNet::Guards
