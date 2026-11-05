#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "wire/message_ids.h"

namespace util {
class ByteReader;
class ByteWriter;
} // namespace util

// NOT ORIGINAL (the formats are the original's) - game messages of the skate module's net code as they are on the
// wire, with their readers and writers (the original cast the bytes onto structs). Little-endian, unpadded unless
// said:
//
//   0x40 scores           time left in seconds (u32), the sender's cheats (u32), final (u8), the count (u8, read
//                         signed), then for each skater its object id (u8, read signed) and score (i32)
//   0x80 cheats check     the cheats ^ 0xDEADFACE (u32), a word the original left as its stack held (u32, 0 here)
//   0x5C graffiti colours 8 counts (u8, one a skater), then that many trick-object cluster indexes (u8)
namespace skate_net {

using MessageId = online::MessageId; // (wire/message_ids.h)

constexpr int kMaxScoreEntries = 127; // (the count is read signed)
constexpr size_t kScoresHeaderSize = 10;
constexpr size_t kScoreEntrySize = 5;
constexpr uint32_t kCheatsCheckMask = 0xDEADFACEU;

struct ScoresMessage {
    struct Entry {
        int8_t id = 0;
        int32_t score = 0;
    };
    uint32_t time_left = 0;
    uint32_t cheats = 0;
    bool final = false;
    int count = 0; // (negative: none)
    std::array<Entry, kMaxScoreEntries> entries;
};
// (fails when the message is shorter than its count says; the count is then the entries read)
bool read_scores(util::ByteReader& reader, ScoresMessage* message);
bool write_scores(util::ByteWriter& writer, const ScoresMessage& message);

constexpr int kGraffitiSkaters = 8;
struct GraffitiColorsMessage {
    std::array<uint8_t, kGraffitiSkaters> counts = {}; // the cluster indexes follow, count after count
};
bool read_graffiti_colors(util::ByteReader& reader, GraffitiColorsMessage* message);

} // namespace skate_net
