#pragma once

#include <cstdint>

// NOT ORIGINAL - what the engine asks of the online session (game/online's online::Hub, the network manager) without
// knowing it: whether a network game is on, and the client app through which this machine's player of an object
// talks to the host. The session installs its answers once at start-up (online::install_session_queries, from
// app/startup.cpp where the original set the objects' copy of the network manager's pointer), before any object is
// made. Each answer is the session's own function, called where the original called it: one indirect call in place
// of the original's direct one, no call added. Not installed: no network game, no player.
namespace net {

class Guest;

struct SessionQueries {
    // online::Hub::in_net_game (0x0044E4F0) on the objects' copy of the network manager's pointer.
    bool (*in_net_game)() = nullptr;
    // The player whose object id is `object_id` (0x0053AEA0): whether it is this machine's (0x00543300); then
    // `*app` is its skater's client app (0x005434B0, 0x00532EA0).
    bool (*own_player_client)(uint32_t object_id, Guest** app) = nullptr;
};

void install_session_queries(const SessionQueries& queries);

// Whether a network game is on (online::Hub::in_net_game).
bool in_net_game();
// Whether the player of object `object_id` is this machine's; `*app` its client app when it is.
bool own_player_client(uint32_t object_id, Guest** app);

} // namespace net
