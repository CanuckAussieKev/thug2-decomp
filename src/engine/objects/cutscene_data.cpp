// Cutscenes: the cutscene's objects, assets, lights, camera and poses (0x00463AA0-0x0046449B). See cutscene.h.

#include <cstdint>
#include <cstring>
#include <array>
#include <span>

#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/flexible_particle_manager.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/model.h"
#include "engine/gfx/scene_lights.h"
#include "engine/gfx/rig.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "engine/objects/compositemgr.h"
#include "engine/gfx/engine.h"
#include "engine/sys/startup_init.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/ints.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/no_destroy.h"
#include "engine/objects/objects_shared.h"
#include <gsl/pointers>

namespace objects {

namespace {
// NOT ORIGINAL grouping - cutscene_data.cpp's state (the original's globals). Made on first use, never destroyed
// (cutscene_data_state()).
struct CutsceneDataState {
    // The objects' poses (the original used the two arrays above for them too, as floats).
    std::array<maths::QuatRotation, 128> object_pose_rotations_{};
    std::array<maths::Vec4, 128> object_pose_translations_{};
};

CutsceneDataState& cutscene_data_state()
{
    static core::NoDestroy<CutsceneDataState> state;
    return state.get();
}

} // namespace

namespace {

constexpr uint32_t kModelModuleName = 0x286A8D26; // the component whose +0x208 is the object's model
constexpr uint32_t kSkeletonModuleName = 0x222756D5;
constexpr uint32_t kCutsceneObjectType = 0xCDEB9229;
constexpr uint32_t kCutsceneHideList = 0x67F08671; // a global array: the objects a cutscene hides
constexpr uint32_t kLights = 0x2A79C44E;           // a global array: the nodes of the cutscene's lights
constexpr uint32_t kCreateScript = 0x1F29C5A5;     // the scripts that make a cutscene object
constexpr uint32_t kInitScript = 0x2E20EA32;
constexpr uint32_t kFlagParam = 0x82C84419; // create_object's parameter: +0x34B0
constexpr uint32_t kProfile = 0x1804A7C4;   // skater_model_name's parameters and table
constexpr uint32_t kProfiles = 0xCA1DB456;
constexpr uint32_t kProfileName = 0x40C698AF;
constexpr uint32_t kProfileModel = 0x16CBB85A;
constexpr uint32_t kMale = 0x3F813177;
constexpr uint32_t kNoColour = 0x5A96985D; // read_colour's structure: {r, g, b}, or this set: white
constexpr uint32_t kRed = 0x7323E97C;
constexpr uint32_t kGreen = 0x0424D9EA;
constexpr uint32_t kBlue = 0x9D2D8850;

// The model of an object's model component (+0x208), and the model calls (not rewritten yet).
void* model_of(ObjectModule* component) { return core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get(); }
maths::Mat4* model_get(void* model) { return static_cast<render::RenderModel*>(model)->fetch_skinning_palette(); }
void model_set(void* model, maths::Mat4* palette)
{
    static_cast<render::RenderModel*>(model)->upload_skinning_palette(palette);
}

// The asset manager (0x006918DC).
assets::ResourceRegistry* asset_manager() { return assets::asset_manager(); }
// (from memory: not permanent, copied)
void load_asset_file(uint32_t name, void* data, uint32_t size)
{
    asset_manager()->read_resource_from_stream(name, 5, 0, data, size, false, true, nullptr, nullptr);
}

} // namespace

// 0x00463AA0 (x87: the squares and w * z unrounded)
void quat_to_matrix(std::span<float, 4> quat, std::span<const float, 4> translation, std::span<float, 16> matrix)
{
    core::at(quat, 0) = -core::at(quat, 0);
    core::at(quat, 1) = -core::at(quat, 1);
    core::at(quat, 2) = -core::at(quat, 2);
    const double xx = static_cast<double>(core::at(quat, 0)) * core::at(quat, 0);
    const double yy = static_cast<double>(core::at(quat, 1)) * core::at(quat, 1);
    const double zz = static_cast<double>(core::at(quat, 2)) * core::at(quat, 2);
    const double zy = static_cast<double>(core::at(quat, 2)) * core::at(quat, 1);
    const double zx = static_cast<double>(core::at(quat, 2)) * core::at(quat, 0);
    const double yx = static_cast<double>(core::at(quat, 1)) * core::at(quat, 0);
    auto wx = static_cast<float>(static_cast<double>(core::at(quat, 3)) * core::at(quat, 0));
    auto wy = static_cast<float>(static_cast<double>(core::at(quat, 3)) * core::at(quat, 1));
    const double wz = static_cast<double>(core::at(quat, 3)) * core::at(quat, 2);
    core::at(matrix, 0) = static_cast<float>(1.0f - (zz + yy) * 2.0);
    core::at(matrix, 1) = static_cast<float>((wz + yx) * 2.0);
    core::at(matrix, 2) = static_cast<float>((zx - wy) * 2.0);
    core::at(matrix, 3) = 0.0f;
    core::at(matrix, 4) = static_cast<float>((yx - wz) * 2.0);
    core::at(matrix, 5) = static_cast<float>(1.0f - (zz + xx) * 2.0);
    core::at(matrix, 6) = static_cast<float>((wx + zy) * 2.0);
    core::at(matrix, 7) = 0.0f;
    core::at(matrix, 8) = static_cast<float>((wy + zx) * 2.0);
    core::at(matrix, 9) = static_cast<float>((zy - wx) * 2.0);
    core::at(matrix, 10) = static_cast<float>(1.0f - (yy + xx) * 2.0);
    core::at(matrix, 11) = 0.0f;
    std::memcpy(matrix.subspan(12, 4).data(), translation.data(), 16);
    core::at(matrix, 15) = 1.0f;
}

// 0x00464340
void assign_value(IdentityQuat& quat)
{
    core::at(quat.q, 0) = 0.0f;
    core::at(quat.q, 1) = 0.0f;
    core::at(quat.q, 2) = 0.0f;
    core::at(quat.q, 3) = 1.0f;
}

// 0x00463BA0
const char* skater_model_name(script::Struct* params)
{
    uint32_t profile = 0;
    if (params->read_hash_value(kProfile, &profile, 0)) {
        script::Array* profiles = script::fetch_list(kProfiles);
        for (int i = 0; i < ints::as_signed(profiles->extent()); ++i) {
            auto* entry = profiles->struct_at(static_cast<uint32_t>(i));
            uint32_t name = 0; // (asserted: the original's is uninitialised)
            entry->read_hash_value(kProfileName, &name, 1);
            if (name == profile) {
                const char* model = nullptr;
                entry->fetch_text(kProfileModel, &model, 1);
                return model;
            }
        }
        return nullptr;
    }
    int male = 0; // (asserted: the original's is uninitialised)
    params->fetch_int_value(kMale, &male, 1);
    return male == 1 ? "Male1" : "Female1"; // (the game's strings 0x00647900 / 0x006478F8)
}

// 0x00463DA0
std::span<float, 4> read_colour(std::span<float, 4> out, script::Struct* params, uint32_t name)
{
    script::Struct* colour = nullptr;
    if (params->read_sub_struct(name, &colour, 0)) {
        int none = 1;
        colour->fetch_int_value(kNoColour, &none, 0);
        if (none == 0) {
            int red = 0, green = 0, blue = 0; // (asserted: the original's are uninitialised)
            colour->fetch_int_value(kRed, &red, 1);
            colour->fetch_int_value(kGreen, &green, 1);
            colour->fetch_int_value(kBlue, &blue, 1);
            core::at(out, 3) = 1.0f;
            core::at(out, 0) = static_cast<float>(red * static_cast<double>(0.0099999998f));
            core::at(out, 1) = static_cast<float>(green * static_cast<double>(0.0099999998f));
            core::at(out, 2) = static_cast<float>(blue * static_cast<double>(0.0099999998f));
            return out;
        }
    }
    core::at(out, 0) = core::at(out, 1) = core::at(out, 2) = core::at(out, 3) = 1.0f;
    return out;
}

// 0x00463E80
void add_cutscene_lights()
{
    script::Array* lights = script::fetch_list(kLights);
    if (!lights)
        return;
    const int count = static_cast<int>(lights->extent());
    for (int i = 0; i < count; ++i) {
        script::Struct* node = script::read_sub_struct(lights->fetch_part(static_cast<uint32_t>(i)));
        render::create_light_from_node(node);
    }
}

// 0x00463ED0
void remove_cutscene_lights()
{
    script::Array* lights = script::fetch_list(kLights);
    if (!lights)
        return;
    const int count = static_cast<int>(lights->extent());
    for (int i = 0; i < count; ++i) {
        script::Struct* node = script::read_sub_struct(lights->fetch_part(static_cast<uint32_t>(i)));
        render::delete_scene_light(level_nodes::get_node_name(node));
    }
}

// 0x004640D0
void set_up_cutscene_heaps()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    render::flexible_particle_manager()->create_temp_heap(0x25800);
    const uint32_t available = memory::Hub::get_pull()->obtainable();
    memory::Hub::get_pull()->init_cutscene_heaps(available - 0x32000);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00463C70
void CinematicTable::destroy_objects()
{
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = core::at(entities_, i);
        if (entry.active) {
            // the level's object it stood in for gets its model's settings back
            ObjectModule* stand_in = static_cast<GameEntity*>(entry.object)->fetch_module(kModelModuleName);
            const uint32_t id = core::down_cast<MeshInstanceModule>(stand_in)->get_linked_entity_name();
            auto* original = core::down_cast<GameEntity>(find_object(id));
            void* model = model_of(original->fetch_module(kModelModuleName));
            static_cast<render::RenderModel*>(model)->change_bound_sphere(&lent_sphere_);
            static_cast<render::RenderModel*>(model)->conceal(lent_3c_);
            static_cast<render::RenderModel*>(model)->allow_resizing(lent_3d_);
            maths::Mat4* mine = model_get(model);
            model_set(model, model_get(model_of(stand_in)));
            model_set(model_of(stand_in), mine);
            original->at_point() = lent_pos_;
        }
        gsl::owner<decltype(entry.object)> object = memory::owned_element(entry.object); // (the cutscene's entry's)
        delete object;
        entry.object = nullptr;
    }
    object_count_ = 0;
    objects_shared().set_cutscene_playing(false);
}

// 0x00463F20
void CinematicTable::load_asset(uint32_t name, void* data, uint32_t size)
{
    load_asset_file(name, data, size);
    core::at(assets_, asset_count_) = name;
    ++asset_count_;
}

// 0x00463F70
void CinematicTable::load_animation(uint32_t name, void* data, uint32_t size)
{
    AnimationEntry& entry = core::at(animations_, animation_count_);
    load_asset_file(name, data, size);
    core::at(assets_, asset_count_) = name;
    ++asset_count_;
    void* anim = render::get_anim_data(name, 1);
    entry.source = nullptr;
    entry.name = name;
    if (!entry.data)
        entry.data = anim;
    ++animation_count_;
}

// 0x00464000
gsl::owner<GameEntity*> CinematicTable::create_object(script::Struct* params)
{
    auto object = memory::make_owned<GameEntity>();
    object->change_species(kCutsceneObjectType);
    objects::object_hub_slot()->enrol_subject(object);
    params->put_int(kFlagParam, flag_34b_0_);
    script::launch_script(kCreateScript, params, object, false, nullptr);
    object->seal();
    script::launch_script(kInitScript, params, object, false, nullptr);
    object->at_point() = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}; // at the origin
    return object;
}

// 0x004641A0
void CinematicTable::show_hidden_objects()
{
    if (!flag_34b_0_)
        return;
    script::Array* hidden = script::fetch_list(kCutsceneHideList);
    if (!hidden)
        return;
    for (uint32_t i = 0; i < hidden->extent(); ++i) {
        auto* object = core::down_cast<GameEntity>(find_object(hidden->fetch_part(i)));
        if (!object)
            continue;
        ObjectModule* component = object->fetch_module(kModelModuleName);
        if (!component || !model_of(component))
            continue;
        static_cast<render::RenderModel*>(model_of(component))->change_live(1);
        component->tick_frame();
    }
}

// 0x00464220
void CinematicTable::refresh_viewer(void* camera)
{
    if (!flag_34b_0_)
        return;
    static_cast<gfx::ViewCam*>(camera)->remember_prev_pos();
    auto* position = (static_cast<gfx::ViewCam*>(camera)->fetch_location())->data();
    auto* matrix = (static_cast<gfx::ViewCam*>(camera)->fetch_mat())->data();
    std::array<float, 4> rotation = {{0.0f, 0.0f, 0.0f, 1.0f}};
    std::array<float, 4> translation{};
    bool flag = false;
    camera_anim_->get_float_key_pose(rotation, translation, &flag,
                                     static_cast<float>(clock_ - static_cast<double>(camera_start_)),
                                     0); // (the original passed the animation source's address: not read)
    std::memcpy(position, translation.data(), 16);
    std::array<float, 4> origin = {{0.0f, 0.0f, 0.0f, 1.0f}};
    quat_to_matrix(rotation, origin, std::span<float, 16>(matrix, 16));
    if (skipped_)
        return;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->cinematic_low_heap());
    object_anims_
        ->fire_events(previous_clock_, clock_, camera, 0);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00464360
void CinematicTable::update_object_poses()
{
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = core::at(entities_, i);
        if (!entry.animation)
            continue;
        ObjectModule* component = entry.object->fetch_module(kSkeletonModuleName);
        if (!component)
            continue;
        // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
        gfx::Rig* skeleton = core::down_cast<objects::BoneRigModule>(component)->bone_rig();
        render::FrameSource* source = entry.animation->source;
        void* animation = entry.animation->data;
        skeleton->set_lod(0.0f);
        auto* rig = skeleton;
        gfx::RigData* data = rig->payload();
        source->set_bone_masks(data ? data->lod_masks().data() : nullptr);
        source->set_mask_bit(static_cast<int>(rig->lod()));
        auto count = static_cast<uint32_t>(rig->get_num_bones());
        std::memcpy(cutscene_data_state().object_pose_translations_.data(), rig->payload()->bind_translations(), count * 16);
        // (within the first twelfth of a second, without the source)
        if (clock_ - static_cast<double>(unknown3480_) < 0.083333336f)
            source = nullptr;
        static_cast<gfx::JointClipFrames*>(animation)->get_frames(
            cutscene_data_state().object_pose_rotations_, cutscene_data_state().object_pose_translations_, objects_shared().cutscene_camera_slot(),
            static_cast<float>(clock_ - static_cast<double>(camera_start_)), -1, source);
        skeleton->tick_frame(cutscene_data_state().object_pose_rotations_.data(), cutscene_data_state().object_pose_translations_.data());
    }
}

} // namespace objects

namespace objects {

// 0x00640A30 (a static initialiser): the 128 cutscene bone rotations, each the quaternion (0, 0, 0, 1).
void construct_cutscene_rotations()
{
    for (int i = 0; i < 128; ++i) {
        core::at(objects_shared().cutscene_rotations(), 4 * i + 0) = 0.0f;
        core::at(objects_shared().cutscene_rotations(), 4 * i + 1) = 0.0f;
        core::at(objects_shared().cutscene_rotations(), 4 * i + 2) = 0.0f;
        core::at(objects_shared().cutscene_rotations(), 4 * i + 3) = 1.0f;
    }
}

} // namespace objects
