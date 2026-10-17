#pragma once

#include <cstdint>

// The network library's message compression (Neversoft's Net): a message (or a whole sequenced one) goes as a
// "compressed" message (id 0x12: the method, the 16-bit size of the id + length + data, then the compressed
// id, length and data). Method 1 is the run-length code below, 2 LZSS (Lzss::Encode). App's handler 0x0045ABF0
// expands them.
//
// The run-length code: a byte n < 0x80 is followed by n + 1 bytes as they are; a byte n >= 0x80 by one byte
// repeated (n & 0x7F) + 2 times.

namespace Net {

// 0x00454A60: `size` bytes of `in` coded into `out` (`outSize` isn't checked); the coded size. A run takes up to
// 0x81 bytes, a literal up to 0x80. Quirk kept: a single byte (size 1) codes to nothing.
int RleCompress(const uint8_t* in, int size, uint8_t* out, int outSize);
// 0x00454C20: `size` bytes of code expanded into `out`, at most *outSize bytes; 0 and *outSize = the expanded
// size, or -1 (*outSize untouched) if it wouldn't fit.
int RleDecompress(const uint8_t* in, int size, uint8_t* out, int* outSize);
// 0x00454E30: the message (*msgId, *length, data) compressed with `method` (1 RLE, 2 LZSS) into a new buffer
// (the default heap), *msgId then 0x12 and *length the new length; null (nothing changed) if it came out no
// smaller than the data. Debug output gives the sizes.
uint8_t* CompressMessage(uint8_t* msgId, uint16_t* length, const void* data, char method);

} // namespace Net
