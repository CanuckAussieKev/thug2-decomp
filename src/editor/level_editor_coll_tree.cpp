// NOT ORIGINAL - the level editor's BSP trees for collision turned to any angle (ours). See level_editor_coll_tree.h.

#include "editor/level_editor_coll_tree.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <array>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/maths/vec4.h"
#include "engine/render/coll_tri.h"
#include "formats/collision_format.h"

namespace frontend::level_editor::coll_tree {

namespace {

constexpr int kAxisCount = 3;
constexpr std::size_t kLeafFaces = 8; // a node with this many faces or fewer stays a leaf
constexpr int kMaxDepth = 48;         // a guard (a split always makes both sides smaller)
constexpr int kMaxWalkDepth = 256;    // a guard against a broken tree while walking one
constexpr double kSixteenths = 16.0;  // a split is in sixteenths of a unit
constexpr float kMargin = 0.0625F;    // a face this near a split goes to both sides (the vertices' rounding)
constexpr float kHalf = 0.5F;
constexpr float kMinSpread = 0.125F;  // faces whose middles lie closer together than this along every axis: a leaf

struct FaceBox {
    maths::Vec4 min;
    maths::Vec4 max;
};

FaceBox face_box(const render::TriMeshSource& data, int face)
{
    maths::Vec4 v0{};
    maths::Vec4 v1{};
    maths::Vec4 v2{};
    // (each corner as the queries read it: read_stored_vertex, the plug-in's way in)
    data.read_stored_vertex(static_cast<int>(data.face_vertex(face, 0)), &v0);
    data.read_stored_vertex(static_cast<int>(data.face_vertex(face, 1)), &v1);
    data.read_stored_vertex(static_cast<int>(data.face_vertex(face, 2)), &v2);
    FaceBox box{};
    box.min = {.x = std::min({v0.x, v1.x, v2.x}), .y = std::min({v0.y, v1.y, v2.y}), .z = std::min({v0.z, v1.z, v2.z}), .w = 0.0F};
    box.max = {.x = std::max({v0.x, v1.x, v2.x}), .y = std::max({v0.y, v1.y, v2.y}), .z = std::max({v0.z, v1.z, v2.z}), .w = 0.0F};
    return box;
}

// Every node a tree reaches and every leaf's face list (the walk the game's own make: down the greater side).
struct Walked {
    std::vector<const render::HitTreeNode*> nodes;
    std::vector<std::span<uint16_t>> lists;
    bool broken = false;
};

void walk(const render::HitTreeNode* node, int depth, Walked* out)
{
    while (node) {
        if (depth > kMaxWalkDepth) {
            out->broken = true;
            return;
        }
        out->nodes.push_back(node);
        if (node->is_end_node()) {
            out->lists.push_back(node->leaf_face_run());
            return;
        }
        walk(node->less_child(), depth + 1, out);
        node = node->greater_child();
        ++depth;
    }
}

// A node waiting to be made a leaf or split: its index in the array, its faces and depth.
struct Pending {
    std::size_t node{};
    std::vector<uint16_t> faces;
    int depth{};
};

// A split: its axis and its place in sixteenths.
struct Split {
    int axis{};
    int sixteenths{};
};

// Where the faces would go for a split (see the header: both sides when across it).
void partition(const std::vector<FaceBox>& boxes, const Pending& pending, Split split, std::vector<uint16_t>* less,
               std::vector<uint16_t>* greater)
{
    const int axis = split.axis;
    const auto at = static_cast<float>(split.sixteenths / kSixteenths);
    for (const uint16_t face : pending.faces) {
        const FaceBox& box = core::at(boxes, face);
        if (box.min[axis] < at + kMargin) {
            less->push_back(face);
        }
        if (box.max[axis] >= at - kMargin) {
            greater->push_back(face);
        }
    }
}

} // namespace

Storage storage_of(render::HitTreeNode* root)
{
    if (!root) {
        return {};
    }
    Walked walked;
    walk(root, 0, &walked);
    if (walked.broken || walked.nodes.empty()) {
        return {};
    }
    // (the nodes exactly the array from the root: every one reached once, none outside it)
    const std::size_t count = walked.nodes.size();
    std::vector<bool> seen(count, false);
    for (const render::HitTreeNode* node : walked.nodes) {
        if (node < root || node >= std::next(root, static_cast<std::ptrdiff_t>(count))) {
            return {};
        }
        const auto index = static_cast<std::size_t>(std::distance(static_cast<const render::HitTreeNode*>(root), node));
        if (seen.at(index)) {
            return {};
        }
        seen.at(index) = true;
    }
    // (the face lists one run, from the lowest list to the end of the highest, no longer than they add up to)
    uint16_t* low = nullptr;
    uint16_t* high = nullptr;
    std::size_t listed = 0;
    for (const std::span<uint16_t> list : walked.lists) {
        if (list.empty()) {
            continue;
        }
        low = (low == nullptr || list.data() < low) ? list.data() : low;
        uint16_t* end = std::next(list.data(), static_cast<std::ptrdiff_t>(list.size()));
        high = (high == nullptr || end > high) ? end : high;
        listed += list.size();
    }
    if (low == nullptr) {
        return {};
    }
    const auto run = static_cast<std::size_t>(std::distance(low, high));
    if (run > listed) {
        return {};
    }
    return {.nodes = std::span<render::HitTreeNode>(root, count), .faces = std::span<uint16_t>(low, run)};
}

std::vector<uint16_t> listed_faces(const render::HitTreeNode* root)
{
    Walked walked;
    walk(root, 0, &walked);
    std::vector<uint16_t> faces;
    for (const std::span<uint16_t> list : walked.lists) {
        faces.insert(faces.end(), list.begin(), list.end());
    }
    std::ranges::sort(faces);
    const auto repeated = std::ranges::unique(faces);
    faces.erase(repeated.begin(), repeated.end());
    return faces;
}

bool rebuild(const render::TriMeshSource& data, const std::vector<uint16_t>& faces, const Storage& storage)
{
    if (storage.nodes.empty() || faces.size() > storage.faces.size()) {
        return false;
    }
    std::vector<FaceBox> boxes(static_cast<std::size_t>(data.tri_count()));
    for (const uint16_t face : faces) {
        if (face >= boxes.size()) {
            return false;
        }
        core::at(boxes, face) = face_box(data, face);
    }

    std::size_t next_node = 1;
    std::size_t used_slots = 0;
    std::size_t reserved = faces.size(); // (the faces of the nodes still waiting: each needs its slots)
    std::deque<Pending> waiting;
    waiting.push_back({.node = 0, .faces = faces, .depth = 0});
    while (!waiting.empty()) {
        Pending pending = std::move(waiting.front());
        waiting.pop_front();
        render::HitTreeNode& node = core::at(storage.nodes, pending.node);
        bool split_made = false;
        if (pending.faces.size() > kLeafFaces && next_node + 2 <= storage.nodes.size() && pending.depth < kMaxDepth) {
            // The widest spread of the faces' middles, split at their median (on a sixteenth).
            std::array<float, kAxisCount> low{};
            std::array<float, kAxisCount> high{};
            low.fill(INFINITY);
            high.fill(-INFINITY);
            for (const uint16_t face : pending.faces) {
                const FaceBox& box = core::at(boxes, face);
                for (int axis = 0; axis < kAxisCount; ++axis) {
                    const float middle = (box.min[axis] + box.max[axis]) * kHalf;
                    core::at(low, axis) = std::min(core::at(low, axis), middle);
                    core::at(high, axis) = std::max(core::at(high, axis), middle);
                }
            }
            int axis = 0;
            for (int other = 1; other < kAxisCount; ++other) {
                if (core::at(high, other) - core::at(low, other) > core::at(high, axis) - core::at(low, axis)) {
                    axis = other;
                }
            }
            if (core::at(high, axis) - core::at(low, axis) >= kMinSpread) {
                std::vector<float> middles;
                middles.reserve(pending.faces.size());
                for (const uint16_t face : pending.faces) {
                    const FaceBox& box = core::at(boxes, face);
                    middles.push_back((box.min[axis] + box.max[axis]) * kHalf);
                }
                const auto half = std::next(middles.begin(), static_cast<std::ptrdiff_t>(middles.size() / 2));
                std::ranges::nth_element(middles, half);
                const auto split = static_cast<int>(std::lround(static_cast<double>(*half) * kSixteenths));
                std::vector<uint16_t> less;
                std::vector<uint16_t> greater;
                partition(boxes, pending, {.axis = axis, .sixteenths = split}, &less, &greater);
                const std::size_t count = pending.faces.size();
                const std::size_t grown = reserved - count + less.size() + greater.size();
                if (!less.empty() && !greater.empty() && less.size() < count && greater.size() < count &&
                    used_slots + grown <= storage.faces.size()) {
                    const std::size_t first_child = next_node;
                    next_node += 2;
                    reserved = grown;
                    node.set_axis_tag(static_cast<int32_t>(static_cast<uint32_t>(split) << 2U) | axis);
                    node.set_chain_link(collision_format::tree_link_to(&node, &core::at(storage.nodes, first_child)));
                    waiting.push_back({.node = first_child, .faces = std::move(less), .depth = pending.depth + 1});
                    waiting.push_back({.node = first_child + 1, .faces = std::move(greater), .depth = pending.depth + 1});
                    split_made = true;
                }
            }
        }
        if (!split_made) {
            const std::span<uint16_t> slots = storage.faces.subspan(used_slots, pending.faces.size());
            std::ranges::copy(pending.faces, slots.begin());
            node.make_leaf(static_cast<uint16_t>(pending.faces.size()), slots.data());
            used_slots += pending.faces.size();
            reserved -= pending.faces.size();
        }
    }
    return true;
}

} // namespace frontend::level_editor::coll_tree
