// Cutscenes: the data's constructor and the objects a cutscene adds (0x004645A0-0x00465502). See cutscene.h.

#include <cstddef>
#include <cstdint>
#include "engine/gfx/boned_anim_frame_data.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/rig.h"
#include "base/maths/vec4.h"
#include "engine/objects/game_hooks.h"
#include "engine/assets/resource_registry.h"
#include "engine/memory/mem.h"
#include "engine/memory/slab_bank.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/scene_load.h"
#include "engine/objects/skeleton_component.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include <gsl/pointers>

namespace objects {

namespace {
constexpr std::size_t kCarveBytes = 8;
constexpr float kPercentToFraction = 0.0099999998F;
constexpr uint32_t kSecondSkaterKind = 0x01815FB3; // text unknown (the object kind of a second skater, when the object is marked as a skater)
constexpr uint32_t kHeadCasMale01 = 0x9954F90F; // Head_Cas_Male01
constexpr uint32_t kPlayerObject = 0x6218C3C9; // PlayerObject
constexpr uint32_t kTHPS6Human = 0x9FE476E2; // THPS6_Human
constexpr uint32_t kSkater = 0x5B8AB877; // Skater
constexpr uint32_t kPlayer = 0x67E6859A; // Player
constexpr uint32_t kReplaceCutsceneSkaterFromAppearance = 0x8FB01036; // replace_cutscene_skater_from_appearance
constexpr uint32_t kH = 0x6E94F918; // h
constexpr uint32_t kS = 0xE4F130F4; // s
constexpr uint32_t kV = 0x949BC47B; // v
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6; // use_default_hsv
constexpr uint32_t kDefaultH = 0xC6CC7F7F; // default_h
constexpr uint32_t kDefaultS = 0x4CA9B693; // default_s
constexpr uint32_t kDefaultV = 0x3CC3421C; // default_v
constexpr uint32_t kPart = 0xB6F08F39; // Part
constexpr uint32_t kMaterial = 0x83418A6A; // Material
constexpr uint32_t kCasheadHead = 0xAA2206EF; // cashead_head
constexpr uint32_t kPass = 0x318F2BDB; // Pass

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


gsl::owner<script::Struct*> make_struct()
{
    void* memory = script::struct_pools().carve(kCarveBytes);
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}

void* model_of(ObjectModule* component) { return core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance().get(); }
bool stands_in(ObjectModule* model) { return core::down_cast<MeshInstanceModule>(model)->level_object(); }
uint32_t stand_in_id(ObjectModule* model) { return core::down_cast<MeshInstanceModule>(model)->get_linked_entity_name(); }
} // namespace

// (no address: the getters folded at 0x00576E60 / 0x00532EB0 - cutscene.h)
void* model_fader(void* model) { return static_cast<render::RenderModel*>(model)->lights_ptr().get(); }
gfx::FaceSkin* appearance_face(gfx::ModelLook* appearance) { return appearance->face_skin(); }

namespace {

void* skeleton_of(ObjectModule* component)
{
    // (the skeleton component's +0x18, through the getter folded at 0x005480F0)
    return core::down_cast<objects::BoneRigModule>(component)->bone_rig();
}
// The level object a cutscene object stands in for.
GameEntity* level_object_of(GameEntity* object)
{
    return core::down_cast<GameEntity>(find_object(stand_in_id(object->fetch_module(kModelModuleName))));
}
void model_set_offset(void* model, const maths::Vec4* sphere)
{
    static_cast<render::RenderModel*>(model)->change_bound_sphere(sphere);
}
// The skater's appearance (+0x34A8) and its parts.
void* appearance_part(gfx::ModelLook* appearance, uint32_t part) { return appearance->get_part_option(part); }

} // namespace

// 0x004645A0
void* anim_source_allocate(unsigned int size) { return render::frame_source_pools().carve(size); }

// 0x004645E0
void anim_source_free(void* item) { render::frame_source_pools().dealloc(item); }

// 0x00464640
CinematicTable::CinematicTable() : bundle_ptr_(nullptr), call_args_(make_struct()) {}

// 0x00464780
void CinematicTable::refresh_objects()
{
    if ((flag_34b_0_ == 0U) || script::fetch_int_value(kRefreshGlobal) == 0) {
        return;
    }
    gsl::owner<script::Struct*> params = make_struct();
    params->put_name_key(checksums::Name{kUnnamed}, kFlagParam);
    for (int i = 0; i < object_count_; ++i) {
        GameEntity* object = core::at(entities_, i).object;
        if (object->fetch_module(kModelModuleName)) {
            script::launch_script(kRefreshScript, params, object, false, nullptr);
        }
    }
    auto* skater = game_hooks().home_rider();
    if (skater) {
        script::launch_script(kRefreshScript, params, skater, false, nullptr);
    }
    free_struct(params);
    script::find_symbol(kRefreshGlobal)->value().set_integer(0);
}

namespace {

// An object of the cutscene's array: its name, kind, model, skeleton, animation, the skater marks, the model
// file (version 2: the name's; a player object wants the skater script run).
void read_cutscene_object(script::Struct* object, int version, CinematicTable::CutsceneObject& item, bool& wants_skater)
{
    uint32_t& name = item.name;
    uint32_t& kind = item.kind;
    uint32_t& model = item.model;
    uint32_t& skeleton = item.skeleton;
    uint32_t& anim = item.anim;
    bool& is_skater = item.is_skater;
    bool& is_skater2 = item.is_skater2;
    bool& special = item.special;
    uint32_t& model_file = item.model_file;
    if (version >= 2) {
        object->read_hash_value(kName, &name, 1);
        object->read_hash_value(kKind, &kind, 1);
        object->read_hash_value(kSkeleton, &skeleton, 0);
        model = name;
        model_file = name + kModelFileOffset;
        anim = name;
        is_skater = object->has_bare_flag(kIsSkaterMarker);
        if (is_skater) {
            is_skater2 = kind == kSecondSkaterKind;
            special = false;
            if (is_skater2) {
                skeleton = kHeadCasMale01;
            }
        } else {
            is_skater2 = kind == kPlayerObject;
            special = is_skater2;
            if (is_skater2) {
                wants_skater = true;
                skeleton = kTHPS6Human;
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
        special = model == kSkater || model == kPlayer;
    }
}

} // namespace

// 0x004648D0
void CinematicTable::add_objects(script::Struct* params)
{
    int version = 0;
    params->fetch_int_value(kVersion, &version, 1);
    script::Array* objects = nullptr;
    params->fetch_list(kObjects, &objects, 1);
    bool wants_skater = false; // (an object of kind 0x6218C3C9: script 0xF37D9F53 runs...)
    bool has_skater = false;   // (...unless a skater object was added)
    for (uint32_t i = 0; i < objects->extent(); ++i) {
        script::Struct* object = objects->struct_at(i);
        gsl::owner<script::Struct*> params2 = make_struct();
        CutsceneObject item;
        read_cutscene_object(object, version, item, wants_skater);
        if (item.is_skater2 && item.is_skater) {
            has_skater = true;
        }
        const bool skater = item.is_skater && item.is_skater2;
        bool add = object_params(object, item, params2);
        if (add && skater) {
            if (skater_part_hidden()) {
                has_skater = false;
                add = false;
            }
        }
        if (add) {
            make_cutscene_object(item, params2);
        }
        free_struct(params2);
    }
    if (objects->extent() != 0 && wants_skater && !has_skater) {
        script::launch_script(kSkaterScript, nullptr, nullptr, false, nullptr);
    }

    link_cutscene_objects();
}

// The object's parameters: its model file (the skater's by its look and profile), the skeleton kept or not; a
// stand-in only while playing a cutscene. Whether it is made.
bool CinematicTable::object_params(script::Struct* object, CutsceneObject& item, script::Struct* params2)
{
    const bool skater = item.is_skater && item.is_skater2;
    bool add = true;
    uint32_t& skeleton = item.skeleton;
    uint32_t& anim = item.anim;
    const bool& special = item.special;
    uint32_t& model_file = item.model_file;
    if (!special) {
        const void* asset = assets::asset_manager()->get_asset_data(model_file, 0);
        if (!asset && !skater) {
            params2->put_name_key(checksums::Name{kUnnamed}, kNoModelFile);
        }
        if (object->has_bare_flag(kKeepSkeleton)) {
            params2->put_name_key(checksums::Name{kUnnamed}, kKeepSkeleton);
        } else {
            skeleton = 0;
            anim = 0;
        }
        if (skater) {
            const gfx::FaceSkin* face = appearance_face(skater_appearance_);
            if (face && face->is_sound()) {
                ++model_file;
            }
            int male = 0;
            skater_details_->fetch_int_value(kMale, &male, 1);
            if (male == 0) {
                model_file += 2;
            }
            params2->put_int(checksums::Name{kMaleParam}, male);
        }
        params2->put_name_key(checksums::Name{kModelFile}, model_file);
    } else if (flag_34b_0_ == 0U) {
        add = false;
    } else {
        params2->put_name_key(checksums::Name{kPlayingCutsceneKey}, 0);
    }
    return add;
}

// Whether a part of the skater's appearance is hidden (its object isn't made then).
bool CinematicTable::skater_part_hidden() const
{
    // the skater's object isn't made when a part of its appearance is hidden
    void* part = appearance_part(skater_appearance_, kPartA);
    if (!part) {
        part = appearance_part(skater_appearance_, kPartB);
    }
    bool hidden = (part != nullptr) && static_cast<script::Struct*>(part)->has_bare_flag(kHidden);
    if (!hidden) {
        part = appearance_part(skater_appearance_, kPartC);
        hidden = (part != nullptr) && static_cast<script::Struct*>(part)->has_bare_flag(kHidden);
    }
    return hidden;
}

// The level object a stand-in replaces: its model lends its settings (saved once), swaps skinning palettes with the
// stand-in's, and is hidden.
void CinematicTable::lend_level_model(ObjectModule* stand_in)
{
    // the level object's model lends its settings (saved once) and is hidden
    auto* level = core::down_cast<GameEntity>(find_object(stand_in_id(stand_in)));
    ObjectModule* level_model = level->fetch_module(kModelModuleName);
    void* model2 = model_of(level_model);
    if (!lent_saved_) {
        maths::Vec4 saved{};
        lent_sphere_ = *static_cast<render::RenderModel*>(model2)->fetch_bound_sphere(&saved);
        const uint8_t flags = static_cast<render::RenderModel*>(model2)->flag_bits();
        lent_3c_ = flags >> 2 & 1;
        lent_3d_ = flags >> 3 & 1;
        lent_pos_ = level->at_point();
        lent_saved_ = true;
    }
    const maths::Vec4 offset2 = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 150.0F};
    model_set_offset(model2, &offset2);
    static_cast<render::RenderModel*>(model2)->conceal(0);
    static_cast<render::RenderModel*>(model2)->allow_resizing(0);
    maths::Mat4* mine = static_cast<render::RenderModel*>(model2)->fetch_skinning_palette();
    maths::Mat4* theirs = static_cast<render::RenderModel*>(model_of(stand_in))->fetch_skinning_palette();
    static_cast<render::RenderModel*>(model2)->upload_skinning_palette(theirs);
    static_cast<render::RenderModel*>(model_of(stand_in))->upload_skinning_palette(mine);
    core::down_cast<MeshInstanceModule>(level_model)->get_update_brightness();
}

// The animation the object plays (a frame source of the table's made for it).
void CinematicTable::attach_animation(ObjectEntry& entry, uint32_t anim)
{
    for (int j = 0; j < animation_count_; ++j) {
        if (core::at(animations_, j).name != anim) {
            continue;
        }
        AnimationEntry* animation = &core::at(animations_, j);
        entry.animation = animation;
        gsl::owner<render::FrameSource*> frames{memory::make_owned<render::FrameSource>()}; // (the table's)
        animation->source = frames;
        animation->source->play_clip(anim);
        animation->source->set_done(true);
        break;
    }
}

// The object made from its parameters: its stand-in model, its name, skeleton and animation.
void CinematicTable::make_cutscene_object(const CutsceneObject& item, script::Struct* params2)
{
    const uint32_t name = item.name;
    const uint32_t skeleton = item.skeleton;
    const uint32_t anim = item.anim;
    const bool special = item.special;
    if (skeleton != 0U) {
        params2->put_name_key(checksums::Name{kSkeleton}, skeleton);
        params2->put_int(checksums::Name{kSkeletonParam}, 0);
    }
    ObjectEntry& entry = core::at(entities_, object_count_);
    gsl::owner<GameEntity*> made = create_object(params2); // (the entry's: the cutscene's end deletes it)
    entry.object = made;
    ObjectModule* stand_in = entry.object->fetch_module(kModelModuleName);
    if (stand_in) {
        const maths::Vec4 offset = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 150.0F};
        model_set_offset(model_of(stand_in), &offset);
        if (stands_in(stand_in)) {
            lend_level_model(stand_in);
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
    if (anim != 0U) {
        attach_animation(entry, anim);
    }
    ++object_count_;
}

// The objects that stand in for level objects take those's skeletons, their models' strip flags, their parents
// and the skater's look; their animation index.
void CinematicTable::link_cutscene_objects()
{
    // the objects that stand in for level objects (and their children) take those's skeletons
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = core::at(entities_, i);
        if (entry.active) {
            copy_skeleton(entry, entry);
        }
        if (ObjectEntry* parent = find_entry(entry.name - 1)) {
            if (parent->active) {
                copy_skeleton(*parent, entry);
            }
        }
    }
    // their models' flags 0x00493B30 merged with their parents' level objects'
    for (int i = 0; i < object_count_; ++i) {
        ObjectEntry& entry = core::at(entities_, i);
        ObjectModule* component = entry.object->fetch_module(kModelModuleName);
        if (!component) {
            continue;
        }
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
        ObjectEntry& entry = core::at(entities_, i);
        const ObjectEntry* parent = find_entry(entry.name - 1);
        if (parent) {
            entry.parent = parent->object;
            if (parent->active) {
                apply_skater_look(entry);
            }
        }
        for (int j = 0; j < anim_list_->bone_count(); ++j) {
            if (entry.name == anim_list_->fetch_joint_name(j)) {
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
        if (core::at(entities_, i).name == name) {
            return &core::at(entities_, i);
        }
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
    if (own && stands_in(object->fetch_module(kModelModuleName))) {
        static_cast<gfx::Rig*>(skeleton_of(own))->copy_bone_scales(static_cast<gfx::Rig*>(skeleton), false);
    }
}

// A child of an object that stands in: the skater's look from its appearance (a colour from part A or B, or
// from 0x949BC47B's alternatives when 0x97DBDDE6 is set).
void CinematicTable::apply_skater_look(ObjectEntry& entry)
{
    ObjectModule* component = entry.object->fetch_module(kModelModuleName);
    core::down_cast<MeshInstanceModule>(component)->set_up_mesh_from_profile(skater_appearance_, false, 0, checksums::Name{kReplaceCutsceneSkaterFromAppearance});
    uint32_t part_name = kPartA;
    auto* part = static_cast<script::Struct*>(skater_appearance_->get_part(kPartA));
    if (!part) {
        part_name = kPartB;
        part = static_cast<script::Struct*>(skater_appearance_->get_part(kPartB));
        if (!part) {
            return;
        }
    }
    void* model = model_of(component);
    int use_other = 1;
    uint32_t hue_name = kH;
    uint32_t saturation_name = kS;
    uint32_t value_name = kV;
    part->fetch_int_value(kUseDefaultHsv, &use_other, 0);
    if (use_other != 0) {
        part = static_cast<script::Struct*>(appearance_part(skater_appearance_, part_name));
        hue_name = kDefaultH;
        saturation_name = kDefaultS;
        value_name = kDefaultV;
    }
    int hue = 0;
    int saturation = 0;
    int value = 0;
    if (!part->fetch_int_value(hue_name, &hue, 0) || !part->fetch_int_value(saturation_name, &saturation, 0) ||
        !part->fetch_int_value(value_name, &value, 0)) {
        return;
    }
    gsl::owner<script::Struct*> colour = make_struct();
    colour->put_name_key(checksums::Name{kPart}, 0);
    colour->put_name_key(checksums::Name{kMaterial}, kCasheadHead);
    colour->put_int(checksums::Name{kPass}, 0);
    static_cast<render::RenderModel*>(model)->set_color_from_script(
        colour, static_cast<float>(hue), static_cast<float>(saturation * static_cast<double>(kPercentToFraction)),
        static_cast<float>(value * static_cast<double>(kPercentToFraction)));
    free_struct(colour);
}

} // namespace objects
