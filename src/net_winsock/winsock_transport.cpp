// NOT ORIGINAL - Winsock's net::Transport (winsock_transport.h): each method is the Winsock call the game made at that
// place, unchanged - the same call with the same arguments (the original's network library: its app's socket made,
// bound and sent on 0x00456BA0-0x00457490, its client's connect and receive 0x00459F40 / 0x0045A300, its server's
// receive 0x0045BBD0, its manager's addresses 0x004548F0 / 0x00454910 / 0x00455270; the online session's and
// WinMain's address and host lookups).
//
// Winsock itself is started by the GameSpy SDK (its SocketStartUp: WSAStartup, at WinMain's availability check), as
// it was in the original: nothing here starts or stops it.

#include "net_winsock/winsock_transport.h"

#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "base/core/no_destroy.h"
#include "base/core/raw_view.h"
#include "engine/net/transport.h"

namespace net::winsock {

namespace {

// net::SocketAddress is sockaddr_in's layout (the game copies it whole into its records).
static_assert(sizeof(SocketAddress) == sizeof(sockaddr_in));
static_assert(offsetof(SocketAddress, family) == offsetof(sockaddr_in, sin_family));
static_assert(offsetof(SocketAddress, port) == offsetof(sockaddr_in, sin_port));
static_assert(offsetof(SocketAddress, ip) == offsetof(sockaddr_in, sin_addr));
static_assert(offsetof(SocketAddress, zero) == offsetof(sockaddr_in, sin_zero));
static_assert(kFamilyIpv4 == AF_INET);
static_assert(kAnyAddress == INADDR_ANY);
static_assert(kNoAddress == INADDR_NONE);
static_assert(kBroadcastAddress == INADDR_BROADCAST);
// The socket's value is Winsock's (a 32-bit build), and the errors are Winsock's numbers.
static_assert(sizeof(SOCKET) == sizeof(SocketId));
static_assert(kSocketError == SOCKET_ERROR);
static_assert(kErrorWouldBlock == WSAEWOULDBLOCK);
static_assert(kErrorInProgress == WSAEINPROGRESS);
static_assert(kErrorNotSocket == WSAENOTSOCK);
static_assert(kErrorMessageSize == WSAEMSGSIZE);
static_assert(kErrorAddressInUse == WSAEADDRINUSE);
static_assert(kErrorNotConnected == WSAENOTCONN);

SOCKET native(SocketId socket) noexcept { return static_cast<SOCKET>(socket); }
// The address as Winsock's generic one (sockaddr_in is the IPv4 form of sockaddr: Winsock reads the family and takes
// the rest by it).
const sockaddr* generic(const SocketAddress* address) noexcept { return raw::as<sockaddr>(address); }
sockaddr* generic(SocketAddress* address) noexcept { return raw::as<sockaddr>(address); }
// A host's entry and Winsock's hostent: the same pointer.
const hostent* native(const HostEntry* host) noexcept { return raw::as<hostent>(host); }
const HostEntry* handle(const hostent* host) noexcept { return raw::as<HostEntry>(host); }

class WinsockTransport final : public Transport {
public:
    SocketId open_datagram() override { return static_cast<SocketId>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)); }

    int set_non_blocking(SocketId socket) override
    {
        u_long on = 1;
        return ioctlsocket(native(socket), FIONBIO, &on);
    }

    int allow_broadcast(SocketId socket) override
    {
        // (the original passes its u_long 1, the one it gave ioctlsocket)
        u_long on = 1;
        return setsockopt(native(socket), SOL_SOCKET, SO_BROADCAST, raw::chars(&on), sizeof(on));
    }

    int bind(SocketId socket, const SocketAddress& address) override
    {
        return ::bind(native(socket), generic(&address), sizeof(address));
    }

    int name_of(SocketId socket, SocketAddress* address, int* length) override
    {
        return getsockname(native(socket), generic(address), length);
    }

    int connect(SocketId socket, const SocketAddress& address) override
    {
        return ::connect(native(socket), generic(&address), sizeof(address));
    }

    int send(SocketId socket, const void* data, int length, int flags) override
    {
        return ::send(native(socket), raw::chars(data), length, flags);
    }

    int send_to(SocketId socket, const void* data, int length, int flags, const SocketAddress& to) override
    {
        return sendto(native(socket), raw::chars(data), length, flags, generic(&to), sizeof(to));
    }

    int receive(SocketId socket, void* buffer, int size, int flags) override
    {
        return recv(native(socket), raw::chars(buffer), size, flags);
    }

    int receive_from(SocketId socket, void* buffer, int size, int flags, SocketAddress* from,
                     int* from_length) override
    {
        return recvfrom(native(socket), raw::chars(buffer), size, flags, generic(from), from_length);
    }

    int close(SocketId socket) override { return closesocket(native(socket)); }

    int last_error() override { return WSAGetLastError(); }

    uint32_t parse_address(const char* text) override { return static_cast<uint32_t>(inet_addr(text)); }

    const char* address_text(uint32_t address) override
    {
        in_addr in{};
        in.s_addr = address;
        return inet_ntoa(in);
    }

    const HostEntry* find_host(const char* name) override { return handle(gethostbyname(name)); }

    const char* host_name(const HostEntry* host) override { return native(host)->h_name; }

    uint32_t host_first_address(const HostEntry* host) override
    {
        uint32_t address = 0;
        std::memcpy(&address, *native(host)->h_addr_list, sizeof(address));
        return address;
    }
};

} // namespace

Transport& transport()
{
    static core::NoDestroy<WinsockTransport> transport;
    return transport.get();
}

} // namespace net::winsock
