// NOT ORIGINAL - the macOS (arm64) port's network backend: net::Transport (engine/net/transport.h) over BSD sockets. It
// stands in for src/net_winsock/winsock_transport.cpp, whose calls are the same ones with different spellings
// (closesocket -> close, ioctlsocket -> fcntl). The results keep Winsock's shapes: kNoSocket is all ones, the errors
// are Winsock's numbers (the game tests them), and SocketAddress is sockaddr_in's own 16 bytes - so the GameSpy SDK,
// which the game hands its sockets to, gets the same values it does on Windows.
//
// IPv4 only, as the game's Winsock use is: the address functions are the old inet_addr / inet_ntoa / gethostbyname.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "engine/net/transport.h"
#include "net_bsd/bsd_transport.h"

namespace net {

// The transport: BSD sockets behind net::Transport.
class PosixTransport final : public Transport {
public:
    SocketId open_datagram() override;
    int set_non_blocking(SocketId socket) override;
    int allow_broadcast(SocketId socket) override;
    int bind(SocketId socket, const SocketAddress& address) override;
    int name_of(SocketId socket, SocketAddress* address, int* length) override;
    int connect(SocketId socket, const SocketAddress& address) override;
    int send(SocketId socket, const void* data, int length, int flags) override;
    int send_to(SocketId socket, const void* data, int length, int flags, const SocketAddress& to) override;
    int receive(SocketId socket, void* buffer, int size, int flags) override;
    int receive_from(SocketId socket, void* buffer, int size, int flags, SocketAddress* from,
                     int* from_length) override;
    int close(SocketId socket) override;
    int last_error() override;
    uint32_t parse_address(const char* text) override;
    const char* address_text(uint32_t address) override;
    const HostEntry* find_host(const char* name) override;
    const char* host_name(const HostEntry* host) override;
    uint32_t host_first_address(const HostEntry* host) override;
};

namespace {

thread_local int g_last_error = 0;

// POSIX's errno in Winsock's numbers (the game's tests and messages).
int winsock_error(int error)
{
    switch (error) {
    case 0:
        return 0;
    case EWOULDBLOCK:
        return kErrorWouldBlock; // WSAEWOULDBLOCK
    case EINPROGRESS:
        return kErrorInProgress; // WSAEINPROGRESS
    case EBADF:
        return kErrorNotSocket; // WSAENOTSOCK
    case EMSGSIZE:
        return kErrorMessageSize; // WSAEMSGSIZE
    case EADDRINUSE:
        return kErrorAddressInUse; // WSAEADDRINUSE
    case ENOTCONN:
        return kErrorNotConnected; // WSAENOTCONN
    case EADDRNOTAVAIL:
        return 10049; // WSAEADDRNOTAVAIL
    case ECONNREFUSED:
        return 10061; // WSAECONNREFUSED
    case EACCES:
        return 10013; // WSAEACCES
    case ENETUNREACH:
        return 10051; // WSAENETUNREACH
    case ENETDOWN:
        return 10050; // WSAENETDOWN
    case ETIMEDOUT:
        return 10060; // WSAETIMEDOUT
    case EAFNOSUPPORT:
        return 10047; // WSAEAFNOSUPPORT
    default:
        return 10022; // WSAEINVAL
    }
}

int fail(int error)
{
    g_last_error = winsock_error(error);
    return kSocketError;
}

} // namespace

SocketId PosixTransport::open_datagram()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return kNoSocket;
    }
    return static_cast<SocketId>(fd);
}

int PosixTransport::set_non_blocking(SocketId socket)
{
    const int fd = static_cast<int>(socket);
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return fail(errno);
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ? fail(errno) : 0;
}

int PosixTransport::allow_broadcast(SocketId socket)
{
    const int on = 1;
    return setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0 ? fail(errno) : 0;
}

int PosixTransport::bind(SocketId socket, const SocketAddress& address)
{
    sockaddr_in local;
    std::memcpy(&local, &address, sizeof(local));
    return ::bind(static_cast<int>(socket), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0 ? fail(errno)
                                                                                                          : 0;
}

int PosixTransport::name_of(SocketId socket, SocketAddress* address, int* length)
{
    sockaddr_in local;
    std::memset(&local, 0, sizeof(local));
    socklen_t size = sizeof(local);
    if (getsockname(static_cast<int>(socket), reinterpret_cast<sockaddr*>(&local), &size) < 0) {
        return fail(errno);
    }
    if (address != nullptr) {
        std::memcpy(address, &local, sizeof(SocketAddress));
    }
    if (length != nullptr) {
        *length = static_cast<int>(size);
    }
    return 0;
}

int PosixTransport::connect(SocketId socket, const SocketAddress& address)
{
    sockaddr_in remote;
    std::memcpy(&remote, &address, sizeof(remote));
    return ::connect(static_cast<int>(socket), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) < 0
               ? fail(errno)
               : 0;
}

int PosixTransport::send(SocketId socket, const void* data, int length, int flags)
{
    const ssize_t sent = ::send(static_cast<int>(socket), data, static_cast<size_t>(length), flags);
    return sent < 0 ? fail(errno) : static_cast<int>(sent);
}

int PosixTransport::send_to(SocketId socket, const void* data, int length, int flags, const SocketAddress& to)
{
    sockaddr_in remote;
    std::memcpy(&remote, &to, sizeof(remote));
    const ssize_t sent = ::sendto(static_cast<int>(socket), data, static_cast<size_t>(length), flags,
                                  reinterpret_cast<const sockaddr*>(&remote), sizeof(remote));
    return sent < 0 ? fail(errno) : static_cast<int>(sent);
}

int PosixTransport::receive(SocketId socket, void* buffer, int size, int flags)
{
    const ssize_t got = ::recv(static_cast<int>(socket), buffer, static_cast<size_t>(size), flags);
    return got < 0 ? fail(errno) : static_cast<int>(got);
}

int PosixTransport::receive_from(SocketId socket, void* buffer, int size, int flags, SocketAddress* from,
                                 int* from_length)
{
    sockaddr_in remote;
    std::memset(&remote, 0, sizeof(remote));
    socklen_t length = sizeof(remote);
    const ssize_t got = ::recvfrom(static_cast<int>(socket), buffer, static_cast<size_t>(size), flags,
                                   reinterpret_cast<sockaddr*>(&remote), &length);
    if (got < 0) {
        return fail(errno);
    }
    if (from != nullptr) {
        std::memcpy(from, &remote, sizeof(SocketAddress));
    }
    if (from_length != nullptr) {
        *from_length = static_cast<int>(length);
    }
    return static_cast<int>(got);
}

int PosixTransport::close(SocketId socket) { return ::close(static_cast<int>(socket)) < 0 ? fail(errno) : 0; }

int PosixTransport::last_error() { return g_last_error; }

uint32_t PosixTransport::parse_address(const char* text)
{
    if (text == nullptr) {
        return kNoAddress;
    }
    // (inet_addr's own rule: four dotted parts, or a single number; anything else is INADDR_NONE)
    struct in_addr address;
    if (inet_pton(AF_INET, text, &address) == 1) {
        return address.s_addr;
    }
    return kNoAddress;
}

const char* PosixTransport::address_text(uint32_t address)
{
    // (inet_ntoa's static buffer: the game copies the text out as it goes)
    struct in_addr value;
    value.s_addr = address;
    static thread_local char text[INET_ADDRSTRLEN];
    return inet_ntop(AF_INET, &value, text, sizeof(text)) != nullptr ? text : "0.0.0.0";
}

const HostEntry* PosixTransport::find_host(const char* name)
{
    if (name == nullptr) {
        return nullptr;
    }
    hostent* host = gethostbyname(name);
    if (host == nullptr) {
        g_last_error = 11001; // WSAHOST_NOT_FOUND
        return nullptr;
    }
    return reinterpret_cast<const HostEntry*>(host);
}

const char* PosixTransport::host_name(const HostEntry* host) { return reinterpret_cast<const hostent*>(host)->h_name; }

uint32_t PosixTransport::host_first_address(const HostEntry* host)
{
    const hostent* entry = reinterpret_cast<const hostent*>(host);
    if (entry->h_addr_list == nullptr || entry->h_addr_list[0] == nullptr) {
        return kNoAddress;
    }
    uint32_t address = 0;
    std::memcpy(&address, entry->h_addr_list[0], sizeof(address));
    return address;
}

} // namespace net

namespace net::winsock {

Transport& transport()
{
    // (the layer keeps its name: it is the transport the exe installs - Winsock's on Windows, BSD sockets' here)
    static net::PosixTransport the_transport;
    return the_transport;
}

} // namespace net::winsock
