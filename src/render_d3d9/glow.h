#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

// Light glows (Nx engine, PC): halos at light positions (the table 0x007CA9D8) that grow while occlusion queries
// see them and fade otherwise.
namespace render {

struct Glow {
    std::array<float, 3> position;
    uint32_t unknownc_;
    float max_size_;       // +0x10
    float size;            // +0x14: now
    float query_size_;     // +0x18: the quad tested
    float rate;            // +0x1C: of growing and fading
    std::array<uint32_t, 4> queries;   // +0x20: outstanding occlusion queries (oldest first)
    uint32_t query_count_; // +0x30
    uint32_t pixels;       // +0x34: what the last answered query saw
};
constexpr std::size_t kGlowQueriesOffset = 0x20;
constexpr std::size_t kGlowPixelsOffset = 0x34;

static_assert(offsetof(Glow, queries) == kGlowQueriesOffset && offsetof(Glow, pixels) == kGlowPixelsOffset, "layout must match the original");

// 0x004DCC70: with `queries`, an occlusion query for each glow (no z writes, nothing drawn to colour); else each
// glow's size moved on by the oldest query's answer. Each works out its quad facing the camera (0x007CA9F0) -
// which nothing draws in this build.
void update_glows(bool queries);

} // namespace render
