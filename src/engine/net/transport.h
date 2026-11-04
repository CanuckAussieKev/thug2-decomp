#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// NOT ORIGINAL - net::Transport: the network sockets and the address and host-name lookups the game's network
// library (engine/net: NetLink's apps, client, server and manager) and the online session (game/online) make. app/
// installs Winsock's (net_winsock/winsock_transport.h) at start-up, before any of the original's code runs; with
// thug2-decomp.ini [debug] net_loopback=1 it installs the in-process loopback over it instead
// (engine/net/loopback_transport.h: local protocol tests, no network). Not installed, a call dereferences null.
//
// The methods are the Winsock calls the game made, one for one, with the same arguments and results: a UDP socket
// made, made non-blocking, allowed to broadcast (LAN discovery), bound, named, connected, sent on, received on and
// closed; the last error; an address parsed or written as text; a host looked up by name. The grain is a socket, a
// send or a receive of one datagram - never a byte. A failed call answers kSocketError (or kNoSocket) and leaves its
// reason for last_error(), in Winsock's numbers (the k*Error constants below), as the game tests them.
//
// The GameSpy SDK isn't behind it: the SDK opens, starts (WSAStartup) and uses its own sockets (chat, query and
// reporting, NAT negotiation, the server list, presence, HTTP) through its own platform layer, linked with the
// platform's glue (platform/gamespy.cpp). Twice the game gives the SDK one of its own sockets - the hosting server's,
// for its reports (peerStartReportingWithSocket), and for a NAT negotiation (NNBeginNegotiationWithSocket) - and the
// SDK then sends on it directly: the SocketId is Winsock's SOCKET value for that.
namespace net {

// A socket: the transport's own value - Winsock's SOCKET (what the original kept at AppHost +0x1428 and gave the
// GameSpy SDK). kNoSocket is the failed open (INVALID_SOCKET).
using SocketId = uint32_t;
constexpr SocketId kNoSocket = 0xFFFFFFFF;
// What a failed call answers (SOCKET_ERROR).
constexpr int kSocketError = -1;

// last_error()'s reasons the game tests (Winsock's numbers).
constexpr int kErrorWouldBlock = 10035;    // WSAEWOULDBLOCK: nothing to read / not done yet (non-blocking)
constexpr int kErrorInProgress = 10036;    // WSAEINPROGRESS
constexpr int kErrorNotSocket = 10038;     // WSAENOTSOCK
constexpr int kErrorMessageSize = 10040;   // WSAEMSGSIZE: a datagram longer than the buffer (the rest dropped)
constexpr int kErrorAddressInUse = 10048;  // WSAEADDRINUSE: the port is bound already
constexpr int kErrorNotConnected = 10057;  // WSAENOTCONN

// An IPv4 address and port, laid out as Winsock's sockaddr_in (16 bytes - the game copies it whole into its records
// and hands it to the GameSpy SDK as one): the family, the port in network order, the address in network order (as
// it is in memory), eight zero bytes.
constexpr size_t kSocketAddressPadding = 8;
struct SocketAddress {
    uint16_t family = 0;
    uint16_t port = 0; // network order: port_of / network_port
    uint32_t ip = 0;   // network order (the bytes as written in dotted text)
    std::array<uint8_t, kSocketAddressPadding> zero{};
};
constexpr size_t kSocketAddressSize = 16; // sockaddr_in's size
static_assert(sizeof(SocketAddress) == kSocketAddressSize);
constexpr uint16_t kFamilyIpv4 = 2; // AF_INET
constexpr uint32_t kAnyAddress = 0;            // INADDR_ANY
constexpr uint32_t kNoAddress = 0xFFFFFFFF;    // INADDR_NONE: what parse_address answers for text that isn't one
constexpr uint32_t kBroadcastAddress = 0xFFFFFFFF; // INADDR_BROADCAST (the same bits)

// A port in network order and back (htons / ntohs: the two bytes swapped).
constexpr unsigned kByteBits = 8;
constexpr uint16_t network_port(uint16_t port)
{
    const unsigned value = port;
    return static_cast<uint16_t>((value >> kByteBits) | (value << kByteBits));
}
constexpr uint16_t port_of(uint16_t network) { return network_port(network); }
// An IPv4 socket address for `ip` (network order) and `port` (host order).
constexpr SocketAddress ipv4(uint32_t ip, uint16_t port)
{
    return {.family = kFamilyIpv4, .port = network_port(port), .ip = ip};
}

// A host found by name (Winsock's hostent: valid until the thread's next lookup, as gethostbyname's).
struct HostEntry;

class Transport {
public:
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

    // --- sockets ---
    // A UDP socket (socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)), or kNoSocket.
    virtual SocketId open_datagram() = 0;
    // Made non-blocking (ioctlsocket FIONBIO on): a receive with nothing waiting fails with kErrorWouldBlock.
    virtual int set_non_blocking(SocketId socket) = 0;
    // Allowed to send to the broadcast address (setsockopt SOL_SOCKET SO_BROADCAST on).
    virtual int allow_broadcast(SocketId socket) = 0;
    virtual int bind(SocketId socket, const SocketAddress& address) = 0;
    // The address it is bound to (getsockname): `length` in, the bytes written out.
    virtual int name_of(SocketId socket, SocketAddress* address, int* length) = 0;
    // The default peer of send / receive (a datagram socket's connect: nothing is sent).
    virtual int connect(SocketId socket, const SocketAddress& address) = 0;
    // A datagram to the connected peer / to `to`: the bytes sent, or kSocketError.
    virtual int send(SocketId socket, const void* data, int length, int flags) = 0;
    virtual int send_to(SocketId socket, const void* data, int length, int flags, const SocketAddress& to) = 0;
    // The next datagram into `buffer`: its length, or kSocketError (kErrorWouldBlock: none waiting). receive_from
    // also gives its sender (`from_length` in, the bytes written out).
    virtual int receive(SocketId socket, void* buffer, int size, int flags) = 0;
    virtual int receive_from(SocketId socket, void* buffer, int size, int flags, SocketAddress* from,
                             int* from_length) = 0;
    virtual int close(SocketId socket) = 0;
    // Why the calling thread's last call failed (WSAGetLastError).
    virtual int last_error() = 0;

    // --- addresses and host names ---
    // Dotted text as an address in network order (inet_addr: kNoAddress if it isn't one).
    virtual uint32_t parse_address(const char* text) = 0;
    // An address in network order as dotted text (inet_ntoa: the thread's buffer, until its next call).
    virtual const char* address_text(uint32_t address) = 0;
    // The host by name (gethostbyname), or null.
    virtual const HostEntry* find_host(const char* name) = 0;
    // Its official name (h_name) and its first address (h_addr_list[0], network order).
    virtual const char* host_name(const HostEntry* host) = 0;
    virtual uint32_t host_first_address(const HostEntry* host) = 0;

protected:
    ~Transport() = default;
};

Transport& transport();
void install_transport(Transport& transport);

} // namespace net
