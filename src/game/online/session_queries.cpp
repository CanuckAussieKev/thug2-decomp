// The online session's answers to the engine (see session_queries.h).

#include "game/online/session_queries.h"

#include <cstdint>

#include "base/core/down_cast.h"
#include "engine/net/net_client.h"
#include "engine/net/session_query.h"
#include "game/online/gamenet_manager.h"
#include "game/online/net_game_state.h"
#include "game/online/player_info.h"

namespace online {

namespace {

// The objects' own copy of the network manager's pointer (set once at start-up).
Hub*& objects_hub()
{
    static Hub* hub{};
    return hub;
}

bool objects_in_net_game() { return objects_hub()->in_net_game(); }

// The network manager (0x007CCE14) and its players: the player with an object id (0x0053AEA0), whether it's this
// machine's (0x00543300), its skater's index (0x005434B0) and that index's client app (0x00532EA0).
bool own_player_client(uint32_t object_id, net::Guest** app)
{
    Hub* manager = online_hub();
    PlayerRecord* player = manager->find_player_slot_by_obj(static_cast<uint16_t>(object_id));
    if (!player || player->is_own_player() == 0) {
        return false;
    }
    *app = core::down_cast<net::Guest>(manager->fetch_patron(static_cast<int>(player->get_skater_view_slot())));
    return true;
}

} // namespace

void install_session_queries(Hub* hub)
{
    objects_hub() = hub;
    net::install_session_queries({.in_net_game = &objects_in_net_game, .own_player_client = &own_player_client});
}

} // namespace online
