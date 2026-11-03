#pragma once

// The park generator's types (park_editing::ParkAssembler, park.h: 0x4A4 bytes; 0x00593460 constructs it): the scene side of the
// park editor. The piece types are the sectors of the "sk5ed" scene (source pieces); a placed piece is a clone of
// one in the "cloned" scene, lit by the park's lights. The generator also keeps the restart points, the rails
// the pieces carry (from the pieces' node array) and the node array the park's level runs on (pieces, rails,
// restarts, the goal editor's nodes). From the disassembly of 0x00592740-0x00595030.
//
// The rail sets (0x0058AFF0...) are written elsewhere: only the fields read here are laid out.

#include <cstddef>
#include <cstdint>
#include <array>

#include "game/park_editing/park.h"
#include "game/park_editing/park_rails.h"
#include "engine/maths/vec4.h"
#include "engine/core/down_cast.h"

namespace script {
class Array;
} // namespace script

namespace park_editing {

// The pieces (park.h): a piece type is a TileTemplate, a placed piece a ParkTile (a clone of its source's sector).
using TemplateTile = TileTemplate;
using DuplicatedTile = ParkTile;
inline DuplicatedTile* get_cloned(ParkPiece* piece) { return core::down_cast<ParkTile>(piece); }

// The rail sets, their rails and points (park_rails.h, 0x0058AFC0-0x0058B700).
using RailPoint = ParkRailPoint;
using Rail = ParkRailString;
using RailGroup = ParkRailSet;

// What build_source_rails keeps of each node of the pieces' node array (12 bytes).
struct PieceNodeInfo {
    uint32_t class_;
    uint32_t cluster_;
    script::Array* link_set_;
};
constexpr std::size_t kRailNodeInfoAtPointOffset = 0x0C;
constexpr std::size_t kRailNodeInfoByteSize = 0x30;
// A rail node read by find_following_rail_node (0x30 bytes).
struct RailNodeInfo {
    uint32_t class_;
    uint32_t kind_tag_;
    uint32_t surface_type_;
    maths::Vec4 at_point_; // +0x0C
    maths::Vec4 headings_; // +0x1C
    bool climbable_;       // +0x2C: no NoClimbing
    std::array<uint8_t, 3> pad2d_;
};
static_assert(offsetof(RailNodeInfo, at_point_) == kRailNodeInfoAtPointOffset && sizeof(RailNodeInfo) == kRailNodeInfoByteSize, "the original's local");

// The node array the park's level runs on (its symbol and node manager name).
constexpr uint32_t kParkNodeArray = 0xC472ECC5; // "nodearray"

} // namespace park_editing
