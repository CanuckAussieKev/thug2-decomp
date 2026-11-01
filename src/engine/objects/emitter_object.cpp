// Sound emitter nodes and the follow trail (0x004680E0-0x00468AFE). See emitter_object.h.

#include "engine/objects/emitter_object.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/sector.h"

#include <cmath>
#include <cstdint>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/lists/key_map.h"
#include "engine/objects/entity.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/bits_as.h"
#include "engine/core/down_cast.h"

namespace objects {

void* g_emitter_table;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;   // name
constexpr uint32_t kShape = 0x7321A8D6;  // (the shape's parameter; the proximity nodes' type)
constexpr uint32_t kRadius = 0xC48391A5; // radius
constexpr uint32_t kScript = 0x2CA8A299; // (the node's script)

lists::KeyMap*& get_table() { return memory::bits_as<lists::KeyMap*>(g_emitter_table); }

// The level sector named `name` (null if none) and its bounding box (not rewritten yet).
void* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }
const maths::BBox* sector_box(void* sector) { return static_cast<render::WorldCell*>(sector)->fetch_bounds(); }

// The node whose item the table's iteration is at (null at the end).
EmitterNode* step_iterator(lists::KeyMap* table)
{
    if (!table->step_iterator() || !table->iter_item_)
        return nullptr;
    return static_cast<EmitterNode*>(table->iter_item_->value);
}
void start_iteration(lists::KeyMap* table)
{
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
}

constexpr float kTrailSpacingSquared = 1600.0f; // 0x006479F4
constexpr float kFacingWeight = 10.0f;          // 0x00646710

} // namespace

// 0x004680E0
EmitterNode* emitter_get_node(uint32_t name) { return static_cast<EmitterNode*>(get_table()->fetch_entry(name)); }

// 0x00468120
bool EmitterNode::is_within(const maths::Vec4* point)
{
    const float dx = point->x - at_point_.x;
    const float dy = point->y - at_point_.y;
    const float dz = point->z - at_point_.z;
    switch (form_) {
    case kEmitterShapeBox:
        return box_.contains_point(*point) != 0;
    case kEmitterShapeSphere: {
        // (summed unrounded: z, x, then y)
        double distance = static_cast<double>(dz) * dz + static_cast<double>(dx) * dx;
        distance += static_cast<double>(dy) * dy;
        return distance <= reach_sq_;
    }
    default:
        return false;
    }
}

// 0x004681A0
maths::Vec4* EmitterNode::nearest_point_on(maths::Vec4* out, const maths::Vec4* point)
{
    if (is_within(point)) {
        *out = *point;
        return out;
    }
    switch (form_) {
    case kEmitterShapeBox: {
        maths::Vec4 closest;
        box_.get_closest_surface_point(*point, closest);
        *out = closest;
        break;
    }
    case kEmitterShapeSphere: {
        maths::Vec4 direction = *point - at_point_;
        direction.make_unit();
        out->x = direction.x * radius_ + at_point_.x;
        out->y = direction.y * radius_ + at_point_.y;
        out->z = direction.z * radius_ + at_point_.z;
        // (w's product unrounded before the add, as the original)
        out->w = static_cast<float>(static_cast<double>(radius_) * direction.w + at_point_.w);
        break;
    }
    default:
        break; // (the original copies its uninitialised locals)
    }
    return out;
}

// 0x004682B0
void emitter_cleanup()
{
    lists::KeyMap* table = get_table();
    if (!table)
        return;
    start_iteration(table);
    for (EmitterNode* node = step_iterator(get_table()); node;) {
        EmitterNode* next = step_iterator(get_table());
        const uint32_t name = node->label_;
        memory::Hub::get_pull()->dealloc(node);
        get_table()->erase_entry(name);
        node = next;
    }
}

// 0x00468340
void emitter_set_active(uint32_t name, int active)
{
    // (no table: a null write, as the original)
    start_iteration(get_table());
    for (EmitterNode* node = step_iterator(get_table()); node; node = step_iterator(get_table())) {
        if (node->label_ == name) {
            node->active_ = active != 0;
            return;
        }
    }
}

// 0x004683B0
void emitter_init()
{
    if (get_table()) {
        emitter_cleanup();
        return;
    }
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    get_table() = memory ? memory::construct_in<lists::KeyMap>(memory, 4) : nullptr;
}

// 0x00468450
void emitter_add_node(uint32_t name, bool active)
{
    auto* node = static_cast<EmitterNode*>(memory::alloc_zeroed(sizeof(EmitterNode)));
    if (node)
        new (&node->box_) maths::BBox();
    node->label_ = name;
    node->active_ = active;
    node->sector_ = nullptr;
    node->form_ = kEmitterShapeSphere;
    script::Struct* params = level_nodes::fetch_vertex(name, true);
    params->read_hash_value(kName, &node->label_, 0);
    get_table()->store_entry(node->label_, node);
    uint32_t shape = 0;
    if (params->read_hash_value(kShape, &shape, 0)) {
        if (shape == 0x03F1E5A7)
            node->form_ = kEmitterShapeOther;
        else if (shape == 0xAA069978)
            node->form_ = kEmitterShapeSphere;
        else
            node->form_ = shape == 0xE460ABDE ? kEmitterShapeBox : kEmitterShapeSphere;
    }
    node->sector_ = look_up_sector(node->label_);
    if (node->sector_ && node->form_ == kEmitterShapeBox)
        node->box_ = *sector_box(node->sector_);
    level_nodes::fetch_place(params, &node->at_point_);
    params->fetch_float(kRadius, &node->radius_, 0);
    node->reach_sq_ = node->radius_ * node->radius_;
    params->read_hash_value(kScript, &node->own_script_, 0);
}

// 0x00468AC0
FollowTrail::FollowTrail()
{
    get_reset();
}

// 0x004685D0
void FollowTrail::get_reset()
{
    num_ = 0;
    head_ = 0;
    entity_key_ = 0;
    aim_point_ = {0.0f, 0.0f, 0.0f, 1.0f};
    reach_ = 0.0f;
    free_facing_ = false;
}

// 0x00468610
void FollowTrail::tick_frame()
{
    auto* object = core::down_cast<GameEntity>(find_object(entity_key_));
    if (!object)
        return;
    aim_point_ = object->at_point_;
    if (num_ == 0) {
        head_ = 0;
        path_vertices_[0] = aim_point_;
        num_ = 1;
        return;
    }
    const maths::Vec4& last = path_vertices_[head_];
    const float dx = last.x - aim_point_.x;
    const float dy = last.y - aim_point_.y;
    const float dz = last.z - aim_point_.z;
    // (summed unrounded: z, x, then y)
    double distance = static_cast<double>(dz) * dz + static_cast<double>(dx) * dx;
    distance += static_cast<double>(dy) * dy;
    if (!(distance > kTrailSpacingSquared))
        return;
    if (++head_ >= 64)
        head_ = 0;
    path_vertices_[head_] = aim_point_;
    if (num_ < 64)
        ++num_;
}

// 0x00468730
void FollowTrail::fetch_place(maths::Vec4* position, maths::Mat4* matrix)
{
    int left = num_;
    if (left < 1)
        return;
    // walk back from the object through the trail until the distance is used up (the remainder is kept
    // unrounded, as the original's x87 register)
    double remaining = reach_;
    maths::Vec4 previous = aim_point_;
    int index = head_;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f, dw = 0.0f, length = 0.0f;
    for (;;) {
        const maths::Vec4& point = path_vertices_[index];
        dx = static_cast<float>(static_cast<double>(point.x) - previous.x);
        dy = point.y - previous.y;
        dz = point.z - previous.z;
        dw = point.w - previous.w;
        --left;
        length = static_cast<float>(
            std::sqrt(static_cast<double>(dy) * dy + static_cast<double>(dx) * dx + static_cast<double>(dz) * dz));
        if (!(remaining > length))
            break;
        remaining -= length;
        previous = point;
        if (left == 0)
            break;
        if (--index < 0)
            index = 63;
    }
    if (!(length > 0.0f))
        return;
    // the unit direction back from that segment's point (times -1 / length, unrounded), and the point the
    // remaining distance along it
    const double scale = -1.0 / length;
    auto nx = static_cast<float>(dx * scale);
    auto ny = static_cast<float>(dy * scale);
    auto nz = static_cast<float>(dz * scale);
    auto nw = static_cast<float>(dw * scale);
    auto ox = static_cast<float>(nx * remaining);
    auto oy = static_cast<float>(ny * remaining);
    auto oz = static_cast<float>(nz * remaining);
    position->x = previous.x - ox;
    position->y = previous.y - oy;
    position->z = previous.z - oz;
    position->w = static_cast<float>(previous.w - nw * remaining); // (then overwritten)
    position->w = 1.0f;

    // the facing: the old front x10 plus the direction (x unrounded before the add, as the original)
    maths::Vec4& front = matrix->row[2];
    const float fy = front.y * kFacingWeight;
    const float fz = front.z * kFacingWeight;
    const float fw = front.w * kFacingWeight;
    front.x = static_cast<float>(static_cast<double>(front.x) * kFacingWeight + nx);
    front.y = fy + ny;
    front.z = fz + nz;
    front.w = fw + nw;
    if (free_facing_) {
        front.w = 1.0f;
        front.make_unit();
        const maths::Vec4 up = {0.0f, 1.0f, 0.0f, 1.0f};
        matrix->row[0] = maths::cross_vec(up, front);
        matrix->row[0].make_unit();
        matrix->row[1] = maths::cross_vec(front, matrix->row[0]);
    } else {
        front.y = 0.0f;
        front.w = 1.0f;
        front.make_unit();
        matrix->row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
        matrix->row[0] = maths::cross_vec(matrix->row[1], front);
    }
}

} // namespace objects
