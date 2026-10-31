// Collision objects: the base class and the multi-object (a collision object made of others).

#include "engine/render/coll_obj.h"

#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_tri.h"
#include "engine/core/game_vtables.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/render/coll_static.h"

namespace render {

namespace {

static VtableSlot g_head_vtable_slots[] = {&render::CollObjListNodes::destroy_head};
void** const kHeadVtable = reinterpret_cast<void**>(g_head_vtable_slots); // lists::Foremost<HitMesh>

void* const kHeadData = reinterpret_cast<void*>(-1);

// 0x00416970: the geometry's destructor.
void free_shape_data(TriMeshSource* geometry) { geometry->destruct(); }

memory::ListNode* new_head()
{
    auto* head = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (head) {
        head->data = kHeadData;
        head->priority = 0;
        head->next = head;
        head->prev = head;
        head->vtable = kHeadVtable;
    }
    return head;
}
// The first child's node (null at the end of the list - the head's data is -1).
memory::ListNode* get_first(memory::ListNode* head)
{
    memory::ListNode* node = head->next;
    return node->data == kHeadData ? nullptr : node;
}
// The children in turn: the head-data test first, then the null one (the original's order).
template <typename F>
void for_each_child(memory::ListNode* head, F f)
{
    for (memory::ListNode* node = head->next; node->data != kHeadData && node; node = node->next)
        f(static_cast<HitMesh*>(node->data));
}

} // namespace

static VtableSlot g_coll_obj_vtable_slots[] = {&render::HitMesh::scalar_delete, stubs::return_false,
        &_purecall, &_purecall, &render::HitMesh::assign_shape_data,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, stubs::return_zero, stubs::ignore_argument,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, &_purecall,
        &_purecall, &_purecall, stubs::return_zero};
void** const kCollObjVtable = reinterpret_cast<void**>(g_coll_obj_vtable_slots);
static VtableSlot g_node_vtable_slots[] = {&render::CollObjListNodes::destroy_node};
void** const kNodeVtable = reinterpret_cast<void**>(g_node_vtable_slots); // lists::Elem<HitMesh>

// 0x0040B370
void HitMesh::assign_shape_data(TriMeshSource* geometry)
{
    geometry_ = geometry;
    if (geometry)
        checksum_ = *reinterpret_cast<uint32_t*>(geometry);
}

void HitMesh::destroy_base()
{
    TriMeshSource* geometry = geometry_;
    vtable_ = kCollObjVtable;
    if (geometry && (flag_bits_ & kOwnsGeometry) && !(flag_bits_ & kSharedGeometry)) {
        free_shape_data(geometry);
        memory::Hub::get_pull()->dealloc(geometry);
    }
}

// 0x00413390
HitMesh* HitMesh::scalar_delete(uint8_t flags)
{
    destroy_base();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004131D0
void CollObjListNodes::destroy_node_body()
{
    reinterpret_cast<memory::ListNode*>(this)->unlink_as(kNodeVtable);
}

// 0x00411880 (the body inlined)
memory::ListNode* CollObjListNodes::destroy_node(uint8_t flags)
{
    destroy_node_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return reinterpret_cast<memory::ListNode*>(this);
}

// 0x00413180
memory::ListNode* CollObjListNodes::construct_head()
{
    auto* head = reinterpret_cast<memory::ListNode*>(this);
    head->data = kHeadData;
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = kHeadVtable;
    return head;
}

// 0x004131A0
memory::ListNode* CollObjListNodes::destroy_head(uint8_t flags)
{
    destroy_node_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return reinterpret_cast<memory::ListNode*>(this);
}

// 0x004133E0
HitMeshGroup* HitMeshGroup::make_new()
{
    flag_bits_ = 0;
    geometry_ = nullptr;
    vtable_ = vt::kVtblCompoundColl;
    new (&box_) maths::BBox();
    memory::ListNode* head = new_head();
    value38_ = 0;
    sub_nodes_ = head;
    dirty_ = true;
    return this;
}

// 0x00411D10
HitMeshGroup* HitMeshGroup::construct_copy(const HitMeshGroup* other)
{
    vtable_ = kCollObjVtable;
    checksum_ = other->checksum_;
    flag_bits_ = other->flag_bits_;
    visit_stamp_ = other->visit_stamp_;
    geometry_ = other->geometry_;
    vtable_ = vt::kVtblCompoundColl;
    sub_nodes_ = other->sub_nodes_;
    box_ = other->box_;
    dirty_ = other->dirty_;
    box_valid_ = other->box_valid_;
    value38_ = other->value38_;
    return this;
}

// 0x00411B10
void HitMeshGroup::destruct()
{
    vtable_ = vt::kVtblCompoundColl;
    if (sub_nodes_) {
        memory::ListNode* node = sub_nodes_->next;
        if (node->data != kHeadData && node) {
            while (node) {
                memory::ListNode* next = node->next->data == kHeadData ? nullptr : node->next;
                if (auto* child = static_cast<HitMesh*>(node->data))
                    child->get_call<void*>(0, 1);
                reinterpret_cast<void(__thiscall*)(memory::ListNode*, int)>(node->vtable[0])(node, 1);
                node = next;
            }
        }
        if (sub_nodes_)
            reinterpret_cast<void(__thiscall*)(memory::ListNode*, int)>(sub_nodes_->vtable[0])(sub_nodes_, 1);
    }
    remove_from_caches(this);
    destroy_base();
}

// 0x00413460
HitMeshGroup* HitMeshGroup::scalar_delete(uint8_t flags)
{
    destruct();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x00413490
HitMeshGroup* HitMeshGroup::replicate(int argument)
{
    void* memory = memory::alloc(sizeof(HitMeshGroup));
    HitMeshGroup* copy = memory ? static_cast<HitMeshGroup*>(memory)->construct_copy(this) : nullptr;
    copy->flag_bits_ |= kOwnsGeometry; // (a failed allocation crashes here, as in the original)
    copy->sub_nodes_ = new_head();
    for_each_child(sub_nodes_, [&](HitMesh* child) {
        auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
        if (node) {
            void* clone = child->get_call<void*>(12, argument);
            node->vtable = kNodeVtable;
            node->data = clone;
            node->priority = 0;
            node->next = node;
            node->prev = node;
        }
        memory::ListNode* head = copy->sub_nodes_;
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
    });
    return copy;
}

// 0x00411BE0
void HitMeshGroup::attach_child(HitMesh* child)
{
    auto* node = static_cast<memory::ListNode*>(memory::alloc_zeroed(sizeof(memory::ListNode)));
    if (node) {
        node->vtable = kNodeVtable;
        node->data = child;
        node->priority = 0;
        node->next = node;
        node->prev = node;
    }
    memory::ListNode* head = sub_nodes_;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// 0x00411D90
bool HitMeshGroup::is_triangle_hit()
{
    bool all = true;
    for_each_child(sub_nodes_, [&](HitMesh* child) { all = all && child->get_call<bool>(1); });
    return all;
}

// 0x00411CC0
void HitMeshGroup::set_geometry_all(void* value)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->get_call<void>(4, value); });
    dirty_ = true;
}

// 0x00411C30
void HitMeshGroup::place_at_absolute_point(const void* position)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->get_call<void>(5, position); });
    dirty_ = true;
}

// 0x00411C80
void HitMeshGroup::change_facing(const void* orientation)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->get_call<void>(6, orientation); });
}

// 0x00411F60
void* HitMeshGroup::get_absolute_point()
{
    return static_cast<HitMesh*>(get_first(sub_nodes_)->data)->get_call<void*>(7);
}

// 0x00411F80
void* HitMeshGroup::fetch_facing()
{
    return static_cast<HitMesh*>(get_first(sub_nodes_)->data)->get_call<void*>(8);
}

// 0x00410960
maths::Vec4 HitMeshGroup::read_vertex_point(int /*vertex*/) const
{
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

// 0x0040AE20
int HitMeshGroup::get_value38() const
{
    return value38_;
}

// 0x0040AE10
void HitMeshGroup::set_value38(int value)
{
    value38_ = value;
}

// 0x00411E80
int HitMeshGroup::inside_bounds(const maths::BBox* box)
{
    if (dirty_) {
        recompute_b_box();
        dirty_ = false;
    }
    if (box_valid_ && !box->intersects(box_))
        return 0;
    return 1;
}

// 0x00411EC0
bool HitMeshGroup::collide_with_line(void* a, void* b, void* c, void* d)
{
    bool hit = false;
    for_each_child(sub_nodes_, [&](HitMesh* child) {
        if (child->get_call<bool>(14, a, b, c, d) || hit)
            hit = true;
    });
    return hit;
}

// 0x00411F20
void HitMeshGroup::pass_down(void* a, void* b)
{
    for_each_child(sub_nodes_, [&](HitMesh* child) { child->get_call<void>(19, a, b); });
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
    box_.min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box_.max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    for (memory::ListNode* node = sub_nodes_->next; node->data != kHeadData && node; node = node->next) {
        auto* box = static_cast<HitMesh*>(node->data)->get_call<maths::BBox*>(20);
        if (!box) {
            box_valid_ = false;
            return;
        }
        box_.append_spot(box->min);
        box_.append_spot(box->max);
    }
}

// 0x0040AE30
uint16_t CollObjStubs::no_terrain(int /*face*/)
{
    return 0;
}

} // namespace Nx
