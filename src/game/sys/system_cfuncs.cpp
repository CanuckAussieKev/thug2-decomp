// The engine's platform and timer script commands, 0x0044E3A0-0x0044E4D4 (see system_cfuncs.h).

#include "game/sys/system_cfuncs.h"

#include <cstdint>

#include "game/mth/random.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/sys/game_config.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using Script::CScript;
using Script::CStruct;

constexpr uint32_t kPlatform = 0xC6AD2F34;    // "platform"
constexpr uint32_t kXbox = 0x87D839B8;        // "xbox" (the PC build answers as the Xbox one)
constexpr uint32_t kStartTime = 0xD16B61E6;   // "StartTime"
constexpr uint32_t kElapsedTime = 0x3EB3566B; // "ElapsedTime"

// The platform the game runs on (0x007CEFA8; 7: Windows).
int Platform() { return GameConfig::g_platform; }

GAME_GLOBAL(uint32_t, g_timerStart, 0x00696D3C);

// The stopwatch's start (Tmr::GetTime, 0x00696D3C).
uint32_t& TimerStart() { return g_timerStart; }
// The game clock (ms), as scripts see it: the clock plus its offset (0x00696D60).
int ScriptClock() { return Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60); }

} // namespace

// 0x0044E3A0: IsWIN32: whether the platform is Windows.
bool __cdecl CFunc_IsWIN32(CStruct*, CScript*) { return Platform() == 7; }

// 0x0044E3B0: GetPlatform: the script's platform=xbox. Always true.
bool __cdecl CFunc_GetPlatform(CStruct*, CScript* script)
{
    script->m_params->AddChecksum(kPlatform, kXbox);
    return true;
}

// 0x0044E3D0: Randomize: the random numbers seeded from the time. Always true.
bool __cdecl CFunc_Randomize(CStruct*, CScript*)
{
    Mth::InitialRand(Tmr::GetTime());
    return true;
}

// 0x0044E3F0: ResetTimer: the stopwatch started. Always true.
bool __cdecl CFunc_ResetTimer(CStruct*, CScript*)
{
    TimerStart() = Tmr::GetTime();
    return true;
}

// 0x0044E400: TimeGreaterThan <seconds (0)>: whether more than that has passed since ResetTimer (the milliseconds
// as unsigned; x87: seconds * 1000 against them, exactly).
bool __cdecl CFunc_TimeGreaterThan(CStruct* params, CScript*)
{
    float seconds = 0.0f;
    params->GetFloat(0u, &seconds, 0);
    uint32_t elapsed = Tmr::GetTime() - TimerStart();
    return static_cast<double>(seconds) * 1000.0 < static_cast<double>(elapsed);
}

// 0x0044E460: GetStartTime: the script clock now as the script's StartTime. Always true.
bool __cdecl CFunc_GetStartTime(CStruct*, CScript* script)
{
    script->m_params->AddInteger(kStartTime, ScriptClock());
    return true;
}

// 0x0044E490: GetElapsedTime StartTime=<ms>: the milliseconds since then as the script's ElapsedTime. Always true.
// (Original quirk: no StartTime leaves it uninitialised; here 0.)
bool __cdecl CFunc_GetElapsedTime(CStruct* params, CScript* script)
{
    int start = 0;
    params->GetInteger(kStartTime, &start, 0);
    script->m_params->AddInteger(kElapsedTime, Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - start));
    return true;
}

} // namespace CFuncs

REPLACE(0x0044E3A0, CFuncs::CFunc_IsWIN32);
REPLACE(0x0044E3B0, CFuncs::CFunc_GetPlatform);
REPLACE(0x0044E3D0, CFuncs::CFunc_Randomize);
REPLACE(0x0044E3F0, CFuncs::CFunc_ResetTimer);
REPLACE(0x0044E400, CFuncs::CFunc_TimeGreaterThan);
REPLACE(0x0044E460, CFuncs::CFunc_GetStartTime);
REPLACE(0x0044E490, CFuncs::CFunc_GetElapsedTime);
