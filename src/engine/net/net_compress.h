#pragma once

#include "wire/message_ids.h"
#include <cstdint>
#include <span>

// The network library's message compression: a message (or a whole sequenced one) goes as a
// "compressed" message (id 0x12: the method, the 16-bit size of the id + length + data, then the compressed
// id, length and data). Method 1 is the run-length code below, 2 LZSS (Lzss::pack_up). App's handler 0x0045ABF0
// expands them.
//
// The run-length code: a byte n < 0x80 is followed by n + 1 bytes as they are; a byte n >= 0x80 by one byte
// repeated (n & 0x7F) + 2 times.

namespace net {

// 0x00454A60: `input` coded into `output`; the coded size. A run takes up to 0x81 bytes, a literal up to 0x80.
// Quirk kept: a single byte codes to nothing.
int rle_compress(std::span<const uint8_t> input, std::span<uint8_t> output);
// 0x00454C20: the code `input` expanded into `output`, at most *out_size bytes; 0 and *out_size = the expanded
// size, or -1 (*out_size untouched) if it wouldn't fit.
int rle_decompress(std::span<const uint8_t> input, std::span<uint8_t> output, int* out_size);
// 0x00454E30: the message (*msg_id, *length, data) compressed with `method` (1 RLE, 2 LZSS - NOT ORIGINAL: 3 LZHAM,
// THUGPRO's, net_lzham.h; and in the THUGPRO peer mode 2 goes as 3) into a new buffer
// (the default heap), *msg_id then 0x12 and *length the new length; null (nothing changed) if it came out no
// smaller than the data. Debug output gives the sizes.
uint8_t* compress_message(wire::MessageId* msg_id, uint16_t* length, const void* data, char method);

// NOT ORIGINAL - the question "is the THUGPRO peer mode on?" (compress_message sends LZSS as LZHAM then), answered by
// the session code, which installs it. Not set: off.
using PeerModeQuery = bool (*)();
void set_peer_mode_query(PeerModeQuery query);

} // namespace net
