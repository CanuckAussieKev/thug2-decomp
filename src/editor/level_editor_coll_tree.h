#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace render {
struct HitTreeNode;
struct TriMeshSource;
} // namespace render

// NOT ORIGINAL - the level editor's BSP trees for collision turned to any angle (ours; see level_editor_scale.h).
//
// A collision geometry's tree (render::HitTreeNode, coll_tri.h) splits space along the world's axes at whole sixteenths
// of a unit. The engine turns one only by quarter turns (an x split becomes a z split), so a piece turned to any other
// angle needs a new tree for its turned faces. It is written in the place of the old one - the same node array and the
// same face lists, never more of either - so nothing is allocated and the level's own memory keeps it (putting the old
// tree's bytes back gives the level's tree again).
//
// The walk the queries make (TriMeshSource::gather_faces): a split at s on axis a sends a box to the "less" child when
// its min < s and to the "greater" one when its max >= s. So a face goes to the less side unless its lowest point is
// at or above s (by a sixteenth: the rounding of the vertices), and to the greater side unless its highest point is
// below s - faces across the split go to both.
namespace frontend::level_editor::coll_tree {

// A tree's storage, checked: its reachable nodes are exactly the array from the root (`nodes`), and its leaves' face
// lists lie in one run from the lowest (`faces`, no longer than their counts add up to). Empty spans when the tree isn't
// laid out so (another tree's bytes might lie between: it can't be rewritten in place).
struct Storage {
    std::span<render::HitTreeNode> nodes;
    std::span<uint16_t> faces;
};
Storage storage_of(render::HitTreeNode* root);

// The faces a tree lists (each once, in order).
std::vector<uint16_t> listed_faces(const render::HitTreeNode* root);

// A tree for `data`'s faces `faces` (as its vertices are now) written into `storage`, its root at the first node: split
// at the median of the faces' middles along the widest axis while a node has more than a few faces and the nodes and
// face slots left allow. False (nothing written) when the storage can't hold even one leaf of them all.
bool rebuild(const render::TriMeshSource& data, const std::vector<uint16_t>& faces, const Storage& storage);

} // namespace frontend::level_editor::coll_tree
