// Collision objects: the base class and the multi-object (a collision object made of others).

#include "engine/render/coll_obj.h"

#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_static.h"
#include "engine/core/arena_alloc.h"
#include <gsl/pointers>
#include <gsl/assert>

namespace render {

namespace {

const void* const kHeadData = memory::ListNode::head_marker();

// 0x00416970: the geometry's destructor.
void free_shape_data(TriMeshSource* geometry) { geometry->destruct(); }

memory::ListNode* new_head() { return memory::ListNode::create_head(); }
// The first child's node (null at the end of the list - the head's data is -1).
memory::ListNode* get_first(memory::ListNode* head)
{
    memory::ListNode* node = head->next();
    return node->data() == kHeadData ? nullptr : node;
}
// The children in turn: the head-data test first, then the null one (the original's order).
template <typename F>
void for_each_child(memory::ListNode* head, F visit)
{
    for (const memory::ListNode* node = head->next(); node->data() != kHeadData && node; node = node->next()) {
        visit(static_cast<HitMesh*>(node->data()));
    }
}

} // namespace

// 0x0040B370
void HitMesh::assign_shape_data(TriMeshSource* geometry)
{
    geometry_ = geometry;
    if (geometry) {
        checksum_ = geometry->checksum();
    }
}

// 0x00413390 (the base's part of every deleting destructor)
HitMesh::~HitMesh()
{
    TriMeshSource* geometry = geometry_;
    if (geometry && ((flag_bits_ & kOwnsGeometry) != 0) && ((flag_bits_ & kSharedGeometry) == 0)) {
        free_shape_data(geometry);
        memory::Hub::dealloc(geometry);
    }
}

// 0x004133E0
HitMeshGroup::HitMeshGroup() : sub_nodes_(new_head()) {}

// 0x00411D10
HitMeshGroup::HitMeshGroup(const HitMeshGroup& other)
    : HitMesh(other), sub_nodes_(other.sub_nodes_), box_(other.box_), dirty_(other.dirty_),
      box_valid_(other.box_valid_), owner_(other.owner_)
{}

// 0x00411B10
HitMeshGroup::~HitMeshGroup()
{
    if (sub_nodes_) {
        memory::ListNode* node = sub_nodes_->next();
        if (node->data() != kHeadData && node) {
            while (node) {
                memory::ListNode* next = node->next()->data() == kHeadData ? nullptr : node->next();
                gsl::owner<HitMesh*> mesh = memory::adopt<HitMesh*>(node->data()); // (the list's)
                delete mesh;
                memory::ListNode::destroy(node); // (0x00411880)
                node = next;
            }
        }
        memory::ListNode::destroy(sub_nodes_); // (0x004131A0)
    }
    remove_from_caches(this);
}

// 0x00413490
gsl::owner<HitMeshGroup*> HitMeshGroup::replicate(int argument)
{
    gsl::owner<render::HitMeshGroup*> copy{memory::make_owned<HitMeshGroup>(*this)};
    copy->flag_bits() |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->sub_nodes_ = new_head();
    for_each_child(sub_nodes_, [&](HitMesh* child) {
        auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
        if (node) {
            node->make_node(child->replicate(argument));
        }
        memory::ListNode* head = copy->sub_nodes_;
        Expects(node != nullptr);
        node->set_prev(head->prev());
        node->set_next(head);
        head->prev()->set_next(node);
        head->set_prev(node);
    });
    return copy;
}

// 0x00411BE0
void HitMeshGroup::attach_child(gsl::owner<HitMesh*> child)
{
    memory::ListNode* node = memory::ListNode::create(child);
    memory::ListNode* head = sub_nodes_;
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
}

// 0x00411D90
bool HitMeshGroup::is_triangle_hit()
{
    bool all = true;
    for_each_child(sub_nodes_, [&](HitMesh* child) { all = all && child->is_triangle_hit(); });
    return all;
}

// 0x00411CC0
void HitMeshGroup::assign_shape_data(TriMeshSource* geometry)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->assign_shape_data(geometry); });
    dirty_ = true;
}

// 0x00411C30
void HitMeshGroup::place_at_absolute_point(const maths::Vec4* position)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->place_at_absolute_point(position); });
    dirty_ = true;
}

// 0x00411C80
void HitMeshGroup::change_facing(const maths::Mat4* orientation)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->change_facing(orientation); });
}

// 0x00411F60
const maths::Vec4* HitMeshGroup::get_absolute_point()
{
    return static_cast<HitMesh*>(get_first(sub_nodes_)->data())->get_absolute_point();
}

// 0x00411F80
const maths::Mat4* HitMeshGroup::fetch_facing()
{
    return static_cast<HitMesh*>(get_first(sub_nodes_)->data())->fetch_facing();
}

// 0x00410960
maths::Vec4 HitMeshGroup::read_vertex_point(int /*index*/) const
{
    return {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 0.0F};
}

// 0x00411E80
bool HitMeshGroup::inside_bounds(const maths::BBox* box)
{
    if (dirty_) {
        recompute_b_box();
        dirty_ = false;
    }
    return !(box_valid_ && !box->intersects(box_));
}

// 0x00411EC0
bool HitMeshGroup::collide_with_line(const maths::Seg* line, const maths::Vec4* direction, CollisionHit* hit,
                                     const maths::BBox* line_box)
{
    bool any = false;
    for_each_child(sub_nodes_, [&](HitMesh* child) {
        if (child->collide_with_line(line, direction, hit, line_box) || any) {
            any = true;
        }
    });
    return any;
}

// 0x00411F20
void HitMeshGroup::draw_diagnostic(int a, int b)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->draw_diagnostic(a, b); });
}

// 0x00411E50
maths::BBox* HitMeshGroup::get_local_bounds()
{
    if (dirty_) {
        recompute_b_box();
        dirty_ = false;
    }
    return box_valid_ ? &box_ : nullptr;
}

// 0x00411DD0
void HitMeshGroup::recompute_b_box()
{
    box_valid_ = true;
    box_.min = {.x = kEmptyBoxExtent, .y = kEmptyBoxExtent, .z = kEmptyBoxExtent, .w = 1.0F};
    box_.max = {.x = -kEmptyBoxExtent, .y = -kEmptyBoxExtent, .z = -kEmptyBoxExtent, .w = 1.0F};
    for (const memory::ListNode* node = sub_nodes_->next(); node->data() != kHeadData && node; node = node->next()) {
        const maths::BBox* box = static_cast<HitMesh*>(node->data())->get_local_bounds();
        if (!box) {
            box_valid_ = false;
            return;
        }
        box_.append_spot(box->min);
        box_.append_spot(box->max);
    }
}

} // namespace render
