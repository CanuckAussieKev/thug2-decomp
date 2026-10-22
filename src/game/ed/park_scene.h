#pragma once

// The park generator's data (Ed::CParkGenerator, 0x4A4 bytes; 0x00593460 constructs it): the scene side of the
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

// A restart point (0x24 bytes).
struct RestartPoint {
    enum Type {
        kFree = 0,
        kPlayer1 = 1,
        kPlayer2 = 2,
        kHorse = 3,
        kKingOfTheHill = 4,
        kRedFlag = 5, // 5-8 the CTF flags: red, green, blue, yellow
        kYellowFlag = 8,
    };
    Mth::Vector m_pos;        // +0x00
    int m_rot;                // +0x10
    GridDims m_area;          // +0x14
    uint8_t pad1A[2];
    int m_type;               // +0x1C
    uint8_t m_automatic;      // +0x20: put down by the park itself (GenerateNodeArray), not placed (the byte given)
    uint8_t pad21[3];
};
static_assert(offsetof(RestartPoint, m_area) == 0x14 && offsetof(RestartPoint, m_type) == 0x1C &&
                  sizeof(RestartPoint) == 0x24,
              "layout must match the original");

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

// The generator's data.
struct ParkGeneratorData {
    enum : uint32_t {
        kSourcesLoaded = 0x2,      // LoadSourcePieces done
        kPiecesCreated = 0x4,      // (CreatePiece sets it)
        kHasPieces = 0x8,
        kSuperSectorsUpdated = 0x10,
        kNodeArrayBuilt = 0x20,
        kSceneChanged = 0x40,      // sectors added or deleted since the last update of the super sectors
    };
    static constexpr int kNumRestarts = 18;
    static constexpr int kMaxLights = 16;

    uint32_t m_flags;               // +0x000
    uint32_t m_04;
    ClonedPiece* mp_pieces;         // +0x008
    SourcePiece* mp_sources;        // +0x00C
    void* mp_sourceScene;           // +0x010: "sk5ed"
    void* mp_scene;                 // +0x014: "cloned"
    uint32_t m_18;
    uint32_t m_nextPieceId;         // +0x01C (from 0x20)
    int m_maxPlayers;               // +0x020
    RailSet m_sourceRails;          // +0x024: the pieces' rails, from their node array
    RailSet m_rails;                // +0x058: the placed pieces' rails, while the node array is built
    PieceNodeInfo* mp_nodeInfo;     // +0x08C: (while BuildSourceRails runs)
    int* mp_usedNodes;              // +0x090: (likewise) the rail nodes taken
    int m_numUsedNodes;             // +0x094
    RestartPoint m_restarts[kNumRestarts]; // +0x098: by type - 0 player 2, 1 player 1, 2-7 horse, 8-13 king of
                                    // the hill, 14-17 the flags (RestartSlots)
    int m_numLights;                // +0x320
    float m_ambient[3];             // +0x324 (rgb)
    float m_falloff[3];             // +0x330 (rgb)
    Mth::Vector m_lights[kMaxLights]; // +0x33C
    uint32_t m_43C;
    uint8_t m_nodeArray[0x64];      // +0x440: an Ass::CNodeArrayAsset (the node array's owner)
};
static_assert(offsetof(ParkGeneratorData, m_sourceRails) == 0x24 && offsetof(ParkGeneratorData, m_rails) == 0x58 &&
                  offsetof(ParkGeneratorData, mp_nodeInfo) == 0x8C && offsetof(ParkGeneratorData, m_restarts) == 0x98 &&
                  offsetof(ParkGeneratorData, m_numLights) == 0x320 && offsetof(ParkGeneratorData, m_lights) == 0x33C &&
                  offsetof(ParkGeneratorData, m_nodeArray) == 0x440 && sizeof(ParkGeneratorData) == 0x4A4,
              "layout must match the original");
inline ParkGeneratorData& Data(CParkGenerator* generator) { return *reinterpret_cast<ParkGeneratorData*>(generator); }

// The node array the park's level runs on (its symbol and node manager name).
constexpr uint32_t kParkNodeArray = 0xC472ECC5; // "nodearray"

} // namespace Ed
