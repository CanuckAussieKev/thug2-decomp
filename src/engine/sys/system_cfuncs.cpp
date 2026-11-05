// The engine's platform and timer script commands, 0x0044E3A0-0x0044E4D4 (see system_cfuncs.h).

#include "engine/sys/system_cfuncs.h"

#include <cstdint>

#include "base/maths/random.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "engine/sys/game_clock.h"
#include "engine/sys/game_config.h"
#include "engine/sys/stopwatch.h"
#include "engine/objects/control_pad.h"

namespace commands {

namespace {

constexpr double kMillisecondsPerSecond = 1000.0;

using script::Context;
using script::Struct;

constexpr uint32_t kPlatform = 0xC6AD2F34;    // "platform"
constexpr uint32_t kXbox = 0x87D839B8;        // "xbox" (the PC build answers as the Xbox one)
constexpr uint32_t kStartTime = 0xD16B61E6;   // "StartTime"
constexpr uint32_t kElapsedTime = 0x3EB3566B; // "ElapsedTime"

// The platform the game runs on (0x007CEFA8; 7: Windows).
int os_layer() { return game_config::hardware(); }

// The stopwatch's start (frame_clock::GetTime, 0x00696D3C).
uint32_t& timer_start()
{
    static uint32_t timer_start{};
    return timer_start;
}
// The game clock (ms), as scripts see it: the clock plus its offset (0x00696D60).
int script_clock() { return frame_clock::game_clock_elapsed() + objects::clock_offset(); }

} // namespace

// 0x0044E3A0: IsWIN32: whether the platform is Windows.
bool cfunc_is_win32(Struct* /*params*/, Context /*context*/) { return os_layer() == game_config::kHardwareWin32; }

// 0x0044E3B0: GetPlatform: the script's platform=xbox. Always true.
bool cfunc_get_platform(Struct* /*params*/, Context context)
{
    context.args()->put_name_key(checksums::Name{kPlatform}, kXbox);
    return true;
}

// 0x0044E3D0: Randomize: the random numbers seeded from the time. Always true.
bool cfunc_randomize(Struct* /*params*/, Context /*context*/)
{
    maths::seed_random(frame_clock::get_time());
    return true;
}

// 0x0044E3F0: ResetTimer: the stopwatch started. Always true.
bool cfunc_reset_timer(Struct* /*params*/, Context /*context*/)
{
    timer_start() = frame_clock::get_time();
    return true;
}

// 0x0044E400: TimeGreaterThan <seconds (0)>: whether more than that has passed since ResetTimer (the milliseconds
// as unsigned; x87: seconds * 1000 against them, exactly).
bool cfunc_time_greater_than(Struct* params, Context /*context*/)
{
    float seconds = 0.0F;
    params->fetch_float(0U, &seconds, 0);
    const uint32_t elapsed = frame_clock::get_time() - timer_start();
    return static_cast<double>(seconds) * kMillisecondsPerSecond < static_cast<double>(elapsed);
}

// 0x0044E460: GetStartTime: the script clock now as the script's StartTime. Always true.
bool cfunc_get_start_time(Struct* /*params*/, Context context)
{
    context.args()->put_int(checksums::Name{kStartTime}, script_clock());
    return true;
}

// 0x0044E490: GetElapsedTime StartTime=<ms>: the milliseconds since then as the script's ElapsedTime. Always true.
// (Original quirk: no StartTime leaves it uninitialised; here 0.)
bool cfunc_get_elapsed_time(Struct* params, Context context)
{
    int start = 0;
    params->fetch_int_value(kStartTime, &start, 0);
    context.args()->put_int(checksums::Name{kElapsedTime},
                            frame_clock::game_clock_elapsed() + (objects::clock_offset() - start));
    return true;
}

} // namespace commands
