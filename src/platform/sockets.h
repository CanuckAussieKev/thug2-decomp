#pragma once

#include <winsock2.h>

// Winsock calls with an IPv4 address (sockaddr_in) where the API takes the generic sockaddr, and buffers of bytes
// where it takes char: the conversions Winsock's C interface wants, in one place.
namespace sockets {

// The address as Winsock's generic one (for the API and the packet hooks).
const sockaddr* generic(const sockaddr_in* address);
sockaddr* generic(sockaddr_in* address);

int bind_to(SOCKET socket, const sockaddr_in& address);                    // bind
int name_of(SOCKET socket, sockaddr_in* address, int* length);              // getsockname
int connect_to(SOCKET socket, const sockaddr_in& address);                  // connect
int send_bytes(SOCKET socket, const void* data, int length, int flags);     // send
int send_bytes_to(SOCKET socket, const void* data, int length, int flags, const sockaddr_in& to); // sendto
int receive_bytes(SOCKET socket, void* buffer, int size, int flags);        // recv
int receive_bytes_from(SOCKET socket, void* buffer, int size, int flags, sockaddr_in* from, int* from_length); // recvfrom
int set_option(SOCKET socket, int level, int name, const void* value, int size); // setsockopt
// inet_ntoa of a host's first address (gethostbyname's hostent).
const char* first_address_text(const hostent* host);

} // namespace sockets
