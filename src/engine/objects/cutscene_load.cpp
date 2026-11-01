// Cutscenes: the data's constructor and the objects a cutscene adds (0x004645A0-0x00465502). See cutscene.h.

#include <cstdint>
#include <cstring>

#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/rig.h"
#include "engine/maths/vec4.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"
#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/net/buddy_status.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "engine/gfx/scene_load.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/skeleton_component.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

namespace objects {

namespace {

constexpr uint32_t kUnnamed = 0; // (a flag: a checksum without a name)
constexpr uint32_t kModelModuleName = 0x286A8D26;
constexpr uint32_t kSkeletonModuleName = 0x222756D5;
constexpr uint32_t kFlagParam = 0x82C84419;
constexpr uint32_t kRefreshScript = 0x2E20EA32; // (create_object's second script)
constexpr uint32_t kRefreshGlobal = 0x4AF50A5B; // a global integer: refresh the objects once

// The objects' parameters (names unknown).
constexpr uint32_t kVersion = 0x40E32C3C;
constexpr uint32_t kObjects = 0x4DE5330C;
constexpr uint32_t kName = 0x2BFC2486;
constexpr uint32_t kModel = 0xB974F2FC;
constexpr uint32_t kKind = 0x7321A8D6;
constexpr uint32_t kSkeleton = 0x09794932;
constexpr uint32_t kAnim = 0x6C2BFB7F;
constexpr uint32_t kIsSkaterMarker = 0x723CDA8F;
constexpr uint32_t kSkaterFlag2 = 0x6914D9D2;
constexpr uint32_t kKeepSkeleton = 0x0CBEC8AC;
constexpr uint32_t kNoModelFile = 0x5B5D592E;
constexpr uint32_t kModelFile = 0x86BD5B8F;
constexpr uint32_t kMale = 0x3F813177;
constexpr uint32_t kMaleParam = 0x92B43E79;
constexpr uint32_t kPlayingCutsceneKey = 0x153A84DE;
constexpr uint32_t kSkeletonParam = 0xD3982061;
constexpr uint32_t kSkaterScript = 0xF37D9F53;
// (the model asset's name: the model checksum plus this)
constexpr uint32_t kModelFileOffset = 0xFD8697E1;

// The skater appearance parts asked about (names unknown).
constexpr uint32_t kPartA = 0x650FAB6D, kPartB = 0x0FC85BAE, kPartC = 0x6DF453B6, kHidden = 0xC4C5B2CC;

memory::PoolPair& struct_pools() { return script::g_struct_pools; }
memory::PoolPair& source_pools() { return render::g_frame_source_pools; }

script::Struct* make_struct()
{
    void* memory = struct_pools().carve(8);
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
void free_struct(script::Struct* s)
{
    if (!s)
        return;
    s->get_clear();
    script::delete_struct(s);
}

void* model_of(ObjectModule* component) { return static_cast<objects::MeshInstanceModule*>(component)->mesh_instance_; }
bool stands_in(ObjectModule* model) { return static_cast<MeshInstanceModule*>(model)->level_object_; }
uint32_t stand_in_id(ObjectModule* model) { return static_cast<MeshInstanceModule*>(model)->get_linked_entity_name(); }
} // namespace

// (no address: the getters folded at 0x00576E60 / 0x00532EB0 - cutscene.h)
void* model_fader(void* model) { return static_cast<render::RenderModel*>(model)->lights_ptr_; }
void* appearance_face(void* appearance) { return static_cast<gfx::ModelLook*>(appearance)->face_skin_; }

namespace {

void* skeleton_of(ObjectModule* component)
{
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    return static_cast<objects::BoneRigModule*>(component)->bone_rig_;
}
// The level object a cutscene object stands in for.
GameEntity* level_object_of(GameEntity* object)
{
    return static_cast<GameEntity*>(find_object(stand_in_id(object->fetch_module(kModelModuleName))));
}
void model_set_offset(void* model, const float* offset)
{
    static_cast<render::RenderModel*>(model)->change_bound_sphere(raw::as<const maths::Vec4>(offset));
}
// The skater's appearance (+0x34A8) and its parts.
void* appearance_part(void* appearance, uint32_t part)
{
    return static_cast<gfx::ModelLook*>(appearance)->get_part_option(part);
}

} // namespace

// 0x004645A0
void* anim_source_allocate(unsigned int size) { return source_pools().carve(size); }

// 0x004645E0
void anim_source_free(void* item) { source_pools().dealloc(item); }

// 0x00464640
CinematicTable::CinematicTable() : bundle_ptr_(nullptr), call_args_(make_struct()) {}

// 0x00464780
void CinematicTable::refresh_objects()
{
    if (!flag_34b_0_ || script::fetch_int_value(kRefreshGlobal) == 0)
        return;
    script::Struct* params = make_struct();
    params->put_name_key(kUnnamed, kFlagParam);
    for (int i = 0; i < object_count_; ++i) {
        GameEntity* object = entities_[i].object;
        if (object->fetch_module(kModelModuleName))
            script::launch_script(kRefreshScript, params, object, false, nullptr);
    }
    auto* skater = static_cast<GameEntity*>(modules::Skate::sole()->get_home_rider());
    if (skater)
        script::launch_script(kRefreshScript, params, skater, false, nullptr);
    free_struct(params);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
    script::find_symbol(kRefreshGlobal)->integer = 0;
}

// 0x004648D0
void CinematicTable::add_objects(script::Struct* params)
{
    int version = 0;
    params->fetch_int_value(kVersion, &version, 1);
    script::Array* objects = nullptr;
    params->fetch_list(kObjects, &objects, 1);
    bool wants_skater = false; // (an object of kind 0x6218C3C9: script 0xF37D9F53 runs...)
    bool has_skater = false;   // (...unless a skater object was added)
    for (uint32_t i = 0; i < objects->extent_; ++i) {
        auto* object = objects->struct_at(i);
        script::Struct* params2 = make_struct();
        uint32_t name = 0, kind = 0, model = 0, skeleton = 0, anim = 0;
        bool is_skater = false, is_skater2 = false, special = false;
        uint32_t model_file = 0;
        if (version >= 2) {
            object->read_hash_value(kName, &name, 1);
            object->read_hash_value(kKind, &kind, 1);
            object->read_hash_value(kSkeleton, &skeleton, 0);
            model = name;
            model_file = name + kModelFileOffset;
            anim = name;
            is_skater = object->has_bare_flag(kIsSkaterMarker);
            if (is_skater) {
                is_skater2 = kind == 0x01815FB3;
                special = false;
                if (is_skater2)
                    skeleton = 0x9954F90F;
            } else {
                is_skater2 = kind == 0x6218C3C9;
                special = is_skater2;
                if (is_skater2) {
                    wants_skater = true;
                    skeleton = 0x9FE476E2;
                }
            }
        } else {
            object->read_hash_value(kName, &name, 1);
            object->read_hash_value(kModel, &model, 1);
            model_file = model + kModelFileOffset;
            object->read_hash_value(kSkeleton, &skeleton, 1);
            object->read_hash_value(kAnim, &anim, 1);
            is_skater = object->has_bare_flag(kIsSkaterMarker);
            is_skater2 = object->has_bare_flag(kSkaterFlag2);
            special = model == 0x5B8AB877 || model == 0x67E6859A;
        }
        if (is_skater2 && is_skater)
            has_skater = true;
        const bool skater = is_skater && is_skater2;
        bool add = true;
        if (!special) {
            const void* asset = assets::g_asset_manager->get_asset_data(model_file, 0);
            if (!asset && !skater)
                params2->put_name_key(kUnnamed, kNoModelFile);
            if (object->has_bare_flag(kKeepSkeleton)) {
                params2->put_name_key(kUnnamed, kKeepSkeleton);
            } else {
                skeleton = 0;
                anim = 0;
            }
            if (skater) {
                auto* face = static_cast<gfx::FaceSkin*>(appearance_face(unknown34a8_));
                if (face && face->is_sound())
                    ++model_file;
                int male = 0;
                static_cast<script::Struct*>(unknown34ac_)->fetch_int_value(kMale, &male, 1);
                if (male == 0)
                    model_file += 2;
                params2->put_int(kMaleParam, male);
            }
            params2->put_name_key(kModelFile, model_file);
        } else if (!flag_34b_0_) {
            add = false;
        } else {
            params2->put_name_key(kPlayingCutsceneKey, 0);
        }
        if (add && skater) {
            // the skater's object isn't made when a part of its appearance is hidden
            void* part = appearance_part(unknown34a8_, kPartA);
            if (!part)
                part = appearance_part(unknown34a8_, kPartB);
            bool hidden = part && static_cast<script::Struct*>(part)->has_bare_flag(kHidden);
            if (!hidden) {
                part = appearance_part(unknown34a8_, kPartC);
                hidden = part && static_cast<script::Struct*>(part)->has_bare_flag(kHidden);
            }
            if (hidden) {
                has_skater = false;
                add = false;
            }
        }
        if (add) {
            if (skeleton) {
                params2->put_name_key(kSkeleton, skeleton);
                params2->put_int(kSkeletonParam, 0);
            }
            ObjectEntry& entry = entities_[object_count_];
            entry.object = create_object(params2);
            ObjectModule* stand_in = entry.object->fetch_module(kModelModuleName);
            if (stand_in) {
                float offset[4] = {0.0f, 0.0f, 0.0f, 150.0f};
                model_set_offset(model_of(stand_in), offset);
                if (stands_in(stand_in)) {
                    // the level object's model lends its settings (saved once) and is hidden
                    auto* level = static_cast<GameEntity*>(find_object(stand_in_id(stand_in)));
                    ObjectModule* level_model = level->fetch_module(kModelModuleName);
                    void* model2 = model_of(level_model);
                    if (!lent_saved_) {
                        float saved[4];
                        const auto* settings = raw::as<const float>(static_cast<render::RenderModel*>(model2)->fetch_bound_sphere(
                                raw::as<maths::Vec4>(saved)));
                        std::memcpy(lent_2c_, settings, 16);
                        const uint8_t flags = static_cast<render::RenderModel*>(model2)->flag_bits_;
                        lent_3c_ = flags >> 2 & 1;
                        lent_3d_ = flags >> 3 & 1;
                        lent_pos_ = level->at_point_;
                        lent_saved_ = true;
                    }
                    float offset2[4] = {0.0f, 0.0f, 0.0f, 150.0f};
                    model_set_offset(model2, offset2);
                    static_cast<render::RenderModel*>(model2)->conceal(0);
                    static_cast<render::RenderModel*>(model2)->allow_resizing(0);
                    auto mine =
                        cell::of(static_cast<render::RenderModel*>(model2)->fetch_skinning_palette());
                    auto theirs = cell::of(static_cast<render::RenderModel*>(model_of(stand_in))->fetch_skinning_palette());
                    static_cast<render::RenderModel*>(model2)->upload_skinning_palette(
                        cell::pointer<uint8_t>(theirs));
                    static_cast<render::RenderModel*>(model_of(stand_in))
                        ->upload_skinning_palette(cell::pointer<uint8_t>(mine));
                    static_cast<MeshInstanceModule*>(level_model)->get_update_brightness();
                }
                stand_in->hold_off(true);
            }
            entry.name = name;
            entry.object->change_serial(name);
            entry.skeleton = skeleton;
            entry.active = special;
            entry.animation = nullptr;
            entry.parent = nullptr;
            entry.anim_index_ = -1;
            if (anim) {
                for (int j = 0; j < animation_count_; ++j) {
                    if (animations_[j].name != anim)
                        continue;
                    AnimationEntry* animation = &animations_[j];
                    entry.animation = animation;
                    animation->source = new render::FrameSource;
                    static_cast<render::FrameSource*>(animation->source)->play_clip(anim);
                    static_cast<render::FrameSource*>(animation->source)->set_done(true);
                    break;
                }
            }
            ++object_count_;
        }
        free_struct(params2);
    }
    if (objects->extent_ != 0 && wants_skater && !has_skater)
        script::launch_script(kSkaterScript, nullptr, nullptr, false, nullptr);

    // the objects that stand in for level objects (and their children) take those's skeletons
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = entities_[i];
        if (entry.active)
            copy_skeleton(entry, entry);
        if (ObjectEntry* parent = find_entry(entry.name - 1)) {
            if (parent->active)
                copy_skeleton(*parent, entry);
        }
    }
    // their models' flags 0x00493B30 merged with their parents' level objects'
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = entities_[i];
        ObjectModule* component = entry.object->fetch_module(kModelModuleName);
        if (!component)
            continue;
        void* model = model_of(component);
        uint32_t flags = static_cast<render::RenderModel*>(model)->get_strip_break_flags();
        const ObjectEntry* parent = find_entry(entry.name - 1);
        if (parent && parent->active) {
            GameEntity* level = level_object_of(parent->object);
            flags |= static_cast<render::RenderModel*>(model_of(level->fetch_module(kModelModuleName)))
                         ->get_strip_break_flags();
        }
        static_cast<render::RenderModel*>(model)->break_strips(flags);
    }
    // children: their parent, the skater's look (colours from its appearance parts), their animation index
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = entities_[i];
        const ObjectEntry* parent = find_entry(entry.name - 1);
        if (parent) {
            entry.parent = parent->object;
            if (parent->active)
                apply_skater_look(entry);
        }
        void* anims = anim_list_;
        for (int j = 0; j < *raw::as<int>(static_cast<uint8_t*>(anims) + 8); ++j) {
            if (entry.name == static_cast<gfx::JointClipFrames*>(anim_list_)->fetch_joint_name(j)) {
                entry.anim_index_ = j;
                break;
            }
        }
    }
}

// The entry with this name (the first).
CinematicTable::ObjectEntry* CinematicTable::find_entry(uint32_t name)
{
    for (int i = 0; i < object_count_; ++i) {
        if (entities_[i].name == name)
            return &entities_[i];
    }
    return nullptr;
}

// `to`'s object takes the skeleton of the level object `from`'s object stands in for (when it has a skeleton and
// stands in itself).
void CinematicTable::copy_skeleton(ObjectEntry& from, ObjectEntry& to)
{
    void* skeleton = skeleton_of(level_object_of(from.object)->fetch_module(kSkeletonModuleName));
    GameEntity* object = to.object;
    ObjectModule* own = object->fetch_module(kSkeletonModuleName);
    if (own && stands_in(object->fetch_module(kModelModuleName)))
        static_cast<gfx::Rig*>(skeleton_of(own))->copy_bone_scales(static_cast<gfx::Rig*>(skeleton), false);
}

// A child of an object that stands in: the skater's look from its appearance (a colour from part A or B, or
// from 0x949BC47B's alternatives when 0x97DBDDE6 is set).
void CinematicTable::apply_skater_look(ObjectEntry& entry)
{
    ObjectModule* component = entry.object->fetch_module(kModelModuleName);
    static_cast<MeshInstanceModule*>(component)->set_up_mesh_from_profile(unknown34a8_, false, 0, 0x8FB01036);
    uint32_t part_name = kPartA;
    auto* part = static_cast<script::Struct*>(static_cast<gfx::ModelLook*>(unknown34a8_)->get_part(kPartA));
    if (!part) {
        part_name = kPartB;
        part = static_cast<script::Struct*>(static_cast<gfx::ModelLook*>(unknown34a8_)->get_part(kPartB));
        if (!part)
            return;
    }
    void* model = model_of(component);
    int use_other = 1;
    uint32_t hue_name = 0x6E94F918, saturation_name = 0xE4F130F4, value_name = 0x949BC47B;
    part->fetch_int_value(0x97DBDDE6, &use_other, 0);
    if (use_other) {
        part = static_cast<script::Struct*>(appearance_part(unknown34a8_, part_name));
        hue_name = 0xC6CC7F7F;
        saturation_name = 0x4CA9B693;
        value_name = 0x3CC3421C;
    }
    int hue = 0, saturation = 0, value = 0;
    if (!part->fetch_int_value(hue_name, &hue, 0) || !part->fetch_int_value(saturation_name, &saturation, 0) ||
        !part->fetch_int_value(value_name, &value, 0))
        return;
    script::Struct* colour = make_struct();
    colour->put_name_key(0xB6F08F39, 0);
    colour->put_name_key(0x83418A6A, 0xAA2206EF);
    colour->put_int(0x318F2BDB, 0);
    static_cast<render::RenderModel*>(model)->set_color_from_script(
        colour, static_cast<float>(hue), static_cast<float>(saturation * static_cast<double>(0.0099999998f)),
        static_cast<float>(value * static_cast<double>(0.0099999998f)));
    free_struct(colour);
}

} // namespace objects
