// NOT ORIGINAL - the online game's state the network files share: the settings (team mode, skill level, most players), the session's times and flags, the lobby's queued packets, the online hub (net_game_state.h): made on first use, never destroyed.

#include "engine/net/net_game_state.h"

#include "engine/core/no_destroy.h"

namespace online {

NetGameState& net_game_state()
{
    static core::NoDestroy<NetGameState> state;
    return state.get();
}

} // namespace online
