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
#include "game/gfx/model_appearance.h"
#include "game/skate/skate_module.h"
#include "game/goals/goal_manager.h"

namespace ScriptCommands {

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
// The player a command names (its unnamed integer; 0).
int PlayerOf(Scripts::CStruct* params)
{
    int player = 0;
    params->FetchIntValue(0u, &player, 0);
    return player;
}
bool GetOption(Scripts::CStruct* params, Option option)
{
    return SkateField<bool>(0x394 + PlayerOf(params) * 8 + option);
}
bool SetOption(Scripts::CStruct* params, Option option, bool on)
{
    int player = PlayerOf(params);
    GameModules::Skate* skate = GameModules::Skate::Sole();
    switch (option) {
    case kVibration:
        skate->ChangeRumble(player, on); // 0x00567C30
        break;
    case kAutoKick:
        skate->SetAutoEject(player, on); // 0x00567C00
        break;
    case kSpinTaps:
        skate->SetTurnTapCount(player, on); // 0x00567BD0
        break;
    }
    return true;
}

GameObjects::CProfileRoster* ProfileManager() { return GameObjects::g_profileManager; }

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
uint32_t TemplateCount() { return ProfileManager()->m_templates.m_num; }
GameObjects::CRiderIdentity* TemplateAt(uint32_t index)
{
    // (0x00557FB0: the lookup table's by-index code, shared with the goal manager's)
    return reinterpret_cast<GameObjects::CRiderIdentity*>(
        reinterpret_cast<GoalSystem::CGoalManager*>(ProfileManager())->GetObjectiveAt(static_cast<int>(index)));
}
// The template named `name` (by its info's name), or null.
GameObjects::CRiderIdentity* FindTemplate(uint32_t name)
{
    int count = static_cast<int>(TemplateCount());
    for (int i = 0; i < count; ++i) {
        GameObjects::CRiderIdentity* profile = TemplateAt(i);
        uint32_t profileName = 0;
        profile->InfoStructure()->ReadHashValue(kName, &profileName, 1);
        if (profileName == name)
            return profile;
    }
    return nullptr;
}
// An element of a global array of structures.
Scripts::CStruct* TableRow(Scripts::CArray* list, uint32_t index)
{
    return reinterpret_cast<Scripts::CStruct*>(list->FetchPart(index));
}
Scripts::CStruct* NewParams()
{
    Scripts::CStruct* memory = Scripts::NewStruct();
    return memory ? memory->PrepareAll() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x005AAEF0: RestoreTemporaryAppearance name=<id> player=<n> [NoFaceTexture] - player n's profile's appearance
// back from the temporary profile `name` (RememberTemporaryAppearance's): with NoFaceTexture only its structure
// (cleared and copied), else the whole appearance (0x004893A0).
bool __cdecl CFunc_RestoreTemporaryAppearance(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing, as the player)
    params->ReadHashValue(kName, &name, 1);
    int player = 0;
    params->FetchIntValue(kPlayer, &player, 1);
    GameObjects::CProfileRoster* manager = ProfileManager();
    GameObjects::CRiderIdentity* profile = manager->FetchAccount(player);
    GameObjects::CRiderIdentity* remembered = manager->GetExtraProfile(name);
    if (params->HasBareFlag(kNoFaceTexture)) {
        auto* appearance = reinterpret_cast<Scripts::CStruct*>(profile->m_lookData + 0x4C);
        appearance->Clear();
        appearance->AppendStructureIfAny(reinterpret_cast<Scripts::CStruct*>(remembered->m_lookData + 0x4C));
        return true;
    }
    reinterpret_cast<Gfx2D::CModelLook*>(profile)->CopyFrom(
        reinterpret_cast<Gfx2D::CModelLook*>(remembered)); // (the appearance copied)
    return true;
}

// 0x005AAF90: SyncPlayer2Profile - the second player's profile copied again from its template.
bool __cdecl CFunc_SyncPlayer2Profile(Scripts::CStruct*, Scripts::CScript*)
{
    ProfileManager()->ResetSecondProfile();
    return true;
}

// 0x005ABBF0: ResetAllToDefaultStats - every skater of master_skater_list: its stats (the stat_names) and
// points_available put back into the template of its name.
bool __cdecl CFunc_ResetAllToDefaultStats(Scripts::CStruct*, Scripts::CScript*)
{
    Scripts::CArray* list = Scripts::FetchList(kMasterSkaterList);
    for (uint32_t i = 0; i < list->m_extent; ++i) {
        Scripts::CStruct* stats = NewParams();
        Scripts::CStruct* entry = TableRow(list, i);
        uint32_t skaterName = 0; // (uninitialised in the original when missing, as the values below)
        entry->ReadHashValue(kName, &skaterName, 1);
        Scripts::CArray* statNames = Scripts::FetchList(kStatNames);
        for (uint32_t j = 0; j < statNames->m_extent; ++j) {
            uint32_t statName = 0;
            TableRow(statNames, j)->ReadHashValue(kName, &statName, 1);
            int value = 0;
            entry->FetchIntValue(statName, &value, 1);
            stats->PutInt(statName, value);
        }
        int points = 0;
        entry->FetchIntValue(kPointsAvailable, &points, 1);
        stats->PutInt(kPointsAvailable, points);
        uint32_t count = TemplateCount();
        for (uint32_t k = 0; k < count; ++k) {
            GameObjects::CRiderIdentity* profile = TemplateAt(k);
            uint32_t profileName = 0;
            profile->InfoStructure()->ReadHashValue(kName, &profileName, 1);
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
bool __cdecl CFunc_ResetToDefaultProfile(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kName, &name, 1);
    Scripts::CArray* list = Scripts::FetchList(kMasterSkaterList);
    for (uint32_t i = 0; i < list->m_extent; ++i) {
        Scripts::CStruct* entry = TableRow(list, i);
        uint32_t entryName = 0;
        entry->ReadHashValue(kName, &entryName, 1);
        if (entryName != name)
            continue;
        GameObjects::CRiderIdentity* profile = ProfileManager()->GetTemplate(name);
        uint32_t partial;
        if (params->ReadHashValue(kPartial, &partial, 0)) {
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
bool __cdecl CFunc_ResetAllToDefaultProfile(Scripts::CStruct*, Scripts::CScript*)
{
    Scripts::CArray* list = Scripts::FetchList(kMasterSkaterList);
    for (uint32_t i = 0; i < list->m_extent; ++i) {
        Scripts::CStruct* entry = TableRow(list, i);
        uint32_t name = 0;
        entry->ReadHashValue(kName, &name, 1);
        ProfileManager()->GetTemplate(name)->Reset(entry);
    }
    return true;
}

// 0x005ABF70: ForEachSkaterName do=<script> [params=<s>] - the script run for every template with the parameters
// and its name.
bool __cdecl CFunc_ForEachSkaterName(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t script = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kDo, &script, 1);
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    int count = static_cast<int>(TemplateCount());
    for (int i = 0; i < count; ++i) {
        uint32_t name = 0;
        TemplateAt(i)->InfoStructure()->ReadHashValue(kName, &name, 1);
        Scripts::CStruct* run = NewParams();
        run->AppendStructureIfAny(scriptParams);
        run->PutNameKey(kName, name);
        Scripts::LaunchScript(script, run, nullptr, false, nullptr);
        DeleteParams(run);
    }
    return true;
}

// 0x005AC0C0: ForEachSkaterProfile do=<script> [params=<s>] - the script run for every template with the
// parameters and its whole info.
bool __cdecl CFunc_ForEachSkaterProfile(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t script = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kDo, &script, 1);
    Scripts::CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParams, &scriptParams, 0);
    int count = static_cast<int>(TemplateCount());
    for (int i = 0; i < count; ++i) {
        Scripts::CStruct* info = TemplateAt(i)->InfoStructure();
        Scripts::CStruct* run = NewParams();
        run->AppendStructureIfAny(scriptParams);
        run->AppendStructureIfAny(info);
        Scripts::LaunchScript(script, run, nullptr, false, nullptr);
        DeleteParams(run);
    }
    return true;
}

// 0x005AC1F0: GetSkaterProfileInfoByName name=<skater> - the template's info added to the script's parameters
// (false without such a template).
bool __cdecl CFunc_GetSkaterProfileInfoByName(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kName, &name, 1);
    GameObjects::CRiderIdentity* profile = FindTemplate(name);
    if (!profile)
        return false;
    script->m_args->AppendStructureIfAny(profile->InfoStructure());
    return true;
}

// 0x005AC280: SetSkaterProfileInfoByName name=<skater> params=<s> - the parameters added to the template's info
// (false without such a template).
bool __cdecl CFunc_SetSkaterProfileInfoByName(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kName, &name, 1);
    Scripts::CStruct* values = nullptr;
    params->ReadSubStruct(kParams, &values, 1);
    GameObjects::CRiderIdentity* profile = FindTemplate(name);
    if (!profile)
        return false;
    profile->InfoStructure()->AppendStructureIfAny(values);
    return true;
}

// 0x005AC320: PrintCurrentAppearance [skater=<n>] - the profile's appearance printed (compiled out: the empty
// 0x005BAB90).
bool __cdecl CFunc_PrintCurrentAppearance(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = GetProfileFromParams(params);
    Scripts::DumpToLog(reinterpret_cast<Scripts::CStruct*>(profile->m_lookData + 0x4C));
    return true;
}

// 0x005AC340: SetNeversoftSkater [skater=<n>] appearance=<s> info=<s> - the profile's appearance set and applied,
// the info added, its 0x09F350E5 value set.
bool __cdecl CFunc_SetNeversoftSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderIdentity* profile = GetProfileFromParams(params);
    Scripts::CStruct* appearance = nullptr; // (uninitialised in the original when missing, as the info)
    params->ReadSubStruct(kAppearance, &appearance, 1);
    profile->SetAppearance(appearance, true);
    Scripts::CStruct* info = nullptr;
    params->ReadSubStruct(kInfo, &info, 1);
    profile->InfoStructure()->AppendStructureIfAny(info);
    profile->SetValue09F350E5(1);
    return true;
}

// 0x005AC3B0: ResetSkaters [node_name=<node>] [RestartWalking] - the skate module's skaters reset at the node (its
// 0x00566980; 0xDD7DF50E - name unknown - is read and not used).
bool __cdecl CFunc_ResetSkaters(Scripts::CStruct* params, Scripts::CScript*)
{
    int unused = 0;
    params->FetchIntValue(kResetValue, &unused, 0);
    uint32_t node = 0;
    params->ReadHashValue(kNodeName, &node, 0);
    bool walking = params->HasBareFlag(kRestartWalking);
    GameModules::Skate::Sole()->ResetSkaters(node, walking);
    return true;
}

// 0x005AC410: SetSkaterProfileInfo player=<n> params=<s> - added to profile n's info. 0x005AC470:
// GetSkaterProfileInfo player=<n> - profile n's info added to the script's parameters.
bool __cdecl CFunc_SetSkaterProfileInfo(Scripts::CStruct* params, Scripts::CScript*)
{
    int player = 0; // (uninitialised in the original when missing, as the structure)
    params->FetchIntValue(kPlayer, &player, 1);
    Scripts::CStruct* values = nullptr;
    params->ReadSubStruct(kParams, &values, 1);
    ProfileManager()->FetchAccount(player)->InfoStructure()->AppendStructureIfAny(values);
    return true;
}

bool __cdecl CFunc_GetSkaterProfileInfo(Scripts::CStruct* params, Scripts::CScript* script)
{
    int player = 0; // (uninitialised in the original when missing)
    params->FetchIntValue(kPlayer, &player, 1);
    script->m_args->AppendStructureIfAny(ProfileManager()->FetchAccount(player)->InfoStructure());
    return true;
}

// 0x005AC4B0: SetSkaterProfileProperty player=<n> <stat> <value> - profile n's stat (SetAbilityLevel's answer: false out of
// 0-10).
bool __cdecl CFunc_SetSkaterProfileProperty(Scripts::CStruct* params, Scripts::CScript*)
{
    int player = 0; // (uninitialised in the original when missing, as the stat and value)
    params->FetchIntValue(kPlayer, &player, 1);
    uint32_t stat = 0;
    params->ReadHashValue(0u, &stat, 0);
    int value = 0;
    params->FetchIntValue(0u, &value, 0);
    return ProfileManager()->FetchAccount(player)->SetAbilityLevel(stat, value);
}

// 0x005AC520: SetClassicStat <stat> <value> - the career's info value (classic mode's stats).
bool __cdecl CFunc_SetClassicStat(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRiderStoryProgress* career = Career();
    uint32_t stat = 0; // (uninitialised in the original when missing, as the value)
    params->ReadHashValue(0u, &stat, 0);
    int value = 0;
    params->FetchIntValue(0u, &value, 0);
    return career->SetInfoInteger(stat, value);
}

// 0x005AC570: ToggleAlwaysSpecial [on | off] - every skater's always-special flag (+0x168) set, cleared or toggled.
bool __cdecl CFunc_ToggleAlwaysSpecial(Scripts::CStruct* params, Scripts::CScript*)
{
    bool on = params->HasBareFlag(kOn);
    bool off = params->HasBareFlag(kOff);
    for (int i = 0; i < 8; ++i) {
        GameObjects::CGameEntity* skater = GetRiderAt(i);
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
bool __cdecl CFunc_VibrationIsOn(Scripts::CStruct* params, Scripts::CScript*) { return GetOption(params, kVibration); }
bool __cdecl CFunc_VibrationOn(Scripts::CStruct* params, Scripts::CScript*) { return SetOption(params, kVibration, true); }
bool __cdecl CFunc_VibrationOff(Scripts::CStruct* params, Scripts::CScript*) { return SetOption(params, kVibration, false); }
bool __cdecl CFunc_AutoKickIsOn(Scripts::CStruct* params, Scripts::CScript*) { return GetOption(params, kAutoKick); }
bool __cdecl CFunc_AutoKickOn(Scripts::CStruct* params, Scripts::CScript*) { return SetOption(params, kAutoKick, true); }
bool __cdecl CFunc_AutoKickOff(Scripts::CStruct* params, Scripts::CScript*) { return SetOption(params, kAutoKick, false); }
bool __cdecl CFunc_SpinTapsAreOn(Scripts::CStruct* params, Scripts::CScript*) { return GetOption(params, kSpinTaps); }
bool __cdecl CFunc_SpinTapsOn(Scripts::CStruct* params, Scripts::CScript*) { return SetOption(params, kSpinTaps, true); }
bool __cdecl CFunc_SpinTapsOff(Scripts::CStruct* params, Scripts::CScript*) { return SetOption(params, kSpinTaps, false); }

// 0x005AE520: GetCurrentProDisplayInfo - the current profile's display name as string.
bool __cdecl CFunc_GetCurrentProDisplayInfo(Scripts::CStruct*, Scripts::CScript* script)
{
    GameObjects::CRiderIdentity* profile = GetProfileFromParams(nullptr);
    script->m_args->AppendWord(kString, Scripts::QBVAL_STRING, profile->FetchShownName());
    return true;
}

// 0x005AE900: SelectCurrentSkater name=<skater> (or the unnamed checksum) - the profile manager's current skater
// (false if it already is); the skater of the current index given its looks.
bool __cdecl CFunc_SelectCurrentSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    if (!params->ReadHashValue(kName, &name, 0))
        params->ReadHashValue(0u, &name, 1);
    GameObjects::CProfileRoster* manager = ProfileManager();
    if (GetProfileFromParams(nullptr)->FetchTitle() == name)
        return false;
    manager->SetCurrentSkater(name);
    GameObjects::CRiderIdentity* profile = GetProfileFromParams(nullptr);
    auto* skater = static_cast<GameObjects::CRider*>(GetRiderAt(manager->m_cursorPos));
    if (skater)
        skater->UpdateFromProfile(profile);
    return true;
}

// 0x005AF950: AwardStatPoint [<n>] - n (1) stat points more: classic mode the career's, otherwise the current
// profile's (their points_available, 0x471FC4DD); stat_play_win_sound run.
bool __cdecl CFunc_AwardStatPoint(Scripts::CStruct* params, Scripts::CScript*)
{
    if (GameModeIsTrue(kIsClassic)) {
        GameObjects::CRiderStoryProgress* career = Career();
        int points = 1;
        params->FetchIntValue(0u, &points, 0);
        career->AddToInfo471FC4DD(points);
    } else {
        GameObjects::CRiderIdentity* profile = GetProfileFromParams(nullptr);
        int points = 1;
        params->FetchIntValue(0u, &points, 0);
        profile->AddToInfo471FC4DD(points);
    }
    Scripts::LaunchScript(kStatPlayWinSound, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005AF9F0: AwardSpecialTrickSlot [all | profile=<name>] - one special trick slot more for every pro (all), the
// named one, or the current profile.
bool __cdecl CFunc_AwardSpecialTrickSlot(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CProfileRoster* manager = ProfileManager();
    if (params->HasBareFlag(kAll)) {
        int count = static_cast<int>(manager->m_templates.m_num);
        for (int i = 0; i < count; ++i) // (0x00557FB0: the table's item by index)
            reinterpret_cast<GameObjects::CRiderIdentity*>(reinterpret_cast<GoalSystem::CGoalManager*>(manager)->GetObjectiveAt(i))
                ->AddMaxSpecials(1);
        return true;
    }
    if (params->HasEntryNamed(kProfile)) {
        uint32_t name = 0; // (uninitialised in the original when not a checksum)
        params->ReadHashValue(kProfile, &name, 1);
        ProfileManager()->GetTemplate(name)->AddMaxSpecials(1);
        return true;
    }
    GetProfileFromParams(nullptr)->AddMaxSpecials(1);
    return true;
}

// 0x005AFAA0: UpdateSkaterStats player=<n> - skater n's stats from profile n.
bool __cdecl CFunc_UpdateSkaterStats(Scripts::CStruct* params, Scripts::CScript*)
{
    int player = 0; // (uninitialised in the original when missing)
    params->FetchIntValue(kPlayer, &player, 1);
    auto* skater = static_cast<GameObjects::CRider*>(GetRiderAt(player));
    skater->SetStatsFromProfile(FetchAccount(player));
    return true;
}

// 0x005AFFE0: GetCurrentSkaterProfileIndex - the profile manager's current index as currentSkaterProfileIndex.
bool __cdecl CFunc_GetCurrentSkaterProfileIndex(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->PutInt(kCurrentSkaterProfileIndex, ProfileManager()->m_cursorPos);
    return true;
}

// 0x005B0000: GetCustomSkaterName - the custom skater's display name as display_name.
bool __cdecl CFunc_GetCustomSkaterName(Scripts::CStruct*, Scripts::CScript* script)
{
    script->m_args->AppendWord(kDisplayName, ProfileManager()->GetTemplate(kCustom)->FetchShownName());
    return true;
}

// 0x005B0030: GetCurrentPedProName - when the global 0x09EEBCB8 (name unknown) names a structure, that structure's
// name as 0xF054556D (name unknown); false if it is a bare name or anything else. (No null check: the global must
// exist.)
bool __cdecl CFunc_GetCurrentPedProName(Scripts::CStruct*, Scripts::CScript* script)
{
    Scripts::CSymbolTableEntry* entry = Scripts::ResolveSymbol(kCurrentPedPro);
    if (entry->type == Scripts::QBVAL_NAME || entry->type != Scripts::QBVAL_STRUCT) // (the original tests both)
        return false;
    script->m_args->PutNameKey(kPedProName, entry->name);
    return true;
}

// 0x005B0280: GetStatValue <stat> [skater=<name>] - stat_value: the skate module's forced stat value (+0x3D0, unless
// 0; then also the flag overridden), classic mode's career value, or the named pro's (else the current profile's)
// stat.
bool __cdecl CFunc_GetStatValue(Scripts::CStruct* params, Scripts::CScript* script)
{
    float forced = SkateField<float>(0x3D0);
    if (forced != 0.0f) {
        int value = static_cast<int>(static_cast<int64_t>(forced)); // (the original's _ftol)
        script->m_args->PutNameKey(kOverridden, kOverridden);
        script->m_args->PutInt(kStatValue, value);
        return true;
    }
    uint32_t stat = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(0u, &stat, 1);
    int value;
    if (GameModeIsTrue(kIsClassic)) {
        value = Career()->GetInfoInteger(stat);
    } else {
        GameObjects::CProfileRoster* manager = ProfileManager();
        uint32_t name;
        GameObjects::CRiderIdentity* profile =
            params->ReadHashValue(kSkater, &name, 0) ? manager->GetTemplate(name) : manager->FetchActiveAccount();
        value = profile->FetchTally(stat);
    }
    script->m_args->PutInt(kStatValue, value);
    return true;
}

// 0x005B0360: GetNumStatPointsAvailable player=<n> - profile n's stat points (points_available).
bool __cdecl CFunc_GetNumStatPointsAvailable(Scripts::CStruct* params, Scripts::CScript* script)
{
    int player = 0; // (uninitialised in the original when missing)
    params->FetchIntValue(kPlayer, &player, 1);
    int points = ProfileManager()->FetchAccount(player)->GetInfo471FC4DD();
    script->m_args->PutInt("points_available", points);
    return true;
}

// 0x005B03B0: UnlockSkater name=<pro> - the pro no longer hidden (its is_hidden 0, if it was set).
bool __cdecl CFunc_UnlockSkater(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->ReadHashValue(kName, &name, 1);
    Scripts::CStruct* info = ProfileManager()->GetTemplate(name)->InfoStructure();
    int hidden = 1;
    info->FetchIntValue(kIsHidden, &hidden, 1);
    if (hidden)
        info->PutInt(kIsHidden, 0);
    return true;
}

} // namespace ScriptCommands
