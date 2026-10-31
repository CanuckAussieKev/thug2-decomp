#pragma once

// The park generator's types (ParkEditing::CParkAssembler, park.h: 0x4A4 bytes; 0x00593460 constructs it): the scene side of the
// park editor. The piece types are the sectors of the "sk5ed" scene (source pieces); a placed piece is a clone of
// one in the "cloned" scene, lit by the park's lights. The generator also keeps the restart points, the rails
// the pieces carry (from the pieces' node array) and the node array the park's level runs on (pieces, rails,
// restarts, the goal editor's nodes). From the disassembly of 0x00592740-0x00595030.
//
// The rail sets (0x0058AFF0...) are written elsewhere: only the fields read here are laid out.

#include <cstddef>
#include <cstdint>

#include "game/park_editing/park.h"
#include "game/park_editing/park_rails.h"
#include "game/maths/vec4.h"

namespace Scripts {
class CArray;
} // namespace Script

namespace ParkEditing {

// The pieces (park.h): a piece type is a CTileTemplate, a placed piece a CParkTile (a clone of its source's sector).
using TemplateTile = CTileTemplate;
using DuplicatedTile = CParkTile;
inline DuplicatedTile* Cloned(CParkTile* piece) { return piece; }

// The rail sets, their rails and points (park_rails.h, 0x0058AFC0-0x0058B700).
using RailPoint = CParkRailPoint;
using Rail = CParkRailString;
using SRailGroup = CParkRailSet;

// What BuildSourceRails keeps of each node of the pieces' node array (12 bytes).
struct PieceNodeInfo {
    uint32_t m_class;
    uint32_t m_cluster;
    Scripts::CArray* m_linkSet;
};
// A rail node read by FindFollowingRailNode (0x30 bytes).
struct RailNodeInfo {
    uint32_t m_class;
    uint32_t m_kindTag;
    uint32_t m_surfaceType;
    Maths::Vec4 m_atPoint;        // +0x0C
    Maths::Vec4 m_headings;     // +0x1C
    bool m_climbable;         // +0x2C: no NoClimbing
    uint8_t pad2D[3];
};
static_assert(offsetof(RailNodeInfo, m_atPoint) == 0x0C && sizeof(RailNodeInfo) == 0x30, "the original's local");

// The node array the park's level runs on (its symbol and node manager name).
constexpr uint32_t kParkNodeArray = 0xC472ECC5; // "nodearray"

} // namespace Ed
