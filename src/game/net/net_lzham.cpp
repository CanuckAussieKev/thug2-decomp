// NOT ORIGINAL - LZHAM compression for the network library (method 3, THUGPRO's). See net_lzham.h.

#include "game/net/net_lzham.h"

#include <cstring>

#include <lzham_static_lib.h>

namespace Net::Lzham {

// THUGPRO's parameters (Net/Encoding.cpp in Morten1337's thugpro_proxy): everything zero but the dictionary, the
// level and no helper threads - the table update settings at their defaults, which both ends must share.
size_t Compress(const uint8_t* in, size_t size, uint8_t* out, size_t capacity)
{
    lzham_compress_params params;
    std::memset(&params, 0, sizeof(params));
    params.m_struct_size = sizeof(params);
    params.m_dict_size_log2 = kDictionarySizeLog2;
    params.m_max_helper_threads = 0;
    params.m_level = LZHAM_COMP_LEVEL_UBER;
    size_t written = capacity;
    lzham_uint32 adler = 0;
    if (lzham_compress_memory(&params, out, &written, in, size, &adler) != LZHAM_COMP_STATUS_SUCCESS)
        return 0;
    return written <= capacity ? written : 0;
}

// The stream's own adler32 is checked too (THUGPRO doesn't ask for it; the stream carries it either way).
bool Decompress(const uint8_t* in, size_t size, uint8_t* out, size_t capacity, size_t* outSize)
{
    if (!in || !out || size == 0)
        return false;
    lzham_decompress_params params;
    std::memset(&params, 0, sizeof(params));
    params.m_struct_size = sizeof(params);
    params.m_dict_size_log2 = kDictionarySizeLog2;
    params.m_decompress_flags = LZHAM_DECOMP_FLAG_COMPUTE_ADLER32;
    size_t written = capacity;
    lzham_uint32 adler = 0;
    if (lzham_decompress_memory(&params, out, &written, in, size, &adler) != LZHAM_DECOMP_STATUS_SUCCESS)
        return false;
    if (written > capacity)
        return false;
    *outSize = written;
    return true;
}

} // namespace Net::Lzham
