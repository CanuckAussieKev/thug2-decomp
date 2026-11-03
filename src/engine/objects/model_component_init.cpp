// The model component's construction, destruction, initialisation and update (GameObjects::MeshInstanceModule; the rest in
// model_component.cpp). From the disassembly of 0x0041FFA0-0x00420058, 0x00420270-0x0042028E, 0x00420350-0x004204ED,
// 0x00420BF0-0x00421225, 0x00421230-0x004213D6, 0x00421490-0x004215DE and 0x004219F0-0x00421AD7.

#include <cstddef>
#include <cstdint>
#include <gsl/pointers>
#include <array>
#include <gsl/assert>

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
#include "engine/gfx/sector.h"
#include "engine/objects/anim_blender.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/model_component.h"
#include "engine/script/param_struct.h"
#include "engine/core/checksums.h"
#include "engine/core/str.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/maths/vec4.h"

namespace objects {

namespace {
constexpr std::size_t kModelBlockBytes = 0x0C;
constexpr int kDisplayModeShadow = 6;
constexpr std::size_t kPathBytes = 0x200;

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
gfx::Rig* skeleton_of(ObjectModule* component) { return core::down_cast<objects::BoneRigModule>(component)->bone_rig(); }

// Nx (not rewritten yet): a new model (0x0048BFA0) and its deletion (0x0048C3C0); the model's geoms cleared
// (0x00494BA0), counted (0x00492E40), by index (0x00494640) and name (0x00493CD0), their names (0x00494680), one added
// (0x004938E0); a geom cloned (0x0048DDC0 - for a model - or 0x0048DD80 - a sector's) and activated (0x0048DE60);
// the model loaded from an asset (0x004934A0) or a file (0x004935E0); its scale (0x00492EE0), active flags (0x00492E10),
// skeleton (0x00492DD0), shadow volume (0x00492E90), bounding sphere (0x00492F30), lights (+0x2C) made (0x00493BD0) or
// taken away (0x00493C10), bone volumes (0x00492FC0 / 0x00492F90), placed (0x00493090) and its skinning updated
// (0x00493410).
gsl::owner<render::RenderModel*> spawn_render_model(int arg) { return (void)arg, render::spawn_model(); }
void delete_model(gsl::owner<render::RenderModel*> model) { delete model; }
render::ModelLights* model_lights(render::RenderModel* model) { return model->lights_ptr().get(); }
render::Geom* clone_model_geom(render::Geom* geom, render::RenderModel* to)
{
    return geom->clone_for_model(1, to);
}
render::Geom* clone_sector_geom(render::Geom* geom) { return geom->replicate(1, nullptr); }
void activate_geom(render::Geom* geom)
{
    geom->change_live(true); // (through the jump 0x0048DE60)
}
void add_geom(render::RenderModel* model, render::Geom* geom, uint32_t name) { model->add_geom(geom, name, true); }
// The scale from a structure's scale parameters (0x00487B00, cdecl: false without any).
bool fetch_magnification(maths::Vec4* scale, script::Struct* params) { return gfx::scale_from_args(scale, params); }
// A level sector by name in any scene (0x0048C510) and its geometry (0x004A2BA0: its +0x14).
render::WorldCell* look_up_sector(uint32_t name) { return render::engine::look_up_sector(name); }
render::Geom* sector_geom(const render::WorldCell* sector) { return static_cast<render::Geom*>(sector->geom_handle()); }
// A model file's name with its extension (0x00487A70: name + `ext` into `out`).
void model_path_for(const char* name, const char* ext, char* out) { gfx::make_model_file_name(name, ext, out); }
// The model's lights' brightness (0x0048E660).
void update_lights_brightness(void* lights) { static_cast<render::ModelLights*>(lights)->get_update_brightness(); }

// Shared bone volumes (0x0C bytes: the volumes, their count, users), by skeleton name (0x0048B3C0); made (0x0048B350),
// sized and registered (0x0048BEA0), filled from the skeleton's data (0x0048B430).

} // namespace

// 0x00421490
gsl::owner<ObjectModule*> MeshInstanceModule::fabricate() { return new MeshInstanceModule; }

// 0x00420BF0
MeshInstanceModule::MeshInstanceModule()
{
    set_kind_tag(kType);
    mesh_instance_ = memory::take_unique<render::RenderModel>(spawn_render_model(1));
    pivot_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    for (ModelSpinAxis& axis : twist_rate_) {
        for (ModelSpin& spin : axis.spins_)
            spin.flag_bits_ &= ~(kModelSpinOn | kModelSpinHold);
        axis.active_ = false;
    }
    shift_ = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
}

// 0x00420000 (0x00420270 the deleting one)
MeshInstanceModule::~MeshInstanceModule() { memory::destroy_then_clear(mesh_instance_); }

// 0x004204B0
void MeshInstanceModule::seal()
{
    bone_rig_ = entity()->fetch_module(kSkeleton);
    anim_blender_ptr_ = entity()->fetch_module(kAnimBlender);
    suspend_ = entity()->fetch_module(kSuspend);
}

// 0x004214F0
void MeshInstanceModule::configure_from_data(script::Struct* params)
{
    if (params->has_bare_flag(kUseModelLights)) {
        if (!model_lights(mesh_instance_.get())) {
            mesh_instance_->create_lights();
            render::ModelLights* lights = model_lights(mesh_instance_.get());
            lights->bind_position_source(&entity()->at_point());
            int count = 1;
            params->fetch_int_value(kMaxSceneLights, &count, 0);
            lights->set_dynamic_radius(static_cast<uint32_t>(count)); // (0x00407700)
        }
    } else if (model_lights(mesh_instance_.get())) {
        mesh_instance_->destroy_lights();
    }
    if (ObjectModule* skeleton = entity()->fetch_module(kSkeleton))
        mesh_instance_->choose_rig(skeleton_of(skeleton));
    int shadow = 0;
    params->fetch_int_value(kShadowVolume, &shadow, 0);
    mesh_instance_->activate_shadow_volume(shadow != 0);
    set_up_mesh(params);
}

// 0x004219F0
void MeshInstanceModule::tick_frame()
{
    maths::Mat4 matrix{};
    if (level_object_) {
        // A level object: its sector's model follows.
        GameEntity* object = entity();
        auto* source = core::down_cast<GameEntity>(find_object(linked_entity_label_));
        render::RenderModel* model = core::down_cast<MeshInstanceModule>(source->fetch_module(kModel))->mesh_instance_.get();
        ObjectModule* skeleton = object->fetch_module(kSkeleton);
        get_draw_matrix(&matrix);
        model->render(&matrix, false, skeleton_of(skeleton));
        model->copy_bones(skeleton_of(skeleton));
        return;
    }
    const uint8_t model_flags = mesh_instance_ ? mesh_instance_->flag_bits() : 0;
    if (!mesh_instance_ || !(model_flags & 2) || (model_flags & 4))
        return; // (not active, or hidden)
    get_draw_matrix(&matrix);
    gfx::Rig* skeleton = bone_rig_ ? skeleton_of(bone_rig_) : nullptr;
    bool ready = bone_rig_ != nullptr;
    if (anim_blender_ptr_)
        ready = core::down_cast<AnimBlenderComponent>(anim_blender_ptr_)->model_ready();
    mesh_instance_->render(&matrix, !ready, skeleton);
}

// 0x00420480
void MeshInstanceModule::jump_to()
{
    tick_frame();
    if (mesh_instance_.get() && model_lights(mesh_instance_.get()))
        update_lights_brightness(model_lights(mesh_instance_.get()));
}

// 0x00421360
void MeshInstanceModule::change_mesh(render::RenderModel* model)
{
    // (the model it had isn't deleted: the caller has it - the model swap hands it to the other component)
    memory::let_go(mesh_instance_);
    mesh_instance_.reset(model);
    if (entity()->composite_flags() & kCompositeFlagFinalized) {
        tick_frame();
        if (mesh_instance_.get() && model_lights(mesh_instance_.get()))
            update_lights_brightness(model_lights(mesh_instance_.get()));
    }
    maths::Vec4 sphere;
    enclosing_ball_ = *model->fetch_bound_sphere(&sphere);
}

// 0x00421230
void MeshInstanceModule::set_up_mesh_from_profile(void* profile, bool via_resource_store, int tex_dict_offset,
                                                  checksums::Name build_script_name)
{
    const uint32_t build_script = build_script_name.value();
    render::RenderModel* model = mesh_instance_.get();
    gfx::Rig* skeleton = nullptr;
    if (ObjectModule* component = entity()->fetch_module(kSkeleton))
        skeleton = skeleton_of(component);
    gfx::ModelAssembler builder(via_resource_store, tex_dict_offset); // (destroyed at the end)
    builder.get_build(static_cast<gfx::ModelLook*>(profile), model, skeleton,
                      build_script ? build_script : kDefaultBuildScript);
    if (entity()->composite_flags() & kCompositeFlagFinalized) {
        tick_frame();
        if (mesh_instance_.get() && model_lights(mesh_instance_.get()))
            update_lights_brightness(model_lights(mesh_instance_.get()));
    }
    maths::Vec4 sphere;
    enclosing_ball_ = *model->fetch_bound_sphere(&sphere);
    if (script::fetch_int_value(kBoneVolumesOn))
        init_bone_volumes();
}

// 0x0041FFA0
void MeshInstanceModule::init_from_sector(uint32_t name)
{
    const render::WorldCell* sector = look_up_sector(name);
    if (!sector)
        return;
    render::Geom* geom = sector_geom(sector);
    if (!geom)
        return;
    render::Geom* clone = clone_sector_geom(geom);
    activate_geom(clone);
    add_geom(mesh_instance_.get(), clone, 0);
    sector_model_ = true;
}

// 0x00420350
void MeshInstanceModule::init_bone_volumes()
{
    if (!bone_rig_) {
        bone_rig_ = entity()->fetch_module(kSkeleton);
        if (!bone_rig_)
            return;
    }
    if (mesh_instance_->get_bone_volumes())
        return;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->fetch_heap_by_key(kBoneVolumeHeap, 1));
    const gfx::Rig* skeleton = skeleton_of(bone_rig_);
    gfx::RigData* data = skeleton->payload();
    // (the volumes are shared by skeletons of the same bone count)
    gfx::BoneVolumes* volumes = gfx::find_bone_volumes(static_cast<uint32_t>(skeleton->bone_total()));
    if (volumes) {
        ++volumes->users(); // (another user)
    } else {
        // Made and registered for the skeleton - but not given to this model (the original's): the next one finds it.
        gsl::owner<void*> made = memory::adopt<void*>(memory::alloc(kModelBlockBytes));
        if (made)
            made = new (made) gfx::BoneVolumes(0);
        Expects(made != nullptr);
        static_cast<gfx::BoneVolumes*>(made)->enrol(static_cast<uint32_t>(skeleton->bone_total()));
        static_cast<gfx::BoneVolumes*>(made)->get_fill(data);
    }
    mesh_instance_->set_bone_volumes(volumes);
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00420D10
void MeshInstanceModule::set_up_mesh(script::Struct* params)
{
    mesh_instance_->drop_geoms();
    maths::Vec4 scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f};
    if (fetch_magnification(&scale, params))
        mesh_instance_->change_scale(&scale);
    uint32_t type = 0;
    params->read_hash_value(kClass, &type, 0);
    if (type == kLevelObject) {
        uint32_t name = 0;
        params->read_hash_value(kName, &name, 1);
        init_from_sector(name);
    } else {
        int via_resource_store = 1;
        params->fetch_int_value(kUseAssetManager, &via_resource_store, 0);
        int tex_dict_offset = 0;
        params->fetch_int_value(kTexDictOffset, &tex_dict_offset, 0);
        uint32_t name = 0;
        if (params->read_hash_value(kRefObjectName, &name, 0)) {
            // A level object's model: its sector's (Update follows it).
            linked_entity_label_ = name;
            level_object_ = true;
            mesh_instance_->set_display_mode(kDisplayModeShadow);
        } else if (params->read_hash_value(kAssetName, &name, 0)) {
            int multicolor = 0;
            params->fetch_int_value(kMulticolor, &multicolor, 0);
            mesh_instance_->add_asset_geom({.asset_name_ = name, .name = 0}, multicolor != 0, true);
        } else if (params->read_hash_value(kCloneFrom, &name, 0)) {
            // Another object's model's geoms cloned (and its colour values).
            auto* object = core::down_cast<GameEntity>(object_hub_slot()->look_up_entity(name));
            auto* source = core::down_cast<MeshInstanceModule>(object->fetch_module(kModel));
            color_value_count_ = source->color_value_count_;
            for (int i = 0; i < color_value_count_; ++i)
                core::at(color_values_, i) = core::at(source->color_values_, i);
            uint32_t mesh_label = 0;
            script::Array* geoms = nullptr;
            if (params->read_hash_value(kGeom, &mesh_label, 0)) {
                render::Geom* geom = source->mesh_instance_->get_geom_by_name(mesh_label);
                if (!geom)
                    return; // (nothing after it either)
                render::Geom* clone = clone_model_geom(geom, mesh_instance_.get());
                activate_geom(clone);
                add_geom(mesh_instance_.get(), clone, mesh_label);
            } else if (params->fetch_list(kGeoms, &geoms, 0)) {
                for (uint32_t i = 0; i < geoms->extent(); ++i) {
                    mesh_label = geoms->fetch_part(i);
                    if (!mesh_label)
                        continue;
                    render::Geom* geom = source->mesh_instance_->get_geom_by_name(mesh_label);
                    if (!geom)
                        return;
                    render::Geom* clone = clone_model_geom(geom, mesh_instance_.get());
                    activate_geom(clone);
                    add_geom(mesh_instance_.get(), clone, mesh_label);
                }
            } else {
                for (int i = 0; i < source->mesh_instance_->fetch_geom_total(); ++i) {
                    render::Geom* clone = clone_model_geom(source->mesh_instance_->fetch_geom(i), mesh_instance_.get());
                    activate_geom(clone);
                    add_geom(mesh_instance_.get(), clone, source->mesh_instance_->fetch_part_label(i));
                }
            }
        } else {
            const char* file = nullptr;
            if (params->fetch_text(kModel, &file, false) || params->fetch_text(kModelName, &file, false)) {
                // A model file (unless it's the "none" name).
                if (checksums::hash_from_text(file) != kNoModel) {
                    std::array<char, kPathBytes> path{};
                    uint32_t skeleton_name = 0;
                    if (params->read_hash_value(kSkeletonName, &skeleton_name, 0))
                        model_path_for(file, ".skin", path.data());
                    else
                        model_path_for(file, ".mdl", path.data());
                    text::to_lower_text(path.data());
                    const bool allow_replace = params->has_bare_flag(kAllowReplaceTex);
                    mesh_instance_->add_file_geom(static_cast<const char*>(path.data()), 0, via_resource_store != 0,
                                                  tex_dict_offset, allow_replace, false, true, false);
                }
            } else if (params->read_hash_value(kAsset, &name, 0)) {
                mesh_instance_->add_asset_geom({.asset_name_ = name, .name = 0}, false, true);
            } else if (params->read_hash_value(kModel, &name, 0)) {
                // A level sector's geometry as the model.
                render::Geom* clone = clone_sector_geom(sector_geom(look_up_sector(name)));
                activate_geom(clone);
                add_geom(mesh_instance_.get(), clone, 0);
            }
        }
    }
    if (entity()->composite_flags() & kCompositeFlagFinalized) {
        tick_frame();
        if (mesh_instance_.get() && model_lights(mesh_instance_.get()))
            update_lights_brightness(model_lights(mesh_instance_.get()));
    }
    maths::Vec4 sphere;
    enclosing_ball_ = *mesh_instance_->fetch_bound_sphere(&sphere);
    if (script::fetch_int_value(kBoneVolumesOn))
        init_bone_volumes();
}

} // namespace objects
