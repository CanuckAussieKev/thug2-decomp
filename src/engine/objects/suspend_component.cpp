// The suspend component and its task (engine/objects/suspend_component.h).

#include "engine/objects/suspend_component.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/model.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/assets/resource_registry.h"
#include "game/skate/skate_module.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/sys/mlp.h"
#include "engine/objects/model_component.h"
#include <gsl/pointers>
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/down_cast.h"

namespace objects {

uint32_t g_suspend_component_member_functions[12]{0xA0B7E09B, 0xCE0CA665, 0, 0, 0, 0, 0, 0xCE0CA665, 0, 0, 0, 0};
namespace {
int g_suspend_frame_count;
int g_suspend_interleave2_count;
int g_suspend_interleave4_count;
gsl::owner<SuspendTaskHolder*> g_suspend_task_holder;

constexpr int kTaskPriority = -1100;

constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kDefaultSuspendDistance = 0xAD501995;                           // DefaultMovingObjectSuspendDistance
constexpr uint32_t kSuspendDistance = 0xE4279BA2;                                  // SuspendDistance
constexpr uint32_t kNeverSuspend = 0xCB839DC1;                                     // NeverSuspend
constexpr uint32_t kNoLod = 0x95EDC6F6;                                            // NoLOD
constexpr uint32_t kLodDist[4] = {0x4EEDFAE7, 0xD7E4AB5D, 0xA0E39BCB, 0x3E870E68}; // lod_dist1-4

constexpr float kInchesPerFoot = 12.0f; // 0x0064546C
constexpr float kLod2Beyond1 = 75.0f;   // 0x006465FC
constexpr int kCheckFrames = DormancyModule::kCheckFrames;
// The distances that mean no LOD (1000000 - 1000003): lod_dist3 at its no-LOD value means the model is always drawn.
const uint32_t kNoLodBits[4] = {0x49742400, 0x49742410, 0x49742420, 0x49742430};

int& frame_count() { return g_suspend_frame_count; }
int& interleave2_count() { return g_suspend_interleave2_count; }
int& interleave4_count() { return g_suspend_interleave4_count; }

void set_no_lod(float* distances)
{
    for (int i = 0; i < 4; ++i)
        raw::as<uint32_t>(distances)[i] = kNoLodBits[i];
}

// The model component's model (+0x208) and what it's asked (not rewritten yet): drawn or not (0x00493C60), its
// bounding sphere (0x00492F30: centre, radius in w), and whether it wants to be drawn regardless (+0x234).
uint8_t* model_of(ObjectModule* component)
{
    return raw::bytes(core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance_);
}
void change_model_enabled(uint8_t* model, bool active)
{
    raw::as<render::RenderModel>(model)->change_live(active);
}

} // namespace

// 0x00434290
gsl::owner<ObjectModule*> DormancyModule::fabricate() { return new DormancyModule; }

// 0x00434610
void DormancyModule::publish_method_table()
{
    script::register_member_function_table(cell::pointer<void>(kType), g_suspend_component_member_functions, 0x18);
    create_suspend_task();
}

// 0x00433F50
DormancyModule::DormancyModule()
{
    kind_tag_ = kType;
    const double distance = static_cast<double>(script::fetch_float(kDefaultSuspendDistance)) * kInchesPerFoot;
    suspend_distance_ = static_cast<float>(distance);
    suspend_distance_squared_ = static_cast<float>(distance * distance);
}

// 0x00433E10 (0x00433FD0 the deleting one)
DormancyModule::~DormancyModule() = default;

// 0x00433E50
void DormancyModule::tick_frame() { ObjectModule::hold_off(true); }

// 0x00434000
void DormancyModule::configure_from_data(script::Struct* params)
{
    if (params->fetch_float(kSuspendDistance, &suspend_distance_, 0)) {
        if (suspend_distance_ == 0.0f && !params->fetch_float(kLodDist[0], &suspend_distance_, 0))
            suspend_distance_ = script::fetch_float(kDefaultSuspendDistance);
        never_suspend_ = false;
        const double distance = static_cast<double>(suspend_distance_) * kInchesPerFoot;
        suspend_distance_ = static_cast<float>(distance);
        suspend_distance_squared_ = static_cast<float>(distance * distance);
    }
    if (params->has_bare_flag(kNeverSuspend))
        never_suspend_ = true;
    if (script::fetch_int_value(kNoLod)) {
        set_no_lod(lod_distances_);
        return;
    }
    float* lod = lod_distances_;
    lod[0] = 50.0f;
    raw::as<uint32_t>(lod)[2] = kNoLodBits[1];
    raw::as<uint32_t>(lod)[3] = kNoLodBits[2];
    params->fetch_float(kLodDist[0], &lod[0], 0);
    lod[1] = lod[0] + kLod2Beyond1;
    params->fetch_float(kLodDist[1], &lod[1], 0);
    params->fetch_float(kLodDist[2], &lod[2], 0);
    params->fetch_float(kLodDist[3], &lod[3], 0);
    const double first = static_cast<double>(lod[0]) * kInchesPerFoot;
    const double second = static_cast<double>(lod[1]) * kInchesPerFoot;
    lod[0] = static_cast<float>(first);
    lod[1] = static_cast<float>(second);
    lod[2] = lod[2] * kInchesPerFoot;
    lod[3] = lod[3] * kInchesPerFoot;
    // Distances out of order are reported (compiled out of this build).
    if (!(first < second))
        script::dump_to_log(params, 0);
    if (!(lod[1] < lod[2]))
        script::dump_to_log(params, 0);
    if (!(lod[2] < lod[3]))
        script::dump_to_log(params, 0);
    lod[0] = script::fetch_float(0xD3D072F9) * lod[0]; // LOD0DistanceMultiplier
    lod[1] = script::fetch_float(0x0432F2A1) * lod[1]; // LOD1DistanceMultiplier
    // NOT ORIGINAL - [Graphics] lod_distance_scale (engine/gfx/graphics_options.h): animated and drawn further out.
    const float scale = render::graphics_options::get_pull().lod_distance_scale_;
    lod[0] = render::graphics_options::get_scaled(lod[0], scale);
    lod[1] = render::graphics_options::get_scaled(lod[1], scale);
}

// 0x004341C0
void DormancyModule::seal() { mesh_instance_ = entity_->fetch_module(kModel); }

// 0x00433E60
void DormancyModule::reset_counts()
{
    frames_before_lod_ = kCheckFrames;
    frames_before_check_ = kCheckFrames;
}

// 0x00433E70
void DormancyModule::disable_lod() { set_no_lod(lod_distances_); }

// 0x00433E90
bool DormancyModule::is_far() { return far_; }

// 0x00433EA0
bool DormancyModule::wants_animating(float* distance_squared)
{
    const float d = distance_squared_;
    if (distance_squared)
        *distance_squared = d;
    bool animate = true;
    const double lod = lod_distances_[0];
    if (d > lod * lod) { // (not a number: as within)
        animate = false;
    } else {
        const float skip_every2 = script::fetch_float(0xDB120FB5);  // AnimLODInterleave2
        const double skip_every4 = script::fetch_float(0x3271AA80); // AnimLODInterleave4
        auto within2 = static_cast<float>(static_cast<double>(skip_every2) * skip_every2);
        if (d > skip_every4 * skip_every4)
            animate = (++interleave4_count() & 3) == 0;
        else if (d > within2)
            animate = (++interleave2_count() & 1) == 0;
    }
    return frames_before_lod_ != 0 ? true : animate;
}

// 0x004342F0
void DormancyModule::refresh_shown_state()
{
    if (!mesh_instance_ || !model_of(mesh_instance_))
        return;
    bool visible = false;
    if (!(entity_->object_flags_ & kEntityHiddenBit)) {
        uint8_t* model = model_of(mesh_instance_);
        // NOLINTNEXTLINE(bugprone-branch-clone): the original's, as written
        if (raw::as<const uint32_t>(lod_distances_)[2] == kNoLodBits[2]) {
            visible = true;
        } else if (modules::g_viewport_count > 1) {
            visible = true;
        } else if (core::down_cast<MeshInstanceModule>(mesh_instance_)->level_object_) {
            visible = true;
        } else {
            const double lod = lod_distances_[1];
            if (distance_squared_ < lod * lod) {
                // In view: its bounding sphere where the object is.
                float sphere[4];
                raw::as<render::RenderModel>(model)->fetch_bound_sphere(
                    raw::as<maths::Vec4>(sphere));
                maths::Vec4 centre = {sphere[0], sphere[1], sphere[2], 1.0f};
                centre.multiply_by(entity_->basis_frame_);
                centre += entity_->at_point_;
                visible = render::engine::sphere_visible_in_one_viewport(&centre, sphere[3]) == 0;
            }
        }
    }
    const uint8_t flags = model_of(mesh_instance_)[0x44];
    const bool shown = (flags & 0x2) && !(flags & 0x4);
    change_model_enabled(model_of(mesh_instance_), visible);
    if (visible && !shown)
        mesh_instance_->tick_frame();
}

// 0x004341E0
bool DormancyModule::obj_reset_no_suspend_count(script::Struct* /*params*/, script::Script* /*script*/)
{
    frames_before_lod_ = kCheckFrames;
    frames_before_check_ = kCheckFrames;
    entity_->hold_off(false);
    return true;
}

// 0x00433E20
void next_suspend_frame()
{
    const int previous = frame_count();
    const int frame = previous + 1;
    frame_count() = frame;
    interleave2_count() = frame;
    interleave4_count() = (((previous >> 1) & 1) + 1) ^ frame;
}

// 0x00434520
SuspendTaskHolder::SuspendTaskHolder()
{
    auto* task = new tasks::Task<SuspendTaskHolder>(&suspend_task, this, kTaskPriority);
    job_handle_ = task;
    (&assets::g_main_loop_manager->logic_tasks_)->queue_job(task); // the logic tasks
}

// 0x004345B0
void create_suspend_task()
{
    void* memory = memory::alloc_zeroed(sizeof(SuspendTaskHolder));
    g_suspend_task_holder = memory ? memory::construct_in<SuspendTaskHolder>(memory) : nullptr;
}

// 0x00434420
void suspend_task(tasks::Task<SuspendTaskHolder>* /*task*/)
{
    // The camera the distances are measured from: the skate module's (a flag that turns the checks off, then the
    // position).
    const modules::Skate* camera = modules::g_skate_module;
    auto* component = core::down_cast<DormancyModule>(object_hub_slot()->find_first_module_of_kind(DormancyModule::kType));
    while (component) {
        component->distance_squared_ = 0.0f;
        component->far_ = false;
        if (component->frames_before_check_) {
            // (the original goes round again with the same component: its frames all counted down at once)
            --component->frames_before_check_;
            continue;
        }
        if (!camera->no_camera_ && !component->never_suspend_) {
            if (component->frames_before_lod_) {
                --component->frames_before_lod_;
            } else {
                const maths::Vec4& pos = component->entity_->at_point_;
                const auto* at = raw::as<const float>(&camera->viewer_pos_);
                const double dx = static_cast<double>(pos.x) - at[0];
                const double dy = static_cast<double>(pos.y) - at[1];
                const double dz = static_cast<double>(pos.z) - at[2];
                const double distance_squared = (dz * dz + dy * dy) + dx * dx;
                component->distance_squared_ = static_cast<float>(distance_squared);
                component->far_ = distance_squared > component->suspend_distance_squared_;
            }
        }
        GameEntity* object = component->entity_;
        const bool suspended = (object->composite_flags_ & kCompositeFlagSuspended) != 0;
        if (component->far_ && !suspended)
            object->hold_off(true);
        else if (!component->far_ && suspended)
            object->hold_off(false);
        if (ObjectModule* model = component->mesh_instance_) {
            if (component->frames_before_lod_) {
                if (model_of(model))
                    change_model_enabled(model_of(model), true);
            } else {
                component->refresh_shown_state();
            }
        }
        component = core::down_cast<DormancyModule>(component->next_of_type_);
    }
}

} // namespace objects
