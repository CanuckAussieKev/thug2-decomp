#pragma once

// Create-a-trick (0x00551CA0-0x005527BF): GoalSystem::CCreateATrick - one of the skater's created tricks (the CRider's
// m_trickInfo slots, +0x17C): its other parameters, rotations and animations - and the script commands that read
// and write them and the editor's state on the skater (+0x1A8-+0x1BF).

#include <cstddef>
#include <cstdint>

namespace Scripts {
class CArray;
class CScript;
class CStruct;
} // namespace Script

namespace GoalSystem {

// A created trick (0x10 bytes, vtable 0x0064DC40).
class CCreateATrick {
public:
    void** m_vtable;
    Scripts::CStruct* m_otherParams; // +0x04 (Default_CAT_other_info's to begin with)
    Scripts::CArray* m_spinList;    // +0x08 six structures (Default_CAT_rotation_info's)
    Scripts::CArray* m_animations;   // +0x0C six structures (Default_CAT_animation_info's)

    // 0x00552560: the constructor: the defaults copied. Returns this.
    CCreateATrick* Construct();
    // 0x00552720: the destructor body. 0x005527A0 (vtable slot 0): the scalar deleting destructor.
    void DestroyBody();
    CCreateATrick* Destroy(uint8_t flags);
};
static_assert(offsetof(CCreateATrick, m_animations) == 0x0C && sizeof(CCreateATrick) == 0x10,
              "layout must match the original");

// The commands (each false without a local skater). trick_index= names the created trick.
// 0x00551CA0 / 0x00551D20 / 0x00551D80 / 0x00551DE0: GetCreateATrickParams (all three) / ...OtherParams /
// ...Rotations / ...Animations into the script's parameters.
bool __cdecl CFunc_GetCreateATrickParams(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetCreateATrickOtherParams(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetCreateATrickRotations(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_GetCreateATrickAnimations(Scripts::CStruct* params, Scripts::CScript* script);
// 0x00551E40 / 0x00551F70 / 0x00551FF0 / 0x00552070: SetCreateATrickParams (all three; false if the skater has no
// created tricks) / ...OtherParams / ...Rotations / ...Animations from the command's parameters.
bool __cdecl CFunc_SetCreateATrickParams(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetCreateATrickOtherParams(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetCreateATrickRotations(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_SetCreateATrickAnimations(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005520F0-0x0055255F: the editor's state on the skater - CAT_SetNumAnims / CAT_GetNumAnims (+0x1A8
// num_animations_on), Anims / Rots / Bail done (+0x1AE / +0x1AF / +0x1AC: cat_animations_done, cat_rotations_done,
// bailtimescriptdone), FlipSkater (+0x1AD: cat_flip_skater_180), HoldTime (+0x1B0: cat_hold_time), the total
// angles Y / X / Z (+0x1B8 / +0x1B4 / +0x1BC).
bool __cdecl CFunc_CAT_SetNumAnims(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetNumAnims(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetAnimsDone(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetAnimsDone(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetRotsDone(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetRotsDone(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetBailDone(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetBailDone(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetFlipSkater(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetFlipSkater(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetHoldTime(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetHoldTime(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetTotalY(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetTotalY(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetTotalX(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetTotalX(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_SetTotalZ(Scripts::CStruct* params, Scripts::CScript* script);
bool __cdecl CFunc_CAT_GetTotalZ(Scripts::CStruct* params, Scripts::CScript* script);

} // namespace Game
