// The engine's model: its members on the geom list - counts, flags, bones, visibility, colours, lights and UVs. See
// model.h (loading, drawing and the rest: model_geoms.cpp).

#include <cstdint>

#include "engine/gfx/model.h"

#include "engine/assets/resource_registry.h"
#include "engine/gfx/engine_mesh.h"
#include "engine/gfx/model_internal.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/pc_mesh.h"
#include "engine/gfx/bone_volumes.h"
#include "engine/maths/vec4.h"
#include <bit>
#include <cstddef>
#include <gsl/pointers>
#include <span>
#include "engine/core/mem.h"

namespace render {

constexpr float kStubSphereY = 36.0F;          // a model's stub bounding sphere: centred 36 up
constexpr float kSkinnedSphereRadius = 48.0F;  // with bones
constexpr float kStaticSphereRadius = 1200.0F; // without
constexpr uint32_t kFloatOneBits = 0x3F800000; // 1.0f

using namespace model_geoms;

namespace {

// A flag of flag_bits_ set from a bit (the setters take a flag's low bit, as the original's shifts and masks).
uint8_t set_flag(uint8_t flags, uint8_t flag, int bit)
{
    return static_cast<uint8_t>(bit ? flags | flag : flags & ~flag);
}

// The first geom's entry (as the original: through the head's next even when there's no head).
ModelGeom* first_entry(RenderModel* model) { return table_row(get_first(model)); }

// An asset's scene data's +0xB8 / +0xB4 (the folded getters 0x00490AC0 / 0x00490AB0).
uint32_t scene_b8(void* scene) { return static_cast<SubMesh*>(scene)->get_hierarchy_count(); }
HierarchyNode* scene_b4(void* scene) { return static_cast<SubMesh*>(scene)->fetch_bone_tree(); }

// Lights made and deleted (the engine's 0x0048EA80 / 0x0048C080).
gsl::owner<ModelLights*> new_lights() { return spawn_model_lights(); }

} // namespace

// 0x00492EB0
maths::Vec4* RenderModel::plat_get_bounding_sphere(maths::Vec4* out)
{
    out->z = 0.0F;
    out->y = kStubSphereY;
    out->x = 0.0F;
    out->w = bone_total_ > 0 ? kSkinnedSphereRadius : kStaticSphereRadius;
    return out;
}

// 0x00492DD0
bool RenderModel::choose_rig(gfx::Rig* skeleton)
{
    if (!skin_palette_) {
        bone_total_ = skeleton_bone_count(skeleton);
        allot_bones(skeleton_bone_count(skeleton));
        skin_palette_ = skeleton_bones(skeleton);
    }
    return true;
}

// 0x00492E10
bool RenderModel::set_display_mode(int mode)
{
    if (mode == display_mode_)
        return false;
    const bool allowed = allows_display_mode(mode);
    if (allowed)
        display_mode_ = mode;
    return allowed;
}

// 0x00492E40
int RenderModel::fetch_geom_total() const { return geom_total_; }

// 0x00492E50
maths::Mat4* RenderModel::fetch_skinning_palette() { return skin_palette_; }

// 0x00493460
void RenderModel::upload_skinning_palette(maths::Mat4* matrices)
{
    skin_palette_ = matrices;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        set_geom_bones(get_geom(node), skin_palette_, bone_total_);
}

// 0x00492E60
void RenderModel::allow_resizing(uint8_t on) { flag_bits_ = set_flag(flag_bits_, kModelScaled, on & 1); }

// 0x00492E80
void RenderModel::change_bound_sphere(const maths::Vec4* sphere)
{
    flag_bits_ &= ~kModelSphereValid;
    plat_set_bounding_sphere(sphere);
}

// 0x00492E90
void RenderModel::activate_shadow_volume(uint8_t on) { flag_bits_ = set_flag(flag_bits_, kModelShadowVolume, on & 1); }

// 0x00492EE0
bool RenderModel::change_scale(const maths::Vec4* scale)
{
    size_factor_ = *scale;
    // (compared as bit patterns)
    auto is_one = [](float value) { return std::bit_cast<uint32_t>(value) == kFloatOneBits; };
    const bool scaled = !(is_one(scale->x) && is_one(scale->y) && is_one(scale->z));
    flag_bits_ = set_flag(flag_bits_, kModelScaled, scaled);
    return true;
}

// 0x00492F30
maths::Vec4* RenderModel::fetch_bound_sphere(maths::Vec4* out)
{
    if (!(flag_bits_ & kModelSphereValid)) {
        maths::Vec4 sphere;
        enclosing_ball_ = *plat_get_bounding_sphere(&sphere);
        flag_bits_ |= kModelSphereValid;
    }
    *out = enclosing_ball_;
    return out;
}

// 0x00492F90
void RenderModel::set_bone_volumes(gfx::BoneVolumes* volumes)
{
    if (!geom_ring_.size())
        return;
    ModelGeom* first = first_entry(this);
    if (first && !first->bone_volumes_)
        first->bone_volumes_ = volumes;
}

// 0x00492FC0
gfx::BoneVolumes* RenderModel::get_bone_volumes()
{
    if (!geom_ring_.size())
        return nullptr;
    const ModelGeom* first = first_entry(this);
    return first ? first->bone_volumes_ : nullptr;
}

// 0x00492FF0
uint32_t RenderModel::get_asset_bone_count()
{
    if (num_asset_geoms_ != 1)
        return 0;
    return scene_b8(assets::asset_manager()->get_asset_data(first_entry(this)->asset_name_, 1));
}

// 0x00493040
HierarchyNode* RenderModel::get_asset_hierarchy()
{
    if (num_asset_geoms_ != 1)
        return nullptr;
    return scene_b4(assets::asset_manager()->get_asset_data(first_entry(this)->asset_name_, 1));
}

std::span<HierarchyNode> RenderModel::asset_hierarchy_run()
{
    return {get_asset_hierarchy(), static_cast<std::size_t>(get_asset_bone_count())};
}

// 0x00493410
bool RenderModel::copy_bones(gfx::Rig* skeleton)
{
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        copy_geom_bones(get_geom(node), skeleton_bones(skeleton), skeleton_bone_count(skeleton));
    return true;
}

// 0x00493B30
uint32_t RenderModel::get_strip_break_flags()
{
    uint32_t flags = 0;
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next) {
        const ModelGeom* entry = table_row(node);
        if (entry->scene) {
            flags |= entry->scene->bone_count();
        } else if (entry->asset_name_) {
            if (const auto* mesh = static_cast<const PcMesh*>(assets::asset_manager()->get_asset_data(entry->asset_name_, 1)))
                flags |= mesh->bone_count();
        }
    }
    return flags;
}

// 0x00493B90
void RenderModel::break_strips(uint32_t mask)
{
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        break_geom_strips(get_geom(node), mask);
}

// 0x00494700
void RenderModel::clean_instance_strips()
{
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        clean_geom_instance_strips(get_geom(node));
}

// 0x00494B30
bool RenderModel::break_own_strips()
{
    const uint32_t mask = get_strip_break_flags();
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        break_geom_strips(get_geom(node), mask);
    return true;
}

// 0x00493BD0
bool RenderModel::create_lights()
{
    memory::let_go(lights_ptr_); // (any it had isn't freed, as the original)
    lights_ptr_ = memory::take_unique<ModelLights>(new_lights());
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        set_geom_lights(get_geom(node), lights_ptr_.get());
    return true;
}

// 0x00493C10
bool RenderModel::destroy_lights()
{
    memory::destroy_then_clear(lights_ptr_);
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        set_geom_lights(get_geom(node), nullptr);
    return true;
}

// 0x00493C60
bool RenderModel::change_live(uint8_t active)
{
    if ((flag_bits_ & kModelHidden) && active)
        return false;
    if (((flag_bits_ >> 1) & 1) != active) {
        for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
            set_geom_active(get_geom(node), active);
        flag_bits_ = set_flag(flag_bits_, kModelEnabledBit, active & 1);
    }
    return true;
}

// 0x00494B70
bool RenderModel::conceal(uint8_t hide)
{
    flag_bits_ = set_flag(flag_bits_, kModelHidden, hide & 1);
    return change_live(hide == 0);
}

// 0x00493CD0
Geom* RenderModel::get_geom_by_name(uint32_t name)
{
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next) {
        if (name == table_row(node)->name)
            return get_geom(node);
    }
    return nullptr;
}

// 0x00494640
Geom* RenderModel::fetch_geom(int index)
{
    int i = 0;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next, ++i) {
        if (index == i)
            return get_geom(node);
    }
    return nullptr;
}

// 0x00494680
uint32_t RenderModel::fetch_part_label(int index)
{
    int i = 0;
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next, ++i) {
        if (index == i)
            return table_row(node)->name;
    }
    return 0;
}

// 0x004946C0
TextureSet* RenderModel::get_geom_textures(int index)
{
    int i = 0;
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next, ++i) {
        if (index == i)
            return table_row(node)->textures;
    }
    return nullptr;
}

// 0x00493FB0
bool RenderModel::clear_geom_color(uint32_t name)
{
    const bool all = name == kAllGeoms;
    bool any = false;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next) {
        if (name == table_row(node)->name || all) {
            model_geoms::clear_geom_color(get_geom(node));
            any = true;
        }
    }
    return any;
}

// 0x00494110
bool RenderModel::choose_uv_shift(uint32_t material, int pass, float u, float v)
{
    bool any = false;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        any |= get_geom(node)->set_material_uv_wibble_offsets(material, pass, u, v);
    return any;
}

// 0x00494160
bool RenderModel::choose_uv_matrix(uint32_t material, int pass, const maths::Mat4* matrix)
{
    bool any = false;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        any |= get_geom(node)->choose_uv_matrix(material, pass, matrix);
    return any;
}

// 0x004941B0
bool RenderModel::create_uv_anim_params(uint32_t material, int pass)
{
    bool any = false;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        any |= get_geom(node)->create_uv_anim_params(material, pass);
    return any;
}

// 0x00494800
bool RenderModel::conceal_mesh(uint32_t name, bool hide)
{
    const bool all = name == kAllGeoms;
    bool any = false;
    int index = 0;
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next, ++index) {
        if (name == table_row(node)->name || all) {
            if (hide)
                visible_geoms_ &= ~geom_bit(index);
            else
                visible_geoms_ |= geom_bit(index);
            any = true;
        }
    }
    return any;
}

// 0x00494880
bool RenderModel::is_part_concealed(uint32_t name)
{
    bool hidden = true;
    int index = 0;
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next, ++index) {
        if (name == table_row(node)->name && (visible_geoms_ & geom_bit(index)))
            hidden = false;
    }
    return hidden;
}

// 0x004948D0
bool RenderModel::change_tint(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint32_t rgba =
        r | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        set_geom_color(get_geom(node), rgba);
    return true;
}

// 0x00494930
uint32_t RenderModel::fetch_shown()
{
    uint32_t mask = 0;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        mask |= get_geom_visibility(get_geom(node));
    return mask;
}

// 0x00494970
bool RenderModel::toggle_shown(uint32_t mask)
{
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        set_geom_visibility(get_geom(node), mask);
    return true;
}

// 0x004949B0
void RenderModel::activate_shadow(uint8_t on)
{
    flag_bits_ = set_flag(flag_bits_, kModelShadow, on & 1);
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head(); node = node->next)
        set_geom_shadow(get_geom(node), on);
}

} // namespace render
