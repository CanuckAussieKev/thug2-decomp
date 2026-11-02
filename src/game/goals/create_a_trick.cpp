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
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

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

objects::Rider* local_skater() { return core::down_cast<objects::Rider>(skate_calls::get_home_rider()); }


} // namespace

// 0x00552560
CreateATrick::CreateATrick()
{
    debug::printf("creating a CCreateATrick\n");                               // "creating a CCreateATrick\n"
    script::Struct* other = script::read_sub_struct("Default_CAT_other_info"); // "Default_CAT_other_info"
    gsl::owner<script::Struct*> params = script::spawn_param_bag();
    other_params_ = params ? memory::construct_in<script::Struct>(params) : nullptr;
    other_params_->append_structure_if_any(other);
    script::Array* rotations = script::fetch_list("Default_CAT_rotation_info"); // "Default_CAT_rotation_info"
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    spin_list_ = script::new_array();
    spin_list_->configure_size_kind(6, script::kQbvalStruct);
    script::duplicate_array(spin_list_, rotations);
    script::Array* animations = script::fetch_list("Default_CAT_animation_info"); // "Default_CAT_animation_info"
    animations_ = script::new_array();
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
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
    for (script::Array* const* array : {&spin_list_, &animations_}) {
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
bool cfunc_get_create_a_trick_params(script::Struct* params, script::Script* script)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    const CreateATrick* trick = core::at(skater->trick_slots(), index).get();
    script->args().get()->put_sub_struct(kOtherParams, trick->other_params());
    script->args().get()->append_array_value(kRotationInfo, trick->spin_list());
    script->args().get()->append_array_value(kAnimationInfo, trick->animations());
    return true;
}

// 0x00551D20
bool cfunc_get_create_a_trick_other_params(script::Struct* params, script::Script* script)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script->args().get()->put_sub_struct(kOtherParams, core::at(skater->trick_slots(), index).get()->other_params());
    return true;
}

// 0x00551D80
bool cfunc_get_create_a_trick_rotations(script::Struct* params, script::Script* script)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script->args().get()->append_array_value(kRotationInfo, core::at(skater->trick_slots(), index).get()->spin_list());
    return true;
}

// 0x00551DE0
bool cfunc_get_create_a_trick_animations(script::Struct* params, script::Script* script)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script->args().get()->append_array_value(kAnimationInfo, core::at(skater->trick_slots(), index).get()->animations());
    return true;
}

// 0x00551E40
bool cfunc_set_create_a_trick_params(script::Struct* params, script::Script*)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    if (!core::at(skater->trick_slots(), 1)) {
        debug::printf("Skater has no created tricks!!\n"); // "Skater has no created tricks!!\n"
        return false;
    }
    script::Struct* other = nullptr;
    if (params->read_sub_struct(kOtherParams, &other, 1)) {
        debug::printf("Storing param info %i\n", index); // "Storing param info %i\n"
        core::at(skater->trick_slots(), index).get()->other_params()->append_structure_if_any(other);
    }
    script::Array* rotations = nullptr;
    if (params->fetch_list(kRotationInfo, &rotations, 1)) {
        debug::printf("Storing rotation info %i\n", index); // "Storing rotation info %i\n"
        script::duplicate_array(core::at(skater->trick_slots(), index).get()->spin_list(), rotations);
    }
    script::Array* animations = nullptr;
    if (params->fetch_list(kAnimationInfo, &animations, 1)) {
        debug::printf("Storing animation info %i\n", index); // "Storing animation info %i\n"
        script::duplicate_array(core::at(skater->trick_slots(), index).get()->animations(), animations);
    }
    return true;
}

// 0x00551F70
bool cfunc_set_create_a_trick_other_params(script::Struct* params, script::Script*)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script::Struct* other = nullptr;
    if (params->read_sub_struct(kOtherParams, &other, 1)) {
        debug::printf("Storing param info %i\n", index);
        core::at(skater->trick_slots(), index).get()->other_params()->append_structure_if_any(other);
    }
    return true;
}

// 0x00551FF0
bool cfunc_set_create_a_trick_rotations(script::Struct* params, script::Script*)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script::Array* rotations = nullptr;
    if (params->fetch_list(kRotationInfo, &rotations, 1)) {
        debug::printf("Storing rotation info %i\n", index);
        script::duplicate_array(core::at(skater->trick_slots(), index).get()->spin_list(), rotations);
    }
    return true;
}

// 0x00552070
bool cfunc_set_create_a_trick_animations(script::Struct* params, script::Script*)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int index = 0;
    params->fetch_int_value(kTrickIndex, &index, 1);
    script::Array* animations = nullptr;
    if (params->fetch_list(kAnimationInfo, &animations, 1)) {
        debug::printf("Storing animation info %i\n", index);
        script::duplicate_array(core::at(skater->trick_slots(), index).get()->animations(), animations);
    }
    return true;
}

namespace {

// A CAT_Set... of an integer field (the command's unnamed integer) / a flag (non-zero), and its CAT_Get... into the
// script's parameters.
bool store_int_field(script::Struct* params, int& (objects::Rider::*field)())
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int value = 0;
    params->fetch_int_value(kNoName, &value, 1);
    (skater->*field)() = value;
    return true;
}
bool fetch_int_value(script::Script* script, int (objects::Rider::*field)() const, const char* name)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    script->args().get()->put_int(name, (skater->*field)());
    return true;
}
bool set_flag(script::Struct* params, bool& (objects::Rider::*field)())
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    int value = 0;
    params->fetch_int_value(kNoName, &value, 1);
    (skater->*field)() = value != 0;
    return true;
}
bool fetch_marker(script::Script* script, bool (objects::Rider::*field)() const, const char* name)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    script->args().get()->put_int(name, (skater->*field)() ? 1 : 0);
    return true;
}

} // namespace

// 0x005520F0
bool cfunc_cat_set_num_anims(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_num_anims);
}
// 0x00552130
bool cfunc_cat_get_num_anims(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_num_anims, kNumAnimationsOn);
}
// 0x00552160
bool cfunc_cat_set_anims_done(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_anims_done);
}
// 0x005521A0
bool cfunc_cat_get_anims_done(script::Struct*, script::Script* script)
{
    return fetch_marker(script, &objects::Rider::cat_anims_done, kAnimationsDone);
}
// 0x005521F0
bool cfunc_cat_set_rots_done(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_rots_done);
}
// 0x00552230
bool cfunc_cat_get_rots_done(script::Struct*, script::Script* script)
{
    return fetch_marker(script, &objects::Rider::cat_rots_done, kRotationsDone);
}
// 0x00552280
bool cfunc_cat_set_bail_done(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_bail_done);
}
// 0x005522C0
bool cfunc_cat_get_bail_done(script::Struct*, script::Script* script)
{
    return fetch_marker(script, &objects::Rider::cat_bail_done, kBailDone);
}
// 0x00552310
bool cfunc_cat_set_flip_skater(script::Struct* params, script::Script*)
{
    return set_flag(params, &objects::Rider::cat_flip_180);
}
// 0x00552350
bool cfunc_cat_get_flip_skater(script::Struct*, script::Script* script)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    script->args().get()->put_int(kFlipSkater180, skater->cat_flip_180() ? 1 : 0);
    return true;
}
// 0x005523A0
bool cfunc_cat_set_hold_time(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    float time = 0.0f;
    params->fetch_float(kNoName, &time, 1);
    skater->set_cat_hold_time(time);
    return true;
}
// 0x005523E0
bool cfunc_cat_get_hold_time(script::Struct*, script::Script* script)
{
    const objects::Rider* skater = local_skater();
    if (!skater)
        return false;
    script->args().get()->push_float(kHoldTime, skater->cat_hold_time());
    return true;
}
// 0x00552410
bool cfunc_cat_set_total_y(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_total_y);
}
// 0x00552450
bool cfunc_cat_get_total_y(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_total_y, kTotalY);
}
// 0x00552480
bool cfunc_cat_set_total_x(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_total_x);
}
// 0x005524C0
bool cfunc_cat_get_total_x(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_total_x, kTotalX);
}
// 0x005524F0
bool cfunc_cat_set_total_z(script::Struct* params, script::Script*)
{
    return store_int_field(params, &objects::Rider::cat_total_z);
}
// 0x00552530
bool cfunc_cat_get_total_z(script::Struct*, script::Script* script)
{
    return fetch_int_value(script, &objects::Rider::cat_total_z, kTotalZ);
}

} // namespace goals
