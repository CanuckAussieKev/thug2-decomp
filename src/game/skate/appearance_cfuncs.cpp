// Script commands of the players' appearances (see appearance_cfuncs.h).

#include "game/skate/appearance_cfuncs.h"

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/objects/cutscene.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity_hub.h"
#include "game/rider/rider.h"
#include "game/rider/trick_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "engine/gfx/face_texture.h"
#include "engine/gfx/model_appearance.h"
#include "game/skate/cas_options.h"
#include "engine/objects/compositemgr.h"

namespace commands {

namespace {

using namespace skate_calls;

constexpr uint32_t kPlayer = 0x67E6859A;            // player
constexpr uint32_t kCurrentFacePoints = 0x12E87395; // current_face_points
constexpr uint32_t kTexture = 0x7D99F28D;           // texture
constexpr uint32_t kPathIsComplete = 0x54C2E57C;    // fullPathIncluded

constexpr uint32_t kProfile = 0x7EA855F0;                             // profile
constexpr uint32_t kSkater = 0x5B8AB877;                              // skater
constexpr uint32_t kModel = 0x286A8D26;                               // model (the component)
constexpr uint32_t kTrick = 0x270F56E1;                               // trick (the component)
constexpr uint32_t kBailBoard = 0x884EF81B;                           // BailBoard (the object)
constexpr uint32_t kNoBoard = 0x41EEC86B;                             // no_board
constexpr uint32_t kBoard = 0xA7A9D4B8;                               // board (the geom)
constexpr uint32_t kTarget = 0xB990D003;                              // target
constexpr uint32_t kDestArgs = 0x46A2869C;                            // target_params
constexpr uint32_t kPart = 0xB6F08F39;                                // part
constexpr uint32_t kDescId = 0x4BB2084E;                              // desc_id
constexpr uint32_t kH = 0x6E94F918, kS = 0xE4F130F4, kV = 0x949BC47B; // h, s, v
constexpr uint32_t kUseDefaultHsv = 0x97DBDDE6;                       // use_default_hsv
constexpr uint32_t kX = 0x7323E97C, kY = 0x0424D9EA, kZ = 0x9D2D8850; // x, y, z
constexpr uint32_t kKeepStockScale = 0x5A96985D;                      // use_default_scale
constexpr uint32_t kUvU = 0xCF6AA087, kUvV = 0x5663F13D, kUvScale = 0x266932C8, kUvRot = 0x1256B6C6; // uv_*
constexpr uint32_t kUseDefaultUv = 0x8602F6EE;                                                       // use_default_uv
// The model build scripts RefreshSkaterColors / Scale / Visibility / UV rebuild with.
constexpr uint32_t kColorModelFromAppearance = 0xD2C041C2; // color_model_from_appearance
constexpr uint32_t kScaleModelFromAppearance = 0xAEE1AF7D; // scale_model_from_appearance
constexpr uint32_t kHideModelFromAppearance = 0xB1A55798;  // hide_model_from_appearance
constexpr uint32_t kSetUvFromAppearance = 0x091D6261;      // set_uv_from_appearance

// The profile of the player a command names (player=<n>; 0 if missing - uninitialised in the original where the
// parameter is asserted).
objects::RiderIdentity* rider_identity_from_params(script::Struct* params, int assert = 1)
{
    int player = 0;
    params->fetch_int_value(kPlayer, &player, assert);
    return fetch_account(player);
}

// The appearance's parts (no source yet): 0x00489050 the part a slot was given as built (its CAS option's structure),
// 0x004890B0 the slot's own structure in the appearance; null if none. 0x00485CF0 (cdecl): the CAS option `descriptor_id` of
// the slot's global part list. The appearance's own structure is at +0x4C.
gfx::ModelLook* get_appearance(objects::RiderIdentity* profile)
{
    return reinterpret_cast<gfx::ModelLook*>(profile);
} // (its first member)
script::Struct* actual_part(objects::RiderIdentity* profile, uint32_t part)
{
    return get_appearance(profile)->get_part_option(part);
}
script::Struct* appearance_part(objects::RiderIdentity* profile, uint32_t part)
{
    return get_appearance(profile)->get_part(part);
}
script::Struct* appearance_structure(objects::RiderIdentity* profile) { return &profile->appearance()->structure_; }
// RefreshSkaterModel's call between the heap switch and the model's build: compiled out of this build (0x0048C1B0, a
// jump to the empty 0x005480A0; no arguments).
inline void model_build_hook() {}
// A float as an integer, as the original's _ftol (truncated towards zero, through 64 bits).
int truncate(float value) { return static_cast<int>(static_cast<int64_t>(value)); }

// refresh_skater*: skater=<n>'s model rebuilt from profile=<n>'s appearance with a build script.
bool refresh_skater(script::Struct* params, uint32_t build_script)
{
    int profile_index = 0;
    int skater_index = 0;
    params->fetch_int_value(kProfile, &profile_index, 1);
    params->fetch_int_value(kSkater, &skater_index, 1);
    objects::GameEntity* skater = get_rider_at(skater_index);
    objects::RiderIdentity* profile = fetch_account(profile_index);
    static_cast<objects::MeshInstanceModule*>(skater->fetch_module(kModel))
        ->rebuild_from_profile(profile, build_script);
    return true;
}

// The face texture (the appearance's +0x54) and its calls (no source yet): 0x00487550 its points (a copy in `out`,
// returned), 0x00487570 set; 0x00487380 its texture (a file, maybe with its path), 0x00487590 the overlay's;
// 0x00487520 marked valid or not; 0x00487510 whether valid. 0x00487000 / 0x004869D0 (cdecl): the points written
// to / read from a structure (false: something missing).
void* face_of(objects::RiderIdentity* profile) { return objects::appearance_face(profile); }
FacePoints* get_face_vertex_count(void* face, FacePoints* out)
{
    return reinterpret_cast<FacePoints*>(
        static_cast<gfx::FaceSkin*>(face)->get_points(reinterpret_cast<gfx::FaceAnchors*>(out)));
}
void set_face_vertex_count(void* face, FacePoints* points)
{
    static_cast<gfx::FaceSkin*>(face)->set_points(reinterpret_cast<const gfx::FaceAnchors*>(points));
}
bool write_face_points(FacePoints* points, script::Struct* to)
{
    return gfx::write_face_points(reinterpret_cast<const gfx::FaceAnchors*>(points), to);
}
bool read_face_points(FacePoints* points, script::Struct* from)
{
    return gfx::read_face_points(reinterpret_cast<gfx::FaceAnchors*>(points), from);
}

} // namespace

// 0x005AE550: SetPlayerAppearance player=<n> appearance_structure=<s> - the player's profile's appearance set (and
// applied).
bool cfunc_set_player_appearance(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params);
    script::Struct* appearance = nullptr; // (uninitialised in the original when missing)
    params->read_sub_struct("appearance_structure", &appearance, 1);
    profile->set_appearance(appearance, true);
    return true;
}

// 0x005AE5B0: GetPlayerFacePoints player=<n> - the player's face points written into the script's
// current_face_points structure (a new one added if there's none). A failed write prints the parameters (compiled
// out).
bool cfunc_get_player_face_points(script::Struct* params, script::Script* script)
{
    void* face = face_of(rider_identity_from_params(params));
    script::Struct* points = nullptr;
    bool made = false;
    if (!script->args_->read_sub_struct(kCurrentFacePoints, &points, 0)) {
        script::Struct* memory = script::spawn_param_bag();
        points = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        made = true;
    }
    FacePoints scratch{};
    FacePoints copy = *get_face_vertex_count(face, &scratch);
    if (!write_face_points(&copy, points))
        script::dump_to_log(params);
    if (made) {
        script->args_->put_sub_struct(kCurrentFacePoints, points);
        if (points) {
            points->get_clear();
            script::delete_struct(points);
        }
    }
    return true;
}

// 0x005AE710: SetPlayerFacePoints player=<n> [face_points=<s>] - the player's face points from the structure (or
// the parameters themselves). A failed read prints the parameters (compiled out); the points are set anyway.
bool cfunc_set_player_face_points(script::Struct* params, script::Script*)
{
    void* face = face_of(rider_identity_from_params(params));
    script::Struct* from = nullptr;
    if (!params->read_sub_struct("face_points", &from, 0))
        from = params;
    FacePoints points{};
    if (!read_face_points(&points, from))
        script::dump_to_log(params);
    set_face_vertex_count(face, &points);
    return true;
}

// 0x005AE7A0: SetPlayerFaceTexture player=<n> texture=<file> [fullPathIncluded].
bool cfunc_set_player_face_texture(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->fetch_text(kTexture, &texture, 1);
    const bool full_path = params->has_bare_flag(kPathIsComplete);
    static_cast<gfx::FaceSkin*>(face_of(profile))->load_image_file(texture, full_path);
    return true;
}

// 0x005AE820: SetPlayerFaceOverlayTexture player=<n> texture=<file>.
bool cfunc_set_player_face_overlay_texture(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params);
    const char* texture = nullptr; // (uninitialised in the original when missing)
    params->fetch_text(kTexture, &texture, 1);
    static_cast<gfx::FaceSkin*>(face_of(profile))->set_overlay_name(texture);
    return true;
}

// 0x005AE880: ClearPlayerFaceTexture player=<n> - the face marked not valid.
bool cfunc_clear_player_face_texture(script::Struct* params, script::Script*)
{
    static_cast<gfx::FaceSkin*>(face_of(rider_identity_from_params(params)))->mark_usable(false);
    return true;
}

// 0x005AE8C0: PlayerFaceIsValid player=<n> (false without a face).
bool cfunc_player_face_is_valid(script::Struct* params, script::Script*)
{
    void* face = face_of(rider_identity_from_params(params));
    return face && static_cast<gfx::FaceSkin*>(face)->is_sound();
}

// 0x005AF750 / 0x005AF7D0 / 0x005AF850 / 0x005AF8D0: RefreshSkaterColors / Scale / Visibility / UV profile=<n>
// skater=<n> - the model rebuilt with color_model_from_appearance / scale_ / hide_ / set_uv_from_appearance.
bool cfunc_refresh_skater_colors(script::Struct* params, script::Script*)
{
    return refresh_skater(params, kColorModelFromAppearance);
}
bool cfunc_refresh_skater_scale(script::Struct* params, script::Script*)
{
    return refresh_skater(params, kScaleModelFromAppearance);
}
bool cfunc_refresh_skater_visibility(script::Struct* params, script::Script*)
{
    return refresh_skater(params, kHideModelFromAppearance);
}
bool cfunc_refresh_skater_uv(script::Struct* params, script::Script*)
{
    return refresh_skater(params, kSetUvFromAppearance);
}

// 0x005AFD80: RefreshSkaterModel [<n>] [profile=<p>] [skater=<s>] [no_board=...] - skater s's (n) model built again
// from profile p's (n) appearance in the skater's geometry heap (the first viewport's skater: the BailBoard object
// killed first), then in its own heap its stats, trick mappings and looks from the profile; no_board: the board geom
// hidden.
bool cfunc_refresh_skater_model(script::Struct* params, script::Script*)
{
    int index = 0;
    params->fetch_int_value(0u, &index, 0);
    int profile_index = index;
    int skater_index = index;
    params->fetch_int_value(kProfile, &profile_index, 0);
    params->fetch_int_value(kSkater, &skater_index, 0);
    auto* skater = static_cast<objects::Rider*>(get_rider_at(skater_index));
    if (skater->view_slot_ == 0) {
        auto* objects = reinterpret_cast<objects::MiscHub*>(objects::g_composite_registry);
        if (objects::Entity* board = objects->look_up_entity(kBailBoard)) {
            board->flag_as_dead();
            objects->flush_dead_objects();
        }
    }
    objects::RiderIdentity* profile = fetch_account(profile_index);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->skater_geom_heaps_[skater->get_skater_number()]);
    model_build_hook();
    auto* model = static_cast<objects::MeshInstanceModule*>(skater->fetch_module(kModel));
    model->set_up_mesh_from_profile(profile, false, skater->get_skater_number(), 0);
    memory::Hub::get_pull()->leave_heap_scope();
    manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->skater_heaps_[skater->get_skater_number()]);
    skater->set_stats_from_profile(profile);
    static_cast<objects::MoveExecutor*>(skater->fetch_module(kTrick))->apply_trick_bindings(profile);
    skater->update_from_profile(profile);
    memory::Hub::get_pull()->leave_heap_scope();
    uint32_t no_board = 0;
    if (params->read_hash_value(kNoBoard, &no_board, 0)) {
        auto* skater_model = static_cast<objects::MeshInstanceModule*>(skater->fetch_module(kModel));
        skater_model->conceal_mesh(kBoard, true, true);
        static_cast<objects::ObjectModule*>(skater_model)->tick_frame(); // (through the vtable: slot 1)
    }
    return true;
}

// 0x005AFF30: EditPlayerAppearance [<n>] [profile=<p>] target=<script> [target_params=<s>] - the target script run
// with the parameters on profile p's (n) profile (passed as the script's object, as the original does).
bool cfunc_edit_player_appearance(script::Struct* params, script::Script*)
{
    int index = 0;
    params->fetch_int_value(0u, &index, 0);
    int profile_index = index;
    params->fetch_int_value(kProfile, &profile_index, 0);
    objects::RiderIdentity* profile = fetch_account(profile_index);
    uint32_t target = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kTarget, &target, 1);
    script::Struct* target_params = nullptr;
    params->read_sub_struct(kDestArgs, &target_params, 0);
    script::launch_script(target, target_params, reinterpret_cast<objects::Entity*>(profile), false, nullptr);
    return true;
}

// 0x005B0420: GetActualCASOptionStruct part=<slot> desc_id=<id> - the CAS option's structure added to the script's
// parameters. Always false.
bool cfunc_get_actual_cas_option_struct(script::Struct* params, script::Script* script)
{
    uint32_t part = 0, descriptor_id = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kPart, &part, 1);
    params->read_hash_value(kDescId, &descriptor_id, 1);
    script::Struct* option = objects::find_cas_option(part, descriptor_id, true);
    script->args_->append_structure_if_any(option);
    return false;
}

// 0x005B0480 / 0x005B0500: GetActualPlayerAppearancePart / GetPlayerAppearancePart [player=<n>] part=<slot> - the
// part as built (its CAS option) / the appearance's own slot structure added to the script's parameters (false
// without).
bool cfunc_get_actual_player_appearance_part(script::Struct* params, script::Script* script)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params, 0);
    uint32_t part = 0;
    if (!params->read_hash_value(kPart, &part, 0))
        return false;
    script::Struct* structure = actual_part(profile, part);
    if (!structure)
        return false;
    script->args_->append_structure_if_any(structure);
    return true;
}

bool cfunc_get_player_appearance_part(script::Struct* params, script::Script* script)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params, 0);
    uint32_t part = 0;
    if (!params->read_hash_value(kPart, &part, 0))
        return false;
    script::Struct* structure = appearance_part(profile, part);
    if (!structure)
        return false;
    script->args_->append_structure_if_any(structure);
    return true;
}

// 0x005B0580: GetPlayerAppearance [player=<n>] - a copy of the appearance's structure as profile.
bool cfunc_get_player_appearance(script::Struct* params, script::Script* script)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params, 0);
    if (reinterpret_cast<uintptr_t>(profile) + 0x4C == 0) // (the original's check of the structure's address)
        return false;
    script::Struct* memory = script::spawn_param_bag();
    script::Struct* copy = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
    copy->append_structure_if_any(appearance_structure(profile));
    script->args_->append_struct_ref("profile", copy);
    return true;
}

// 0x005B0670: SetPlayerAppearanceColor [player=<n>] part=<slot> h= s= v= use_default_hsv= - the slot's colour (the
// floats truncated to integers); nothing if the appearance hasn't the slot.
bool cfunc_set_player_appearance_color(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing, as the values)
    params->read_hash_value(kPart, &part, 1);
    script::Struct* slot = appearance_part(profile, part);
    if (slot) {
        float h = 0.0f, s = 0.0f, v = 0.0f;
        int use_default = 0;
        params->fetch_float(kH, &h, 1);
        params->fetch_float(kS, &s, 1);
        params->fetch_float(kV, &v, 1);
        params->fetch_int_value(kUseDefaultHsv, &use_default, 1);
        slot->put_int(kH, truncate(h));
        slot->put_int(kS, truncate(s));
        slot->put_int(kV, truncate(v));
        slot->put_int(kUseDefaultHsv, use_default);
    }
    return true;
}

// 0x005B0780: SetPlayerAppearanceScale [player=<n>] part=<slot> [x=] [y=] [z=] [use_default_scale=] - the values
// given set in the slot (made in the appearance if it hasn't the slot); the floats truncated.
bool cfunc_set_player_appearance_scale(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kPart, &part, 1);
    script::Struct* slot = appearance_part(profile, part);
    if (!slot) {
        script::Struct* memory = script::spawn_param_bag();
        slot = memory ? memory::construct_in<script::Struct>(memory) : nullptr;
        appearance_structure(profile)->append_struct_ref(part, slot);
    }
    float value = 0.0f;
    if (params->fetch_float(kX, &value, 0))
        slot->put_int(kX, truncate(value));
    if (params->fetch_float(kY, &value, 0))
        slot->put_int(kY, truncate(value));
    if (params->fetch_float(kZ, &value, 0))
        slot->put_int(kZ, truncate(value));
    int use_default = 0;
    if (params->fetch_int_value(kKeepStockScale, &use_default, 0))
        slot->put_int(kKeepStockScale, use_default);
    return true;
}

// 0x005B0920: SetPlayerAppearanceUV [player=<n>] part=<slot> uv_u= uv_v= uv_scale= uv_rot= use_default_uv= - the
// slot's texture placement; nothing if the appearance hasn't the slot.
bool cfunc_set_player_appearance_uv(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = rider_identity_from_params(params, 0);
    uint32_t part = 0; // (uninitialised in the original when missing, as the values)
    params->read_hash_value(kPart, &part, 1);
    script::Struct* slot = appearance_part(profile, part);
    if (slot) {
        float u = 0.0f, v = 0.0f, scale = 0.0f, rotation = 0.0f;
        int use_default = 0;
        params->fetch_float(kUvU, &u, 1);
        params->fetch_float(kUvV, &v, 1);
        params->fetch_float(kUvScale, &scale, 1);
        params->fetch_float(kUvRot, &rotation, 1);
        params->fetch_int_value(kUseDefaultUv, &use_default, 1);
        slot->push_float(kUvU, u);
        slot->push_float(kUvV, v);
        slot->push_float(kUvScale, scale);
        slot->push_float(kUvRot, rotation);
        slot->put_int(kUseDefaultUv, use_default);
    }
    return true;
}

} // namespace commands
