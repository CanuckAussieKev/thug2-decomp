// The model component's construction, destruction, initialisation and update (GameObjects::MeshInstanceModule; the rest in
// model_component.cpp). From the disassembly of 0x0041FFA0-0x00420058, 0x00420270-0x0042028E, 0x00420350-0x004204ED,
// 0x00420BF0-0x00421225, 0x00421230-0x004213D6, 0x00421490-0x004215DE and 0x004219F0-0x00421AD7.

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/bone_volumes.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/model_builder.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/rig.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/param_struct.h"
#include "engine/core/checksums.h"
#include "engine/core/str.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/skeleton_component.h"

namespace objects {

namespace {

constexpr uint32_t kSkeleton = 0x222756D5;
constexpr uint32_t kAnimBlender = 0x7149B753;
constexpr uint32_t kSuspend = 0xCE0CA665;

// configure_from_data's / set_up_mesh's parameters.
constexpr uint32_t kUseModelLights = 0x10079F2D;
constexpr uint32_t kMaxSceneLights = 0xD223B8BD;
constexpr uint32_t kShadowVolume = 0x2A1F92C0;
constexpr uint32_t kClass = 0x12B4E660;
constexpr uint32_t kLevelObject = 0xB7B3BD86;
constexpr uint32_t kName = 0xA1DC81F9;
constexpr uint32_t kUseAssetManager = 0xC63C8D38;
constexpr uint32_t kTexDictOffset = 0xF891AC27;
constexpr uint32_t kRefObjectName = 0x153A84DE;
constexpr uint32_t kAssetName = 0x86BD5B8F;
constexpr uint32_t kMulticolor = 0x92B43E79;
constexpr uint32_t kCloneFrom = 0x7DD037B3;
constexpr uint32_t kGeom = 0xE7308C1F;
constexpr uint32_t kGeoms = 0x44E31DFF;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kModelName = 0xB974F2FC;
constexpr uint32_t kAsset = 0x354BA8E9; // (an asset by checksum; its name isn't in THUG2's scripts)
constexpr uint32_t kSkeletonName = 0x09794932;
constexpr uint32_t kAllowReplaceTex = 0x6C37FDC7;
constexpr uint32_t kNoModel = 0x806FFF30; // (a model name meaning none)
constexpr uint32_t kDefaultBuildScript = 0x91C1EC93;
constexpr uint32_t kBoneVolumesOn = 0x74FF5CB4; // (a script global integer: bone volumes made for models)
constexpr uint32_t kBoneVolumeHeap = 0xA432DC41;

// A skeleton component's skeleton (0x005480F0: its +0x18), a skeleton's name (0x004A2BA0: its +0x14) and data (+0x08).
void* skeleton_of(ObjectModule* component) { return static_cast<objects::BoneRigModule*>(component)->bone_rig_; }
uint32_t get_skeleton_name(void* skeleton)
{
    return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(skeleton) + 0x14);
}
void* rig_definition(void* skeleton) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(skeleton) + 0x08); }

// Nx (not rewritten yet): a new model (0x0048BFA0) and its deletion (0x0048C3C0); the model's geoms cleared
// (0x00494BA0), counted (0x00492E40), by index (0x00494640) and name (0x00493CD0), their names (0x00494680), one added
// (0x004938E0); a geom cloned (0x0048DDC0 - for a model - or 0x0048DD80 - a sector's) and activated (0x0048DE60);
// the model loaded from an asset (0x004934A0) or a file (0x004935E0); its scale (0x00492EE0), active flags (0x00492E10),
// skeleton (0x00492DD0), shadow volume (0x00492E90), bounding sphere (0x00492F30), lights (+0x2C) made (0x00493BD0) or
// taken away (0x00493C10), bone volumes (0x00492FC0 / 0x00492F90), placed (0x00493090) and its skinning updated
// (0x00493410).
render::RenderModel* spawn_render_model(int arg) { return (void)arg, render::spawn_model(); }
void delete_model(render::RenderModel* model) { delete model; }
void* model_lights(render::RenderModel* model) { return model->lights_ptr_; }
void* clone_model_geom(void* geom, render::RenderModel* to)
{
    return static_cast<render::Geom*>(geom)->clone_for_model(1, to);
}
void* clone_sector_geom(void* geom) { return static_cast<render::Geom*>(geom)->replicate(1, nullptr); }
void activate_geom(void* geom)
{
    static_cast<render::Geom*>(geom)->change_live(true); // (through the jump 0x0048DE60)
}
void add_geom(render::RenderModel* model, void* geom, uint32_t name) { model->add_geom(geom, name, true); }
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any).
bool fetch_magnification(maths::Vec4* scale, script::Struct* params) { return gfx::scale_from_args(scale, params); }
// A level sector by name in any scene (0x0048C510) and its geometry (0x004A2BA0: its +0x14).
void* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }
void* sector_geom(void* sector) { return *reinterpret_cast<void**>(static_cast<uint8_t*>(sector) + 0x14); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void model_path_for(const char* name, const char* ext, char* out) { gfx::make_model_file_name(name, ext, out); }
// The model's lights' brightness (0x0048E660).
void update_lights_brightness(void* lights) { static_cast<render::ModelLights*>(lights)->get_update_brightness(); }

// Shared bone volumes (0x0C bytes: the volumes, their count, users), by skeleton name (0x0048B3C0); made (0x0048B350),
// sized and registered (0x0048BEA0), filled from the skeleton's data (0x0048B430).
void* find_bone_volumes(uint32_t name) { return gfx::find_bone_volumes(name); }

} // namespace

// 0x00421490
ObjectModule* __cdecl MeshInstanceModule::fabricate() { return new MeshInstanceModule; }

// 0x00420BF0
MeshInstanceModule::MeshInstanceModule()
{
    kind_tag_ = kType;
    mesh_instance_ = spawn_render_model(1);
    pivot_ = {0.0f, 0.0f, 0.0f, 1.0f};
    for (ModelSpinAxis& axis : twist_rate_) {
        for (ModelSpin& spin : axis.spins_)
            spin.flag_bits_ &= ~(kModelSpinOn | kModelSpinHold);
        axis.active_ = false;
    }
    color_value_count_ = 0;
    sector_model_ = false;
    shift_ = {0.0f, 0.0f, 0.0f, 1.0f};
    brightness_ = 1.0f;
}

// 0x00420000 (0x00420270 the deleting one)
MeshInstanceModule::~MeshInstanceModule() { delete_model(mesh_instance_); }

// 0x004204B0
void MeshInstanceModule::seal()
{
    bone_rig_ = entity_->fetch_module(kSkeleton);
    anim_blender_ptr_ = entity_->fetch_module(kAnimBlender);
    suspend_ = entity_->fetch_module(kSuspend);
}

// 0x004214F0
void MeshInstanceModule::configure_from_data(script::Struct* params)
{
    if (params->has_bare_flag(kUseModelLights)) {
        if (!model_lights(mesh_instance_)) {
            mesh_instance_->create_lights();
            void* lights = model_lights(mesh_instance_);
            static_cast<render::ModelLights*>(lights)->bind_position_source(&entity_->at_point_);
            int count = 1;
            params->fetch_int_value(kMaxSceneLights, &count, 0);
            *reinterpret_cast<int*>(static_cast<uint8_t*>(lights) + 4) = count; // (0x00407700)
        }
    } else if (model_lights(mesh_instance_)) {
        mesh_instance_->destroy_lights();
    }
    if (ObjectModule* skeleton = entity_->fetch_module(kSkeleton))
        mesh_instance_->choose_rig(reinterpret_cast<uint8_t*>(skeleton_of(skeleton)));
    int shadow = 0;
    params->fetch_int_value(kShadowVolume, &shadow, 0);
    mesh_instance_->activate_shadow_volume(shadow != 0);
    set_up_mesh(params);
}

// 0x004219F0
void MeshInstanceModule::tick_frame()
{
    maths::Mat4 matrix;
    if (level_object_) {
        // A level object: its sector's model follows.
        GameEntity* object = entity_;
        auto* source = static_cast<GameEntity*>(find_object(linked_entity_label_));
        render::RenderModel* model = static_cast<MeshInstanceModule*>(source->fetch_module(kModel))->mesh_instance_;
        ObjectModule* skeleton = object->fetch_module(kSkeleton);
        get_draw_matrix(&matrix);
        model->render(&matrix, false, reinterpret_cast<uint8_t*>(skeleton_of(skeleton)));
        model->copy_bones(reinterpret_cast<uint8_t*>(skeleton_of(skeleton)));
        return;
    }
    uint8_t model_flags = mesh_instance_ ? mesh_instance_->flag_bits_ : 0;
    if (!mesh_instance_ || !(model_flags & 2) || (model_flags & 4))
        return; // (not active, or hidden)
    get_draw_matrix(&matrix);
    void* skeleton = bone_rig_ ? skeleton_of(bone_rig_) : nullptr;
    bool ready = bone_rig_ != nullptr;
    if (anim_blender_ptr_)
        ready = static_cast<AnimBlenderComponent*>(anim_blender_ptr_)->model_ready();
    mesh_instance_->render(&matrix, !ready, reinterpret_cast<uint8_t*>(skeleton));
}

// 0x00420480
void MeshInstanceModule::jump_to()
{
    tick_frame();
    if (mesh_instance_ && model_lights(mesh_instance_))
        update_lights_brightness(model_lights(mesh_instance_));
}

// 0x00421360
void MeshInstanceModule::change_mesh(render::RenderModel* model)
{
    mesh_instance_ = model;
    if (entity_->composite_flags_ & kCompositeFlagFinalized) {
        tick_frame();
        if (mesh_instance_ && model_lights(mesh_instance_))
            update_lights_brightness(model_lights(mesh_instance_));
    }
    maths::Vec4 sphere;
    enclosing_ball_ = *model->fetch_bound_sphere(&sphere);
}

// 0x00421230
void MeshInstanceModule::set_up_mesh_from_profile(void* profile, bool via_resource_store, int tex_dict_offset,
                                                  uint32_t build_script)
{
    render::RenderModel* model = mesh_instance_;
    void* skeleton = nullptr;
    if (ObjectModule* component = entity_->fetch_module(kSkeleton))
        skeleton = skeleton_of(component);
    gfx::ModelAssembler builder(via_resource_store, tex_dict_offset); // (destroyed at the end)
    builder.get_build(static_cast<gfx::ModelLook*>(profile), model, static_cast<gfx::Rig*>(skeleton),
                      build_script ? build_script : kDefaultBuildScript);
    if (entity_->composite_flags_ & kCompositeFlagFinalized) {
        tick_frame();
        if (mesh_instance_ && model_lights(mesh_instance_))
            update_lights_brightness(model_lights(mesh_instance_));
    }
    maths::Vec4 sphere;
    enclosing_ball_ = *model->fetch_bound_sphere(&sphere);
    if (script::fetch_int_value(kBoneVolumesOn))
        init_bone_volumes();
}

// 0x0041FFA0
void MeshInstanceModule::init_from_sector(uint32_t name)
{
    void* sector = look_up_sector(name);
    if (!sector)
        return;
    void* geom = sector_geom(sector);
    if (!geom)
        return;
    void* clone = clone_sector_geom(geom);
    activate_geom(clone);
    add_geom(mesh_instance_, clone, 0);
    sector_model_ = true;
}

// 0x00420350
void MeshInstanceModule::init_bone_volumes()
{
    if (!bone_rig_) {
        bone_rig_ = entity_->fetch_module(kSkeleton);
        if (!bone_rig_)
            return;
    }
    if (mesh_instance_->get_bone_volumes())
        return;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->fetch_heap_by_key(kBoneVolumeHeap, 1));
    void* skeleton = skeleton_of(bone_rig_);
    void* data = rig_definition(skeleton);
    void* volumes = find_bone_volumes(get_skeleton_name(skeleton));
    if (volumes) {
        ++*reinterpret_cast<int*>(static_cast<uint8_t*>(volumes) + 8); // (another user)
    } else {
        // Made and registered for the skeleton - but not given to this model (the original's): the next one finds it.
        void* made = memory::alloc(0x0C);
        if (made)
            made = static_cast<gfx::BoneVolumes*>(made)->make_new(0);
        static_cast<gfx::BoneVolumes*>(made)->enrol(get_skeleton_name(skeleton));
        static_cast<gfx::BoneVolumes*>(made)->get_fill(static_cast<gfx::RigData*>(data));
    }
    mesh_instance_->set_bone_volumes(reinterpret_cast<uint8_t*>(volumes));
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00420D10
void MeshInstanceModule::set_up_mesh(script::Struct* params)
{
    mesh_instance_->drop_geoms();
    maths::Vec4 scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (fetch_magnification(&scale, params))
        mesh_instance_->change_scale(&scale);
    uint32_t type = 0;
    params->read_hash_value(kClass, &type, 0);
    if (type == kLevelObject) {
        uint32_t name;
        params->read_hash_value(kName, &name, 1);
        init_from_sector(name);
    } else {
        int via_resource_store = 1;
        params->fetch_int_value(kUseAssetManager, &via_resource_store, 0);
        int tex_dict_offset = 0;
        params->fetch_int_value(kTexDictOffset, &tex_dict_offset, 0);
        uint32_t name;
        if (params->read_hash_value(kRefObjectName, &name, 0)) {
            // A level object's model: its sector's (Update follows it).
            linked_entity_label_ = name;
            level_object_ = true;
            mesh_instance_->set_display_mode(6);
        } else if (params->read_hash_value(kAssetName, &name, 0)) {
            int multicolor = 0;
            params->fetch_int_value(kMulticolor, &multicolor, 0);
            mesh_instance_->add_asset_geom(name, 0, multicolor != 0, 1);
        } else if (params->read_hash_value(kCloneFrom, &name, 0)) {
            // Another object's model's geoms cloned (and its colour values).
            auto* object = static_cast<GameEntity*>(object_hub_slot()->look_up_entity(name));
            auto* source = static_cast<MeshInstanceModule*>(object->fetch_module(kModel));
            color_value_count_ = source->color_value_count_;
            for (int i = 0; i < color_value_count_; ++i)
                color_values_[i] = source->color_values_[i];
            uint32_t mesh_label = 0;
            script::Array* geoms;
            if (params->read_hash_value(kGeom, &mesh_label, 0)) {
                void* geom = source->mesh_instance_->get_geom_by_name(mesh_label);
                if (!geom)
                    return; // (nothing after it either)
                void* clone = clone_model_geom(geom, mesh_instance_);
                activate_geom(clone);
                add_geom(mesh_instance_, clone, mesh_label);
            } else if (params->fetch_list(kGeoms, &geoms, 0)) {
                for (uint32_t i = 0; i < geoms->extent_; ++i) {
                    mesh_label = geoms->fetch_part(i);
                    if (!mesh_label)
                        continue;
                    void* geom = source->mesh_instance_->get_geom_by_name(mesh_label);
                    if (!geom)
                        return;
                    void* clone = clone_model_geom(geom, mesh_instance_);
                    activate_geom(clone);
                    add_geom(mesh_instance_, clone, mesh_label);
                }
            } else {
                for (int i = 0; i < source->mesh_instance_->fetch_geom_total(); ++i) {
                    void* clone = clone_model_geom(source->mesh_instance_->fetch_geom(i), mesh_instance_);
                    activate_geom(clone);
                    add_geom(mesh_instance_, clone, source->mesh_instance_->fetch_part_label(i));
                }
            }
        } else {
            const char* file;
            if (params->fetch_text(kModel, &file, false) || params->fetch_text(kModelName, &file, false)) {
                // A model file (unless it's the "none" name).
                if (checksums::hash_from_text(file) != kNoModel) {
                    char path[0x200];
                    uint32_t skeleton_name;
                    if (params->read_hash_value(kSkeletonName, &skeleton_name, 0))
                        model_path_for(file, ".skin", path);
                    else
                        model_path_for(file, ".mdl", path);
                    text::to_lower_text(path);
                    bool allow_replace = params->has_bare_flag(kAllowReplaceTex);
                    mesh_instance_->add_file_geom(static_cast<const char*>(path), 0, via_resource_store != 0,
                                                  tex_dict_offset, allow_replace, 0, 1, 0);
                }
            } else if (params->read_hash_value(kAsset, &name, 0)) {
                mesh_instance_->add_asset_geom(name, 0, 0, 1);
            } else if (params->read_hash_value(kModel, &name, 0)) {
                // A level sector's geometry as the model.
                void* clone = clone_sector_geom(sector_geom(look_up_sector(name)));
                activate_geom(clone);
                add_geom(mesh_instance_, clone, 0);
            }
        }
    }
    if (entity_->composite_flags_ & kCompositeFlagFinalized) {
        tick_frame();
        if (mesh_instance_ && model_lights(mesh_instance_))
            update_lights_brightness(model_lights(mesh_instance_));
    }
    maths::Vec4 sphere;
    enclosing_ball_ = *mesh_instance_->fetch_bound_sphere(&sphere);
    if (script::fetch_int_value(kBoneVolumesOn))
        init_bone_volumes();
}

} // namespace objects
