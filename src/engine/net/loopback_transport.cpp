// NOT ORIGINAL - the in-process loopback transport (loopback_transport.h).

#include "engine/net/loopback_transport.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "base/core/no_destroy.h"
#include "engine/net/transport.h"

namespace net {

namespace {

// Winsock's numbers for the failures only the loopback reports (the game tests none of them).
constexpr int kErrorAccess = 10013;  // WSAEACCES: a broadcast from a socket not allowed to
constexpr int kErrorInvalid = 10022; // WSAEINVAL: bound twice

constexpr SocketId kFirstSocket = 1;
constexpr uint16_t kFirstEphemeralPort = 49152; // where Windows' dynamic ports start
constexpr size_t kQueueLimit = 256;             // datagrams waiting per socket; more are dropped, as UDP may
// 127.0.0.1 in network order (as it is in memory): the address the datagrams come from.
constexpr uint32_t kLoopbackAddress = 0x0100007F;

struct Datagram {
    std::vector<uint8_t> bytes;
    SocketAddress from;
};

struct Endpoint {
    bool bound = false;
    bool broadcast = false;
    bool connected = false;
    SocketAddress address{}; // as bound (port in network order)
    SocketAddress peer{};    // connect's
    std::deque<Datagram> queue;
};

class LoopbackTransport final : public Transport {
public:
    explicit LoopbackTransport(Transport& names) : names_(&names) {}

    // --- sockets ---
    SocketId open_datagram() override
    {
        const std::scoped_lock lock(mutex_);
        const SocketId socket = next_socket_++;
        sockets_[socket] = Endpoint{};
        return socket;
    }

    int set_non_blocking(SocketId socket) override
    {
        // (every loopback socket is non-blocking: the game makes each one so)
        const std::scoped_lock lock(mutex_);
        return find(socket) ? 0 : fail(kErrorNotSocket);
    }

    int allow_broadcast(SocketId socket) override
    {
        const std::scoped_lock lock(mutex_);
        Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        endpoint->broadcast = true;
        return 0;
    }

    int bind(SocketId socket, const SocketAddress& address) override
    {
        const std::scoped_lock lock(mutex_);
        Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        if (endpoint->bound) {
            return fail(kErrorInvalid);
        }
        return bind_to(*endpoint, address.ip, port_of(address.port));
    }

    int name_of(SocketId socket, SocketAddress* address, int* length) override
    {
        const std::scoped_lock lock(mutex_);
        const Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        if (!endpoint->bound) {
            return fail(kErrorInvalid);
        }
        *address = endpoint->address;
        *length = sizeof(SocketAddress);
        return 0;
    }

    int connect(SocketId socket, const SocketAddress& address) override
    {
        const std::scoped_lock lock(mutex_);
        Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        if (!endpoint->bound) {
            bind_to(*endpoint, kAnyAddress, 0);
        }
        endpoint->peer = address;
        endpoint->connected = true;
        return 0;
    }

    int send(SocketId socket, const void* data, int length, int /*flags*/) override
    {
        const std::scoped_lock lock(mutex_);
        Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        if (!endpoint->connected) {
            return fail(kErrorNotConnected);
        }
        return deliver(*endpoint, data, length, endpoint->peer);
    }

    int send_to(SocketId socket, const void* data, int length, int /*flags*/, const SocketAddress& to) override
    {
        const std::scoped_lock lock(mutex_);
        Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        return deliver(*endpoint, data, length, to);
    }

    int receive(SocketId socket, void* buffer, int size, int /*flags*/) override
    {
        const std::scoped_lock lock(mutex_);
        return take(socket, buffer, size, nullptr);
    }

    int receive_from(SocketId socket, void* buffer, int size, int /*flags*/, SocketAddress* from,
                     int* from_length) override
    {
        const std::scoped_lock lock(mutex_);
        const int received = take(socket, buffer, size, from);
        if (received >= 0 && from_length) {
            *from_length = sizeof(SocketAddress);
        }
        return received;
    }

    int close(SocketId socket) override
    {
        const std::scoped_lock lock(mutex_);
        return sockets_.erase(socket) != 0 ? 0 : fail(kErrorNotSocket);
    }

    int last_error() override { return error(); }

    // --- addresses and host names: no network read, so the real transport's ---
    uint32_t parse_address(const char* text) override { return names_->parse_address(text); }
    const char* address_text(uint32_t address) override { return names_->address_text(address); }
    const HostEntry* find_host(const char* name) override { return names_->find_host(name); }
    const char* host_name(const HostEntry* host) override { return names_->host_name(host); }
    uint32_t host_first_address(const HostEntry* host) override { return names_->host_first_address(host); }

private:
    // Why the calling thread's last call failed (Winsock's is per thread too).
    static int& error()
    {
        thread_local int last = 0;
        return last;
    }
    static int fail(int reason)
    {
        error() = reason;
        return kSocketError;
    }

    Endpoint* find(SocketId socket)
    {
        const auto at = sockets_.find(socket);
        return at == sockets_.end() ? nullptr : &at->second;
    }

    [[nodiscard]] bool port_taken(uint16_t port) const
    {
        return std::ranges::any_of(sockets_, [port](const auto& entry) {
            return entry.second.bound && port_of(entry.second.address.port) == port;
        });
    }

    // The next of the dynamic ports no socket is bound to.
    uint16_t next_free_port()
    {
        for (;;) {
            const uint16_t port = next_port_++;
            if (next_port_ == 0) {
                next_port_ = kFirstEphemeralPort;
            }
            if (!port_taken(port)) {
                return port;
            }
        }
    }

    // Bound to `port` (0: the next free one of the dynamic ports).
    int bind_to(Endpoint& endpoint, uint32_t ip, uint16_t port)
    {
        if (port == 0) {
            port = next_free_port();
        } else if (port_taken(port)) {
            return fail(kErrorAddressInUse);
        }
        endpoint.address = ipv4(ip, port);
        endpoint.bound = true;
        return 0;
    }

    // A datagram from `sender` to every socket bound to `to`'s port (one, or each one for a broadcast): the length
    // sent, as UDP answers whether or not anyone receives it.
    int deliver(Endpoint& sender, const void* data, int length, const SocketAddress& to)
    {
        if (length < 0) {
            return fail(kErrorInvalid);
        }
        if (to.ip == kBroadcastAddress && !sender.broadcast) {
            return fail(kErrorAccess);
        }
        if (!sender.bound) {
            bind_to(sender, kAnyAddress, 0);
        }
        const SocketAddress from = ipv4(kLoopbackAddress, port_of(sender.address.port));
        const std::span<const uint8_t> bytes(static_cast<const uint8_t*>(data), static_cast<size_t>(length));
        for (auto& [id, endpoint] : sockets_) {
            if (endpoint.bound && endpoint.address.port == to.port && endpoint.queue.size() < kQueueLimit) {
                endpoint.queue.push_back({.bytes = {bytes.begin(), bytes.end()}, .from = from});
                if (to.ip != kBroadcastAddress) {
                    break;
                }
            }
        }
        return length;
    }

    // The next datagram for `socket` (from its peer only, once connected) into `buffer`.
    int take(SocketId socket, void* buffer, int size, SocketAddress* from)
    {
        Endpoint* const endpoint = find(socket);
        if (!endpoint) {
            return fail(kErrorNotSocket);
        }
        while (!endpoint->queue.empty()) {
            Datagram datagram = std::move(endpoint->queue.front());
            endpoint->queue.pop_front();
            if (endpoint->connected && datagram.from.port != endpoint->peer.port) {
                continue; // (a connected datagram socket hears its peer alone)
            }
            if (from) {
                *from = datagram.from;
            }
            const size_t room = size > 0 ? static_cast<size_t>(size) : 0;
            const size_t copied = std::min(room, datagram.bytes.size());
            if (copied > 0) {
                std::memcpy(buffer, datagram.bytes.data(), copied);
            }
            if (copied < datagram.bytes.size()) {
                return fail(kErrorMessageSize); // (the rest is lost, as Winsock does)
            }
            return static_cast<int>(copied);
        }
        return fail(kErrorWouldBlock);
    }

    Transport* names_;
    std::mutex mutex_;
    std::map<SocketId, Endpoint> sockets_;
    SocketId next_socket_ = kFirstSocket;
    uint16_t next_port_ = kFirstEphemeralPort;
};

} // namespace

Transport& loopback_transport(Transport& names)
{
    static core::NoDestroy<LoopbackTransport> made(names);
    return made.get();
}

} // namespace net
