// The engine's model: made and destroyed, its geoms added (from a model file, the asset manager or as they are) and
// removed, drawn, tested against bone volumes, their textures replaced and their colours set from a script. See
// model.h (the simpler members: model.cpp).

#include <bit>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/assets/resource_entry.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/gfx/bone_volumes.h"
#include "engine/gfx/debug_lines.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_internal.h"
#include "engine/gfx/particle_system.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_texture.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/pager_message.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/pc_mesh.h"

namespace render {

using namespace model_geoms;

namespace {

// Checksums: "none" (a new model's file name), the names a geom gets when it's the first, the debug switch read
// when no texture was replaced, the script parameters.
constexpr uint32_t kNone = 0x6607DD3E;
constexpr uint32_t kFirstAssetGeom = 0x7DE76579;
constexpr uint32_t kFirstAddedGeom = 0x9FACC1A5;
constexpr uint32_t kTextureReplaceDebug = 0x2A648514;
constexpr uint32_t kName = 0xB6F08F39;
constexpr uint32_t kMaterials = 0x64E8E94A;
constexpr uint32_t kMaterial = 0x83418A6A;
constexpr uint32_t kPass = 0x318F2BDB;
constexpr uint32_t kSkinColor = 0xA435752C; // the profile's skin colour structure
constexpr uint32_t kFaceImage = 0xB00B0DC0; // the face texture's name in the dictionary

// A geom list node (std::list<SModelGeom*>, the game's): one inserted before `where` (0x004964A0), a range erased
// (0x004ECC30; the result iterator out).
void insert_geom(RenderModel* model, ModelGeomNode* where, ModelGeom** entry)
{
    model->geom_ring_.insert(where, *entry);
}
void erase_geoms(RenderModel* model, ModelGeomNode* first, ModelGeomNode* last) { model->geom_ring_.erase(first, last); }

// A node's memory: from the default heap, or the current one (0x00658555: whether to assert when out of it).
ModelGeom* new_entry(bool default_heap)
{
    if (default_heap)
        return static_cast<ModelGeom*>(memory::alloc(sizeof(ModelGeom)));
    return static_cast<ModelGeom*>(memory::Hub::get_pull()->carve(sizeof(ModelGeom), g_alloc_assert_flag, nullptr));
}

// A geom's own mesh deleted with its textures.
void delete_own_scene(PcMesh* mesh)
{
    TextureSet* textures = mesh->textures_;
    discard_mesh(mesh);
    unload_textures(textures);
}

// The engine (not rewritten yet): a model file's textures (0x0049F180) and scene (0x0048C3D0) loaded; bone volumes
// let go (0x0048B400); a line tested against bone volumes (0x0048B580) and a bone's volume (0x0048BC40); a skeleton
// drawn (0x004A31E0).
TextureSet* load_textures(const char* file_name, int tex_dict_offset, bool force_tex_dict)
{
    return load_model_textures(file_name, tex_dict_offset, force_tex_dict);
}
PcMesh* load_scene(const char* file_name, TextureSet* textures, int tex_dict_offset, bool shadow_volume)
{
    return engine::load_model_file(file_name, textures, tex_dict_offset, shadow_volume);
}

// The texture dictionary (0x00700F1C): a texture by checksum (0x0049F540), one taken out (0x0049F500), a texture
// made from an image (0x0049FA20) or loaded (0x0049F9B0); a texture's own replaced in a dictionary by name (0x0049FB20)
// or by a texture (0x0049F800).
void* get_textures() { return g_game_texture_dict; }
void* find_texture(uint32_t checksum) { return static_cast<TextureSet*>(get_textures())->fetch_skin(checksum); }
void detach_texture(void* texture)
{
    static_cast<TextureSet*>(get_textures())->unload_texture(static_cast<BitmapTexture*>(texture));
}

// The texture's checksum (+4, the folded getter 0x005480C0).
uint32_t texture_hash(void* texture) { return static_cast<BitmapTexture*>(texture)->checksum_; }

// A colour channel (0-1) as a byte: x * 255 + 0.5, truncated (the original's x87 and _ftol).
uint32_t color_byte(float value)
{
    // NOLINTNEXTLINE(bugprone-incorrect-roundings): rounds a value that is never negative
    return static_cast<uint8_t>(static_cast<int64_t>(static_cast<double>(value) * 255.0 + 0.5));
}
uint32_t rgba_from_hsv(float h, float s, float v)
{
    float r = 0.0f, g = 0.0f, b = 0.0f;
    hsv_to_rgb(&r, &g, &b, h, s, v);
    return color_byte(r) | (color_byte(g) << 8) | (color_byte(b) << 16) | 0xFF000000u;
}

// Strings copied and appended (no length limit, as the original's).
void copy_string(char* into, const char* from) { std::memcpy(into, from, std::strlen(from) + 1); }
void append_string(char* into, const char* from) { copy_string(into + std::strlen(into), from); }

// A texture name made lower case with ".png" on the end (unless it has it).
void get_png_name(char* into, const char* name)
{
    copy_string(into, name);
    text::to_lower_text(into);
    if (!std::strstr(into, ".png"))
        append_string(into, ".png");
}

} // namespace

// 0x00494C40
RenderModel::RenderModel()
{
    // The list's head: a node linked to itself (none when out of memory).
    auto* head = static_cast<ModelGeomNode*>(memory::alloc(sizeof(ModelGeomNode)));
    if (head)
        head->next = head->prev = head;
    geom_ring_.head = head;
    bone_total_ = 0;
    skin_palette_ = nullptr;
    if (display_mode_ != 6 && allows_display_mode(6))
        display_mode_ = 6;
    geom_total_ = 0;
    source_path_ = kNone;
    flag_bits_ = static_cast<uint8_t>((flag_bits_ & ~kModelHidden) | kModelEnabledBit);
    size_factor_ = {1.0f, 1.0f, 1.0f, 1.0f};
    flag_bits_ &= ~(kModelSphereValid | kModelScaled | kModelShadow | kModelShadowVolume);
    lights_ptr_ = nullptr;
}

// 0x00494D00
RenderModel::~RenderModel()
{
    if (lights_ptr_)
        discard_lights(lights_ptr_);
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        if (const gfx::BoneVolumes* volumes = table_row(node)->bone_volumes_)
            gfx::release_bone_volumes(volumes->num_); // (the table keeps them by their bone count)
    }
    drop_geoms();
    erase_geoms(this, get_first(this), geom_ring_.head);
    memory::Hub::get_pull()->dealloc(geom_ring_.head);
}

// 0x00494BA0
void RenderModel::drop_geoms()
{
    const ModelGeomNode* head = geom_ring_.head;
    for (const ModelGeomNode* node = get_first(this); node != head; node = node->next) {
        ModelGeom* entry = table_row(node);
        delete_geom_object(entry->geom);
        if (entry->scene)
            delete_own_scene(entry->scene);
        memory::Hub::get_pull()->dealloc(entry);
    }
    erase_geoms(this, get_first(this), geom_ring_.head);
    geom_total_ = 0;
    num_file_geoms_ = 0;
    num_asset_geoms_ = 0;
}

// 0x00494730
void RenderModel::detach_geom(uint32_t name)
{
    ModelGeomNode* node = get_first(this);
    while (node != geom_ring_.head && table_row(node)->name != name)
        node = node->next;
    if (node == geom_ring_.head)
        return;
    debug::printf("Uninitializing geometry named %s\n", script::find_checksum_name_or_unknown(name));
    ModelGeom* entry = table_row(node);
    delete_geom_object(entry->geom);
    if (entry->scene)
        delete_own_scene(entry->scene);
    if (entry->scene)
        --num_file_geoms_;
    else
        --num_asset_geoms_;
    --geom_total_;
    memory::Hub::get_pull()->dealloc(entry);
    if (node != geom_ring_.head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        memory::Hub::get_pull()->dealloc(node);
        --geom_ring_.size;
    }
}

// 0x004934A0
bool RenderModel::add_asset_geom(uint32_t asset_name, uint32_t name, bool multicolor, bool default_heap)
{
    flag_bits_ &= ~kModelSphereValid;
    auto* scene = static_cast<PcMesh*>(assets::asset_manager()->get_asset_data(asset_name, 0));
    ModelGeom* entry = new_entry(default_heap);
    if (!default_heap && !entry)
        return false;
    entry->asset_name_ = asset_name;
    ++num_asset_geoms_;
    entry->scene = nullptr;
    entry->geom = create_geom();
    if (!default_heap && !entry->geom)
        return false; // (the node isn't freed)
    load_geom(entry->geom, scene, this, multicolor);
    entry->textures = scene->textures_;
    set_geom_shadow(entry->geom, (flag_bits_ >> 4) & 1);
    entry->name = name;
    visible_geoms_ |= geom_bit(geom_total_);
    if (lights_ptr_)
        set_geom_lights(entry->geom, lights_ptr_);
    ++geom_total_;
    if (display_mode_ != 0 && allows_display_mode(0))
        display_mode_ = 0;
    entry->bone_volumes_ = nullptr;
    insert_geom(this, geom_ring_.head, &entry);
    if (geom_total_ == 1)
        source_path_ = kFirstAssetGeom;
    return true;
}

// 0x004935E0
bool RenderModel::add_file_geom(const char* file_name, uint32_t name, bool via_resource_store, int tex_dict_offset,
                                bool force_tex_dict, bool multicolor, bool default_heap, bool by_name)
{
    flag_bits_ &= ~kModelSphereValid;
    assets::ResourceRegistry* assets = assets::asset_manager();
    ModelGeom* entry = new_entry(default_heap);
    if (!default_heap && !entry)
        return false;
    PcMesh* scene = nullptr;
    if (via_resource_store) {
        // The textures first ("<file without its .skin / .mdl>.tex"), then the file itself: the texture
        // dictionary's parameters (only these fields set - the rest as the stack left it in the original).
        struct TexDictParams {
            uint8_t flag0;
            std::array<uint8_t, 3> pad01;
            int tex_dict_offset_;
            uint8_t has_skinning_;
            uint8_t force_tex_dict_;
            std::array<uint8_t, 0x42> rest;
        } tex_params{};
        std::memset(&tex_params, 0, sizeof(tex_params));
        std::array<char, 0xF0> tex_name{};
        tex_params.flag0 = 0;
        tex_params.tex_dict_offset_ = tex_dict_offset;
        tex_params.force_tex_dict_ = force_tex_dict;
        tex_params.has_skinning_ = engine::split_skin_name(file_name, tex_name.data());
        append_string(tex_name.data(), ".tex");
        if (by_name)
            assets->get_asset_data_by_name(tex_name.data(), 1);
        else
            assets->fetch_or_load_resource(tex_name.data(), 0, 0, 0, false, false, assets->get_permanent_by_default(),
                                           &tex_params, 0);
        assets::SkinParams skin_params{};
        skin_params.get_clear();
        skin_params.flag0 = (flag_bits_ >> 5) & 1;
        skin_params.value = tex_dict_offset;
        if (by_name)
            scene = static_cast<PcMesh*>(assets->get_asset_data_by_name(file_name, 1));
        else
            scene = static_cast<PcMesh*>(
                assets->fetch_or_load_resource(file_name, 0, 0, checksums::hash_from_text(tex_name.data()), false, false,
                                               assets->get_permanent_by_default(), &skin_params, 0));
        entry->asset_name_ = checksums::hash_from_text(file_name);
        ++num_asset_geoms_;
        entry->scene = nullptr;
    } else {
        memory::push_mem_profile(file_name);
        TextureSet* textures = load_textures(file_name, tex_dict_offset, force_tex_dict);
        scene = load_scene(file_name, textures, tex_dict_offset, (flag_bits_ >> 5) & 1);
        memory::pop_mem_profile();
        entry->scene = scene;
        ++num_file_geoms_;
        entry->asset_name_ = 0;
    }
    entry->geom = create_geom();
    if (!default_heap && !entry->geom)
        return false; // (the node isn't freed)
    load_geom(entry->geom, scene, this, multicolor);
    entry->textures = scene->textures_;
    set_geom_shadow(entry->geom, (flag_bits_ >> 4) & 1);
    entry->name = name;
    visible_geoms_ |= geom_bit(geom_total_);
    if (lights_ptr_)
        set_geom_lights(entry->geom, lights_ptr_);
    ++geom_total_;
    if (display_mode_ != 0 && allows_display_mode(0))
        display_mode_ = 0;
    entry->bone_volumes_ = nullptr;
    insert_geom(this, geom_ring_.head, &entry);
    if (geom_total_ == 1)
        source_path_ = checksums::hash_from_text(file_name);
    return true;
}

// 0x004938E0
bool RenderModel::add_geom(void* geom, uint32_t name, bool default_heap)
{
    flag_bits_ &= ~kModelSphereValid;
    ModelGeom* entry = new_entry(default_heap);
    if (!default_heap && !entry)
        return false;
    entry->geom = geom;
    set_geom_shadow(geom, (flag_bits_ >> 4) & 1);
    entry->name = name;
    visible_geoms_ |= geom_bit(geom_total_);
    entry->textures = nullptr;
    if (lights_ptr_)
        set_geom_lights(entry->geom, lights_ptr_);
    entry->bone_volumes_ = nullptr;
    entry->scene = nullptr;
    entry->asset_name_ = 0;
    insert_geom(this, geom_ring_.head, &entry);
    ++geom_total_;
    if (display_mode_ != 0 && allows_display_mode(0))
        display_mode_ = 0;
    if (geom_total_ == 1)
        source_path_ = kFirstAddedGeom;
    return true;
}

// 0x00493090
bool RenderModel::render(maths::Mat4* matrix, bool no_bones, gfx::Rig* skeleton)
{
    const bool normal = display_mode_ == 0;
    int index = 0;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next, ++index) {
        const bool active = normal && (flag_bits_ & kModelEnabledBit) && (visible_geoms_ & geom_bit(index));
        set_geom_active(get_geom(node), active);
    }
    if (flag_bits_ & kModelScaled) {
        for (int row = 0; row < 3; ++row) {
            const float factor = (&size_factor_.x)[row];
            for (int i = 0; i < 4; ++i)
                (*matrix)[row][i] *= factor;
        }
    }
    switch (display_mode_) {
    case 0:
        (*matrix)[0][3] = 0.0f;
        (*matrix)[1][3] = 0.0f;
        (*matrix)[2][3] = 0.0f;
        (*matrix)[3][3] = 1.0f;
        if (!skeleton || no_bones) {
            for (ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next)
                set_geom_bone_matrices(get_geom(node), matrix, nullptr, 0, 0, 0);
        } else {
            const maths::Mat4* bones = skeleton_bones(skeleton);
            const int count = skeleton_bone_count(skeleton);
            const int flag = get_bone_flag(count);
            int geom = 0;
            for (ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next, ++geom)
                set_geom_bone_matrices(get_geom(node), matrix, bones, count, flag, geom);
        }
        break;
    case 1:
        if (skeleton)
            // (the original passes the floats 0.5, 0.5 and 1 as its colours and frame count: their bits)
            skeleton->blit(matrix, std::bit_cast<uint32_t>(0.5f), std::bit_cast<uint32_t>(0.5f),
                           std::bit_cast<int>(1.0f));
        break;
    case 2:
    case 3:
    case 4:
    case 6:
        break;
    default: {
        // A box about the position (compiled out).
        maths::Mat4 orientation = *matrix;
        const maths::Vec4 position = (*matrix)[3];
        orientation[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        std::array<maths::Vec4, 2> box = {{{10.0f, 10.0f, 10.0f, 1.0f}, {-10.0f, -10.0f, -10.0f, 1.0f}}};
        gfx::draw_debug_cube(&orientation, &position, box.data(), 0, 1, 0, 0xFF0000C8);
        break;
    }
    }
    return true;
}

// 0x004939E0
int RenderModel::test_bone_volumes(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                   uint32_t* out)
{
    int hit = 0;
    uint32_t hit_extra = 0;
    float nearest = 0.01f;
    uint32_t extra = e; // (the original's argument slot: each test's extra result lands there, kept between geoms)
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        gfx::BoneVolumes* volumes = table_row(node)->bone_volumes_;
        if (!volumes)
            continue;
        float distance = nearest;
        const int result = static_cast<int>(volumes->segment_hits_bone(
            cell::pointer<const maths::Seg>(a), cell::pointer<gfx::Rig>(b),
            cell::pointer<const maths::Mat4>(c), d, std::bit_cast<float>(e), std::bit_cast<float>(f), &distance,
            raw::as<float>(&extra)));
        if (!hit || distance < nearest) {
            nearest = distance;
            hit_extra = extra;
            hit = result;
        }
    }
    if (out)
        *out = hit_extra;
    return hit;
}

// 0x00493AB0
float RenderModel::get_bone_volume(uint32_t a, uint32_t b, uint32_t bone)
{
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        gfx::BoneVolumes* volumes = table_row(node)->bone_volumes_;
        if (!volumes)
            continue;
        float value = 0.0f;
        if (volumes->get_bone_volume(
                cell::pointer<gfx::Rig>(a), cell::pointer<const maths::Mat4>(b), bone, &value))
            return value;
    }
    return 0.0f;
}

// 0x00493D10
bool RenderModel::swap_texture(uint32_t name, const char* source, const char* dest)
{
    const bool all = name == kAllGeoms;
    bool replaced = false;
    begin_texture_swap();
    std::array<char, 0x200> from{};
    get_png_name(from.data(), source);
    std::array<char, 0x200> to{};
    copy_string(to.data(), dest);
    text::to_lower_text(to.data());
    if (char* extension = std::strstr(to.data(), ".png"))
        *extension = 0;
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        const ModelGeom* entry = table_row(node);
        if ((name == entry->name || all || entry->name == 0) && entry->textures &&
            (entry->textures)->replace_texture_from_file(from.data(), to.data()))
            replaced = true;
    }
    if (!replaced)
        script::fetch_int_value(kTextureReplaceDebug);
    end_texture_swap();
    return replaced;
}

// 0x00493E80
bool RenderModel::replace_texture_with(uint32_t name, const char* source, uint32_t dest)
{
    const bool all = name == kAllGeoms;
    bool replaced = false;
    begin_texture_swap();
    std::array<char, 0x200> from{};
    get_png_name(from.data(), source);
    void* texture = find_texture(dest);
    for (const ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        const ModelGeom* entry = table_row(node);
        if ((name == entry->name || all) && entry->textures &&
            (entry->textures)->swap_texture(from.data(), static_cast<BitmapTexture*>(texture)))
            replaced = true;
    }
    if (!replaced)
        script::fetch_int_value(kTextureReplaceDebug);
    end_texture_swap();
    return replaced;
}

// 0x00494010
bool RenderModel::set_geom_color_hsv(uint32_t name, float h, float s, float v)
{
    const bool all = name == kAllGeoms;
    bool any = false;
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        if ((name == table_row(node)->name || all) && !is_geom_multicolor(get_geom(node))) {
            set_geom_color(get_geom(node), rgba_from_hsv(h, s, v));
            any = true;
        }
    }
    return any;
}

// 0x00494200
bool RenderModel::set_color_from_script(script::Struct* params, float h, float s, float v)
{
    bool any = false;
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    const uint32_t rgba = rgba_from_hsv(h, s, v);
    const bool all = name == kAllGeoms;
    uint32_t material = 0; // (uninitialised in the original; kept from one geom and pass to the next)
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        void* geom = get_geom(node);
        int pass = 0;
        if (!is_geom_multicolor(geom)) {
            if (name == table_row(node)->name || all) {
                set_geom_color(geom, rgba);
                any = true;
            }
            continue;
        }
        // A multicolour geom: its material passes.
        script::Array* materials = nullptr;
        if (params->fetch_list(kMaterials, &materials, 0)) {
            for (uint32_t i = 0; i < materials->extent_; ++i) {
                auto* entry = materials->struct_at(i);
                entry->read_hash_value(0u, &material, 1);
                entry->fetch_int_value(0u, &pass, 1);
                any |= set_geom_material_color(geom, material, pass, rgba);
            }
        } else if (params->read_hash_value(kMaterial, &material, 0)) {
            script::Array* passes = nullptr;
            if (params->fetch_list(kPass, &passes, 0)) {
                for (uint32_t i = 0; i < passes->extent_; ++i)
                    any |= set_geom_material_color(geom, material, static_cast<int>(passes->fetch_part(i)), rgba);
            } else if (params->fetch_int_value(kPass, &pass, 1)) {
                any |= set_geom_material_color(geom, material, pass, rgba);
            }
        }
    }
    return any;
}

// 0x00494450
bool RenderModel::clear_color_from_script(script::Struct* params)
{
    constexpr uint32_t kGray = 0x80808080;
    bool any = false;
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    const bool all = name == kAllGeoms;
    uint32_t material = 0; // (as set_color_from_script)
    for (ModelGeomNode* node = get_first(this); node != geom_ring_.head; node = node->next) {
        void* geom = get_geom(node);
        int pass = 0;
        if (!is_geom_multicolor(geom)) {
            if (name == table_row(node)->name || all) {
                model_geoms::clear_geom_color(geom);
                any = true;
            }
            continue;
        }
        script::Array* materials = nullptr;
        if (params->fetch_list(kMaterials, &materials, 0)) {
            for (uint32_t i = 0; i < materials->extent_; ++i) {
                auto* entry = materials->struct_at(i);
                entry->read_hash_value(0u, &material, 1);
                entry->fetch_int_value(0u, &pass, 1);
                any |= set_geom_material_color(geom, material, pass, kGray);
            }
        } else if (params->read_hash_value(kMaterial, &material, 0)) {
            script::Array* passes = nullptr;
            if (params->fetch_list(kPass, &passes, 0)) {
                for (uint32_t i = 0; i < passes->extent_; ++i)
                    any |= set_geom_material_color(geom, material, static_cast<int>(passes->fetch_part(i)), kGray);
            } else if (params->fetch_int_value(kPass, &pass, 1)) {
                any |= set_geom_material_color(geom, material, pass, kGray);
            }
        }
    }
    return any;
}

// 0x00494A00
bool RenderModel::replace_face_texture(void* face, const char* source, uint32_t name)
{
    // The face texture: its image's size (0x00487540) and data (0x00487530), its overlay's file name (0x004875B0),
    // its colour parameters (0x00487550, into a buffer).
    auto* face_texture = static_cast<gfx::FaceSkin*>(face);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    const int size = face_texture->fetch_skin_resolution();
    const void* data = face_texture->get_image();
    void* image = static_cast<TextureSet*>(get_textures())
                      ->read_texture_buffer(static_cast<const uint8_t*>(data), size, kFaceImage, 1, 1, false, 1);
    void* overlay = static_cast<TextureSet*>(get_textures())->ingest_skin(face_texture->get_overlay_name(), 1, 1, true);
    memory::Hub::get_pull()->leave_heap_scope();
    // The profile's skin colour (0x004869D0 reads it, 0x004869A0 makes it current) and the overlay (0x004869C0)
    // applied to the image (0x00486F70) with the face's colour parameters.
    std::array<uint8_t, 0x30> color{};
    gfx::read_face_points(raw::as<gfx::FaceAnchors>(color.data()), script::read_sub_struct(kSkinColor));
    gfx::change_face_anchors(raw::as<const gfx::FaceAnchors>(color.data()));
    gfx::set_face_overlay(static_cast<BitmapTexture*>(overlay));
    std::array<uint8_t, 0x38> parameters{};
    auto get_parameters = [face_texture](uint8_t* out) {
        return raw::bytes(face_texture->get_points(raw::as<gfx::FaceAnchors>(out)));
    };
    const uint8_t generate_palette = get_parameters(parameters.data())[0x20]; // (the byte as it is - not made 0 / 1)
    gfx::massage_face_texture(static_cast<BitmapTexture*>(image),
                              raw::as<const gfx::FaceAnchors>(get_parameters(parameters.data())), generate_palette,
                              false, 0x80808080);
    const bool replaced = replace_texture_with(name, source, texture_hash(image));
    detach_texture(image);
    detach_texture(overlay);
    return replaced;
}

} // namespace render
