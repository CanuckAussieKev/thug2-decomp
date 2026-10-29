#pragma once

#include <cstdint>

namespace Script {
class CStruct;
}
namespace Str {
class String;
}
namespace Mth {
class Vector;
}

// Helpers of the models and animations code: an animation's range and loop type from script parameters, a model
// file's path, the least of three, a scale from parameters, hiding every visible model for a cutscene (and showing them
// again), and the folded jump through vtable slot 4. From the disassembly of 0x00487770-0x00487D54. All cdecl but
// the hidden-models list's.
namespace Gfx {

// 0x00487770: the loop type from the flags cycle (1), pingpong (2) or wobble (3) - 0 without any. True.
bool __cdecl GetAnimLoopType(int* loopType, Script::CStruct* params);
// 0x004877E0: an animation's start and end times (seconds) from `from` / `to`: start / middle (half the duration) /
// current / end by name, or a number (frames - truncated, at 60 a second - unless `seconds`); 0 and the duration by
// default. With `backwards` the two are swapped. True.
bool __cdecl GetAnimTimes(float* start, float* end, float current, float duration, Script::CStruct* params);
// 0x004879D0: "models\<name>\<name><extension>" made in `out` (a new String) - or "models\<name>" when the name
// has a path separator. Returns out.
Str::String* __cdecl MakeModelPath(Str::String* out, const char* name, const char* extension);
// 0x00487A70: the same into `out`'s characters.
void __cdecl MakeModelFileName(const char* name, const char* extension, char* out);
// 0x00487AD0: the least of three (a NaN loses each comparison it's second in).
float __cdecl LeastOf(float a, float b, float c);
// 0x00487B00: a scale from `scale` (a float: all three; or a vector), else from x / y / z (each one given). False
// when there's none of them.
bool __cdecl GetScaleFromParams(Mth::Vector* scale, Script::CStruct* params);

// The models hidden for a cutscene (0x006FA2F0): their objects' ids.
struct SHiddenModels {
    uint32_t* mp_ids;
    int m_count;

    // 0x00487BD0: every model component's model with an object id that isn't hidden yet is hidden, its object's id
    // kept ("Hid %d objects").
    void Hide();
    // 0x00487CB0: those objects still there get their models shown again; the list freed.
    void Show();
};
// 0x00487D20 / 0x00487D30: the cutscene's (0x006FA2F0).
void __cdecl HideModelsForCutscene();
void __cdecl ShowModelsAfterCutscene();

// 0x00487D50: a tail jump to the object's vtable slot 4 (its arguments left as they are) - the textures', the fonts'
// and the geoms' calls of their slot 4.
void VirtualSlot4Jump();

} // namespace Gfx
