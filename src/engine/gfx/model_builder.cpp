// The model builder (gfx::ModelAssembler): see model_builder.h. From the disassembly of 0x004897D0-0x0048B32D.

#include <cstdint>
#include <memory>
#include <new>

#include "engine/gfx/model_builder.h"

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/model.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/rig.h"
#include "engine/gfx/texture_dict.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/entity.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/checked_index.h"

namespace gfx {

namespace {

render::RenderModel* g_built_model;
Rig* g_built_skeleton;
std::unique_ptr<ModelLook> g_built_appearance;

// While Build runs: the model, its skeleton and the appearance's copy.
render::RenderModel*& built_model() { return g_built_model; }
Rig*& built_skeleton() { return g_built_skeleton; }
std::unique_ptr<ModelLook>& built_appearance() { return g_built_appearance; }
// The texture dictionaries: the game's and the skater's (its cag_workspace).
render::TextureSet* game_textures() { return render::g_game_texture_dict; }
render::TextureSet* skater_textures() { return render::g_particle_texture_dict; }

constexpr float kPercent = 0.01f;           // 0x0064544C
constexpr float kDegrees = 0.0174532924f;   // 0x00645464 (pi / 180)
constexpr float kHalf = 0.5f;               // 0x006453BC
constexpr float kQuarterTurn = 1.57079637f; // 0x3FC90FDB

// The build script's member functions.
constexpr uint32_t kGeomSetUvOffset = 0x18924DA1;             // GeomSetUVOffset
constexpr uint32_t kModelClearAllGeoms = 0x0ECF0248;          // ModelClearAllGeoms
constexpr uint32_t kModelApplyBoneScale = 0x21AEC583;         // ModelApplyBoneScale
constexpr uint32_t kAppearanceUnused30e2951d = 0x30E2951D;    // (name unknown: an empty call on the appearance)
constexpr uint32_t kGeomAllocateUvMatrixParams = 0x3BF41299;  // GeomAllocateUVMatrixParams
constexpr uint32_t kModelApplyObjectScale = 0x50F1285B;       // ModelApplyObjectScale
constexpr uint32_t kModelAddGeom = 0x53DA94F1;                // ModelAddGeom
constexpr uint32_t kAppearanceAllowScalingCheat = 0x6516B484; // AppearanceAllowScalingCheat
constexpr uint32_t kModelHideGeom = 0x681A03AF;               // ModelHideGeom
constexpr uint32_t kModelPropagateHeadColor = 0x705117E1;     // ModelPropagateHeadColor
constexpr uint32_t kModelApplyFaceTexture = 0x73AB02BC;       // ModelApplyFaceTexture
constexpr uint32_t kModelRemovePolys = 0x9041E901;            // ModelRemovePolys
constexpr uint32_t kGeomReplaceTexture = 0xAA42A104;          // GeomReplaceTexture
constexpr uint32_t kGeomModulateColor = 0xAEE6E915;           // GeomModulateColor
constexpr uint32_t kModelRunScript = 0xB1C39A54;              // ModelRunScript
constexpr uint32_t kModelRemovePart = 0xBFF957A9;             // (name unknown: the slot taken out)
constexpr uint32_t kModelApplyBodyShape = 0xC0BC8271;         // ModelApplyBodyShape
constexpr uint32_t kModelResetScale = 0xCDEEA636;             // ModelResetScale
constexpr uint32_t kModelFinalize = 0xCEFE8478;               // ModelFinalize
constexpr uint32_t kGeomSetCreatedTexture = 0xDD9C297E;       // GeomSetCreatedTexture

// Names.
constexpr uint32_t kPart = 0xB6F08F39;               // part
constexpr uint32_t kMaterial = 0x83418A6A;           // material
constexpr uint32_t kPass = 0x318F2BDB;               // pass
constexpr uint32_t kTarget = 0xB990D003;             // target
constexpr uint32_t kBoneScaling = 0x3F5F5BC2;        // bone_scaling
constexpr uint32_t kMaleCasPart = 0xE6530EF0;        // male_cas_part
constexpr uint32_t kFemaleCasPart = 0x564E3BC0;      // female_cas_part
constexpr uint32_t kBodyShape = 0x812684EF;          // body_shape
constexpr uint32_t kIsBigheadCheat = 0x3C4D849D;     // is_bighead_cheat
constexpr uint32_t kBigheadScaleInfo = 0x1ADE0DD8;   // bighead_scale_info
constexpr uint32_t kSrc = 0x9FBBDB72;                // src
constexpr uint32_t kIn = 0xA01371B1;                 // in
constexpr uint32_t kAll = 0xC4E78E22;                // All
constexpr uint32_t kAllowsScalingCheat = 0xB25E39FD; // (name unknown: a body part's flag - no scaling cheat)
constexpr uint32_t kMulticolor = 0x92B43E79;         // multicolor
constexpr uint32_t kMeshIfFacemapped = 0xC9AED80F;   // mesh_if_facemapped
constexpr uint32_t kDescId = 0x4BB2084E;             // desc_id
constexpr uint32_t kPaperBag = 0x64E9DAEF;           // PaperBag
constexpr uint32_t kBigfoot = 0x92986E0D;            // bigfoot
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;      // use_default_hsv
constexpr uint32_t kDefaultH = 0xC6CC7F7F;           // default_h
constexpr uint32_t kDefaultS = 0x4CA9B693;           // default_s
constexpr uint32_t kDefaultV = 0x3CC3421C;           // default_v
constexpr uint32_t kH = 0x6E94F918;                  // h
constexpr uint32_t kS = 0xE4F130F4;                  // s
constexpr uint32_t kV = 0x949BC47B;                  // v
constexpr uint32_t kCreatedGraphic = 0x79DAA2F9;     // CreatedGraphic
constexpr uint32_t kXScale = 0x6AA8D676;             // x_scale
constexpr uint32_t kYScale = 0xCCDFDDC2;             // y_scale
constexpr uint32_t kWith = 0x676F1DF1;               // with
constexpr uint32_t kTempTexture = 0xC553C612;        // temp_texture
constexpr uint32_t kCagWorkspace = 0xAA286543;       // cag_workspace
constexpr uint32_t kRotate = 0xC876ADF3;             // rotate
constexpr uint32_t kIsDeck = 0x25A0E7E9;             // is_deck
constexpr uint32_t kBaseTexture = 0x499F4BE9;        // base_texture
constexpr uint32_t kLogoTexture = 0x49B318FD;        // logo_texture
constexpr uint32_t kKeepStockScale = 0x5A96985D;     // use_default_scale
constexpr uint32_t kX = 0x7323E97C;                  // x
constexpr uint32_t kY = 0x0424D9EA;                  // y
constexpr uint32_t kZ = 0x9D2D8850;                  // z
constexpr uint32_t kNonlocalBones = 0x20D9AC2F;      // nonlocal_bones
constexpr uint32_t kObjectScaling = 0x8B314358;      // object_scaling
constexpr uint32_t kUseDefaultUv = 0x8602F6EE;       // use_default_uv
constexpr uint32_t kUvU = 0xCF6AA087;                // uv_u
constexpr uint32_t kUvV = 0x5663F13D;                // uv_v
constexpr uint32_t kUvScale = 0x266932C8;            // uv_scale
constexpr uint32_t kUvRot = 0x1256B6C6;              // uv_rot
// The CAS slots.
constexpr uint32_t kSkaterMHead = 0x650FAB6D;      // skater_m_head
constexpr uint32_t kSkaterMHands = 0x7E54E2A2;     // skater_m_hands
constexpr uint32_t kSkaterMLowerLegs = 0x0571DA24; // skater_m_lower_legs
constexpr uint32_t kSkaterFHead = 0x0FC85BAE;      // skater_f_head
constexpr uint32_t kSkaterFHands = 0x7C53B658;     // skater_f_hands
constexpr uint32_t kSkaterFLowerLegs = 0x74E37E2A; // skater_f_lower_legs
constexpr uint32_t kBareTorso = 0xA921D236;        // bare_torso
constexpr uint32_t kNone = 0x806FFF30;             // None
constexpr uint32_t kHands = 0x99D1CC07;            // Hands
constexpr uint32_t kShoes = 0xEB307E68;            // shoes
constexpr uint32_t kBareFeet = 0xF11C6E74;         // bare_feet
constexpr uint32_t kBody = 0x2457F44D;             // body

// A part's geoms, one model file each.
constexpr auto kMeshes = std::to_array<uint32_t>({0x1E90C5A9, 0xFECA8BB3, 0x67C3DA09, 0x10C4EA9F, 0x8EA07F3C}); // mesh, mesh1 ... mesh4
// A part's texture replacements: replace / with / in, then replace1 / with1 / in1 ... replace6.
constexpr auto kReplacements = std::to_array<std::array<uint32_t, 3>>({
    {0xF362DBBA, 0x676F1DF1, 0xA01371B1}, {0x7A993873, 0x9B03ADAD, 0xED189051}, {0xE39069C9, 0x020AFC17, 0x7411C1EB},
    {0x9497595F, 0x750DCC81, 0x0316F17D}, {0x0AF3CCFC, 0xEB695922, 0x9D7264DE}, {0x7DF4FC6A, 0x9C6E69B4, 0xEA755448},
    {0xE4FDADD0, 0x0567380E, 0x737C05F2},
});

// Whether a face texture holds one (0x00487510, called through the game: the tests script it).
bool face_texture_is_valid(FaceSkin* face) { return face->is_sound(); }

// The scale a structure gives (not rewritten yet, cdecl: its scale - one value or a vector - or its x / y / z),
// into `scale`. Whether it had one.
bool fetch_magnification(maths::Vec4* scale, script::Struct* from) { return scale_from_args(scale, from); }
bool fetch_script(script::Struct* s, uint32_t name, script::ParamScript* out, int assert)
{
    return s->fetch_script(name, out, assert);
}

// The compiled-out call on the appearance 0x30E2951D makes (the empty 0x005BAB90, one argument).
inline void unused_appearance_call(ModelLook*, int) {}

// The colour's three parts as the model takes them: h as it is, s and v as fractions of their percentages.
float get_percent(int value) { return static_cast<float>(static_cast<double>(value) * kPercent); }
float percent_of(float value) { return static_cast<float>(static_cast<double>(value) * kPercent); }
// A colour added to a slot: h, s, v and use_default_hsv = 0.
void add_colour(script::Struct* slot, int h, int s, int v)
{
    slot->put_int(kH, h);
    slot->put_int(kS, s);
    slot->put_int(kV, v);
    slot->put_int(kUseDefaultHsv, 0);
}

} // namespace

// 0x004897D0
ModelAssembler::ModelAssembler(bool via_resource_store, int tex_dict_offset)
    : via_asset_store_(via_resource_store), dict_shift_(tex_dict_offset)
{}

// 0x0048A2F0
void ModelAssembler::get_build(ModelLook* appearance, render::RenderModel* model, Rig* skeleton, uint32_t script)
{
    built_model() = model;
    built_skeleton() = skeleton;
    const memory::Hub* manager = memory::Hub::get_pull();
    void* memory = memory::alloc_zeroed(sizeof(ModelLook), manager->top_down_heap_, 1);
    built_appearance() = std::unique_ptr<ModelLook>(memory::construct_in<ModelLook>(memory)); // (null without memory)
    built_appearance()->get_reset();             // (no memory: a crash, as the original)
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->top_down_heap_);
    built_appearance()->copy_from(appearance);
    memory::Hub::get_pull()->leave_heap_scope();
    script::launch_script(script, nullptr, this, false, nullptr);
    built_model() = nullptr;
    built_skeleton() = nullptr;
    memory::destroy_then_clear(built_appearance());
}

// 0x0048A3B0. The names are asserted; a missing one is 0 (the original's was what its stack slot held: an argument's -
// the member function's name, `params` or `script`).
bool ModelAssembler::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script)
{
    switch (name) {
    case kGeomSetUvOffset: {
        uint32_t part = 0, material = name;
        int pass = 0;
        params->read_hash_value(kPart, &part, 1);
        params->read_hash_value(kMaterial, &material, 1);
        params->fetch_int_value(kPass, &pass, 1);
        choose_uv_shift(part, material, pass);
        return true;
    }
    case kModelClearAllGeoms:
        clear_all_geoms();
        return true;
    case kModelApplyBoneScale: {
        if (!params->has_entry_named(kBoneScaling))
            return true;
        uint32_t cast_part = 0;
        if (built_appearance()->get_part(kSkaterMHead))
            params->read_hash_value(kMaleCasPart, &cast_part, 0);
        else if (built_appearance()->get_part(kSkaterFHead))
            params->read_hash_value(kFemaleCasPart, &cast_part, 0);
        uint32_t part = name;
        params->read_hash_value(kPart, &part, 1);
        apply_bone_sizing(part, cast_part);
        return true;
    }
    case kAppearanceUnused30e2951d:
        unused_appearance_call(built_appearance().get(), 0);
        return true;
    case kGeomAllocateUvMatrixParams: {
        uint32_t material = name;
        int pass = 0;
        params->read_hash_value(kMaterial, &material, 1);
        params->fetch_int_value(kPass, &pass, 1);
        create_uv_anim_params(material, pass);
        return true;
    }
    case kModelApplyObjectScale: {
        script::Struct* body_shape = nullptr;
        if (!params->read_sub_struct(kBodyShape, &body_shape, 0)) {
            apply_object_scale();
            return true;
        }
        maths::Vec4 scale = built_model()->size_factor_;
        maths::Vec4 factor = {1.0f, 1.0f, 1.0f, 1.0f};
        if (!fetch_magnification(&factor, body_shape))
            return true;
        scale.x = factor.x * scale.x;
        scale.y = factor.y * scale.y;
        scale.z = factor.z * scale.z;
        scale.w = factor.w * scale.w;
        built_model()->change_scale(&scale);
        return true;
    }
    case kModelAddGeom: {
        uint32_t part = 0;
        params->read_hash_value(kPart, &part, 1);
        add_geom(part);
        return true;
    }
    case kAppearanceAllowScalingCheat: {
        script::Struct* body = built_appearance()->get_part_option(kBody);
        if (!body)
            return true;
        return !body->has_bare_flag(kAllowsScalingCheat);
    }
    case kModelHideGeom: {
        uint32_t geom = name;
        int hide = 0;
        params->read_hash_value(kPart, &geom, 1);
        params->fetch_int_value(0u, &hide, 1); // (the unnamed integer)
        conceal_mesh(geom, hide != 0);
        return true;
    }
    case kModelPropagateHeadColor:
        propagate_head_color();
        return true;
    case kModelApplyFaceTexture: {
        FaceSkin* face = built_appearance()->face_skin_;
        if (!face || !face_texture_is_valid(face) || !built_model())
            return true;
        const char* source = nullptr; // (asserted; the original's slot held the method's name)
        params->fetch_text(kSrc, &source, 1);
        uint32_t in = kAll;
        params->read_hash_value(kIn, &in, 0);
        return built_model()->replace_face_texture(face, source, in);
    }
    case kModelRemovePart: {
        uint32_t part = 0;
        params->read_hash_value(kPart, &part, 1);
        remove_part(part);
        return true;
    }
    case kGeomModulateColor:
        blend_tint_over(params);
        return true;
    case kModelRemovePolys:
        detach_polys();
        return true;
    case kGeomReplaceTexture: {
        uint32_t part = 0;
        params->read_hash_value(kPart, &part, 1);
        swap_texture(part);
        return true;
    }
    case kModelRunScript: {
        uint32_t part = name, target = 0;
        params->read_hash_value(kPart, &part, 1);
        params->read_hash_value(kTarget, &target, 1);
        launch_script(part, target);
        return true;
    }
    case kModelFinalize:
        seal();
        return true;
    case kModelApplyBodyShape: {
        script::Struct* body_shape = nullptr;
        if (!params->read_sub_struct(kBodyShape, &body_shape, 0)) {
            apply_body_shape();
            return true;
        }
        script::Struct* head = built_appearance()->get_part_option(kSkaterMHead);
        if (head && params->has_bare_flag(kIsBigheadCheat))
            head->read_sub_struct(kBigheadScaleInfo, &body_shape, 0);
        if (!built_skeleton())
            return true;
        return built_skeleton()->apply_bone_scales(body_shape);
    }
    case kModelResetScale:
        restore_scale();
        return true;
    case kGeomSetCreatedTexture: {
        uint32_t part = 0;
        params->read_hash_value(kPart, &part, 1);
        set_created_texture(part);
        detach_polys();
        return true;
    }
    default:
        return Entity::invoke_method_by_name(name, params, script);
    }
}

// 0x00489820
bool ModelAssembler::add_geom(uint32_t part)
{
    script::Struct* option = built_appearance()->get_part_option(part);
    if (!option)
        return true;
    int multicolor = 0;
    option->fetch_int_value(kMulticolor, &multicolor, 0);
    auto add = [&](const char* mesh) {
        built_model()->add_file_geom(mesh, part, via_asset_store_, dict_shift_, false, multicolor != 0, true, false);
    };
    const char* mesh = nullptr;
    FaceSkin* face = built_appearance()->face_skin_;
    if ((face && face_texture_is_valid(face)) || built_appearance()->has_face_texture()) {
        if (option->fetch_text(kMeshIfFacemapped, &mesh, 0)) {
            add(mesh);
            return true;
        }
    }
    for (const uint32_t name : kMeshes) {
        if (option->fetch_text(name, &mesh, 0))
            add(mesh);
    }
    return true;
}

// 0x0048A9C0
bool ModelAssembler::remove_part(uint32_t part)
{
    built_appearance()->structure_.erase_module(part);
    return true;
}

// 0x00489800
bool ModelAssembler::conceal_mesh(uint32_t geom, bool hide)
{
    built_model()->conceal_mesh(geom, hide);
    return true;
}

// 0x00489A50. (The part is asserted: missing, the original reads stack garbage - here 0.)
bool ModelAssembler::blend_tint_over(script::Struct* params)
{
    uint32_t part = 0;
    params->read_hash_value(kPart, &part, 1);
    script::Struct* slot = built_appearance()->get_part(part);
    if (!slot)
        return true;
    uint32_t descriptor_id = 0;
    slot->read_hash_value(kDescId, &descriptor_id, 0);
    if (descriptor_id == kPaperBag || descriptor_id == kBigfoot)
        return false;
    int use_default = 0;
    if (!slot->has_entry_named(kUseDefaultHsv))
        use_default = 1;
    else
        slot->fetch_int_value(kUseDefaultHsv, &use_default, 0);
    if (use_default) {
        int h = 0, s = 0, v = 0;
        script::Struct* option = built_appearance()->get_part_option(part);
        if (option && option->fetch_int_value(kDefaultH, &h, 0) && option->fetch_int_value(kDefaultS, &s, 0) &&
            option->fetch_int_value(kDefaultV, &v, 0)) {
            built_model()->set_color_from_script(params, static_cast<float>(h), get_percent(s), get_percent(v));
        } else {
            built_model()->clear_color_from_script(params);
        }
        return false;
    }
    int h = 0, s = 0, v = 0;
    if (!slot->fetch_int_value(kH, &h, 0) || !slot->fetch_int_value(kS, &s, 0) || !slot->fetch_int_value(kV, &v, 0))
        return true;
    built_model()->set_color_from_script(params, static_cast<float>(h), get_percent(s), get_percent(v));
    return true;
}

// 0x00489C30
bool ModelAssembler::create_uv_anim_params(uint32_t material, int pass)
{
    built_model()->create_uv_anim_params(material, pass);
    return true;
}

// 0x00489C50. (A missing `with` is the previous one - the first time null: the original's stack slot held the part's
// checksum.)
bool ModelAssembler::swap_texture(uint32_t part)
{
    bool result = false;
    script::Struct* option = built_appearance()->get_part_option(part);
    if (!option)
        return false;
    const char* with = nullptr;
    for (const std::array<uint32_t, 3>& names : kReplacements) {
        const char* source = nullptr;
        if (!option->fetch_text(core::at(names, 0), &source, 0))
            continue;
        option->fetch_text(core::at(names, 1), &with, 1);
        uint32_t in = part;
        option->read_hash_value(core::at(names, 2), &in, 0);
        result = built_model()->swap_texture(in, source, with);
    }
    return result;
}

// 0x00489A30
bool ModelAssembler::clear_all_geoms()
{
    built_model()->drop_geoms();
    return true;
}

// 0x00489A40
bool ModelAssembler::detach_polys()
{
    built_model()->break_own_strips();
    return true;
}

// 0x00489F10. (A head with h but no s / v, or with neither and no part: the original's stack garbage - here 0.)
bool ModelAssembler::propagate_head_color()
{
    bool male = true;
    uint32_t head_part = kSkaterMHead, hands_part = kSkaterMHands, lower_legs_part = kSkaterMLowerLegs;
    script::Struct* head = built_appearance()->get_part(head_part);
    if (!head) {
        head_part = kSkaterFHead;
        male = false;
        hands_part = kSkaterFHands;
        lower_legs_part = kSkaterFLowerLegs;
        head = built_appearance()->get_part(head_part);
        if (!head)
            return false;
    }
    int h = 0, s = 0, v = 0;
    if (head->fetch_int_value(kH, &h, 0)) {
        head->fetch_int_value(kS, &s, 0);
        head->fetch_int_value(kV, &v, 0);
    } else if (script::Struct* option = built_appearance()->get_part_option(head_part)) {
        option->fetch_int_value(kDefaultH, &h, 1);
        option->fetch_int_value(kDefaultS, &s, 1);
        option->fetch_int_value(kDefaultV, &v, 1);
    }
    uint32_t descriptor_id = 0;
    if (male) {
        if (script::Struct* torso = built_appearance()->get_part(kBareTorso)) {
            torso->read_hash_value(kDescId, &descriptor_id, 1);
            if (descriptor_id != kNone)
                add_colour(torso, h, s, v);
        }
    }
    if (script::Struct* hands = built_appearance()->get_part(hands_part)) {
        hands->read_hash_value(kDescId, &descriptor_id, 1);
        if (descriptor_id == kHands)
            add_colour(hands, h, s, v);
    }
    if (script::Struct* lower_legs = built_appearance()->get_part(lower_legs_part))
        add_colour(lower_legs, h, s, v);
    script::Struct* shoes = built_appearance()->get_part_option(kShoes);
    if (shoes && shoes->has_bare_flag(kBareFeet)) {
        if (script::Struct* slot = built_appearance()->get_part(kShoes))
            add_colour(slot, h, s, v);
    }
    if (script::Struct* body = built_appearance()->get_part(kBody))
        add_colour(body, h, s, v);
    return true;
}

// 0x0048A220
bool ModelAssembler::seal()
{
    built_model()->clean_instance_strips();
    return true;
}

// 0x0048A9E0
bool ModelAssembler::restore_scale()
{
    const maths::Vec4 one = {1.0f, 1.0f, 1.0f, 1.0f};
    built_model()->change_scale(&one);
    if (!built_skeleton())
        return false;
    built_skeleton()->reset_bone_scales();
    return true;
}

// 0x0048AA30 (no skeleton: a crash, as the original)
bool ModelAssembler::apply_body_shape()
{
    script::Struct* body_shape = nullptr;
    if (!built_appearance()->structure_.read_sub_struct(kBodyShape, &body_shape, 0))
        return false;
    return built_skeleton()->apply_bone_scales(body_shape);
}

// 0x0048AA70
bool ModelAssembler::apply_bone_sizing(uint32_t part, uint32_t cast_part)
{
    script::Struct* slot = built_appearance()->get_part(part);
    if (!slot) {
        if (!cast_part)
            return false;
        // (no option: a crash, as the original)
        built_appearance()->get_part_option(cast_part)->read_sub_struct(part, &slot, 0);
        if (!slot)
            return false;
    }
    if (!slot->has_entry_named(kKeepStockScale))
        return false;
    int use_default = 0;
    slot->fetch_int_value(kKeepStockScale, &use_default, 0);
    if (use_default)
        return false;
    int x = 0, y = 0, z = 0;
    if (!slot->fetch_int_value(kX, &x, 0) || !slot->fetch_int_value(kY, &y, 0) || !slot->fetch_int_value(kZ, &z, 0))
        return false;
    script::Array* bones = script::fetch_list(part);
    if (!bones)
        return false;
    maths::Vec4 scale;
    scale.w = 1.0f;
    for (uint32_t i = 0; i < bones->extent_; ++i) {
        const uint32_t bone = bones->fetch_part(i);
        built_skeleton()->fetch_bone_sizing(bone, &scale);
        bool local = true;
        scale.x = static_cast<float>(static_cast<double>(x) * kPercent * scale.x);
        scale.y = static_cast<float>(static_cast<double>(y) * kPercent * scale.y);
        scale.z = static_cast<float>(static_cast<double>(z) * kPercent * scale.z);
        if (script::Array* nonlocal = script::fetch_list(kNonlocalBones)) {
            for (uint32_t j = 0; j < nonlocal->extent_; ++j) {
                if (nonlocal->fetch_part(j) == bone)
                    local = false;
            }
        }
        built_skeleton()->change_bone_sizing(bone, &scale, local);
    }
    return false;
}

// 0x0048AC40
bool ModelAssembler::apply_object_scale()
{
    maths::Vec4 scale = {1.0f, 1.0f, 1.0f, 1.0f};
    if (fetch_magnification(&scale, &built_appearance()->structure_)) {
        built_model()->change_scale(&scale);
        return true;
    }
    script::Struct* body_shape = nullptr;
    if (built_appearance()->structure_.read_sub_struct(kBodyShape, &body_shape, 0) &&
        fetch_magnification(&scale, body_shape)) {
        built_model()->change_scale(&scale);
        return true;
    }
    script::Struct* slot = built_appearance()->get_part(kObjectScaling);
    if (!slot || !slot->has_entry_named(kKeepStockScale))
        return false;
    int use_default = 0;
    slot->fetch_int_value(kKeepStockScale, &use_default, 0);
    if (use_default)
        return false;
    int x = 0, y = 0, z = 0;
    if (slot->fetch_int_value(kX, &x, 0) && slot->fetch_int_value(kY, &y, 0) && slot->fetch_int_value(kZ, &z, 0)) {
        const maths::Vec4 object_scale = {get_percent(x), get_percent(y), get_percent(z), 1.0f};
        built_model()->change_scale(&object_scale);
    }
    return true;
}

// 0x0048ADD0
bool ModelAssembler::set_created_texture(uint32_t part)
{
    script::Struct* option = built_appearance()->get_part_option(part);
    if (!option)
        return false;
    uint32_t descriptor_id = 0; // (asserted: the original's is garbage when missing)
    option->read_hash_value(kDescId, &descriptor_id, 1);
    if (descriptor_id != kCreatedGraphic)
        return false;
    memory::Hub::get_pull()->enter_heap_scope(memory::Hub::get_pull()->top_down_heap_);
    uint32_t in = 0, with = 0;
    float x_scale = 0.0f, y_scale = 0.0f;
    const char* temporary = nullptr;
    option->read_hash_value(kIn, &in, 1);
    option->fetch_float(kXScale, &x_scale, 1);
    option->fetch_float(kYScale, &y_scale, 1);
    option->read_hash_value(kWith, &with, 0);
    option->fetch_text(kTempTexture, &temporary, 1);
    render::BitmapTexture* texture = game_textures()->ingest_skin(temporary, 0, 0, true);
    texture->begin_edit(false, false); // (the jump 0x0048D060)
    render::BitmapTexture* workspace = skater_textures()->fetch_skin(kCagWorkspace);
    workspace->begin_edit(false, false);
    texture->resize_from(workspace, 1);  // (0x0048DE80)
    const uint32_t black = 0;            // (four zero bytes)
    texture->get_border(black);          // (0x0048DED0)
    texture->get_zoom(x_scale, y_scale); // (0x0048DEB0)
    int rotate = 0;
    if (option->fetch_int_value(kRotate, &rotate, 0))
        texture->spin_by(kQuarterTurn, -1.0f, -1.0f); // (0x0049F360)
    render::BitmapTexture* base = nullptr;
    bool deck = false;
    int is_deck = 0;
    if (option->fetch_int_value(kIsDeck, &is_deck, 0)) {
        deck = true;
        const char* base_name = nullptr; // (asserted; missing: the stack slot the colour above zeroed)
        option->fetch_text(kBaseTexture, &base_name, 1);
        base = game_textures()->ingest_skin(base_name, 0, 0, true);
        base->begin_edit(false, false);
        texture->write_back(1, nullptr); // (0x0048DE30)
        base->get_overlay(texture, 1);   // (0x0049F420)
        base->write_back(1, nullptr);
    }
    texture->write_back(1, nullptr); // (0x0048DE30)
    const char* logo = nullptr;
    option->fetch_text(kLogoTexture, &logo, 1);
    const bool result = built_model()->replace_texture_with(in, logo, with);
    if (deck)
        game_textures()->unload_texture(base);
    game_textures()->unload_texture(texture);
    workspace->end_edit(); // (0x0048E4C0)
    memory::Hub::get_pull()->leave_heap_scope();
    return result;
}

// 0x0048B030
bool ModelAssembler::choose_uv_shift(uint32_t part, uint32_t material, int pass)
{
    script::Struct* slot = built_appearance()->get_part(part);
    if (!slot)
        return true;
    if (!slot->has_entry_named(kUseDefaultUv))
        return false;
    int use_default = static_cast<int>(part); // (the original's stack slot: the part)
    slot->fetch_int_value(kUseDefaultUv, &use_default, 0);
    if (use_default)
        return false;
    float u = 0.0f, v = 0.0f, scale = 0.0f, rotation = 0.0f;
    slot->fetch_float(kUvU, &u, 1);
    slot->fetch_float(kUvV, &v, 1);
    slot->fetch_float(kUvScale, &scale, 1);
    slot->fetch_float(kUvRot, &rotation, 1);
    maths::Mat4 uv{};
    uv.reset_to_identity();
    // about the texture's middle: moved there, scaled, turned, moved back
    core::at(uv.row, 3).x = static_cast<float>(static_cast<double>(core::at(uv.row, 3).x) - kHalf);
    core::at(uv.row, 3).y = static_cast<float>(static_cast<double>(core::at(uv.row, 3).y) - kHalf);
    const float size = percent_of(scale);
    uv.scale_tool(maths::Vec4{size, size, size, 1.0f});
    uv.spin_by(maths::Vec4{0.0f, 0.0f, 1.0f, 1.0f}, static_cast<float>(static_cast<double>(rotation) * kDegrees));
    core::at(uv.row, 3).x = static_cast<float>(static_cast<double>(core::at(uv.row, 3).x) + kHalf);
    core::at(uv.row, 3).y = static_cast<float>(static_cast<double>(core::at(uv.row, 3).y) + kHalf);
    uv.offset_local(maths::Vec4{static_cast<float>(static_cast<double>(u) * kPercent),
                                static_cast<float>(static_cast<double>(v) * kPercent), 0.0f, 0.0f});
    built_model()->choose_uv_matrix(material, pass, &uv);
    return true;
}

// 0x0048B200
bool ModelAssembler::launch_script(uint32_t part, uint32_t target)
{
    script::Struct* option = built_appearance()->get_part_option(part);
    if (!option)
        return true;
    auto* held = static_cast<script::ParamScript*>(memory::alloc(sizeof(script::ParamScript)));
    if (held) {
        held->own_name_hash_ = 0;
        held->code_stream_ = nullptr;
    }
    if (fetch_script(option, target, held, 0)) {
        script::Script* script = script::Script::create();
        script->set_script_copy(held, nullptr, built_appearance().get()); // (no memory: a crash, as the original)
        while (script->tick_frame()) {
        }
        if (script) {
            script::Script::destroy(script);
        }
    }
    memory::Hub::get_pull()->dealloc(held);
    return true;
}

} // namespace gfx
