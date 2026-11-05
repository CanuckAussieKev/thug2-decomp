// The engine's questions to the online session (see session_query.h).

#include "engine/net/session_query.h"

#include <cstdint>

namespace net {

namespace {

SessionQueries& installed_queries()
{
    static SessionQueries queries;
    return queries;
}

} // namespace

void install_session_queries(const SessionQueries& queries) { installed_queries() = queries; }

bool in_net_game() { return installed_queries().in_net_game != nullptr && installed_queries().in_net_game(); }

bool own_player_client(uint32_t object_id, Guest** app)
{
    return installed_queries().own_player_client != nullptr && installed_queries().own_player_client(object_id, app);
}

bool manager_in_net_game()
{
    return installed_queries().manager_in_net_game != nullptr && installed_queries().manager_in_net_game();
}

AppHost* client_app(int index)
{
    return installed_queries().client_app != nullptr ? installed_queries().client_app(index) : nullptr;
}

} // namespace net
