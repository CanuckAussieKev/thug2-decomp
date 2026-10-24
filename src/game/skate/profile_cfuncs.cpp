// Script commands of the skater profiles and the players' options (see profile_cfuncs.h).

#include "game/skate/profile_cfuncs.h"

#include <cstdint>

#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/skate/career.h"
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
constexpr uint32_t kIsClassic = 0x5A2C589A;        // is_classic
constexpr uint32_t kStatPlayWinSound = 0x5A90FB86; // stat_play_win_sound
constexpr uint32_t kAll = 0xC4E78E22;              // all
constexpr uint32_t kProfile = 0x7EA855F0;          // profile
constexpr uint32_t kPlayer = 0x67E6859A;           // player
constexpr uint32_t kSkater = 0x5B8AB877;           // skater
constexpr uint32_t kCurrentSkaterProfileIndex = 0x3C64476B; // currentSkaterProfileIndex
constexpr uint32_t kDisplayName = 0x2AB66CB8;      // display_name
constexpr uint32_t kCustom = 0x0A7BE964;           // custom (the custom skater's template)
constexpr uint32_t kCurrentPedPro = 0x09EEBCB8;    // (name unknown) a global naming the current ped pro's structure
constexpr uint32_t kPedProName = 0xF054556D;       // (name unknown) GetCurrentPedProName's result
constexpr uint32_t kOverridden = 0xC70E7415;       // overridden
constexpr uint32_t kStatValue = 0x8EAF7ADD;        // stat_value
constexpr uint32_t kIsHidden = 0x27EB9B9D;         // is_hidden

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

// 0x005AF950: AwardStatPoint [<n>] - n (1) stat points more: classic mode the career's, otherwise the current
// profile's (their points_available, 0x471FC4DD); stat_play_win_sound run.
bool __cdecl CFunc_AwardStatPoint(Script::CStruct* params, Script::CScript*)
{
    if (GameModeIsTrue(kIsClassic)) {
        Obj::CSkaterCareer* career = Career();
        int points = 1;
        params->GetInteger(0u, &points, 0);
        career->AddToInfo471FC4DD(points);
    } else {
        Obj::CSkaterProfile* profile = GetProfileFromParams(nullptr);
        int points = 1;
        params->GetInteger(0u, &points, 0);
        profile->AddToInfo471FC4DD(points);
    }
    Script::RunScript(kStatPlayWinSound, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005AF9F0: AwardSpecialTrickSlot [all | profile=<name>] - one special trick slot more for every pro (all), the
// named one, or the current profile.
bool __cdecl CFunc_AwardSpecialTrickSlot(Script::CStruct* params, Script::CScript*)
{
    Obj::CPlayerProfileManager* manager = ProfileManager();
    if (params->ContainsFlag(kAll)) {
        int count = static_cast<int>(manager->m_templates.m_count);
        for (int i = 0; i < count; ++i) // (0x00557FB0: the table's item by index)
            GameFunction<Obj::CSkaterProfile*(__thiscall*)(void*, int)>(0x00557FB0)(manager, i)->AddMaxSpecials(1);
        return true;
    }
    if (params->ContainsComponentNamed(kProfile)) {
        uint32_t name = 0; // (uninitialised in the original when not a checksum)
        params->GetChecksum(kProfile, &name, 1);
        ProfileManager()->GetTemplate(name)->AddMaxSpecials(1);
        return true;
    }
    GetProfileFromParams(nullptr)->AddMaxSpecials(1);
    return true;
}

// 0x005AFAA0: UpdateSkaterStats player=<n> - skater n's stats from profile n.
bool __cdecl CFunc_UpdateSkaterStats(Script::CStruct* params, Script::CScript*)
{
    int player = 0; // (uninitialised in the original when missing)
    params->GetInteger(kPlayer, &player, 1);
    auto* skater = static_cast<Obj::CSkater*>(GetSkater(player));
    skater->SetStatsFromProfile(GetProfile(player));
    return true;
}

// 0x005AFFE0: GetCurrentSkaterProfileIndex - the profile manager's current index as currentSkaterProfileIndex.
bool __cdecl CFunc_GetCurrentSkaterProfileIndex(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddInteger(kCurrentSkaterProfileIndex, ProfileManager()->m_currentIndex);
    return true;
}

// 0x005B0000: GetCustomSkaterName - the custom skater's display name as display_name.
bool __cdecl CFunc_GetCustomSkaterName(Script::CStruct*, Script::CScript* script)
{
    script->m_params->AddString(kDisplayName, ProfileManager()->GetTemplate(kCustom)->GetDisplayName());
    return true;
}

// 0x005B0030: GetCurrentPedProName - when the global 0x09EEBCB8 (name unknown) names a structure, that structure's
// name as 0xF054556D (name unknown); false if it is a bare name or anything else. (No null check: the global must
// exist.)
bool __cdecl CFunc_GetCurrentPedProName(Script::CStruct*, Script::CScript* script)
{
    Script::CSymbolTableEntry* entry = Script::LookUpSymbol(kCurrentPedPro);
    if (entry->type == Script::ESYMBOLTYPE_NAME || entry->type != Script::ESYMBOLTYPE_STRUCTURE) // (the original tests both)
        return false;
    script->m_params->AddChecksum(kPedProName, entry->name);
    return true;
}

// 0x005B0280: GetStatValue <stat> [skater=<name>] - stat_value: the skate module's forced stat value (+0x3D0, unless
// 0; then also the flag overridden), classic mode's career value, or the named pro's (else the current profile's)
// stat.
bool __cdecl CFunc_GetStatValue(Script::CStruct* params, Script::CScript* script)
{
    float forced = SkateField<float>(0x3D0);
    if (forced != 0.0f) {
        int value = static_cast<int>(static_cast<int64_t>(forced)); // (the original's _ftol)
        script->m_params->AddChecksum(kOverridden, kOverridden);
        script->m_params->AddInteger(kStatValue, value);
        return true;
    }
    uint32_t stat = 0; // (uninitialised in the original when missing)
    params->GetChecksum(0u, &stat, 1);
    int value;
    if (GameModeIsTrue(kIsClassic)) {
        value = Career()->GetInfoInteger(stat);
    } else {
        Obj::CPlayerProfileManager* manager = ProfileManager();
        uint32_t name;
        Obj::CSkaterProfile* profile =
            params->GetChecksum(kSkater, &name, 0) ? manager->GetTemplate(name) : manager->GetCurrentProfile();
        value = profile->GetStat(stat);
    }
    script->m_params->AddInteger(kStatValue, value);
    return true;
}

// 0x005B0360: GetNumStatPointsAvailable player=<n> - profile n's stat points (points_available).
bool __cdecl CFunc_GetNumStatPointsAvailable(Script::CStruct* params, Script::CScript* script)
{
    int player = 0; // (uninitialised in the original when missing)
    params->GetInteger(kPlayer, &player, 1);
    int points = ProfileManager()->GetProfile(player)->GetInfo471FC4DD();
    script->m_params->AddInteger("points_available", points);
    return true;
}

// 0x005B03B0: UnlockSkater name=<pro> - the pro no longer hidden (its is_hidden 0, if it was set).
bool __cdecl CFunc_UnlockSkater(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kName, &name, 1);
    Script::CStruct* info = ProfileManager()->GetTemplate(name)->InfoStructure();
    int hidden = 1;
    info->GetInteger(kIsHidden, &hidden, 1);
    if (hidden)
        info->AddInteger(kIsHidden, 0);
    return true;
}

} // namespace CFuncs

REPLACE(0x005AF950, CFuncs::CFunc_AwardStatPoint);
REPLACE(0x005AF9F0, CFuncs::CFunc_AwardSpecialTrickSlot);
REPLACE(0x005AFAA0, CFuncs::CFunc_UpdateSkaterStats);
REPLACE(0x005AFFE0, CFuncs::CFunc_GetCurrentSkaterProfileIndex);
REPLACE(0x005B0000, CFuncs::CFunc_GetCustomSkaterName);
REPLACE(0x005B0030, CFuncs::CFunc_GetCurrentPedProName);
REPLACE(0x005B0280, CFuncs::CFunc_GetStatValue);
REPLACE(0x005B0360, CFuncs::CFunc_GetNumStatPointsAvailable);
REPLACE(0x005B03B0, CFuncs::CFunc_UnlockSkater);
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
