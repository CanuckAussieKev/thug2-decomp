// The camera component (GameObjects::ViewpointModule): see camera_component.h. From the disassembly of
// 0x0041B6D0-0x0041B903.

#include "engine/objects/camera_component.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr int kCameraPriority = -1000;

} // namespace

// 0x0041B840
ObjectModule* __cdecl ViewpointModule::fabricate() { return new ViewpointModule; }

// 0x0041B730
ViewpointModule::ViewpointModule()
{
    kind_tag_ = kType;
    view_camera_ = new gfx::ViewCam;
}

// 0x0041B6D0 (0x0041B7C0 the deleting one)
ViewpointModule::~ViewpointModule() { delete view_camera_; }

// 0x0041B7F0
void ViewpointModule::configure_from_data(script::Struct*)
{
    ObjectHubBase* manager = reinterpret_cast<ObjectHubBase*>(object_hub_slot());
    manager->set_entity_rank(entity_, kCameraPriority);
}

// 0x0041B810
void ViewpointModule::seal()
{
    view_camera_->change_location(entity_->at_point_);
    view_camera_->change_mat(entity_->basis_frame_);
}

// 0x0041B8A0
void ViewpointModule::tick_frame()
{
    maths::Vec4 pos = entity_->at_point_;
    maths::Mat4 matrix;
    matrix = entity_->basis_frame_;
    entity_->display_matrix_ = matrix;
    view_camera_->change_location(pos);
    view_camera_->change_mat(matrix);
}

} // namespace objects
