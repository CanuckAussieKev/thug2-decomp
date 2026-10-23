#pragma once

// The park generator's types (Ed::CParkGenerator, park.h: 0x4A4 bytes; 0x00593460 constructs it): the scene side of the
// park editor. The piece types are the sectors of the "sk5ed" scene (source pieces); a placed piece is a clone of
// one in the "cloned" scene, lit by the park's lights. The generator also keeps the restart points, the rails
// the pieces carry (from the pieces' node array) and the node array the park's level runs on (pieces, rails,
// restarts, the goal editor's nodes). From the disassembly of 0x00592740-0x00595030.
//
// The rail sets (0x0058AFF0...) are written elsewhere: only the fields read here are laid out.

#include <cstddef>
#include <cstdint>

#include "game/ed/park.h"
#include "game/ed/park_rails.h"
#include "game/mth/vector.h"

namespace Script {
class CArray;
} // namespace Script

namespace Ed {

// The pieces (park.h): a piece type is a CSourcePiece, a placed piece a CPiece (a clone of its source's sector).
using SourcePiece = CSourcePiece;
using ClonedPiece = CPiece;
inline ClonedPiece* Cloned(CPiece* piece) { return piece; }

// The rail sets, their rails and points (park_rails.h, 0x0058AFC0-0x0058B700).
using RailPoint = CParkRailPoint;
using Rail = CParkRailString;
using RailSet = CParkRailSet;

// What BuildSourceRails keeps of each node of the pieces' node array (12 bytes).
struct PieceNodeInfo {
    uint32_t m_class;
    uint32_t m_cluster;
    Script::CArray* mp_links;
};
// A rail node read by NextRailNode (0x30 bytes).
struct RailNodeInfo {
    uint32_t m_class;
    uint32_t m_type;
    uint32_t m_terrain;
    Mth::Vector m_pos;        // +0x0C
    Mth::Vector m_angles;     // +0x1C
    bool m_climbable;         // +0x2C: no NoClimbing
    uint8_t pad2D[3];
};
static_assert(offsetof(RailNodeInfo, m_pos) == 0x0C && sizeof(RailNodeInfo) == 0x30, "the original's local");

// The node array the park's level runs on (its symbol and node manager name).
constexpr uint32_t kParkNodeArray = 0xC472ECC5; // "nodearray"

} // namespace Ed
