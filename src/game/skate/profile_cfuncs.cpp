// Script commands of the skater profiles and the players' options (see profile_cfuncs.h).

#include "game/skate/profile_cfuncs.h"

#include <cstdint>

#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace CFuncs {

namespace {

using namespace SkateCalls;

constexpr uint32_t kString = 0x61414D56; // string
constexpr uint32_t kName = 0xA1DC81F9;   // name

// The players' options: the skate module's table at +0x394 (8 bytes a player: vibration, auto kick, spin taps), set
// through 0x00567C30 / 0x00567C00 / 0x00567BD0 (player, on).
enum Option : uint32_t { kVibration = 0, kAutoKick = 1, kSpinTaps = 2 };
constexpr uint32_t kSetOption[] = {0x00567C30, 0x00567C00, 0x00567BD0};
// The player a command names (its unnamed integer; 0).
int PlayerOf(Script::CStruct* params)
{
    int player = 0;
    params->GetInteger(0u, &player, 0);
    return player;
}
bool GetOption(Script::CStruct* params, Option option)
{
    return SkateField<bool>(0x394 + PlayerOf(params) * 8 + option);
}
bool SetOption(Script::CStruct* params, Option option, bool on)
{
    GameFunction<void(__thiscall*)(uint8_t*, int, bool)>(kSetOption[option])(Skate(), PlayerOf(params), on);
    return true;
}

Obj::CPlayerProfileManager* ProfileManager() { return GameVariable<Obj::CPlayerProfileManager*>(0x007CE4B4); }

} // namespace

// 0x005AE370 ... 0x005AE4F0: VibrationIsOn / VibrationOn / VibrationOff, AutoKickIsOn / On / Off, SpinTapsAreOn /
// On / Off [<player>].
bool __cdecl CFunc_VibrationIsOn(Script::CStruct* params, Script::CScript*) { return GetOption(params, kVibration); }
bool __cdecl CFunc_VibrationOn(Script::CStruct* params, Script::CScript*) { return SetOption(params, kVibration, true); }
bool __cdecl CFunc_VibrationOff(Script::CStruct* params, Script::CScript*) { return SetOption(params, kVibration, false); }
bool __cdecl CFunc_AutoKickIsOn(Script::CStruct* params, Script::CScript*) { return GetOption(params, kAutoKick); }
bool __cdecl CFunc_AutoKickOn(Script::CStruct* params, Script::CScript*) { return SetOption(params, kAutoKick, true); }
bool __cdecl CFunc_AutoKickOff(Script::CStruct* params, Script::CScript*) { return SetOption(params, kAutoKick, false); }
bool __cdecl CFunc_SpinTapsAreOn(Script::CStruct* params, Script::CScript*) { return GetOption(params, kSpinTaps); }
bool __cdecl CFunc_SpinTapsOn(Script::CStruct* params, Script::CScript*) { return SetOption(params, kSpinTaps, true); }
bool __cdecl CFunc_SpinTapsOff(Script::CStruct* params, Script::CScript*) { return SetOption(params, kSpinTaps, false); }

// 0x005AE520: GetCurrentProDisplayInfo - the current profile's display name as string.
bool __cdecl CFunc_GetCurrentProDisplayInfo(Script::CStruct*, Script::CScript* script)
{
    Obj::CSkaterProfile* profile = GetProfileFromParams(nullptr);
    script->m_params->AddString(kString, Script::ESYMBOLTYPE_STRING, profile->GetDisplayName());
    return true;
}

// 0x005AE900: SelectCurrentSkater name=<skater> (or the unnamed checksum) - the profile manager's current skater
// (false if it already is); the skater of the current index given its looks.
bool __cdecl CFunc_SelectCurrentSkater(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    if (!params->GetChecksum(kName, &name, 0))
        params->GetChecksum(0u, &name, 1);
    Obj::CPlayerProfileManager* manager = ProfileManager();
    if (GetProfileFromParams(nullptr)->GetName() == name)
        return false;
    manager->SetCurrentSkater(name);
    Obj::CSkaterProfile* profile = GetProfileFromParams(nullptr);
    auto* skater = static_cast<Obj::CSkater*>(GetSkater(manager->m_currentIndex));
    if (skater)
        skater->UpdateFromProfile(profile);
    return true;
}

} // namespace CFuncs

REPLACE(0x005AE370, CFuncs::CFunc_VibrationIsOn);
REPLACE(0x005AE3A0, CFuncs::CFunc_VibrationOn);
REPLACE(0x005AE3D0, CFuncs::CFunc_VibrationOff);
REPLACE(0x005AE400, CFuncs::CFunc_AutoKickIsOn);
REPLACE(0x005AE430, CFuncs::CFunc_AutoKickOn);
REPLACE(0x005AE460, CFuncs::CFunc_AutoKickOff);
REPLACE(0x005AE490, CFuncs::CFunc_SpinTapsAreOn);
REPLACE(0x005AE4C0, CFuncs::CFunc_SpinTapsOn);
REPLACE(0x005AE4F0, CFuncs::CFunc_SpinTapsOff);
REPLACE(0x005AE520, CFuncs::CFunc_GetCurrentProDisplayInfo);
REPLACE(0x005AE900, CFuncs::CFunc_SelectCurrentSkater);
