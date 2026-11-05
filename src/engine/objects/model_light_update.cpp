// The model light update component (GameObjects::MeshLightingModule): see model_light_update.h. From the disassembly of
// 0x00422D00-0x00423127. The x87 roundings are kept: values the original stores are rounded to float, the ones it keeps
// on the FPU stack are worked in double.

#include "engine/objects/model_light_update.h"

#include <cstdint>
#include <gsl/pointers>

#include "engine/render/coll_obj.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/model_component.h"
#include "engine/objects/model_component_nx.h"
#include "engine/script/param_struct.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/script/fwd.h"
#include "engine/objects/camera_lookaround.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/maths/vec4.h"

namespace objects {

namespace {
constexpr render::FaceFlag kLightRequireFlags = render::FaceFlag::kShadeSurface;

constexpr uint32_t kOnce = 0xE7375C4C;     // (its name isn't in THUG2's scripts)
constexpr uint32_t kInterval = 0x470B8AE9; // (nor this one's)
constexpr uint32_t kModel = 0x286A8D26;

constexpr float kUpNudge = 0.001F;    // 0x006461D8: a thousandth of the object's up vector
constexpr float kAbove = 8.0F;        // 0x006461D4: the first feeler starts this far above
constexpr float kDepth = 4800.0F;     // 0x006461D0: and goes this far down
constexpr float kSecondAbove = 48.0F; // 0x006461CC: the second starts this far above the first's start
constexpr float kLerpRate = 0.02F;    // 0x3CA3D70A

} // namespace

// 0x00422DC0
gsl::owner<ObjectModule*> MeshLightingModule::fabricate() { return new MeshLightingModule; }

// (the constructor: inlined in fabricate in the original)
MeshLightingModule::MeshLightingModule() { set_kind_tag(kType); }

// 0x00422D00 (0x00422D10 the deleting one)
MeshLightingModule::~MeshLightingModule() = default;

// 0x00422D40
void MeshLightingModule::configure_from_data(script::Struct* params)
{
    if (params->has_bare_flag(kOnce)) {
        light_flags_ |= kModelLightOnce;
    } else {
        light_flags_ &= ~kModelLightOnce;
    }
    gap_time_ = 0;
    counter_ = 0;
    if (params->fetch_int_value(kInterval, &gap_time_, 0)) {
        // (a start in the interval that differs between components: the original used the component's address;
        // NOT ORIGINAL: a running number - addresses aren't values)
        static uint32_t s_made = 0;
        counter_ = (++s_made % gap_time_) + 1;
    }
}

// 0x00422DA0
void MeshLightingModule::seal() { mesh_instance_ = core::down_cast<MeshInstanceModule>(entity()->fetch_module(kModel)); }

// 0x00422E40
void MeshLightingModule::get_sample(bool lerp, float rate)
{
    // Down from just above the object (nudged along its up vector).
    render::ProbeRay down;
    const GameEntity* object = entity();
    maths::Vec4& start = down.line().start;
    maths::Vec4& end = down.line().end;
    start = object->at_point();
    end = object->at_point();
    const maths::Vec4& up = core::at(object->basis_frame().row, 1);
    const double step_x = static_cast<double>(up.x) * kUpNudge;
    const double step_y = static_cast<double>(up.y) * kUpNudge;
    const double step_z = static_cast<double>(up.z) * kUpNudge;
    const double step_w = static_cast<double>(up.w) * kUpNudge;
    start.x = static_cast<float>(step_x + start.x);
    start.y = static_cast<float>(start.y + step_y);
    start.z = static_cast<float>(start.z + step_z);
    start.w = static_cast<float>(start.w + step_w);
    start.y = start.y + kAbove;
    end.y = end.y - kDepth;
    const bool hit = down.get_hit_shape(false, false) != 0;
    // Again from higher up, to just under what it hit (or as far down), faces flagged 0x20 only.
    render::ProbeRay again;
    again.set_excluded({.ignore = render::kNoFaceFlags, .require = kLightRequireFlags});
    again.line().start = {.x = start.x, .y = start.y + kSecondAbove, .z = start.z, .w = start.w};
    if (hit) {
        again.line().end = {.x = down.contact_pos().x,
                            .y = down.contact_pos().y - 1.0F,
                            .z = down.contact_pos().z,
                            .w = down.contact_pos().w};
    } else {
        again.line().end = {.x = start.x, .y = start.y - kDepth, .z = start.z, .w = start.w};
    }
    if (again.get_hit_shape(false, false) != 0) {
        mesh_instance_->apply_brightness(&again, lerp, rate);
        brightness_ = static_cast<float>(again.get_hit_vertex_value());
    } else if (hit) {
        mesh_instance_->apply_brightness(&down, lerp, 1.0F);
        brightness_ = static_cast<float>(down.get_hit_vertex_value());
    }
}

// 0x004230B0
void MeshLightingModule::tick_frame()
{
    if ((light_flags_ & kModelLightSampled) == 0U) {
        get_sample(false, 1.0F);
        light_flags_ |= kModelLightSampled;
        return;
    }
    if ((gap_time_ != 0U) && counter_++ < gap_time_) {
        static_cast<render::ModelLights*>(model_nx::model_lights(mesh_instance_->mesh_instance().get()))
            ->move_brightness({.target = brightness_, .step = kLerpRate});
        return;
    }
    counter_ = 0;
    if ((light_flags_ & kModelLightOnce) != 0U) {
        hold_off(true);
    } else {
        get_sample(true, 1.0F);
    }
}

} // namespace objects
