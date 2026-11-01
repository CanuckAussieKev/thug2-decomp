// The engine's platform and timer script commands, 0x0044E3A0-0x0044E4D4 (see system_cfuncs.h).

#include "engine/sys/system_cfuncs.h"

#include <cstdint>

#include "engine/maths/random.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/sys/game_config.h"
#include "platform/stopwatch.h"
#include "engine/objects/control_pad.h"

namespace commands {

namespace {

using script::Script;
using script::Struct;

constexpr uint32_t kPlatform = 0xC6AD2F34;    // "platform"
constexpr uint32_t kXbox = 0x87D839B8;        // "xbox" (the PC build answers as the Xbox one)
constexpr uint32_t kStartTime = 0xD16B61E6;   // "StartTime"
constexpr uint32_t kElapsedTime = 0x3EB3566B; // "ElapsedTime"

// The platform the game runs on (0x007CEFA8; 7: Windows).
int os_layer() { return game_config::g_platform; }

uint32_t g_timer_start;

// The stopwatch's start (frame_clock::GetTime, 0x00696D3C).
uint32_t& timer_start() { return g_timer_start; }
// The game clock (ms), as scripts see it: the clock plus its offset (0x00696D60).
int script_clock() { return frame_clock::game_clock_elapsed() + objects::g_clock_offset; }

} // namespace

// 0x0044E3A0: IsWIN32: whether the platform is Windows.
bool cfunc_is_win32(Struct*, Script*) { return os_layer() == 7; }

// 0x0044E3B0: GetPlatform: the script's platform=xbox. Always true.
bool cfunc_get_platform(Struct*, Script* script)
{
    script->args_->put_name_key(kPlatform, kXbox);
    return true;
}

// 0x0044E3D0: Randomize: the random numbers seeded from the time. Always true.
bool cfunc_randomize(Struct*, Script*)
{
    maths::seed_random(frame_clock::get_time());
    return true;
}

// 0x0044E3F0: ResetTimer: the stopwatch started. Always true.
bool cfunc_reset_timer(Struct*, Script*)
{
    timer_start() = frame_clock::get_time();
    return true;
}

// 0x0044E400: TimeGreaterThan <seconds (0)>: whether more than that has passed since ResetTimer (the milliseconds
// as unsigned; x87: seconds * 1000 against them, exactly).
bool cfunc_time_greater_than(Struct* params, Script*)
{
    float seconds = 0.0f;
    params->fetch_float(0u, &seconds, 0);
    uint32_t elapsed = frame_clock::get_time() - timer_start();
    return static_cast<double>(seconds) * 1000.0 < static_cast<double>(elapsed);
}

// 0x0044E460: GetStartTime: the script clock now as the script's StartTime. Always true.
bool cfunc_get_start_time(Struct*, Script* script)
{
    script->args_->put_int(kStartTime, script_clock());
    return true;
}

// 0x0044E490: GetElapsedTime StartTime=<ms>: the milliseconds since then as the script's ElapsedTime. Always true.
// (Original quirk: no StartTime leaves it uninitialised; here 0.)
bool cfunc_get_elapsed_time(Struct* params, Script* script)
{
    int start = 0;
    params->fetch_int_value(kStartTime, &start, 0);
    script->args_->put_int(kElapsedTime, frame_clock::game_clock_elapsed() + (objects::g_clock_offset - start));
    return true;
}

} // namespace commands
