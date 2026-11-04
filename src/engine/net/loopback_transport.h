#pragma once

// NOT ORIGINAL - an in-process net::Transport (transport.h) for local protocol tests: thug2-decomp.ini
// [debug] net_loopback=1 (default 0: Winsock's, unchanged) makes app/ install it in place of Winsock's. Nothing reaches
// a network: the game's sockets are queues in this process, and every datagram sent to a port lands in the queue of
// the socket bound to that port here (a broadcast - LAN discovery - in every socket bound to it), whatever the
// address. Two endpoints in the same process (a server and a client app, as a hosted game makes) talk through it as
// they would over UDP: non-blocking, datagrams whole or not at all, a datagram too long for the receive buffer cut
// and reported (kErrorMessageSize), a full queue dropping what comes next.
//
// The address and host-name lookups (parse_address, address_text, find_host...) are forwarded to the transport it
// stands over: they read no network. The GameSpy SDK's own sockets aren't behind any Transport, and the socket values
// the game hands it (a hosted server's reports, a NAT negotiation) are this transport's ids - not Winsock sockets - so
// the online modes don't work over it; it is for the game's own protocol (net_app, net_client, net_server).
namespace net {

class Transport;

// The loopback over `names` (made on first use, over the transport given then; never destroyed).
Transport& loopback_transport(Transport& names);

} // namespace net
