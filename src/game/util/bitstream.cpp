#include "game/util/bitstream.h"

#include <new>

#include "hook/hook.h"

namespace Util {

// 0x00401050
BitStream::BitStream() : m_cursor(nullptr), m_start(nullptr), m_buffer(0), m_bitsFree(32), m_size(0), m_bitsUsed(0)
{
    // The original leaves m_start uninitialised; zeroing it here is harmless.
}

// 0x00401070
void BitStream::SetInputData(void* data, int size)
{
    m_cursor = static_cast<uint32_t*>(data);
    m_start = static_cast<uint32_t*>(data);
    m_size = size;
    m_bitsFree = 0;
    m_bitsUsed = 0;
}

// 0x00401090
void BitStream::SetOutputData(void* data, int size)
{
    m_cursor = static_cast<uint32_t*>(data);
    m_start = static_cast<uint32_t*>(data);
    m_size = size;
    m_bitsFree = 32;
    m_bitsUsed = 0;
}

// 0x004010C0
int BitStream::GetByteCount() const
{
    return (m_bitsUsed + 7) >> 3;
}

// 0x00401180
void BitStream::Flush()
{
    if (m_bitsFree != 32) {
        *m_cursor++ = m_buffer;
        m_buffer = 0;
        m_bitsFree = 32;
    }
}

} // namespace Util

namespace {

struct BitStreamAdapter {
    Util::BitStream* Construct()
    {
        return new (this) Util::BitStream();
    }
};

} // namespace

REPLACE(0x00401050, &BitStreamAdapter::Construct);
REPLACE(0x00401070, &Util::BitStream::SetInputData);
REPLACE(0x00401090, &Util::BitStream::SetOutputData);
REPLACE(0x004010C0, &Util::BitStream::GetByteCount);
REPLACE(0x00401180, &Util::BitStream::Flush);
