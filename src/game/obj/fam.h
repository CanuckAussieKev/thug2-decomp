#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script

namespace Obj {

// The fam component (type 0x0301D67C "fam", vtable 0x00645F8C, 0x20 bytes; made by 0x0041D200): plays a "fam" on its
// object - a stream (a voice line) with the facial animation that goes with it (fam\<checksum>.fam, loaded into the
// object's anim set and played through an anim controller - partialAnim jawRotation), run by the script
// fam_play_stream spawned on the object.
//
// Vtable: 0 the scalar deleting destructor; the rest the base's / shared (1 0x0051B970, 2 / 8 / 10 0x005BAB90, 3 / 4 /
// 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 0x0045D760, 11 0x0042BB20, 12 0x0051ADC0).
class CFamComponent : public CBaseComponent {
public:
    static constexpr uint32_t kType = 0x0301D67C;

    // 0x0041D200 (registered with the composite object manager by 0x004E4860): a new one (the constructor inlined: the
    // base's, its vtable and type, no fam, no stream; 0 if out of memory).
    static CBaseComponent* __cdecl Create();
    // 0x0041D170 (not a Ghidra function; registered with Create): its member functions' table (0x006836E8; the
    // functions are put in by the static initialiser at 0x00642B30).
    static void __cdecl RegisterMemberFunctions();
    // 0x0041D190: the base's body.
    void DestroyBody();
    // 0x0041D1A0 (vtable slot 0)
    CFamComponent* Destroy(unsigned int flags);

    // 0x0041D2C0: the loaded fam's scripts (fam_play_stream) stopped, its anim controller removed from the animation
    // blender (RemoveAnimController type=partialAnim id=jawRotation), the asset unloaded; none loaded.
    void UnloadFam();
    // 0x0041D3E0: UnloadFam, then fam\<name>.fam loaded as an animation (Ass::CAssMan::LoadAnim with `name` and
    // `animSet`; the asset manager's +0x18 kept across it); loaded: its checksum kept.
    void LoadFam(uint32_t name, uint32_t animSet);
    // 0x0041D480: false if `name` isn't a stream. The last fam's stream stopped (and its scripts); then fam_play_stream
    // spawned on the object with stream_checksum = name, vol (when not 0) and - unless `streamOnly` - use_pos_info = 0,
    // the fam loaded into the anim set, the stream preloaded (`priority`; its id as streamId, kept) and the unnamed
    // flag play_anim when the fam's animation is there. True.
    bool PlayFam(uint32_t name, bool streamOnly, int volume, int priority);

    // --- the member functions scripts call (the table at 0x006836E8) ---
    // 0x0041D660 [Obj_PlayFam 0xE5894C80]: PlayFam(the unnamed checksum (asserted), false, vol (0), priority (0x66)).
    int Obj_PlayFam(Script::CStruct* params, Script::CScript* script);
    // 0x0041D280 [Obj_StopLastFam 0x48AC2139]: the last fam's stream stopped (and its scripts). 1.
    int Obj_StopLastFam(Script::CStruct* params, Script::CScript* script);

    uint32_t m_fam;    // +0x18 the loaded fam's asset checksum (0: none)
    uint32_t m_stream; // +0x1C the stream's id (0: none)
};
static_assert(offsetof(CFamComponent, m_fam) == 0x18 && offsetof(CFamComponent, m_stream) == 0x1C &&
                  sizeof(CFamComponent) == 0x20,
              "layout must match the original");

} // namespace Obj
