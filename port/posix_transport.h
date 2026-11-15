#pragma once

// NOT ORIGINAL - the macOS (arm64) port's net::Transport (engine/net/transport.h) over BSD sockets: it stands in for
// src/net_winsock/winsock_transport.h (the same calls, spelled as POSIX spells them), and keeps Winsock's results -
// the all-ones socket, the error numbers, sockaddr_in's 16-byte address - because the game and the GameSpy SDK read
// them. app/ installs it where it installs the Winsock one.
namespace net {
class Transport;
} // namespace net

namespace net::winsock {

Transport& transport();

} // namespace net::winsock
