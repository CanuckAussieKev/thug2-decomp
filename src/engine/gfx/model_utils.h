#pragma once

#include <cstdint>

namespace script {
class Struct;
}
namespace text {
class Chars;
}
namespace maths {
class Vec4;
}

// Helpers of the models and animations code: an animation's range and loop type from script parameters, a model
// file's path, the least of three, a scale from parameters, hiding every visible model for a cutscene (and showing them
// again), and the folded jump through vtable slot 4. From the disassembly of 0x00487770-0x00487D54. All cdecl but
// the hidden-models list's.
namespace gfx {

// 0x00487770: the loop type from the flags cycle (1), pingpong (2) or wobble (3) - 0 without any. True.
bool get_anim_loop_type(int* loop_type, script::Struct* params);
// 0x004877E0: an animation's start and end times (seconds) from `from` / `to`: start / middle (half the duration) /
// current / end by name, or a number (frames - truncated, at 60 a second - unless `seconds`); 0 and the duration by
// default. With `backwards` the two are swapped. True.
bool fetch_anim_durations(float* start, float* end, float current, float duration, script::Struct* params);
// 0x004879D0: "models\<name>\<name><extension>" made in `out` (a new String) - or "models\<name>" when the name
// has a path separator. Returns out.
text::Chars* make_model_path(text::Chars* out, const char* name, const char* extension);
// 0x00487A70: the same into `out`'s characters.
void make_model_file_name(const char* name, const char* extension, char* out);
// 0x00487AD0: the least of three (a NaN loses each comparison it's second in).
float least_of(float a, float b, float c);
// 0x00487B00: a scale from `scale` (a float: all three; or a vector), else from x / y / z (each one given). False
// when there's none of them.
bool scale_from_args(maths::Vec4* scale, script::Struct* params);

// The models hidden for a cutscene (0x006FA2F0): their objects' ids.
struct HiddenModels {
    uint32_t* ids_ptr_;
    int num_;

    // 0x00487BD0: every model component's model with an object id that isn't hidden yet is hidden, its object's id
    // kept ("Hid %d objects").
    void conceal();
    // 0x00487CB0: those objects still there get their models shown again; the list freed.
    void get_show();
};
// 0x00487D20 / 0x00487D30: the cutscene's (0x006FA2F0).
void hide_models_for_cutscene();
void show_models_after_cutscene();

} // namespace gfx
