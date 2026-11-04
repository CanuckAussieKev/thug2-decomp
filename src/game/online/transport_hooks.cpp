// The online session's hooks into the network transport (see transport_hooks.h).

#include "game/online/transport_hooks.h"

#include "engine/net/net_compress.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_guards.h"
#include "engine/net/thugpro_peer.h"

namespace online {

void install_transport_hooks()
{
    net::set_peer_mode_query(&thugpro::peer_mode);
    net::set_received_message_check(&guards::check_received_message);
}

} // namespace online
