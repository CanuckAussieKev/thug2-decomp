// Create-a-trick: the created tricks and their script commands, 0x00551CA0-0x005527BF (see create_a_trick.h).

#include <cstdint>
#include <initializer_list>

#include "game/goals/create_a_trick.h"

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"

namespace goals {

namespace {

constexpr uint32_t kFlipSkater180 = 0xAFB9A15E; // cat_flip_skater_180
constexpr uint32_t kNoName = 0;                 // (the command's unnamed value)

const char* const kTrickIndex = "trick_index";             // "trick_index"
const char* const kOtherParams = "other_params";           // "other_params"
const char* const kRotationInfo = "rotation_info";         // "rotation_info"
const char* const kAnimationInfo = "animation_info";       // "animation_info"
const char* const kNumAnimationsOn = "num_animations_on";  // "num_animations_on"
const char* const kAnimationsDone = "cat_animations_done"; // "cat_animations_done"
const char* const kRotationsDone = "cat_rotations_done";   // "cat_rotations_done"
const char* const kBailDone = "bailtimescriptdone";        // "bailtimescriptdone"
const char* const kHoldTime = "cat_hold_time";             // "cat_hold_time"
const char* const kTotalY = "total_Y_angle";               // "total_Y_angle"
const char* const kTotalX = "total_X_angle";               // "total_X_angle"
const char* const kTotalZ = "total_Z_angle";               // "total_Z_angle"

objects::Rider* local_skater() { return static_cast<objects::Rider*>(skate_calls::get_home_rider()); }
CreateATrick* created_trick(objects::Rider* skater, int index) { return skater->trick_slots_[index].get(); }
// The editor's state on the skater.
template <typename T>
T& prop(objects::Rider* skater, uint32_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(skater) + offset);
}
script::Struct* get_results(script::Script* script) { return script->args_; }

script::Array* alloc_script_array()
{
    script::Array* array = script::alloc_script_array();
    return array ? new (array) script::Array : nullptr;
}

} // namespace

// 0x00552560
CreateATrick::CreateATrick()
{
    debug::printf("creating a CCreateATrick\n");                               // "creating a CCreateATrick\n"
    script::Struct* other = script::read_sub_struct("Default_CAT_other_info"); // "Default_CAT_other_info"
    script::Struct* params = script::spawn_param_bag();
    other_params_ = params ? new (params) script::Struct : nullptr;
    other_params_->append_structure_if_any(other);
    script::Array* rotations = script::fetch_list("Default_CAT_rotation_info"); // "Default_CAT_rotation_info"
    spin_list_ = alloc_script_array();
    spin_list_->configure_size_kind(6, script::kQbvalStruct);
    script::duplicate_array(spin_list_, rotations);
    script::Array* animations = script::fetch_list("Default_CAT_animation_info"); // "Default_CAT_animation_info"
    animations_ = alloc_script_array();
    animations_->configure_size_kind(6, script::kQbvalStruct);
    script::duplicate_array(animations_, animations);
}

// 0x00552720
CreateATrick::~CreateATrick()
{
    if (script::Struct* other = other_params_) {
        other->get_clear();
        script::delete_struct(other);
    }
    for (script::Array** array : {&spin_list_, &animations_}) {
        if (!*array)
            continue;
        script::release_array_storage(*array);
        if (script::Array* own = *array) {
            own->get_clear();
            script::delete_array(own);
        }
    }
}

// 0x00551CA0
bool __cdecl cfunc_get_create_a_trick_params(script::Struct* params, script::Script* script)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    CreateATrick* trick = created_trick(skater, index);
    get_results(script)->put_sub_struct(kOtherParams, trick->other_params_);
    get_results(script)->append_array_value(kRotationInfo, trick->spin_list_);
    get_results(script)->append_array_value(kAnimationInfo, trick->animations_);
    return true;
}

// 0x00551D20
bool __cdecl cfunc_get_create_a_trick_other_params(script::Struct* params, script::Script* script)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    get_results(script)->put_sub_struct(kOtherParams, created_trick(skater, index)->other_params_);
    return true;
}

// 0x00551D80
bool __cdecl cfunc_get_create_a_trick_rotations(script::Struct* params, script::Script* script)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    get_results(script)->append_array_value(kRotationInfo, created_trick(skater, index)->spin_list_);
    return true;
}

// 0x00551DE0
bool __cdecl cfunc_get_create_a_trick_animations(script::Struct* params, script::Script* script)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    get_results(script)->append_array_value(kAnimationInfo, created_trick(skater, index)->animations_);
    return true;
}

// 0x00551E40
bool __cdecl cfunc_set_create_a_trick_params(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    if (!skater->trick_slots_[1]) {
        debug::printf("Skater has no created tricks!!\n"); // "Skater has no created tricks!!\n"
        return false;
    }
    script::Struct* other;
    if (params->read_sub_struct(kOtherParams, &other, 1)) {
        debug::printf("Storing param info %i\n", index); // "Storing param info %i\n"
        created_trick(skater, index)->other_params_->append_structure_if_any(other);
    }
    script::Array* rotations;
    if (params->fetch_list(kRotationInfo, &rotations, 1)) {
        debug::printf("Storing rotation info %i\n", index); // "Storing rotation info %i\n"
        script::duplicate_array(created_trick(skater, index)->spin_list_, rotations);
    }
    script::Array* animations;
    if (params->fetch_list(kAnimationInfo, &animations, 1)) {
        debug::printf("Storing animation info %i\n", index); // "Storing animation info %i\n"
        script::duplicate_array(created_trick(skater, index)->animations_, animations);
    }
    return true;
}

// 0x00551F70
bool __cdecl cfunc_set_create_a_trick_other_params(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script::Struct* other;
    if (params->read_sub_struct(kOtherParams, &other, 1)) {
        debug::printf("Storing param info %i\n", index);
        created_trick(skater, index)->other_params_->append_structure_if_any(other);
    }
    return true;
}

// 0x00551FF0
bool __cdecl cfunc_set_create_a_trick_rotations(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script::Array* rotations;
    if (params->fetch_list(kRotationInfo, &rotations, 1)) {
        debug::printf("Storing rotation info %i\n", index);
        script::duplicate_array(created_trick(skater, index)->spin_list_, rotations);
    }
    return true;
}

// 0x00552070
bool __cdecl cfunc_set_create_a_trick_animations(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script::Array* animations;
    if (params->fetch_list(kAnimationInfo, &animations, 1)) {
        debug::printf("Storing animation info %i\n", index);
        script::duplicate_array(created_trick(skater, index)->animations_, animations);
    }
    return true;
}

namespace {

// A CAT_Set... of an integer field (the command's unnamed integer) / a flag (non-zero), and its CAT_Get... into the
// script's parameters.
bool store_int_field(script::Struct* params, int objects::Rider::* field)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int value;
    params->fetch_int_value(kNoName, &value, 1);
    skater->*field = value;
    return true;
}
bool fetch_int_value(script::Script* script, int objects::Rider::* field, const char* name)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    get_results(script)->put_int(name, skater->*field);
    return true;
}
bool set_flag(script::Struct* params, bool objects::Rider::* field)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int value;
    params->fetch_int_value(kNoName, &value, 1);
    skater->*field = value != 0;
    return true;
}
bool fetch_marker(script::Script* script, bool objects::Rider::* field, const char* name)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    get_results(script)->put_int(name, skater->*field ? 1 : 0);
    return true;
}

} // namespace

// 0x005520F0
bool __cdecl cfunc_cat_set_num_anims(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_num_anims_);
}
// 0x00552130
bool __cdecl cfunc_cat_get_num_anims(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_num_anims_, kNumAnimationsOn);
}
// 0x00552160
bool __cdecl cfunc_cat_set_anims_done(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_anims_done_);
}
// 0x005521A0
bool __cdecl cfunc_cat_get_anims_done(script::Struct*, script::Script* script)
{
    return fetch_marker(script, &objects::Rider::cat_anims_done_, kAnimationsDone);
}
// 0x005521F0
bool __cdecl cfunc_cat_set_rots_done(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_rots_done_);
}
// 0x00552230
bool __cdecl cfunc_cat_get_rots_done(script::Struct*, script::Script* script)
{
    return fetch_marker(script, &objects::Rider::cat_rots_done_, kRotationsDone);
}
// 0x00552280
bool __cdecl cfunc_cat_set_bail_done(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_bail_done_);
}
// 0x005522C0
bool __cdecl cfunc_cat_get_bail_done(script::Struct*, script::Script* script)
{
    return fetch_marker(script, &objects::Rider::cat_bail_done_, kBailDone);
}
// 0x00552310
bool __cdecl cfunc_cat_set_flip_skater(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_flip_180_);
}
// 0x00552350
bool __cdecl cfunc_cat_get_flip_skater(script::Struct*, script::Script* script)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    get_results(script)->put_int(kFlipSkater180, skater->cat_flip_180_ ? 1 : 0);
    return true;
}
// 0x005523A0
bool __cdecl cfunc_cat_set_hold_time(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    float time;
    params->fetch_float(kNoName, &time, 1);
    skater->cat_hold_time_ = time;
    return true;
}
// 0x005523E0
bool __cdecl cfunc_cat_get_hold_time(script::Struct*, script::Script* script)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    get_results(script)->push_float(kHoldTime, skater->cat_hold_time_);
    return true;
}
// 0x00552410
bool __cdecl cfunc_cat_set_total_y(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_total_y_);
}
// 0x00552450
bool __cdecl cfunc_cat_get_total_y(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_total_y_, kTotalY);
}
// 0x00552480
bool __cdecl cfunc_cat_set_total_x(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_total_x_);
}
// 0x005524C0
bool __cdecl cfunc_cat_get_total_x(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_total_x_, kTotalX);
}
// 0x005524F0
bool __cdecl cfunc_cat_set_total_z(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_total_z_);
}
// 0x00552530
bool __cdecl cfunc_cat_get_total_z(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_total_z_, kTotalZ);
}

} // namespace goals
