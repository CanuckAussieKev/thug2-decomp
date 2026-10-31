// Cutscenes: the cutscene's objects, assets, lights, camera and poses (0x00463AA0-0x0046449B). See cutscene.h.

#include <bit>
#include <cstdint>
#include <cstring>

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
#include "engine/core/code_address.h"
#include "engine/gfx/engine.h"
#include "engine/sys/startup_init.h"

namespace objects {

uint8_t g_cutscene_camera_slot[5];
float g_cutscene_rotations[512];
float g_cutscene_translations[512];

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
void* model_of(ObjectModule* component)
{
    return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(component) + 0x208);
}
uint32_t model_get(void* model)
{
    return reinterpret_cast<uint32_t>(static_cast<render::RenderModel*>(model)->fetch_skinning_palette());
}
void model_set(void* model, uint32_t value)
{
    static_cast<render::RenderModel*>(model)->upload_skinning_palette(reinterpret_cast<uint8_t*>(value));
}

// The asset manager (0x006918DC).
assets::ResourceRegistry* asset_manager() { return assets::g_asset_manager; }
// (from memory, a2 the data and a3 its size: not permanent, copied)
void load_asset_file(uint32_t name, uint32_t a2, uint32_t a3)
{
    asset_manager()->read_resource_from_stream(name, 5, 0, reinterpret_cast<void*>(a2), a3, false, true, nullptr, 0);
}

} // namespace

// 0x00463AA0 (x87: the squares and w * z unrounded)
void __cdecl quat_to_matrix(float* quat, const float* translation, float* matrix)
{
    quat[0] = -quat[0];
    quat[1] = -quat[1];
    quat[2] = -quat[2];
    double xx = static_cast<double>(quat[0]) * quat[0];
    double yy = static_cast<double>(quat[1]) * quat[1];
    double zz = static_cast<double>(quat[2]) * quat[2];
    double zy = static_cast<double>(quat[2]) * quat[1];
    double zx = static_cast<double>(quat[2]) * quat[0];
    double yx = static_cast<double>(quat[1]) * quat[0];
    float wx = static_cast<float>(static_cast<double>(quat[3]) * quat[0]);
    float wy = static_cast<float>(static_cast<double>(quat[3]) * quat[1]);
    double wz = static_cast<double>(quat[3]) * quat[2];
    matrix[0] = static_cast<float>(1.0f - (zz + yy) * 2.0);
    matrix[1] = static_cast<float>((wz + yx) * 2.0);
    matrix[2] = static_cast<float>((zx - wy) * 2.0);
    matrix[3] = 0.0f;
    matrix[4] = static_cast<float>((yx - wz) * 2.0);
    matrix[5] = static_cast<float>(1.0f - (zz + xx) * 2.0);
    matrix[6] = static_cast<float>((wx + zy) * 2.0);
    matrix[7] = 0.0f;
    matrix[8] = static_cast<float>((wy + zx) * 2.0);
    matrix[9] = static_cast<float>((zy - wx) * 2.0);
    matrix[10] = static_cast<float>(1.0f - (yy + xx) * 2.0);
    matrix[11] = 0.0f;
    std::memcpy(matrix + 12, translation, 16);
    matrix[15] = 1.0f;
}

// 0x00464340
void IdentityQuat::assign_value()
{
    q[0] = 0.0f;
    q[1] = 0.0f;
    q[2] = 0.0f;
    q[3] = 1.0f;
}

// 0x00463BA0
const char* __cdecl skater_model_name(script::Struct* params)
{
    uint32_t profile;
    if (params->read_hash_value(kProfile, &profile, 0)) {
        script::Array* profiles = script::fetch_list(kProfiles);
        for (int i = 0; i < static_cast<int>(profiles->extent_); ++i) {
            auto* entry = reinterpret_cast<script::Struct*>(profiles->fetch_part(static_cast<uint32_t>(i)));
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
float* __cdecl read_colour(float* out, script::Struct* params, uint32_t name)
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
            out[3] = 1.0f;
            out[0] = static_cast<float>(red * static_cast<double>(0.0099999998f));
            out[1] = static_cast<float>(green * static_cast<double>(0.0099999998f));
            out[2] = static_cast<float>(blue * static_cast<double>(0.0099999998f));
            return out;
        }
    }
    out[0] = out[1] = out[2] = out[3] = 1.0f;
    return out;
}

// 0x00463E80
void add_cutscene_lights()
{
    script::Array* lights = script::fetch_list(kLights);
    if (!lights)
        return;
    int count = static_cast<int>(lights->extent_);
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
    int count = static_cast<int>(lights->extent_);
    for (int i = 0; i < count; ++i) {
        script::Struct* node = script::read_sub_struct(lights->fetch_part(static_cast<uint32_t>(i)));
        render::delete_scene_light(level_nodes::get_node_name(node));
    }
}

// 0x004640D0
void set_up_cutscene_heaps()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    reinterpret_cast<render::FlexibleParticleManager*&>(render::g_flexible_particle_manager)->create_temp_heap(0x25800);
    uint32_t available = memory::Hub::get_pull()->obtainable();
    memory::Hub::get_pull()->init_cutscene_heaps(available - 0x32000);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00463C70
void CinematicTable::destroy_objects()
{
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = entities_[i];
        if (entry.active) {
            // the level's object it stood in for gets its model's settings back
            ObjectModule* stand_in = static_cast<GameEntity*>(entry.object)->fetch_module(kModelModuleName);
            uint32_t id = static_cast<MeshInstanceModule*>(stand_in)->get_linked_entity_name();
            auto* original = static_cast<GameEntity*>(find_object(id));
            void* model = model_of(original->fetch_module(kModelModuleName));
            static_cast<render::RenderModel*>(model)->change_bound_sphere(
                reinterpret_cast<const maths::Vec4*>(lent_2c_));
            static_cast<render::RenderModel*>(model)->conceal(lent_3c_);
            static_cast<render::RenderModel*>(model)->allow_resizing(lent_3d_);
            uint32_t mine = model_get(model);
            model_set(model, model_get(model_of(stand_in)));
            model_set(model_of(stand_in), mine);
            std::memcpy(reinterpret_cast<uint8_t*>(original) + 0x4C, shade_, 16);
        }
        if (entry.object) {
            void** vtable = *reinterpret_cast<void***>(entry.object);
            reinterpret_cast<void(__thiscall*)(void*, int)>(vtable[0])(entry.object, 1);
        }
        entry.object = nullptr;
    }
    object_count_ = 0;
    g_cutscene_playing = false;
}

// 0x00463F20
void CinematicTable::load_asset(uint32_t name, uint32_t a2, uint32_t a3)
{
    load_asset_file(name, a2, a3);
    assets_[asset_count_] = name;
    ++asset_count_;
}

// 0x00463F70
void CinematicTable::load_animation(uint32_t name, uint32_t a2, uint32_t a3)
{
    AnimationEntry& entry = animations_[animation_count_];
    load_asset_file(name, a2, a3);
    assets_[asset_count_] = name;
    ++asset_count_;
    void* data = render::get_anim_data(name, 1);
    entry.source = nullptr;
    entry.name = name;
    if (!entry.data)
        entry.data = data;
    ++animation_count_;
}

// 0x00464000
GameEntity* CinematicTable::create_object(script::Struct* params)
{
    void* memory = memory::alloc_zeroed(0x108);
    auto* object = memory ? static_cast<GameEntity*>(memory)->make_new() : nullptr;
    object->change_species(kCutsceneObjectType);
    void* objects = static_cast<void*>(g_composite_registry);
    reinterpret_cast<void(__thiscall*)(void*, GameEntity*)>((*reinterpret_cast<void***>(objects))[1])(objects, object);
    params->put_int(kFlagParam, flag_34b_0_);
    script::launch_script(kCreateScript, params, object, false, nullptr);
    reinterpret_cast<void(__thiscall*)(GameEntity*)>((*reinterpret_cast<void***>(object))[0x20 / 4])(object);
    script::launch_script(kInitScript, params, object, false, nullptr);
    auto* colour = reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(object) + 0x4C);
    colour[0] = 0.0f;
    colour[1] = 0.0f;
    colour[2] = 0.0f;
    colour[3] = 1.0f;
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
    for (uint32_t i = 0; i < hidden->extent_; ++i) {
        auto* object = static_cast<GameEntity*>(find_object(hidden->fetch_part(i)));
        if (!object)
            continue;
        ObjectModule* component = object->fetch_module(kModelModuleName);
        if (!component || !model_of(component))
            continue;
        static_cast<render::RenderModel*>(model_of(component))->change_live(1);
        reinterpret_cast<void(__thiscall*)(ObjectModule*)>((*reinterpret_cast<void***>(component))[1])(component);
    }
}

// 0x00464220
void CinematicTable::refresh_viewer(void* camera)
{
    if (!flag_34b_0_)
        return;
    static_cast<gfx::ViewCam*>(camera)->remember_prev_pos();
    auto* position = reinterpret_cast<float*>(static_cast<gfx::ViewCam*>(camera)->fetch_location());
    auto* matrix = reinterpret_cast<float*>(static_cast<gfx::ViewCam*>(camera)->fetch_mat());
    float rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float translation[4];
    bool flag = false;
    static_cast<gfx::JointClipFrames*>(camera_anim_)
        ->get_float_key_pose(rotation, translation, &flag,
                             static_cast<float>(clock_ - static_cast<double>(camera_start_)),
                             static_cast<int>(reinterpret_cast<uint32_t>(camera_source_)));
    std::memcpy(position, translation, 16);
    float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    quat_to_matrix(rotation, origin, matrix);
    if (skipped_)
        return;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->cinematic_low_heap_);
    static_cast<gfx::JointClipFrames*>(object_anims_)
        ->fire_events(std::bit_cast<float>(unknown3478_), clock_, camera, 0);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00464360
void CinematicTable::update_object_poses()
{
    float* rotations = g_cutscene_rotations;
    float* translations = g_cutscene_translations;
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = entities_[i];
        if (!entry.animation)
            continue;
        ObjectModule* component = entry.object->fetch_module(kSkeletonModuleName);
        if (!component)
            continue;
        // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
        auto* skeleton = reinterpret_cast<uint8_t*>(reinterpret_cast<online::FoldedGetters*>(component)->word18());
        auto* source = static_cast<uint8_t*>(entry.animation->source);
        void* animation = entry.animation->data;
        reinterpret_cast<gfx::Rig*>(skeleton)->set_lod(0.0f);
        uint8_t* data = *reinterpret_cast<uint8_t**>(skeleton + 8);
        *reinterpret_cast<uint8_t**>(source + 0x20C) = data ? data + 0x408 : nullptr;
        *reinterpret_cast<uint32_t*>(source + 0x210) = *reinterpret_cast<uint32_t*>(skeleton + 0xC);
        const uint8_t* base_pose = *reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(skeleton + 8) + 0x590);
        uint32_t count =
            reinterpret_cast<uint32_t(__thiscall*)(void*)>(code_address(&gfx::Rig::get_num_bones))(skeleton);
        std::memcpy(translations, base_pose, count * 16);
        // (within the first twelfth of a second, without the source)
        if (clock_ - static_cast<double>(unknown3480_) < 0.083333336f)
            source = nullptr;
        static_cast<gfx::JointClipFrames*>(animation)->get_frames(
            reinterpret_cast<maths::QuatRotation*>(rotations), reinterpret_cast<maths::Vec4*>(translations),
            g_cutscene_camera_slot, static_cast<float>(clock_ - static_cast<double>(camera_start_)), -1, source);
        reinterpret_cast<gfx::Rig*>(skeleton)->tick_frame(reinterpret_cast<const float (*)[4]>(rotations),
                                                          reinterpret_cast<const float (*)[4]>(translations));
    }
}

} // namespace objects

namespace objects {

// 0x00640A30 (a static initialiser): the 128 cutscene bone rotations, each the quaternion (0, 0, 0, 1).
void construct_cutscene_rotations()
{
    for (int i = 0; i < 128; ++i) {
        g_cutscene_rotations[4 * i + 0] = 0.0f;
        g_cutscene_rotations[4 * i + 1] = 0.0f;
        g_cutscene_rotations[4 * i + 2] = 0.0f;
        g_cutscene_rotations[4 * i + 3] = 1.0f;
    }
}

} // namespace objects
