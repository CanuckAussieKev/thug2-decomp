// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_ntoa: what the game uses
#include "platform/sockets.h"

#include <cstring>

#include "engine/core/raw_view.h"

namespace sockets {

// (sockaddr_in is the IPv4 form of sockaddr: Winsock reads the family and takes the rest by it)
const sockaddr* generic(const sockaddr_in* address) { return raw::as<sockaddr>(address); }
sockaddr* generic(sockaddr_in* address) { return raw::as<sockaddr>(address); }

int bind_to(SOCKET socket, const sockaddr_in& address) { return bind(socket, generic(&address), sizeof(address)); }

int name_of(SOCKET socket, sockaddr_in* address, int* length) { return getsockname(socket, generic(address), length); }

int connect_to(SOCKET socket, const sockaddr_in& address)
{
    return connect(socket, generic(&address), sizeof(address));
}

int send_bytes(SOCKET socket, const void* data, int length, int flags)
{
    return send(socket, raw::chars(data), length, flags);
}

int send_bytes_to(SOCKET socket, const void* data, int length, int flags, const sockaddr_in& to)
{
    return sendto(socket, raw::chars(data), length, flags, generic(&to), sizeof(to));
}

int receive_bytes(SOCKET socket, void* buffer, int size, int flags)
{
    return recv(socket, raw::chars(buffer), size, flags);
}

int receive_bytes_from(SOCKET socket, void* buffer, int size, int flags, sockaddr_in* from, int* from_length)
{
    return recvfrom(socket, raw::chars(buffer), size, flags, generic(from), from_length);
}

int set_option(SOCKET socket, int level, int name, const void* value, int size)
{
    return setsockopt(socket, level, name, raw::chars(value), size);
}

const char* first_address_text(const hostent* host)
{
    in_addr address{};
    std::memcpy(&address, *host->h_addr_list, sizeof(address));
    return inet_ntoa(address);
}

} // namespace sockets
