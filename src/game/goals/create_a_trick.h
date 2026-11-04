#pragma once

// Create-a-trick (0x00551CA0-0x005527BF): goals::CCreateATrick - one of the skater's created tricks (the Rider's
// trick_slots_ slots, +0x17C): its other parameters, rotations and animations - and the script commands that read
// and write them and the editor's state on the skater (+0x1A8-+0x1BF).

#include <cstddef>
#include <cstdint>

#include "engine/memory/mem.h"
#include <gsl/pointers>

namespace script {
class Array;
class Script;
class Struct;
} // namespace script

namespace goals {

// A created trick (the original's vtable 0x0064DC40 held only its destructor).
class CreateATrick : public memory::HeapObject {
public:
    // 0x00552560: the defaults copied.
    CreateATrick();
    // 0x00552720 (0x005527A0 the deleting one): the structure and arrays freed.
    ~CreateATrick();
    CreateATrick(const CreateATrick&) = delete;
    CreateATrick& operator=(const CreateATrick&) = delete;

    // The fields other code reads and sets.
    [[nodiscard]] script::Struct* other_params() const { return other_params_; }
    [[nodiscard]] script::Array* spin_list() const { return static_cast<script::Array*>(spin_list_); } // (a view: it stays the owner's)
    [[nodiscard]] script::Array* animations() const { return static_cast<script::Array*>(animations_); } // (a view: it stays the owner's)

private:
    script::Struct* other_params_; // Default_CAT_other_info's to begin with
    gsl::owner<script::Array*> spin_list_;     // six structures (Default_CAT_rotation_info's)
    gsl::owner<script::Array*> animations_;    // six structures (Default_CAT_animation_info's)
};

// The commands (each false without a local skater). trick_index= names the created trick.
// 0x00551CA0 / 0x00551D20 / 0x00551D80 / 0x00551DE0: GetCreateATrickParams (all three) / ...OtherParams /
// ...Rotations / ...Animations into the script's parameters.
bool cfunc_get_create_a_trick_params(script::Struct* params, script::Script* script);
bool cfunc_get_create_a_trick_other_params(script::Struct* params, script::Script* script);
bool cfunc_get_create_a_trick_rotations(script::Struct* params, script::Script* script);
bool cfunc_get_create_a_trick_animations(script::Struct* params, script::Script* script);
// 0x00551E40 / 0x00551F70 / 0x00551FF0 / 0x00552070: SetCreateATrickParams (all three; false if the skater has no
// created tricks) / ...OtherParams / ...Rotations / ...Animations from the command's parameters.
bool cfunc_set_create_a_trick_params(script::Struct* params, script::Script* script);
bool cfunc_set_create_a_trick_other_params(script::Struct* params, script::Script* script);
bool cfunc_set_create_a_trick_rotations(script::Struct* params, script::Script* script);
bool cfunc_set_create_a_trick_animations(script::Struct* params, script::Script* script);
// 0x005520F0-0x0055255F: the editor's state on the skater - CAT_SetNumAnims / CAT_GetNumAnims (+0x1A8
// num_animations_on), Anims / Rots / Bail done (+0x1AE / +0x1AF / +0x1AC: cat_animations_done, cat_rotations_done,
// bailtimescriptdone), FlipSkater (+0x1AD: cat_flip_skater_180), HoldTime (+0x1B0: cat_hold_time), the total
// angles Y / X / Z (+0x1B8 / +0x1B4 / +0x1BC).
bool cfunc_cat_set_num_anims(script::Struct* params, script::Script* script);
bool cfunc_cat_get_num_anims(script::Struct* params, script::Script* script);
bool cfunc_cat_set_anims_done(script::Struct* params, script::Script* script);
bool cfunc_cat_get_anims_done(script::Struct* params, script::Script* script);
bool cfunc_cat_set_rots_done(script::Struct* params, script::Script* script);
bool cfunc_cat_get_rots_done(script::Struct* params, script::Script* script);
bool cfunc_cat_set_bail_done(script::Struct* params, script::Script* script);
bool cfunc_cat_get_bail_done(script::Struct* params, script::Script* script);
bool cfunc_cat_set_flip_skater(script::Struct* params, script::Script* script);
bool cfunc_cat_get_flip_skater(script::Struct* params, script::Script* script);
bool cfunc_cat_set_hold_time(script::Struct* params, script::Script* script);
bool cfunc_cat_get_hold_time(script::Struct* params, script::Script* script);
bool cfunc_cat_set_total_y(script::Struct* params, script::Script* script);
bool cfunc_cat_get_total_y(script::Struct* params, script::Script* script);
bool cfunc_cat_set_total_x(script::Struct* params, script::Script* script);
bool cfunc_cat_get_total_x(script::Struct* params, script::Script* script);
bool cfunc_cat_set_total_z(script::Struct* params, script::Script* script);
bool cfunc_cat_get_total_z(script::Struct* params, script::Script* script);

} // namespace goals
