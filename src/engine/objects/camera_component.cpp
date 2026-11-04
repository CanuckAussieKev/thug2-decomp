// The camera component (GameObjects::ViewpointModule): see camera_component.h. From the disassembly of
// 0x0041B6D0-0x0041B903.

#include "engine/objects/camera_component.h"

#include "engine/gfx/view_cam.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/script/fwd.h"
#include "engine/objects/camera_lookaround.h"
#include <gsl/pointers>
#include <memory>

namespace objects {

namespace {

constexpr int kCameraPriority = -1000;

} // namespace

// 0x0041B840
gsl::owner<ObjectModule*> ViewpointModule::fabricate() { return new ViewpointModule; }

// 0x0041B730
ViewpointModule::ViewpointModule()
{
    set_kind_tag(kType);
    view_camera_ = std::make_unique<gfx::ViewCam>();
}

// 0x0041B6D0 (0x0041B7C0 the deleting one)
ViewpointModule::~ViewpointModule() { view_camera_.reset(); }

// 0x0041B7F0
void ViewpointModule::configure_from_data(script::Struct* /*params*/)
{
    auto* manager = static_cast<ObjectHubBase*>(object_hub_slot().get());
    manager->set_entity_rank(entity(), kCameraPriority);
}

// 0x0041B810
void ViewpointModule::seal()
{
    view_camera_->change_location(entity()->at_point());
    view_camera_->change_mat(entity()->basis_frame());
}

// 0x0041B8A0
void ViewpointModule::tick_frame()
{
    const maths::Vec4 pos = entity()->at_point();
    maths::Mat4 matrix{};
    matrix = entity()->basis_frame();
    entity()->display_matrix() = matrix;
    view_camera_->change_location(pos);
    view_camera_->change_mat(matrix);
}

} // namespace objects
