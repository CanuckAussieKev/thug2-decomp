#pragma once

// NOT ORIGINAL - what the online session gives the network transport (engine/net), set once at start-up, before any of
// the original's code runs: the THUGPRO peer mode's question (net_compress.h) and the check on received messages
// (net_dispatcher.h, net_guards.h). The transport knows nothing of the session; without this call both are off.
namespace online {

void install_transport_hooks();

} // namespace online
