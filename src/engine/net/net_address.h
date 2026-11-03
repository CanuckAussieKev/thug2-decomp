#pragma once

#include <cstdint>

namespace net {

// NOT ORIGINAL - a host's address on the network: its IPv4 address and its port (as the original keeps them).
struct Address {
    uint32_t ip{};
    uint16_t port{};
};

} // namespace net
