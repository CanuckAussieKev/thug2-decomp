#pragma once

namespace script {
class Context;
class Struct;
} // namespace script

// Script commands (CFunctions) for maths, 0x00448AE0-0x00448EF0: trigonometry in degrees, square roots, vector
// normalising and rotating, linear mapping and clamping. Each reads its arguments from `params` and leaves its
// result in the calling script's parameters (CScript::args_) under a fixed name. All are cdecl (params,
// script) and return true. A missing asserted argument leaves its value undefined in the original (the getter's
// compiled-out assert); here it is 0.
namespace commands {

bool cfunc_sin(script::Struct* params, script::Context context);              // 0x00448AE0
bool cfunc_cos(script::Struct* params, script::Context context);              // 0x00448B20
bool cfunc_tan(script::Struct* params, script::Context context);              // 0x00448B60
bool cfunc_a_sin(script::Struct* params, script::Context context);            // 0x00448BA0
bool cfunc_a_cos(script::Struct* params, script::Context context);            // 0x00448BE0
bool cfunc_a_tan(script::Struct* params, script::Context context);            // 0x00448C20
bool cfunc_sqrt(script::Struct* params, script::Context context);             // 0x00448C60
bool cfunc_normalize_vector(script::Struct* params, script::Context context); // 0x00448CA0
bool cfunc_linear_map(script::Struct* params, script::Context context);       // 0x00448D70
bool cfunc_clamp(script::Struct* params, script::Context context);            // 0x00448E60
bool cfunc_rotate_vector(script::Struct* params, script::Context context);    // 0x00448EF0

inline constexpr float kScriptRadiansToDegrees{57.295776F}; // 0x00645468

} // namespace commands
