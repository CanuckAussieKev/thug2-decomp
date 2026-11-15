// NOT ORIGINAL - the macOS (arm64) port's stand-in for LZHAM, THUGPRO's compression method of the network library
// (engine/net/net_lzham.h). The fetched codec's own source is written for MSVC's Win32 API - its macOS branch is MSVC
// inline assembly, which clang does not accept - so a portable build compresses nothing: THUGPRO's peers stay
// reachable, their messages sent as they are (the method's own id is never written). port/README.md has it.

#include "engine/net/net_lzham.h"

namespace net::lzham {

std::size_t compress(const std::uint8_t* /*in*/, std::size_t /*size*/, std::uint8_t* /*out*/, std::size_t /*capacity*/)
{
    return 0; // (nothing compressed: the caller sends the message as it is)
}

bool decompress(const std::uint8_t* /*in*/, std::size_t /*size*/, std::uint8_t* /*out*/, std::size_t /*capacity*/,
                std::size_t* /*out_size*/)
{
    return false;
}

} // namespace net::lzham
