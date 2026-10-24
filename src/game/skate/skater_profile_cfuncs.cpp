// Script commands of the skater profiles and the players' options (see skater_profile_cfuncs.h).

#include "game/skate/skater_profile_cfuncs.h"

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

constexpr uint32_t kMasterSkaterList = 0xF31E8FF8; // master_skater_list
constexpr uint32_t kStatNames = 0x61EB6BA9;        // stat_names
constexpr uint32_t kPointsAvailable = 0x471FC4DD;  // points_available
constexpr uint32_t kPartial = 0x55F82F0B;          // partial
constexpr uint32_t kStatsAndTricks = 0xC164AF20;   // stats_and_tricks
constexpr uint32_t kDo = 0x62BA3F6A;               // do
constexpr uint32_t kParams = 0x7031F10C;           // params
constexpr uint32_t kAppearance = 0x554C7D6F;       // appearance
constexpr uint32_t kInfo = 0x3476CEA8;             // info
constexpr uint32_t kResetValue = 0xDD7DF50E;       // (name unknown) ResetSkaters reads it and doesn't use it
constexpr uint32_t kNodeName = 0x66780C6F;         // node_name
constexpr uint32_t kRestartWalking = 0xD6F06BF6;   // RestartWalking
constexpr uint32_t kOn = 0xF649D637;               // on
constexpr uint32_t kOff = 0xD443A2BC;              // off
constexpr uint32_t kNoFaceTexture = 0x3AF36E6D;     // NoFaceTexture

// The profile manager's templates (its table, 0x00557FB0 by index) and each one's info structure.
uint32_t TemplateCount() { return ProfileManager()->m_templates.m_count; }
Obj::CSkaterProfile* TemplateAt(uint32_t index)
{
    return GameFunction<Obj::CSkaterProfile*(__thiscall*)(void*, uint32_t)>(0x00557FB0)(ProfileManager(), index);
}
// The template named `name` (by its info's name), or null.
Obj::CSkaterProfile* FindTemplate(uint32_t name)
{
    int count = static_cast<int>(TemplateCount());
    for (int i = 0; i < count; ++i) {
        Obj::CSkaterProfile* profile = TemplateAt(i);
        uint32_t profileName = 0;
        profile->InfoStructure()->GetChecksum(kName, &profileName, 1);
        if (profileName == name)
            return profile;
    }
    return nullptr;
}
// An element of a global array of structures.
Script::CStruct* Entry(Script::CArray* list, uint32_t index)
{
    return reinterpret_cast<Script::CStruct*>(list->GetElement(index));
}
Script::CStruct* NewParams()
{
    Script::CStruct* memory = Script::NewStruct();
    return memory ? memory->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// 0x005AAEF0: RestoreTemporaryAppearance name=<id> player=<n> [NoFaceTexture] - player n's profile's appearance
// back from the temporary profile `name` (RememberTemporaryAppearance's): with NoFaceTexture only its structure
// (cleared and copied), else the whole appearance (0x004893A0).
bool __cdecl CFunc_RestoreTemporaryAppearance(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing, as the player)
    params->GetChecksum(kName, &name, 1);
    int player = 0;
    params->GetInteger(kPlayer, &player, 1);
    Obj::CPlayerProfileManager* manager = ProfileManager();
    Obj::CSkaterProfile* profile = manager->GetProfile(player);
    Obj::CSkaterProfile* remembered = manager->GetExtraProfile(name);
    if (params->ContainsFlag(kNoFaceTexture)) {
        auto* appearance = reinterpret_cast<Script::CStruct*>(profile->m_appearance + 0x4C);
        appearance->Clear();
        appearance->AppendStructureIfAny(reinterpret_cast<Script::CStruct*>(remembered->m_appearance + 0x4C));
        return true;
    }
    GameFunction<void*(__thiscall*)(void*, void*)>(0x004893A0)(profile, remembered); // (the appearance copied)
    return true;
}

// 0x005AAF90: SyncPlayer2Profile - the second player's profile copied again from its template.
bool __cdecl CFunc_SyncPlayer2Profile(Script::CStruct*, Script::CScript*)
{
    ProfileManager()->ResetSecondProfile();
    return true;
}

// 0x005ABBF0: ResetAllToDefaultStats - every skater of master_skater_list: its stats (the stat_names) and
// points_available put back into the template of its name.
bool __cdecl CFunc_ResetAllToDefaultStats(Script::CStruct*, Script::CScript*)
{
    Script::CArray* list = Script::GetArray(kMasterSkaterList);
    for (uint32_t i = 0; i < list->m_size; ++i) {
        Script::CStruct* stats = NewParams();
        Script::CStruct* entry = Entry(list, i);
        uint32_t skaterName = 0; // (uninitialised in the original when missing, as the values below)
        entry->GetChecksum(kName, &skaterName, 1);
        Script::CArray* statNames = Script::GetArray(kStatNames);
        for (uint32_t j = 0; j < statNames->m_size; ++j) {
            uint32_t statName = 0;
            Entry(statNames, j)->GetChecksum(kName, &statName, 1);
            int value = 0;
            entry->GetInteger(statName, &value, 1);
            stats->AddInteger(statName, value);
        }
        int points = 0;
        entry->GetInteger(kPointsAvailable, &points, 1);
        stats->AddInteger(kPointsAvailable, points);
        uint32_t count = TemplateCount();
        for (uint32_t k = 0; k < count; ++k) {
            Obj::CSkaterProfile* profile = TemplateAt(k);
            uint32_t profileName = 0;
            profile->InfoStructure()->GetChecksum(kName, &profileName, 1);
            if (profileName == skaterName) {
                profile->InfoStructure()->AppendStructureIfAny(stats);
                break;
            }
        }
        DeleteParams(stats);
    }
    return true;
}

// 0x005ABE30: ResetToDefaultProfile name=<skater> [partial=stats_and_tricks | partial=<other>] - the template reset
// from its master_skater_list entry: entirely, its stats and tricks only, or its info only.
bool __cdecl CFunc_ResetToDefaultProfile(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kName, &name, 1);
    Script::CArray* list = Script::GetArray(kMasterSkaterList);
    for (uint32_t i = 0; i < list->m_size; ++i) {
        Script::CStruct* entry = Entry(list, i);
        uint32_t entryName = 0;
        entry->GetChecksum(kName, &entryName, 1);
        if (entryName != name)
            continue;
        Obj::CSkaterProfile* profile = ProfileManager()->GetTemplate(name);
        uint32_t partial;
        if (params->GetChecksum(kPartial, &partial, 0)) {
            if (partial == kStatsAndTricks)
                profile->SetFromParams(entry, false, true);
            else
                profile->SetFromParams(entry, true, false);
        } else {
            profile->Reset(entry);
        }
    }
    return true;
}

// 0x005ABF00: ResetAllToDefaultProfile - every master_skater_list skater's template reset from its entry.
bool __cdecl CFunc_ResetAllToDefaultProfile(Script::CStruct*, Script::CScript*)
{
    Script::CArray* list = Script::GetArray(kMasterSkaterList);
    for (uint32_t i = 0; i < list->m_size; ++i) {
        Script::CStruct* entry = Entry(list, i);
        uint32_t name = 0;
        entry->GetChecksum(kName, &name, 1);
        ProfileManager()->GetTemplate(name)->Reset(entry);
    }
    return true;
}

// 0x005ABF70: ForEachSkaterName do=<script> [params=<s>] - the script run for every template with the parameters
// and its name.
bool __cdecl CFunc_ForEachSkaterName(Script::CStruct* params, Script::CScript*)
{
    uint32_t script = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kDo, &script, 1);
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    int count = static_cast<int>(TemplateCount());
    for (int i = 0; i < count; ++i) {
        uint32_t name = 0;
        TemplateAt(i)->InfoStructure()->GetChecksum(kName, &name, 1);
        Script::CStruct* run = NewParams();
        run->AppendStructureIfAny(scriptParams);
        run->AddChecksum(kName, name);
        Script::RunScript(script, run, nullptr, false, nullptr);
        DeleteParams(run);
    }
    return true;
}

// 0x005AC0C0: ForEachSkaterProfile do=<script> [params=<s>] - the script run for every template with the
// parameters and its whole info.
bool __cdecl CFunc_ForEachSkaterProfile(Script::CStruct* params, Script::CScript*)
{
    uint32_t script = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kDo, &script, 1);
    Script::CStruct* scriptParams = nullptr;
    params->GetStructure(kParams, &scriptParams, 0);
    int count = static_cast<int>(TemplateCount());
    for (int i = 0; i < count; ++i) {
        Script::CStruct* info = TemplateAt(i)->InfoStructure();
        Script::CStruct* run = NewParams();
        run->AppendStructureIfAny(scriptParams);
        run->AppendStructureIfAny(info);
        Script::RunScript(script, run, nullptr, false, nullptr);
        DeleteParams(run);
    }
    return true;
}

// 0x005AC1F0: GetSkaterProfileInfoByName name=<skater> - the template's info added to the script's parameters
// (false without such a template).
bool __cdecl CFunc_GetSkaterProfileInfoByName(Script::CStruct* params, Script::CScript* script)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kName, &name, 1);
    Obj::CSkaterProfile* profile = FindTemplate(name);
    if (!profile)
        return false;
    script->m_params->AppendStructureIfAny(profile->InfoStructure());
    return true;
}

// 0x005AC280: SetSkaterProfileInfoByName name=<skater> params=<s> - the parameters added to the template's info
// (false without such a template).
bool __cdecl CFunc_SetSkaterProfileInfoByName(Script::CStruct* params, Script::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->GetChecksum(kName, &name, 1);
    Script::CStruct* values = nullptr;
    params->GetStructure(kParams, &values, 1);
    Obj::CSkaterProfile* profile = FindTemplate(name);
    if (!profile)
        return false;
    profile->InfoStructure()->AppendStructureIfAny(values);
    return true;
}

// 0x005AC320: PrintCurrentAppearance [skater=<n>] - the profile's appearance printed (compiled out: the empty
// 0x005BAB90).
bool __cdecl CFunc_PrintCurrentAppearance(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = GetProfileFromParams(params);
    Script::PrintContents(reinterpret_cast<Script::CStruct*>(profile->m_appearance + 0x4C));
    return true;
}

// 0x005AC340: SetNeversoftSkater [skater=<n>] appearance=<s> info=<s> - the profile's appearance set and applied,
// the info added, its 0x09F350E5 value set.
bool __cdecl CFunc_SetNeversoftSkater(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterProfile* profile = GetProfileFromParams(params);
    Script::CStruct* appearance = nullptr; // (uninitialised in the original when missing, as the info)
    params->GetStructure(kAppearance, &appearance, 1);
    profile->SetAppearance(appearance, true);
    Script::CStruct* info = nullptr;
    params->GetStructure(kInfo, &info, 1);
    profile->InfoStructure()->AppendStructureIfAny(info);
    profile->SetValue09F350E5(1);
    return true;
}

// 0x005AC3B0: ResetSkaters [node_name=<node>] [RestartWalking] - the skate module's skaters reset at the node (its
// 0x00566980; 0xDD7DF50E - name unknown - is read and not used).
bool __cdecl CFunc_ResetSkaters(Script::CStruct* params, Script::CScript*)
{
    int unused = 0;
    params->GetInteger(kResetValue, &unused, 0);
    uint32_t node = 0;
    params->GetChecksum(kNodeName, &node, 0);
    bool walking = params->ContainsFlag(kRestartWalking);
    GameFunction<void(__thiscall*)(uint8_t*, uint32_t, bool)>(0x00566980)(Skate(), node, walking);
    return true;
}

// 0x005AC410: SetSkaterProfileInfo player=<n> params=<s> - added to profile n's info. 0x005AC470:
// GetSkaterProfileInfo player=<n> - profile n's info added to the script's parameters.
bool __cdecl CFunc_SetSkaterProfileInfo(Script::CStruct* params, Script::CScript*)
{
    int player = 0; // (uninitialised in the original when missing, as the structure)
    params->GetInteger(kPlayer, &player, 1);
    Script::CStruct* values = nullptr;
    params->GetStructure(kParams, &values, 1);
    ProfileManager()->GetProfile(player)->InfoStructure()->AppendStructureIfAny(values);
    return true;
}

bool __cdecl CFunc_GetSkaterProfileInfo(Script::CStruct* params, Script::CScript* script)
{
    int player = 0; // (uninitialised in the original when missing)
    params->GetInteger(kPlayer, &player, 1);
    script->m_params->AppendStructureIfAny(ProfileManager()->GetProfile(player)->InfoStructure());
    return true;
}

// 0x005AC4B0: SetSkaterProfileProperty player=<n> <stat> <value> - profile n's stat (SetStat's answer: false out of
// 0-10).
bool __cdecl CFunc_SetSkaterProfileProperty(Script::CStruct* params, Script::CScript*)
{
    int player = 0; // (uninitialised in the original when missing, as the stat and value)
    params->GetInteger(kPlayer, &player, 1);
    uint32_t stat = 0;
    params->GetChecksum(0u, &stat, 0);
    int value = 0;
    params->GetInteger(0u, &value, 0);
    return ProfileManager()->GetProfile(player)->SetStat(stat, value);
}

// 0x005AC520: SetClassicStat <stat> <value> - the career's info value (classic mode's stats).
bool __cdecl CFunc_SetClassicStat(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkaterCareer* career = Career();
    uint32_t stat = 0; // (uninitialised in the original when missing, as the value)
    params->GetChecksum(0u, &stat, 0);
    int value = 0;
    params->GetInteger(0u, &value, 0);
    return career->SetInfoInteger(stat, value);
}

// 0x005AC570: ToggleAlwaysSpecial [on | off] - every skater's always-special flag (+0x168) set, cleared or toggled.
bool __cdecl CFunc_ToggleAlwaysSpecial(Script::CStruct* params, Script::CScript*)
{
    bool on = params->ContainsFlag(kOn);
    bool off = params->ContainsFlag(kOff);
    for (int i = 0; i < 8; ++i) {
        Obj::CCompositeObject* skater = GetSkater(i);
        if (!skater)
            continue;
        uint8_t& special = reinterpret_cast<uint8_t*>(skater)[0x168];
        if (on)
            special = 1;
        else if (off)
            special = 0;
        else
            special = special == 0;
    }
    return true;
}

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
REPLACE(0x005ABBF0, CFuncs::CFunc_ResetAllToDefaultStats);
REPLACE(0x005ABE30, CFuncs::CFunc_ResetToDefaultProfile);
REPLACE(0x005ABF00, CFuncs::CFunc_ResetAllToDefaultProfile);
REPLACE(0x005ABF70, CFuncs::CFunc_ForEachSkaterName);
REPLACE(0x005AC0C0, CFuncs::CFunc_ForEachSkaterProfile);
REPLACE(0x005AC1F0, CFuncs::CFunc_GetSkaterProfileInfoByName);
REPLACE(0x005AC280, CFuncs::CFunc_SetSkaterProfileInfoByName);
REPLACE(0x005AC320, CFuncs::CFunc_PrintCurrentAppearance);
REPLACE(0x005AC340, CFuncs::CFunc_SetNeversoftSkater);
REPLACE(0x005AC3B0, CFuncs::CFunc_ResetSkaters);
REPLACE(0x005AC410, CFuncs::CFunc_SetSkaterProfileInfo);
REPLACE(0x005AC470, CFuncs::CFunc_GetSkaterProfileInfo);
REPLACE(0x005AC4B0, CFuncs::CFunc_SetSkaterProfileProperty);
REPLACE(0x005AC520, CFuncs::CFunc_SetClassicStat);
REPLACE(0x005AC570, CFuncs::CFunc_ToggleAlwaysSpecial);
REPLACE(0x005AAEF0, CFuncs::CFunc_RestoreTemporaryAppearance);
REPLACE(0x005AAF90, CFuncs::CFunc_SyncPlayer2Profile);
