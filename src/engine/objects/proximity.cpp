// Proximity nodes and the restart node lists (0x0046AAC0-0x0046B790). See proximity.h.

#include "engine/objects/proximity.h"

#include <cstdint>
#include <new>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/sector.h"
#include "engine/render/coll_obj.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/ints.h"
#include "engine/core/bits_as.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace objects {

void* g_proximity_hub;

namespace {

constexpr uint32_t kName = 0xA1DC81F9;           // name
constexpr uint32_t kShape = 0x22CF0027;          // (the shape's parameter)
constexpr uint32_t kProximType = 0x7321A8D6;     // (what the node reacts to)
constexpr uint32_t kRadius = 0xC48391A5;         // radius
constexpr uint32_t kScript = 0x2CA8A299;         // (the node's script)
constexpr uint32_t kProximityTag = 0x5113F19C;   // ProximObject (a flag)
constexpr uint32_t kOwnerSectorKey = 0x08470F2E; // (the object's sector, by the node's name)
constexpr uint32_t kClass = 0x12B4E660;          // class
constexpr uint32_t kRestart = 0x1806DDF8;        // Restart

// The skaters by object id (0-31).
constexpr int kSkaterSlots = 32;

ProximityRegistry*& hub() { return memory::bits_as<ProximityRegistry*>(g_proximity_hub); }
EntityHub* entity_hub_handle() { return g_composite_registry.get(); }
Entity* entity_for_id(uint32_t id) { return entity_hub_handle()->look_up_entity(id); }

// A level sector's bounding box and world matrix (not rewritten yet).
const maths::BBox* sector_box(void* sector) { return static_cast<render::WorldCell*>(sector)->fetch_bounds(); }
const maths::Mat4* sector_world_matrix(void* sector) { return static_cast<render::WorldCell*>(sector)->fetch_facing(); }
// The sector's collision asked whether `point` is inside (the original's vtable +0x48, slot 18: is_point_behind_faces;
// `offset` gets the point from the node, passed as its second argument).
bool sector_contains(void* sector, const maths::Vec4* point, maths::Vec4* offset)
{
    render::HitMesh* collision = static_cast<render::WorldCell*>(sector)->hit_body_;
    return collision->is_point_behind_faces(point, static_cast<int>(cell::of(offset)));
}
// The level sector named `name` (null if none).
void* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }

void flag_as_dead(Entity* object) { object->flag_as_dead(); }

// The node's script, run for `object` (as a spawned script on it) or by itself; `entering` tells it which.
void execute_node_script(ProximityRegistry* manager, ProximNode* node, Entity* object, bool entering)
{
    manager->entering_ = entering;
    manager->in_script_ = true;
    if (object) {
        object->start_script_task(node->own_script_, static_cast<int>(node->label_), false, false, nullptr);
        if (entering)
            node->is_within_ |= 1u << object->ident_;
        else
            node->is_within_ &= ~(1u << object->ident_);
    } else {
        script::launch_script(node->own_script_, nullptr, nullptr, false, nullptr);
    }
    manager->in_script_ = false;
}

// `point` taken into the sector's frame about the box's centre `center`.
maths::Vec4 into_sector(const ProximNode* node, const maths::Vec4* point, const maths::Vec4& center)
{
    return (*point - center) * node->to_sector_ + center;
}

} // namespace

// 0x0046AAC0
int proxim_shape_from_checksum(uint32_t checksum)
{
    switch (checksum) {
    case 0xE460ABDE:
        return kProximShapeBox;
    case 0x03F1E5A7:
        return kProximShapeSectorBox;
    case 0x64FBA415:
        return kProximShapeNone;
    case 0x6AADF154:
        return kProximShapeSector;
    case 0xAA069978: // (the sphere's own name)
    default:
        return kProximShapeSphere;
    }
}

// 0x0046AB10
void proxim_allocate_manager()
{
    auto* manager = static_cast<ProximityRegistry*>(memory::alloc_zeroed(sizeof(ProximityRegistry)));
    if (manager)
        manager->in_script_ = false;
    hub() = manager;
}

// 0x0046AB30
void nearby_zone_startup()
{
    if (hub())
        return;
    proxim_allocate_manager();
    hub()->head_ = nullptr; // (out of memory: a null write, as the original)
}

// 0x0046AB50
void nearby_zone_shutdown()
{
    const ProximityRegistry* manager = hub();
    if (!manager)
        return;
    for (ProximNode* node = manager->head_; node;) {
        ProximNode* next = node->succ_;
        memory::Hub::get_pull()->dealloc(node);
        node = next;
    }
    hub()->head_ = nullptr;
}

// 0x0046AB90
ProximNode* proxim_get_node(uint32_t name)
{
    // (no manager: a null read, as the original)
    for (ProximNode* node = hub()->head_; node; node = node->succ_) {
        if (node->label_ == name)
            return node;
    }
    return nullptr;
}

// 0x0046ABC0
void ProximNode::change_live(bool active)
{
    if (active_ == active)
        return;
    if (active_) {
        if (entity_key_ != 0) {
            // its objects killed
            for (int id = 0; entities_ != 0; ++id) {
                if (entities_ & 1) {
                    if (Entity* object = entity_for_id(entity_key_ + id))
                        flag_as_dead(object);
                }
                entities_ >>= 1;
            }
        } else if (ProximityRegistry* manager = hub()) {
            // the exit script for each skater inside
            for (int id = 0; is_within_ != 0; ++id) {
                if (is_within_ & 1) {
                    Entity* object = entity_for_id(id);
                    manager->entering_ = false;
                    manager->in_script_ = true;
                    if (object)
                        object->start_script_task(own_script_, static_cast<int>(label_), false, false, nullptr);
                    manager->in_script_ = false;
                }
                is_within_ >>= 1;
            }
        }
    }
    outside_ = 0xFFFFFFFF;
    active_ = active;
}

// 0x0046ACB0
void proximity_node_enable(uint32_t name, int active)
{
    const ProximityRegistry* manager = hub();
    if (!manager)
        return;
    for (ProximNode* node = manager->head_; node; node = node->succ_) {
        if (node->label_ == name) {
            node->change_live(active != 0);
            return;
        }
    }
}

// 0x0046ACF0
void ProximNode::set_sector(void* sector)
{
    sector_ = sector;
    if (!sector)
        return;
    switch (form_) {
    case kProximShapeBox:
        box_ = *sector_box(sector);
        break;
    case kProximShapeSectorBox:
    case kProximShapeSector:
        box_ = *sector_box(sector);
        to_sector_ = *sector_world_matrix(sector_);
        to_sector_.upend();
        break;
    default:
        break;
    }
}

// 0x0046AD50
bool ProximNode::is_within(const maths::Vec4* point)
{
    maths::Vec4 offset; // (w not set)
    offset.x = point->x - at_point_.x;
    offset.y = point->y - at_point_.y;
    offset.z = point->z - at_point_.z;
    switch (form_) {
    case kProximShapeSphere: {
        // (summed unrounded, z first, as the original)
        double distance = static_cast<double>(offset.z) * offset.z + static_cast<double>(offset.y) * offset.y;
        distance += static_cast<double>(offset.x) * offset.x;
        return distance <= reach_sq_;
    }
    case kProximShapeBox:
        return box_.contains_point(*point) != 0;
    case kProximShapeSectorBox: {
        maths::Vec4 center;
        box_.get_center(center);
        const maths::Vec4 local = into_sector(this, point, center);
        return box_.contains_point(local) != 0;
    }
    case kProximShapeSector:
        return sector_contains(sector_, point, &offset);
    default:
        return false;
    }
}

// 0x0046AEC0
maths::Vec4* ProximNode::nearest_point_on(maths::Vec4* out, const maths::Vec4* point)
{
    switch (form_) {
    case kProximShapeSphere: {
        maths::Vec4 direction = *point - at_point_;
        direction.make_unit();
        out->x = direction.x * radius_ + at_point_.x;
        out->y = direction.y * radius_ + at_point_.y;
        out->z = direction.z * radius_ + at_point_.z;
        // (w's product unrounded before the add, as the original)
        out->w = static_cast<float>(static_cast<double>(radius_) * direction.w + at_point_.w);
        return out;
    }
    case kProximShapeBox: {
        maths::Vec4 closest;
        box_.get_closest_surface_point(*point, closest);
        *out = closest;
        return out;
    }
    case kProximShapeSectorBox:
    case kProximShapeSector: {
        // into the sector's frame, the box's nearest point, and back out by its world matrix
        maths::Vec4 center;
        box_.get_center(center);
        const maths::Vec4 local = into_sector(this, point, center);
        maths::Vec4 closest;
        box_.get_closest_surface_point(local, closest);
        closest = closest - center;
        *out = closest * *sector_world_matrix(sector_) + center;
        return out;
    }
    default:
        // (the original copies its uninitialised locals)
        return out;
    }
}

// 0x0046B1F0
void nearby_zone_add_node(uint32_t name, bool active)
{
    auto* node = static_cast<ProximNode*>(memory::alloc_zeroed(sizeof(ProximNode)));
    if (node)
        new (&node->box_) maths::BBox();
    const ProximityRegistry* manager = hub();
    node->succ_ = manager->head_;
    hub()->head_ = node;
    node->label_ = name;
    node->active_ = active;
    node->sector_ = nullptr;
    node->form_ = kProximShapeSphere;
    script::Struct* params = level_nodes::fetch_vertex(name, true);
    params->read_hash_value(kName, &node->label_, 0);
    uint32_t shape = 0;
    if (params->read_hash_value(kShape, &shape, 0))
        node->form_ = proxim_shape_from_checksum(shape);
    node->set_sector(look_up_sector(node->label_));
    level_nodes::fetch_place(params, &node->at_point_);
    node->kind_tag_ = 0;
    uint32_t type = 0;
    if (params->read_hash_value(kProximType, &type, 0)) {
        if (type == 0x57525413)
            node->kind_tag_ = 2;
        else if (type == 0x806FFF30)
            node->kind_tag_ = 0;
        else
            node->kind_tag_ = type == 0xC4E311FA ? 1 : 0;
    }
    params->fetch_float(kRadius, &node->radius_, 0);
    node->reach_sq_ = node->radius_ * node->radius_;
    params->read_hash_value(kScript, &node->own_script_, 0);
    node->entity_key_ = 0;
    node->entities_ = 0;
    node->is_within_ = 0;
    if (params->has_bare_flag(kProximityTag)) {
        node->outside_ = 0xFFFFFFFF;
        params->read_hash_value(kName, &node->entity_key_, 0);
        return;
    }
    node->outside_ = 0;
    node->active_ = true;
    int id = 0;
    for (Entity* skater = entity_for_id(0); skater && id < kSkaterSlots; skater = entity_for_id(++id)) {
        if (core::down_cast<GameEntity>(skater)->is_local_skater_)
            node->is_within_ |= 1u << id;
    }
    node->change_live(false);
    node->change_live(active);
}

// 0x0046B400
void proximity_notify(uint32_t mask, Entity* object, const maths::Vec4* point, int type)
{
    ProximityRegistry* manager = hub();
    if (!manager)
        return;
    for (ProximNode* node = manager->head_; node; node = node->succ_) {
        if (!node->active_ || node->kind_tag_ != type)
            continue;
        const bool inside = node->is_within(point);
        if (node->outside_ & mask) {
            if (!inside)
                continue;
            // entered
            node->outside_ &= ~mask;
            if (node->entity_key_ == 0) {
                execute_node_script(manager, node, object, true);
            } else if (object) {
                GameEntity* made = entity_hub_handle()->create_composite_object();
                made->change_serial(node->entity_key_ + object->ident_);
                made->at_point_ = node->at_point_;
                node->entities_ |= 1u << object->ident_;
                script::Struct params;
                if (node->sector_)
                    params.put_name_key(kOwnerSectorKey, node->label_);
                made->build_modules_from_list(script::fetch_list("proximobj_composite_structure"), &params);
                made->seal();
                script::Struct script_params;
                made->replace_script(node->own_script_, &script_params);
                script_params.get_clear();
                params.get_clear();
            }
        } else {
            if (inside)
                continue;
            // left
            node->outside_ |= mask;
            if (node->entity_key_ == 0) {
                execute_node_script(manager, node, object, false);
            } else if (object) {
                if (Entity* made = entity_for_id(node->entity_key_ + object->ident_))
                    flag_as_dead(made);
                node->entities_ &= ~(1u << object->ident_);
            }
        }
    }
}

// 0x0046B680
bool is_restart_node(uint32_t name)
{
    uint32_t node_class = 0;
    level_nodes::fetch_vertex(name, true)->read_hash_value(kClass, &node_class, 0);
    return node_class == kRestart;
}

// 0x0046B6C0
int get_restart_nodes(uint32_t type, uint32_t* out, uint32_t start)
{
    int count = 0;
    level_nodes::start_node_walk(start);
    if (start)
        level_nodes::get_next_node();
    for (script::Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
        const uint32_t name = level_nodes::get_node_name(node);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class != kRestart)
            continue;
        if (type == 0) {
            out[count++] = name;
            continue;
        }
        script::Array* types = nullptr;
        if (!node->fetch_list("restart_types", &types, 0))
            continue;
        for (int i = 0; i < ints::as_signed(types->extent_); ++i) {
            if (types->fetch_part(i) == type)
                out[count++] = name;
        }
    }
    level_nodes::end_node_walk();
    return count;
}

// 0x0046B790
uint32_t get_spawn_node(uint32_t type, int index, uint32_t start)
{
    std::array<uint32_t, 256> names{};
    const int count = get_restart_nodes(type, names.data(), start);
    if (count <= 0)
        return 0;
    // (a negative index reads before the list, as the original)
    return index >= count ? core::at(names, index % count) : core::at(names, index);
}

} // namespace objects
