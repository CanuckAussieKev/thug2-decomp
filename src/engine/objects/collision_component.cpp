// The collision component (GameObjects::ContactShapeModule): an object's own movable collision object, made from its node's
// CollisionMode and its model's (or its level sector's) collision, placed where the object is.

#include <cstdint>
#include <gsl/pointers>

#include "engine/objects/collision_component.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/model.h"
#include "engine/gfx/engine.h"

#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/net/buddy_status.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_world.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/gfx/weather.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/model_component.h"
#include "engine/gfx/sector.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"
#include "engine/gfx/scene_load.h"
#include "engine/core/checked_index.h"

namespace objects {

namespace {

constexpr uint32_t kModelModuleName = 0x286A8D26; // Model
constexpr uint32_t kCollisionMode = 0x2D7E583B;   // CollisionMode
constexpr uint32_t kClass = 0x12B4E660;           // Class
constexpr uint32_t kLevelObject = 0xB7B3BD86;     // LevelObject
constexpr uint32_t kName = 0xA1DC81F9;            // Name

// The model component's model (+0x208).
uint8_t* model_of(ObjectModule* model)
{
    return raw::bytes(core::down_cast<objects::MeshInstanceModule>(model)->mesh_instance_);
}

// A level sector by name in any scene (0x0048C510) and its collision object (0x005480F0: +0x18).
uint8_t* look_up_sector(uint32_t name) { return static_cast<uint8_t*>(render::engine::look_up_sector(name)); }
render::HitMesh* sector_collision(uint8_t* sector) { return raw::as<render::WorldCell>(sector)->hit_body_; }

// The model's bone matrix (0x00493040: a single-bone model's, from the asset manager; null otherwise): its rotation at
// +0x10, its translation at +0x40 (the matrix's fourth row).
const render::HierarchyNode* bone_of(uint8_t* model) { return raw::as<render::RenderModel>(model)->get_asset_hierarchy(); }

void d3dx_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

} // namespace

// 0x0041C920
gsl::owner<ObjectModule*> ContactShapeModule::fabricate() { return new ContactShapeModule; }

// (the constructor: inlined in fabricate in the original)
ContactShapeModule::ContactShapeModule() { kind_tag_ = kType; }

// 0x0041C710 (0x0041C780 the deleting one)
ContactShapeModule::~ContactShapeModule()
{
    if (hit_shape_) {
        render::unregister_movable(hit_shape_);
        delete hit_shape_;
    }
}

// 0x0041C7B0
void ContactShapeModule::configure_from_data(script::Struct* params)
{
    uint32_t mode = 0;
    params->read_hash_value(kCollisionMode, &mode, 0);
    const int type = render::shape_type_from_checksum(mode);
    if (!type)
        return;
    uint32_t cls = 0;
    params->read_hash_value(kClass, &cls, 0);
    if (cls != kLevelObject) {
        set_up_hit_shape(type, nullptr);
        return;
    }
    // (the original passes on whatever was in the name's slot when there's no Name - an assertion's case; ours 0)
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    uint8_t* sector = look_up_sector(name); // (no such sector: a crash, as in the original)
    if (sector_collision(sector))
        set_up_hit_shape(type, sector_collision(sector)->geometry_);
    else
        set_up_hit_shape(type, nullptr);
}

// 0x0041C890
void ContactShapeModule::set_up_hit_shape(int type, render::TriMeshSource* geometry)
{
    if (geometry) {
        hit_shape_ = render::build_dynamic_hit_mesh(type, geometry, 1, static_cast<int>(cell::of(entity_)));
        return;
    }
    uint8_t* model = model_of(entity_->fetch_module(kModelModuleName));
    auto* data = static_cast<uint8_t*>(
        assets::asset_manager()->get_asset_data(raw::as<render::RenderModel>(model)->source_path_, 0));
    if (data) {
        hit_shape_ =
            render::build_dynamic_hit_mesh(type, *raw::field_at<render::TriMeshSource*>(data, 0x8C),
                                           *raw::field_at<int>(data, 0x88), static_cast<int>(cell::of(entity_)));
    } else {
        hit_shape_ = render::build_dynamic_hit_mesh(type, nullptr, 0, static_cast<int>(cell::of(entity_)));
    }
}

// 0x0041C980
void ContactShapeModule::tick_frame()
{
    render::HitMesh* collision = hit_shape_;
    if (!collision)
        return;
    ObjectModule* model = entity_->fetch_module(kModelModuleName);
    const render::HierarchyNode* bone = model ? bone_of(model_of(model)) : nullptr;
    if (!bone) {
        collision->place_at_absolute_point(&entity_->at_point_);
        collision->change_facing(&entity_->display_matrix_);
        return;
    }
    const maths::Vec4& translation = core::at(bone->matrix.row, 3);
    const maths::Vec4& pos = entity_->at_point_;
    const maths::Vec4 position = {translation.x + pos.x, translation.y + pos.y, translation.z + pos.z, pos.w};
    collision->place_at_absolute_point(&position);
    maths::Mat4 orientation{};
    d3dx_multiply(&orientation, &bone->matrix, &entity_->display_matrix_);
    collision->change_facing(&orientation);
}

} // namespace objects
