#pragma once

namespace net {
class Transport;
} // namespace net

// NOT ORIGINAL - Winsock's net::Transport (engine/net/transport.h; app/ installs it first): each method is the Winsock
// call the game made at that place (socket, ioctlsocket, setsockopt, bind, sendto, recvfrom, gethostbyname...), with
// the same arguments. The one place the game's code calls Winsock (tools/net_calls.test.ts) - but the GameSpy SDK,
// which keeps its own sockets (platform/gamespy.cpp).
namespace net::winsock {

Transport& transport();

} // namespace net::winsock
