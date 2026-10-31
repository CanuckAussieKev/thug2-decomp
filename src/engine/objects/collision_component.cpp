// The collision component (GameObjects::ContactShapeModule): an object's own movable collision object, made from its node's
// CollisionMode and its model's (or its level sector's) collision, placed where the object is.

#include <cstdint>

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
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/gfx/weather.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

extern void** const kVtable;

constexpr uint32_t kModelModuleName = 0x286A8D26; // Model
constexpr uint32_t kCollisionMode = 0x2D7E583B;  // CollisionMode
constexpr uint32_t kClass = 0x12B4E660;          // Class
constexpr uint32_t kLevelObject = 0xB7B3BD86;    // LevelObject
constexpr uint32_t kName = 0xA1DC81F9;           // Name

// The model component's model (+0x208).
uint8_t* model_of(ObjectModule* model) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0x208); }

// A level sector by name in any scene (0x0048C510) and its collision object (0x005480F0: +0x18).
uint8_t* look_up_sector(uint32_t name) { return static_cast<uint8_t*>(render::engine::look_up_sector(name)); }
render::HitMesh* sector_collision(uint8_t* sector)
{
    return reinterpret_cast<render::HitMesh*>(reinterpret_cast<online::FoldedGetters*>(sector)->word18());
}

// The model's bone matrix (0x00493040: a single-bone model's, from the asset manager; null otherwise): its rotation at
// +0x10, its translation at +0x40 (the matrix's fourth row).
const uint8_t* bone_of(uint8_t* model) { return reinterpret_cast<const uint8_t*>(reinterpret_cast<render::RenderModel*>(model)->get_asset_hierarchy()); }

void d3dx_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b)
{
    d3dx::matrix_multiply(out, a, b);
}

// The collision object's slots 5 / 6: its world position, its orientation.
void place_at_absolute_point(render::HitMesh* collision, const maths::Vec4* position) { collision->get_call<void>(5, position); }
void change_facing(render::HitMesh* collision, const maths::Mat4* orientation) { collision->get_call<void>(6, orientation); }

} // namespace

// 0x0041C920
ObjectModule* __cdecl ContactShapeModule::fabricate()
{
    auto* component = static_cast<ContactShapeModule*>(memory::alloc_zeroed(sizeof(ContactShapeModule)));
    if (!component)
        return nullptr;
    component->ObjectModule::make_new();
    component->vtable_ = kVtable;
    component->kind_tag_ = kType;
    return component;
}

// 0x0041C710
void ContactShapeModule::destroy_body()
{
    vtable_ = kVtable;
    if (hit_shape_) {
        render::unregister_movable(hit_shape_);
        if (hit_shape_)
            hit_shape_->get_call<void*>(0, 1); // (its scalar deleting destructor)
    }
    ObjectModule::destroy_body();
}

// 0x0041C780
ContactShapeModule* ContactShapeModule::teardown(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x0041C7B0
void ContactShapeModule::configure_from_data(script::Struct* params)
{
    uint32_t mode = 0;
    params->read_hash_value(kCollisionMode, &mode, 0);
    int type = render::shape_type_from_checksum(mode);
    if (!type)
        return;
    using InitCollisionSlot = void(__thiscall*)(ContactShapeModule*, int, render::TriMeshSource*);
    auto init_collision = reinterpret_cast<InitCollisionSlot>(vtable_[13]);
    uint32_t cls = 0;
    params->read_hash_value(kClass, &cls, 0);
    if (cls != kLevelObject) {
        init_collision(this, type, nullptr);
        return;
    }
    // (the original passes on whatever was in the name's slot when there's no Name - an assertion's case; ours 0)
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    uint8_t* sector = look_up_sector(name); // (no such sector: a crash, as in the original)
    if (sector_collision(sector))
        init_collision(this, type, sector_collision(sector)->geometry_);
    else
        init_collision(this, type, nullptr);
}

// 0x0041C890
void ContactShapeModule::set_up_hit_shape(int type, render::TriMeshSource* geometry)
{
    if (geometry) {
        hit_shape_ = render::build_dynamic_hit_mesh(type, geometry, 1, reinterpret_cast<int>(entity_));
        return;
    }
    uint8_t* model = model_of(entity_->fetch_module(kModelModuleName));
    auto* data = static_cast<uint8_t*>(assets::asset_manager()->get_asset_data(*reinterpret_cast<uint32_t*>(model + 0x30), 0));
    if (data) {
        hit_shape_ = render::build_dynamic_hit_mesh(type, *reinterpret_cast<render::TriMeshSource**>(data + 0x8C),
                                                  *reinterpret_cast<int*>(data + 0x88), reinterpret_cast<int>(entity_));
    } else {
        hit_shape_ = render::build_dynamic_hit_mesh(type, nullptr, 0, reinterpret_cast<int>(entity_));
    }
}

// 0x0041C980
void ContactShapeModule::tick_frame()
{
    render::HitMesh* collision = hit_shape_;
    if (!collision)
        return;
    ObjectModule* model = entity_->fetch_module(kModelModuleName);
    const uint8_t* bone = model ? bone_of(model_of(model)) : nullptr;
    if (!bone) {
        place_at_absolute_point(collision, &entity_->at_point_);
        change_facing(collision, &entity_->display_matrix_);
        return;
    }
    const auto* translation = reinterpret_cast<const float*>(bone + 0x40);
    const maths::Vec4& pos = entity_->at_point_;
    maths::Vec4 position = {translation[0] + pos.x, translation[1] + pos.y, translation[2] + pos.z, pos.w};
    place_at_absolute_point(collision, &position);
    maths::Mat4 orientation;
    d3dx_multiply(&orientation, reinterpret_cast<const maths::Mat4*>(bone + 0x10), &entity_->display_matrix_);
    change_facing(collision, &orientation);
}

} // namespace Obj

namespace objects {
namespace {

static VtableSlot g_vtable_slots[] = {&objects::ContactShapeModule::teardown,
         &objects::ContactShapeModule::tick_frame,
         &objects::ContactShapeModule::configure_from_data,
         stubs::do_nothing,
         stubs::do_nothing,
         objects::reapply_from_data,
         stubs::ignore_arguments2,
         &objects::ObjectModule::hold_off,
         stubs::ignore_argument,
         render::virtual_slot1_jump,
         stubs::ignore_argument,
         static_cast<int (objects::ObjectModule::*)(script::MemberFunctionEntry*, script::Struct*, script::Script*)>(&objects::ObjectModule::invoke_method_by_name),
         &objects::ComponentWithoutMemberFunctions::invoke_method_by_name,
         &objects::ContactShapeModule::set_up_hit_shape};
void** const kVtable = reinterpret_cast<void**>(g_vtable_slots);

} // namespace
} // namespace Obj
