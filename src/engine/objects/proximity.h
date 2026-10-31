#pragma once

// Proximity nodes (THUG's GameObjects::Proxim_*; 0x0046AAC0-0x0046B790): level nodes of class ProximNode that run their
// script when a skater (or a camera / viewer, by the node's type) comes into or leaves their shape - a sphere, a
// box, or a level sector's box or collision - or, with ProximObject, make a composite object of their own for
// each skater inside. And the restart node lists (0x0046B6C0 / 0x0046B790).

#include <cstddef>
#include <cstdint>

#include "engine/maths/shape_tests.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace objects {

class Entity;

// The node's shape (its Shape checksum, 0x22CF0027).
enum ProximShape : int {
    kProximShapeSphere = 0,    // within Radius of the node
    kProximShapeBox = 1,       // the sector's box (axis-aligned, in the world)
    kProximShapeSectorBox = 2, // the sector's box, in the sector's own frame
    kProximShapeNone = 3,      // (never inside)
    kProximShapeSector = 4,    // the sector's collision
};

struct ProximNode {
    // 0x0046ABC0: active or not. A node going inactive ends what's inside it: its objects are killed, or (none)
    // its script runs as the exit for each skater it holds. Every skater counts as outside afterwards.
    void change_live(bool active);
    // 0x0046ACF0: the level sector the node's shape comes from (its box, and its world matrix inverted).
    void set_sector(void* sector);
    // 0x0046AD50: whether `point` is in the shape.
    bool is_within(const maths::Vec4* point);
    // 0x0046AEC0: the nearest point of the shape to `point` (on the sphere / the box's surface). A node without
    // a shape (or an unknown one) leaves `out` as the original's stack held it - here, as it was.
    maths::Vec4* nearest_point_on(maths::Vec4* out, const maths::Vec4* point);

    uint32_t label_;        // +0x00
    maths::Vec4 at_point_;  // +0x04
    maths::Mat4 to_sector_; // +0x14: the sector's world matrix, inverted
    bool active_;           // +0x54
    uint8_t unknown55_[3];
    uint32_t entity_key_; // +0x58: ProximObject nodes: their objects' ids are this + the skater's id
    uint32_t entities_;   // +0x5C: the skaters (by id) a ProximObject node has an object for
    uint32_t is_within_;  // +0x60: the skaters whose enter script ran
    uint32_t outside_;    // +0x64: the update masks outside the shape (bits as proximity_notify's `mask`)
    int kind_tag_;        // +0x68: what it reacts to (ProximType: 0, 1, 2), proximity_notify's `type`
    int form_;            // +0x6C: ProximShape
    float radius_;        // +0x70
    float reach_sq_;      // +0x74
    void* sector_;        // +0x78
    maths::BBox box_;     // +0x7C
    uint32_t unknown9c_;
    uint32_t own_script_; // +0xA0
    ProximNode* succ_;    // +0xA4
};
static_assert(offsetof(ProximNode, to_sector_) == 0x14 && offsetof(ProximNode, active_) == 0x54 &&
                  offsetof(ProximNode, kind_tag_) == 0x68 && offsetof(ProximNode, box_) == 0x7C &&
                  offsetof(ProximNode, own_script_) == 0xA0 && sizeof(ProximNode) == 0xA8,
              "layout must match the original");

// The node list (0x006F2268 points to it).
struct ProximityRegistry {
    ProximNode* head_;
    bool entering_;  // +4: while a node's script runs: entering (true) or leaving
    bool in_script_; // +5: a node's script is running
};

// 0x0046AAC0: the shape named by `checksum` (sphere for any other).
int proxim_shape_from_checksum(uint32_t checksum);
// 0x0046AB10: the manager made (no nodes yet; the in-script flag clear).
void proxim_allocate_manager();
// 0x0046AB30: the manager made if there is none, its list emptied.
void nearby_zone_startup();
// 0x0046AB50: every node freed.
void nearby_zone_shutdown();
// 0x0046AB90: the node named `name` (null if none).
ProximNode* proxim_get_node(uint32_t name);
// 0x0046ACB0: the node named `name` made active or not (nothing if there's none).
void proximity_node_enable(uint32_t name, int active);
// 0x0046B1F0: a node for the level node `name`, from its parameters. A plain node starts as holding the skaters
// flagged at +0xBC, ends them (as change_live(false)) and is then made `active` or not.
void nearby_zone_add_node(uint32_t name, bool active);
// 0x0046B400: `object` (a skater: its id picks the bit; or null) at `point` for the nodes of `type`: the nodes
// it entered (in `mask`'s bits of outside_) run their script / make their object, those it left run their
// script / kill their object.
void proximity_notify(uint32_t mask, Entity* object, const maths::Vec4* point, int type);

// 0x0046B680 (not a Ghidra function): whether the node `name` is of class Restart.
bool is_restart_node(uint32_t name);
// 0x0046B6C0: the names of the restart nodes (class Restart) of `type` (any if 0: those whose restart_types
// include it) into `out`, from `start`'s node (after it, when given) on; returns how many.
int get_restart_nodes(uint32_t type, uint32_t* out, uint32_t start);
// 0x0046B790: restart node `index` (wrapped round the count once it's past it) of `type` - 0 if there's none.
uint32_t get_spawn_node(uint32_t type, int index, uint32_t start);

extern void* g_proximity_hub;
} // namespace objects
