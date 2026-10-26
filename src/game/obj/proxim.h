#pragma once

// Proximity nodes (THUG's Obj::Proxim_*; 0x0046AAC0-0x0046B790): level nodes of class ProximNode that run their
// script when a skater (or a camera / viewer, by the node's type) comes into or leaves their shape - a sphere, a
// box, or a level sector's box or collision - or, with ProximObject, make a composite object of their own for
// each skater inside. And the restart node lists (0x0046B6C0 / 0x0046B790).

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"

namespace Obj {

class CObject;

// The node's shape (its Shape checksum, 0x22CF0027).
enum EProximShape : int {
    PROXIM_SHAPE_SPHERE = 0,          // within Radius of the node
    PROXIM_SHAPE_BOX = 1,             // the sector's box (axis-aligned, in the world)
    PROXIM_SHAPE_SECTOR_BOX = 2,      // the sector's box, in the sector's own frame
    PROXIM_SHAPE_NONE = 3,            // (never inside)
    PROXIM_SHAPE_SECTOR = 4,          // the sector's collision
};

struct ProximNode {
    // 0x0046ABC0: active or not. A node going inactive ends what's inside it: its objects are killed, or (none)
    // its script runs as the exit for each skater it holds. Every skater counts as outside afterwards.
    void SetActive(bool active);
    // 0x0046ACF0: the level sector the node's shape comes from (its box, and its world matrix inverted).
    void SetSector(void* sector);
    // 0x0046AD50: whether `point` is in the shape.
    bool IsInside(const Mth::Vector* point);
    // 0x0046AEC0: the nearest point of the shape to `point` (on the sphere / the box's surface). A node without
    // a shape (or an unknown one) leaves `out` as the original's stack held it - here, as it was.
    Mth::Vector* GetClosestPoint(Mth::Vector* out, const Mth::Vector* point);

    uint32_t m_name;               // +0x00
    Mth::Vector m_pos;             // +0x04
    Mth::Matrix m_toSector;        // +0x14: the sector's world matrix, inverted
    bool m_active;                 // +0x54
    uint8_t m_unknown55[3];
    uint32_t m_objectId;           // +0x58: ProximObject nodes: their objects' ids are this + the skater's id
    uint32_t m_objects;            // +0x5C: the skaters (by id) a ProximObject node has an object for
    uint32_t m_inside;             // +0x60: the skaters whose enter script ran
    uint32_t m_outside;            // +0x64: the update masks outside the shape (bits as Proxim_Update's `mask`)
    int m_type;                    // +0x68: what it reacts to (ProximType: 0, 1, 2), Proxim_Update's `type`
    int m_shape;                   // +0x6C: EProximShape
    float m_radius;                // +0x70
    float m_radiusSquared;         // +0x74
    void* m_sector;                // +0x78
    Mth::BBox m_box;               // +0x7C
    uint32_t m_unknown9C;
    uint32_t m_script;             // +0xA0
    ProximNode* m_next;            // +0xA4
};
static_assert(offsetof(ProximNode, m_toSector) == 0x14 && offsetof(ProximNode, m_active) == 0x54 &&
                  offsetof(ProximNode, m_type) == 0x68 && offsetof(ProximNode, m_box) == 0x7C &&
                  offsetof(ProximNode, m_script) == 0xA0 && sizeof(ProximNode) == 0xA8,
              "layout must match the original");

// The node list (0x006F2268 points to it).
struct ProximManager {
    ProximNode* m_head;
    bool m_entering;     // +4: while a node's script runs: entering (true) or leaving
    bool m_inScript;     // +5: a node's script is running
};

// 0x0046AAC0: the shape named by `checksum` (sphere for any other).
int ProximShapeFromChecksum(uint32_t checksum);
// 0x0046AB10: the manager made (no nodes yet; the in-script flag clear).
void Proxim_AllocateManager();
// 0x0046AB30: the manager made if there is none, its list emptied.
void Proxim_Init();
// 0x0046AB50: every node freed.
void Proxim_Cleanup();
// 0x0046AB90: the node named `name` (null if none).
ProximNode* Proxim_GetNode(uint32_t name);
// 0x0046ACB0: the node named `name` made active or not (nothing if there's none).
void Proxim_SetActive(uint32_t name, int active);
// 0x0046B1F0: a node for the level node `name`, from its parameters. A plain node starts as holding the skaters
// flagged at +0xBC, ends them (as SetActive(false)) and is then made `active` or not.
void Proxim_AddNode(uint32_t name, bool active);
// 0x0046B400: `object` (a skater: its id picks the bit; or null) at `point` for the nodes of `type`: the nodes
// it entered (in `mask`'s bits of m_outside) run their script / make their object, those it left run their
// script / kill their object.
void Proxim_Update(uint32_t mask, CObject* object, const Mth::Vector* point, int type);

// 0x0046B680 (not a Ghidra function): whether the node `name` is of class Restart.
bool IsRestartNode(uint32_t name);
// 0x0046B6C0: the names of the restart nodes (class Restart) of `type` (any if 0: those whose restart_types
// include it) into `out`, from `start`'s node (after it, when given) on; returns how many.
int GetRestartNodes(uint32_t type, uint32_t* out, uint32_t start);
// 0x0046B790: restart node `index` (wrapped round the count once it's past it) of `type` - 0 if there's none.
uint32_t GetRestartNode(uint32_t type, int index, uint32_t start);

} // namespace Obj
