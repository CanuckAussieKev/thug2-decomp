// Proximity nodes and the restart node lists (0x0046AAC0-0x0046B790). See proximity.h.

#include "engine/objects/proximity.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <array>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/sector.h"
#include "engine/maths/shape_tests.h"
#include "engine/render/coll_obj.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include "engine/maths/vec4.h"
#include <gsl/pointers>
#include <span>

namespace objects {

namespace {
constexpr std::size_t kNameCapacity = 256;
constexpr uint32_t kBoundingBox = 0xE460ABDE; // BoundingBox
constexpr uint32_t kOtherShape = 0x03F1E5A7; // text unknown (the shape that is never inside)
constexpr uint32_t kCylinder = 0x64FBA415; // Cylinder
constexpr uint32_t kGeometry = 0x6AADF154; // Geometry
constexpr uint32_t kSphere = 0xAA069978; // Sphere
constexpr uint32_t kAllBitsSet = 0xFFFFFFFF; // (text unknown)
constexpr uint32_t kObject = 0x57525413; // Object
constexpr uint32_t kNone = 0x806FFF30; // None
constexpr uint32_t kCamera = 0xC4E311FA; // Camera

ProximityRegistry*& stored_proximity_hub()
{
    static ProximityRegistry* proximity_hub{};
    return proximity_hub;
}

} // namespace

ProximityRegistry* proximity_hub() { return stored_proximity_hub(); }

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

EntityHub* entity_hub_handle() { return objects::object_hub_slot().get(); }
Entity* entity_for_id(uint32_t id) { return objects::EntityHub::look_up_entity(id); }

// A level sector's bounding box and world matrix (not rewritten yet).
const maths::BBox* sector_box(void* sector) { return static_cast<render::WorldCell*>(sector)->fetch_bounds(); }
const maths::Mat4* sector_world_matrix(void* sector) { return static_cast<render::WorldCell*>(sector)->fetch_facing(); }
// The sector's collision asked whether `point` is inside (the original's vtable +0x48, slot 18: is_point_behind_faces;
// the original passed `offset`'s address as its second argument, which isn't read).
bool sector_contains(void* sector, const maths::Vec4* point, maths::Vec4* /*offset*/)
{
    render::HitMesh* collision = static_cast<render::WorldCell*>(sector)->hit_body();
    return collision->is_point_behind_faces(point, 0);
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
        object->start_script_task(node->own_script(), static_cast<int>(node->label()), false, false, nullptr);
        if (entering) {
            node->get_is_within() |= 1U << object->ident();
        } else {
            node->get_is_within() &= ~(1U << object->ident());
        }
    } else {
        script::launch_script(node->own_script(), nullptr, nullptr, false, nullptr);
    }
    manager->in_script_ = false;
}

// `point` taken into the sector's frame about the box's centre `center`.
maths::Vec4 into_sector(const ProximNode* node, const maths::Vec4* point, const maths::Vec4& center)
{
    return (*point - center) * node->to_sector() + center;
}

} // namespace

// 0x0046AAC0
ProximShape proxim_shape_from_checksum(uint32_t checksum)
{
    switch (checksum) {
    case kBoundingBox:
        return objects::ProximShape::kBox;
    case kOtherShape:
        return objects::ProximShape::kSectorBox;
    case kCylinder:
        return objects::ProximShape::kNone;
    case kGeometry:
        return objects::ProximShape::kSector;
    case kSphere: // (the sphere's own name)
    default:
        return objects::ProximShape::kSphere;
    }
}

// 0x0046AB10
void proxim_allocate_manager()
{
    auto* manager = static_cast<ProximityRegistry*>(memory::alloc_zeroed(sizeof(ProximityRegistry)));
    if (manager) {
        manager->in_script_ = false;
    }
    stored_proximity_hub() = manager;
}

// 0x0046AB30
void nearby_zone_startup()
{
    if (stored_proximity_hub()) {
        return;
    }
    proxim_allocate_manager();
    stored_proximity_hub()->head_ = nullptr; // (out of memory: a null write, as the original)
}

// 0x0046AB50
void nearby_zone_shutdown()
{
    const ProximityRegistry* manager = stored_proximity_hub();
    if (!manager) {
        return;
    }
    for (ProximNode* node = manager->head_; node;) {
        ProximNode* next = node->succ();
        memory::Hub::dealloc(node);
        node = next;
    }
    stored_proximity_hub()->head_ = nullptr;
}

// 0x0046AB90
ProximNode* proxim_get_node(uint32_t name)
{
    // (no manager: a null read, as the original)
    for (ProximNode* node = stored_proximity_hub()->head_; node; node = node->succ()) {
        if (node->label() == name) {
            return node;
        }
    }
    return nullptr;
}

// 0x0046ABC0
void ProximNode::change_live(bool active)
{
    if (active_ == active) {
        return;
    }
    if (active_) {
        if (entity_key_ != 0) {
            // its objects killed
            for (int id = 0; entities_ != 0; ++id) {
                if ((entities_ & 1) != 0U) {
                    if (Entity* object = entity_for_id(entity_key_ + id)) {
                        flag_as_dead(object);
                    }
                }
                entities_ >>= 1;
            }
        } else if (ProximityRegistry* manager = stored_proximity_hub()) {
            // the exit script for each skater inside
            for (int id = 0; is_within_ != 0; ++id) {
                if ((is_within_ & 1) != 0U) {
                    Entity* object = entity_for_id(id);
                    manager->entering_ = false;
                    manager->in_script_ = true;
                    if (object) {
                        object->start_script_task(own_script_, static_cast<int>(label_), false, false, nullptr);
                    }
                    manager->in_script_ = false;
                }
                is_within_ >>= 1;
            }
        }
    }
    outside_ = kAllBitsSet;
    active_ = active;
}

// 0x0046ACB0
void proximity_node_enable(checksums::Name node_name, int active)
{
    const uint32_t name = node_name.value();
    const ProximityRegistry* manager = stored_proximity_hub();
    if (!manager) {
        return;
    }
    for (ProximNode* node = manager->head_; node; node = node->succ()) {
        if (node->label() == name) {
            node->change_live(active != 0);
            return;
        }
    }
}

// 0x0046ACF0
void ProximNode::set_sector(void* sector)
{
    sector_ = sector;
    if (!sector) {
        return;
    }
    switch (form_) {
    case objects::ProximShape::kBox:
        box_ = *sector_box(sector);
        break;
    case objects::ProximShape::kSectorBox:
    case objects::ProximShape::kSector:
        box_ = *sector_box(sector);
        to_sector_ = *sector_world_matrix(sector_);
        to_sector_.upend();
        break;
    case objects::ProximShape::kSphere:
    case objects::ProximShape::kNone:
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
    case objects::ProximShape::kSphere: {
        // (summed unrounded, z first, as the original)
        double distance = (static_cast<double>(offset.z) * offset.z) + (static_cast<double>(offset.y) * offset.y);
        distance += static_cast<double>(offset.x) * offset.x;
        return distance <= reach_sq_;
    }
    case objects::ProximShape::kBox:
        return box_.contains_point(*point) != 0;
    case objects::ProximShape::kSectorBox: {
        maths::Vec4 center;
        box_.get_center(center);
        const maths::Vec4 local = into_sector(this, point, center);
        return box_.contains_point(local) != 0;
    }
    case objects::ProximShape::kSector:
        return sector_contains(sector_, point, &offset);
    case objects::ProximShape::kNone:
        break;
    }
    return false;
}

// 0x0046AEC0
maths::Vec4* ProximNode::nearest_point_on(maths::Vec4* out, const maths::Vec4* point)
{
    switch (form_) {
    case objects::ProximShape::kSphere: {
        maths::Vec4 direction = *point - at_point_;
        direction.make_unit();
        out->x = (direction.x * radius_) + at_point_.x;
        out->y = (direction.y * radius_) + at_point_.y;
        out->z = (direction.z * radius_) + at_point_.z;
        // (w's product unrounded before the add, as the original)
        out->w = static_cast<float>((static_cast<double>(radius_) * direction.w) + at_point_.w);
        return out;
    }
    case objects::ProximShape::kBox: {
        maths::Vec4 closest;
        box_.get_closest_surface_point(*point, closest);
        *out = closest;
        return out;
    }
    case objects::ProximShape::kSectorBox:
    case objects::ProximShape::kSector: {
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
    case objects::ProximShape::kNone:
        break;
    }
    return out; // (the original copies its uninitialised locals)
}

// 0x0046B1F0
void nearby_zone_add_node(uint32_t name, bool active)
{
    auto* node = static_cast<ProximNode*>(memory::alloc_zeroed(sizeof(ProximNode)));
    if (node) {
        new (&node->box()) maths::BBox();
    }
    const ProximityRegistry* manager = stored_proximity_hub();
    node->set_succ(manager->head_);
    stored_proximity_hub()->head_ = node;
    node->label() = name;
    node->set_active(active);
    node->assign_sector(nullptr);
    node->set_form(objects::ProximShape::kSphere);
    script::Struct* params = level_nodes::fetch_vertex(name, true);
    params->read_hash_value(kName, &node->label(), 0);
    uint32_t shape = 0;
    if (params->read_hash_value(kShape, &shape, 0)) {
        node->set_form(proxim_shape_from_checksum(shape));
    }
    node->set_sector(look_up_sector(node->label()));
    level_nodes::fetch_place(params, &node->at_point());
    node->set_kind_tag(0);
    uint32_t type = 0;
    if (params->read_hash_value(kProximType, &type, 0)) {
        if (type == kObject) {
            node->set_kind_tag(2);
        } else if (type == kNone) {
            node->set_kind_tag(0);
        } else {
            node->set_kind_tag(type == kCamera ? 1 : 0);
        }
    }
    params->fetch_float(kRadius, &node->radius(), 0);
    node->set_reach_sq(node->radius() * node->radius());
    params->read_hash_value(kScript, &node->own_script(), 0);
    node->entity_key() = 0;
    node->entities() = 0;
    node->get_is_within() = 0;
    if (params->has_bare_flag(kProximityTag)) {
        node->outside() = kAllBitsSet;
        params->read_hash_value(kName, &node->entity_key(), 0);
        return;
    }
    node->outside() = 0;
    node->set_active(true);
    int id = 0;
    for (Entity* skater = entity_for_id(0); skater && id < kSkaterSlots; skater = entity_for_id(++id)) {
        if (core::down_cast<GameEntity>(skater)->is_local_skater() != 0U) {
            node->get_is_within() |= 1U << id;
        }
    }
    node->change_live(false);
    node->change_live(active);
}

// 0x0046B400
void proximity_notify(uint32_t mask, Entity* object, const maths::Vec4* point, int type)
{
    ProximityRegistry* manager = stored_proximity_hub();
    if (!manager) {
        return;
    }
    for (ProximNode* node = manager->head_; node; node = node->succ()) {
        if (!node->active() || node->kind_tag() != type) {
            continue;
        }
        const bool inside = node->is_within(point);
        if ((node->outside() & mask) != 0U) {
            if (!inside) {
                continue;
            }
            // entered
            node->outside() &= ~mask;
            if (node->entity_key() == 0) {
                execute_node_script(manager, node, object, true);
            } else if (object) {
                gsl::owner<GameEntity*> made = entity_hub_handle()->create_composite_object();
                made->change_serial(node->entity_key() + object->ident());
                made->at_point() = node->at_point();
                node->entities() |= 1U << object->ident();
                script::Struct params;
                if (node->sector()) {
                    params.put_name_key(checksums::Name{kOwnerSectorKey}, node->label());
                }
                made->build_modules_from_list(script::fetch_list("proximobj_composite_structure"), &params);
                made->seal();
                script::Struct script_params;
                made->replace_script(node->own_script(), &script_params);
                script_params.get_clear();
                params.get_clear();
            }
        } else {
            if (inside) {
                continue;
            }
            // left
            node->outside() |= mask;
            if (node->entity_key() == 0) {
                execute_node_script(manager, node, object, false);
            } else if (object) {
                if (Entity* made = entity_for_id(node->entity_key() + object->ident())) {
                    flag_as_dead(made);
                }
                node->entities() &= ~(1U << object->ident());
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
int get_restart_nodes(uint32_t type, std::span<uint32_t> out, uint32_t start)
{
    int count = 0;
    level_nodes::start_node_walk(start);
    if (start != 0U) {
        level_nodes::get_next_node();
    }
    for (script::Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
        const uint32_t name = level_nodes::get_node_name(node);
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class != kRestart) {
            continue;
        }
        if (type == 0) {
            core::at(out, count++) = name;
            continue;
        }
        script::Array* types = nullptr;
        if (!node->fetch_list("restart_types", &types, 0)) {
            continue;
        }
        for (int i = 0; i < ints::as_signed(types->extent()); ++i) {
            if (types->fetch_part(i) == type) {
                core::at(out, count++) = name;
            }
        }
    }
    level_nodes::end_node_walk();
    return count;
}

// 0x0046B790
uint32_t get_spawn_node(checksums::Name spawn_type, int index, uint32_t start)
{
    const uint32_t type = spawn_type.value();
    std::array<uint32_t, kNameCapacity> names{};
    const int count = get_restart_nodes(type, names, start);
    if (count <= 0) {
        return 0;
    }
    // (a negative index reads before the list, as the original)
    return index >= count ? core::at(names, index % count) : core::at(names, index);
}

} // namespace objects
