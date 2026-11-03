// Script commands of the skater profiles and the players' options (see skater_profile_cfuncs.h).

#include "game/skate/skater_profile_cfuncs.h"

#include <cstdint>

#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "game/skate/career.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "engine/gfx/model_appearance.h"
#include "game/skate/skate_module.h"
#include "game/goals/goal_manager.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace commands {

namespace {

using namespace skate_calls;

constexpr uint32_t kString = 0x61414D56;                    // string
constexpr uint32_t kName = 0xA1DC81F9;                      // name
constexpr uint32_t kIsClassic = 0x5A2C589A;                 // is_classic
constexpr uint32_t kStatPlayWinSound = 0x5A90FB86;          // stat_play_win_sound
constexpr uint32_t kAll = 0xC4E78E22;                       // all
constexpr uint32_t kProfile = 0x7EA855F0;                   // profile
constexpr uint32_t kPlayer = 0x67E6859A;                    // player
constexpr uint32_t kSkater = 0x5B8AB877;                    // skater
constexpr uint32_t kCurrentSkaterProfileIndex = 0x3C64476B; // currentSkaterProfileIndex
constexpr uint32_t kDisplayName = 0x2AB66CB8;               // display_name
constexpr uint32_t kCustom = 0x0A7BE964;                    // custom (the custom skater's template)
constexpr uint32_t kCurrentPedPro = 0x09EEBCB8; // (name unknown) a global naming the current ped pro's structure
constexpr uint32_t kPedProName = 0xF054556D;    // (name unknown) GetCurrentPedProName's result
constexpr uint32_t kOverridden = 0xC70E7415;    // overridden
constexpr uint32_t kStatValue = 0x8EAF7ADD;     // stat_value
constexpr uint32_t kIsHidden = 0x27EB9B9D;      // is_hidden

// The players' options: the skate module's table at +0x394 (8 bytes a player: vibration, auto kick, spin taps), set
// through 0x00567C30 / 0x00567C00 / 0x00567BD0 (player, on).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class): values fixed by a file, a script, the wire or the SDK, used as integers
enum Option : uint8_t { kVibration = 0, kAutoKick = 1, kSpinTaps = 2 };
// The player a command names (its unnamed integer; 0).
int player_of(script::Struct* params)
{
    int player = 0;
    params->fetch_int_value(0u, &player, 0);
    return player;
}
bool get_option(script::Struct* params, Option option)
{
    const modules::Skate::PlayerSettings& player = core::at(modules::Skate::sole()->participants(), player_of(params));
    return option == kVibration ? player.vibration_ : option == kAutoKick ? player.auto_kick_ : player.spin_taps_;
}
bool set_option(script::Struct* params, Option option, bool on)
{
    const int player = player_of(params);
    modules::Skate* skate = modules::Skate::sole();
    switch (option) {
    case kVibration:
        skate->change_rumble(player, on); // 0x00567C30
        break;
    case kAutoKick:
        skate->set_auto_eject(player, on); // 0x00567C00
        break;
    case kSpinTaps:
        skate->set_turn_tap_count(player, on); // 0x00567BD0
        break;
    }
    return true;
}


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
constexpr uint32_t kNoFaceTexture = 0x3AF36E6D;    // NoFaceTexture

// The profile manager's templates (its table, 0x00557FB0 by index) and each one's info structure.
uint32_t template_count() { return objects::profile_roster()->templates().num(); }
objects::RiderIdentity* template_at(uint32_t index)
{
    // (0x00557FB0: the lookup table's by-index code, shared with the goal manager's)
    return objects::profile_roster()->get_template_at(static_cast<int>(index));
}
// The template named `name` (by its info's name), or null.
objects::RiderIdentity* find_template(uint32_t name)
{
    const int count = static_cast<int>(template_count());
    for (int i = 0; i < count; ++i) {
        objects::RiderIdentity* profile = template_at(i);
        uint32_t profile_name = 0;
        profile->details_params()->read_hash_value(kName, &profile_name, 1);
        if (profile_name == name)
            return profile;
    }
    return nullptr;
}
// An element of a global array of structures.
script::Struct* table_row(script::Array* list, uint32_t index)
{
    return list->struct_at(index);
}

} // namespace

// 0x005AAEF0: RestoreTemporaryAppearance name=<id> player=<n> [NoFaceTexture] - player n's profile's appearance
// back from the temporary profile `name` (RememberTemporaryAppearance's): with NoFaceTexture only its structure
// (cleared and copied), else the whole appearance (0x004893A0).
bool cfunc_restore_temporary_appearance(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original when missing, as the player)
    params->read_hash_value(kName, &name, 1);
    int player = 0;
    params->fetch_int_value(kPlayer, &player, 1);
    objects::ProfileRoster* manager = objects::profile_roster();
    objects::RiderIdentity* profile = manager->fetch_account(player);
    objects::RiderIdentity* remembered = manager->get_extra_profile(name);
    if (params->has_bare_flag(kNoFaceTexture)) {
        auto* appearance = &profile->appearance()->structure();
        appearance->get_clear();
        appearance->append_structure_if_any(&remembered->appearance()->structure());
        return true;
    }
    profile->appearance()->copy_from(
        remembered->appearance()); // (the appearance copied)
    return true;
}

// 0x005AAF90: SyncPlayer2Profile - the second player's profile copied again from its template.
bool cfunc_sync_player2_profile(script::Struct*, script::Script*)
{
    objects::profile_roster()->reset_second_profile();
    return true;
}

// 0x005ABBF0: ResetAllToDefaultStats - every skater of master_skater_list: its stats (the stat_names) and
// points_available put back into the template of its name.
bool cfunc_reset_all_to_default_stats(script::Struct*, script::Script*)
{
    script::Array* list = script::fetch_list(kMasterSkaterList);
    for (uint32_t i = 0; i < list->extent(); ++i) {
        gsl::owner<script::Struct*> stats = script::new_struct();
        script::Struct* entry = table_row(list, i);
        uint32_t skater_name = 0; // (uninitialised in the original when missing, as the values below)
        entry->read_hash_value(kName, &skater_name, 1);
        script::Array* stat_names = script::fetch_list(kStatNames);
        for (uint32_t j = 0; j < stat_names->extent(); ++j) {
            uint32_t stat_name = 0;
            table_row(stat_names, j)->read_hash_value(kName, &stat_name, 1);
            int value = 0;
            entry->fetch_int_value(stat_name, &value, 1);
            stats->put_int(checksums::Name{stat_name}, value);
        }
        int points = 0;
        entry->fetch_int_value(kPointsAvailable, &points, 1);
        stats->put_int(checksums::Name{kPointsAvailable}, points);
        const uint32_t count = template_count();
        for (uint32_t k = 0; k < count; ++k) {
            objects::RiderIdentity* profile = template_at(k);
            uint32_t profile_name = 0;
            profile->details_params()->read_hash_value(kName, &profile_name, 1);
            if (profile_name == skater_name) {
                profile->details_params()->append_structure_if_any(stats);
                break;
            }
        }
        script::free_struct(stats);
    }
    return true;
}

// 0x005ABE30: ResetToDefaultProfile name=<skater> [partial=stats_and_tricks | partial=<other>] - the template reset
// from its master_skater_list entry: entirely, its stats and tricks only, or its info only.
bool cfunc_reset_to_default_profile(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kName, &name, 1);
    script::Array* list = script::fetch_list(kMasterSkaterList);
    for (uint32_t i = 0; i < list->extent(); ++i) {
        script::Struct* entry = table_row(list, i);
        uint32_t entry_name = 0;
        entry->read_hash_value(kName, &entry_name, 1);
        if (entry_name != name)
            continue;
        objects::RiderIdentity* profile = objects::profile_roster()->get_template(name);
        uint32_t partial = 0;
        if (params->read_hash_value(kPartial, &partial, 0)) {
            if (partial == kStatsAndTricks)
                profile->set_from_params(entry, false, true);
            else
                profile->set_from_params(entry, true, false);
        } else {
            profile->get_reset(entry);
        }
    }
    return true;
}

// 0x005ABF00: ResetAllToDefaultProfile - every master_skater_list skater's template reset from its entry.
bool cfunc_reset_all_to_default_profile(script::Struct*, script::Script*)
{
    script::Array* list = script::fetch_list(kMasterSkaterList);
    for (uint32_t i = 0; i < list->extent(); ++i) {
        script::Struct* entry = table_row(list, i);
        uint32_t name = 0;
        entry->read_hash_value(kName, &name, 1);
        objects::profile_roster()->get_template(name)->get_reset(entry);
    }
    return true;
}

// 0x005ABF70: ForEachSkaterName do=<script> [params=<s>] - the script run for every template with the parameters
// and its name.
bool cfunc_for_each_skater_name(script::Struct* params, script::Script*)
{
    uint32_t script = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kDo, &script, 1);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    const int count = static_cast<int>(template_count());
    for (int i = 0; i < count; ++i) {
        uint32_t name = 0;
        template_at(i)->details_params()->read_hash_value(kName, &name, 1);
        const script::StructPtr run = script::make_struct();
        run->append_structure_if_any(script_params);
        run->put_name_key(checksums::Name{kName}, name);
        script::launch_script(script, run.get(), nullptr, false, nullptr);
    }
    return true;
}

// 0x005AC0C0: ForEachSkaterProfile do=<script> [params=<s>] - the script run for every template with the
// parameters and its whole info.
bool cfunc_for_each_skater_profile(script::Struct* params, script::Script*)
{
    uint32_t script = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kDo, &script, 1);
    script::Struct* script_params = nullptr;
    params->read_sub_struct(kParams, &script_params, 0);
    const int count = static_cast<int>(template_count());
    for (int i = 0; i < count; ++i) {
        script::Struct* info = template_at(i)->details_params();
        const script::StructPtr run = script::make_struct();
        run->append_structure_if_any(script_params);
        run->append_structure_if_any(info);
        script::launch_script(script, run.get(), nullptr, false, nullptr);
    }
    return true;
}

// 0x005AC1F0: GetSkaterProfileInfoByName name=<skater> - the template's info added to the script's parameters
// (false without such a template).
bool cfunc_get_skater_profile_info_by_name(script::Struct* params, script::Script* script)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kName, &name, 1);
    objects::RiderIdentity* profile = find_template(name);
    if (!profile)
        return false;
    script->args()->append_structure_if_any(profile->details_params());
    return true;
}

// 0x005AC280: SetSkaterProfileInfoByName name=<skater> params=<s> - the parameters added to the template's info
// (false without such a template).
bool cfunc_set_skater_profile_info_by_name(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kName, &name, 1);
    script::Struct* values = nullptr;
    params->read_sub_struct(kParams, &values, 1);
    objects::RiderIdentity* profile = find_template(name);
    if (!profile)
        return false;
    profile->details_params()->append_structure_if_any(values);
    return true;
}

// 0x005AC320: PrintCurrentAppearance [skater=<n>] - the profile's appearance printed (compiled out: the empty
// 0x005BAB90).
bool cfunc_print_current_appearance(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = get_profile_from_params(params);
    script::dump_to_log(&profile->appearance()->structure());
    return true;
}

// 0x005AC340: SetNeversoftSkater [skater=<n>] appearance=<s> info=<s> - the profile's appearance set and applied,
// the info added, its 0x09F350E5 value set.
bool cfunc_set_neversoft_skater(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = get_profile_from_params(params);
    script::Struct* appearance = nullptr; // (uninitialised in the original when missing, as the info)
    params->read_sub_struct(kAppearance, &appearance, 1);
    profile->set_appearance(appearance, true);
    script::Struct* info = nullptr;
    params->read_sub_struct(kInfo, &info, 1);
    profile->details_params()->append_structure_if_any(info);
    profile->set_value_09f_350e_5(1);
    return true;
}

// 0x005AC3B0: ResetSkaters [node_name=<node>] [RestartWalking] - the skate module's skaters reset at the node (its
// 0x00566980; 0xDD7DF50E - name unknown - is read and not used).
bool cfunc_reset_skaters(script::Struct* params, script::Script*)
{
    int unused = 0;
    params->fetch_int_value(kResetValue, &unused, 0);
    uint32_t node = 0;
    params->read_hash_value(kNodeName, &node, 0);
    const bool walking = params->has_bare_flag(kRestartWalking);
    modules::Skate::sole()->reset_skaters({.node = node, .walking = walking});
    return true;
}

// 0x005AC410: SetSkaterProfileInfo player=<n> params=<s> - added to profile n's info. 0x005AC470:
// GetSkaterProfileInfo player=<n> - profile n's info added to the script's parameters.
bool cfunc_set_skater_profile_info(script::Struct* params, script::Script*)
{
    int player = 0; // (uninitialised in the original when missing, as the structure)
    params->fetch_int_value(kPlayer, &player, 1);
    script::Struct* values = nullptr;
    params->read_sub_struct(kParams, &values, 1);
    objects::profile_roster()->fetch_account(player)->details_params()->append_structure_if_any(values);
    return true;
}

bool cfunc_get_skater_profile_info(script::Struct* params, script::Script* script)
{
    int player = 0; // (uninitialised in the original when missing)
    params->fetch_int_value(kPlayer, &player, 1);
    script->args()->append_structure_if_any(objects::profile_roster()->fetch_account(player)->details_params());
    return true;
}

// 0x005AC4B0: SetSkaterProfileProperty player=<n> <stat> <value> - profile n's stat (set_ability_level's answer: false out of
// 0-10).
bool cfunc_set_skater_profile_property(script::Struct* params, script::Script*)
{
    int player = 0; // (uninitialised in the original when missing, as the stat and value)
    params->fetch_int_value(kPlayer, &player, 1);
    uint32_t stat = 0;
    params->read_hash_value(0u, &stat, 0);
    int value = 0;
    params->fetch_int_value(0u, &value, 0);
    return objects::profile_roster()->fetch_account(player)->set_ability_level(stat, value);
}

// 0x005AC520: SetClassicStat <stat> <value> - the career's info value (classic mode's stats).
bool cfunc_set_classic_stat(script::Struct* params, script::Script*)
{
    objects::RiderStoryProgress* career = get_career();
    uint32_t stat = 0; // (uninitialised in the original when missing, as the value)
    params->read_hash_value(0u, &stat, 0);
    int value = 0;
    params->fetch_int_value(0u, &value, 0);
    return career->set_info_integer(stat, value);
}

// 0x005AC570: ToggleAlwaysSpecial [on | off] - every skater's always-special flag (+0x168) set, cleared or toggled.
bool cfunc_toggle_always_special(script::Struct* params, script::Script*)
{
    const bool on = params->has_bare_flag(kOn);
    const bool off = params->has_bare_flag(kOff);
    for (int i = 0; i < 8; ++i) {
        objects::GameEntity* skater = get_rider_at(i);
        if (!skater)
            continue;
        bool& special = core::down_cast<objects::Rider>(skater)->always_special();
        if (on)
            special = true;
        else if (off)
            special = false;
        else
            special = special == 0;
    }
    return true;
}

// 0x005AE370 ... 0x005AE4F0: VibrationIsOn / VibrationOn / VibrationOff, AutoKickIsOn / On / Off, SpinTapsAreOn /
// On / Off [<player>].
bool cfunc_vibration_is_on(script::Struct* params, script::Script*) { return get_option(params, kVibration); }
bool cfunc_vibration_on(script::Struct* params, script::Script*) { return set_option(params, kVibration, true); }
bool cfunc_vibration_off(script::Struct* params, script::Script*) { return set_option(params, kVibration, false); }
bool cfunc_auto_kick_is_on(script::Struct* params, script::Script*) { return get_option(params, kAutoKick); }
bool cfunc_auto_kick_on(script::Struct* params, script::Script*) { return set_option(params, kAutoKick, true); }
bool cfunc_auto_kick_off(script::Struct* params, script::Script*) { return set_option(params, kAutoKick, false); }
bool cfunc_spin_taps_are_on(script::Struct* params, script::Script*) { return get_option(params, kSpinTaps); }
bool cfunc_spin_taps_on(script::Struct* params, script::Script*) { return set_option(params, kSpinTaps, true); }
bool cfunc_spin_taps_off(script::Struct* params, script::Script*) { return set_option(params, kSpinTaps, false); }

// 0x005AE520: GetCurrentProDisplayInfo - the current profile's display name as string.
bool cfunc_get_current_pro_display_info(script::Struct*, script::Script* script)
{
    objects::RiderIdentity* profile = get_profile_from_params(nullptr);
    script->args()->append_word(kString, script::kQbvalString, profile->fetch_shown_name());
    return true;
}

// 0x005AE900: SelectCurrentSkater name=<skater> (or the unnamed checksum) - the profile manager's current skater
// (false if it already is); the skater of the current index given its looks.
bool cfunc_select_current_skater(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    if (!params->read_hash_value(kName, &name, 0))
        params->read_hash_value(0u, &name, 1);
    objects::ProfileRoster* manager = objects::profile_roster();
    if (get_profile_from_params(nullptr)->fetch_title() == name)
        return false;
    manager->set_current_skater(name);
    objects::RiderIdentity* profile = get_profile_from_params(nullptr);
    auto* skater = core::down_cast<objects::Rider>(get_rider_at(manager->cursor_pos()));
    if (skater)
        skater->update_from_profile(profile);
    return true;
}

// 0x005AF950: AwardStatPoint [<n>] - n (1) stat points more: classic mode the career's, otherwise the current
// profile's (their points_available, 0x471FC4DD); stat_play_win_sound run.
bool cfunc_award_stat_point(script::Struct* params, script::Script*)
{
    if (game_mode_is_true(kIsClassic)) {
        objects::RiderStoryProgress* career = get_career();
        int points = 1;
        params->fetch_int_value(0u, &points, 0);
        career->add_to_info_471f_c_4d_d(points);
    } else {
        objects::RiderIdentity* profile = get_profile_from_params(nullptr);
        int points = 1;
        params->fetch_int_value(0u, &points, 0);
        profile->add_to_info_471f_c_4d_d(points);
    }
    script::launch_script(kStatPlayWinSound, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005AF9F0: AwardSpecialTrickSlot [all | profile=<name>] - one special trick slot more for every pro (all), the
// named one, or the current profile.
bool cfunc_award_special_trick_slot(script::Struct* params, script::Script*)
{
    objects::ProfileRoster* manager = objects::profile_roster();
    if (params->has_bare_flag(kAll)) {
        const int count = static_cast<int>(manager->templates().num());
        for (int i = 0; i < count; ++i) // (0x00557FB0: the table's item by index)
            manager->get_template_at(i)
                ->add_max_specials(1);
        return true;
    }
    if (params->has_entry_named(kProfile)) {
        uint32_t name = 0; // (uninitialised in the original when not a checksum)
        params->read_hash_value(kProfile, &name, 1);
        objects::profile_roster()->get_template(name)->add_max_specials(1);
        return true;
    }
    get_profile_from_params(nullptr)->add_max_specials(1);
    return true;
}

// 0x005AFAA0: UpdateSkaterStats player=<n> - skater n's stats from profile n.
bool cfunc_update_skater_stats(script::Struct* params, script::Script*)
{
    int player = 0; // (uninitialised in the original when missing)
    params->fetch_int_value(kPlayer, &player, 1);
    auto* skater = core::down_cast<objects::Rider>(get_rider_at(player));
    skater->set_stats_from_profile(fetch_account(player));
    return true;
}

// 0x005AFFE0: GetCurrentSkaterProfileIndex - the profile manager's current index as currentSkaterProfileIndex.
bool cfunc_get_current_skater_profile_index(script::Struct*, script::Script* script)
{
    script->args()->put_int(checksums::Name{kCurrentSkaterProfileIndex}, objects::profile_roster()->cursor_pos());
    return true;
}

// 0x005B0000: GetCustomSkaterName - the custom skater's display name as display_name.
bool cfunc_get_custom_skater_name(script::Struct*, script::Script* script)
{
    script->args()->append_word(kDisplayName, objects::profile_roster()->get_template(kCustom)->fetch_shown_name());
    return true;
}

// 0x005B0030: GetCurrentPedProName - when the global 0x09EEBCB8 (name unknown) names a structure, that structure's
// name as 0xF054556D (name unknown); false if it is a bare name or anything else. (No null check: the global must
// exist.)
bool cfunc_get_current_ped_pro_name(script::Struct*, script::Script* script)
{
    const script::SymbolTableEntry* entry = script::resolve_symbol(kCurrentPedPro);
    if (entry->type() != script::kQbvalStruct) // (the original also tests for a name first)
        return false;
    script->args()->put_name_key(checksums::Name{kPedProName}, entry->name_value());
    return true;
}

// 0x005B0280: GetStatValue <stat> [skater=<name>] - stat_value: the skate module's forced stat value (+0x3D0, unless
// 0; then also the flag overridden), classic mode's career value, or the named pro's (else the current profile's)
// stat.
bool cfunc_get_stat_value(script::Struct* params, script::Script* script)
{
    const float forced = modules::Skate::sole()->forced_stat();
    if (forced != 0.0f) {
        const int value = static_cast<int>(static_cast<int64_t>(forced)); // (the original's _ftol)
        script->args()->put_name_key(checksums::Name{kOverridden}, kOverridden);
        script->args()->put_int(checksums::Name{kStatValue}, value);
        return true;
    }
    uint32_t stat = 0; // (uninitialised in the original when missing)
    params->read_hash_value(0u, &stat, 1);
    int value = 0;
    if (game_mode_is_true(kIsClassic)) {
        value = get_career()->get_info_integer(stat);
    } else {
        objects::ProfileRoster* manager = objects::profile_roster();
        uint32_t name = 0;
        objects::RiderIdentity* profile =
            params->read_hash_value(kSkater, &name, 0) ? manager->get_template(name) : manager->fetch_active_account();
        value = profile->fetch_tally(stat);
    }
    script->args()->put_int(checksums::Name{kStatValue}, value);
    return true;
}

// 0x005B0360: GetNumStatPointsAvailable player=<n> - profile n's stat points (points_available).
bool cfunc_get_num_stat_points_available(script::Struct* params, script::Script* script)
{
    int player = 0; // (uninitialised in the original when missing)
    params->fetch_int_value(kPlayer, &player, 1);
    const int points = objects::profile_roster()->fetch_account(player)->get_info_471f_c_4d_d();
    script->args()->put_int("points_available", points);
    return true;
}

// 0x005B03B0: UnlockSkater name=<pro> - the pro no longer hidden (its is_hidden 0, if it was set).
bool cfunc_unlock_skater(script::Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kName, &name, 1);
    script::Struct* info = objects::profile_roster()->get_template(name)->details_params();
    int hidden = 1;
    info->fetch_int_value(kIsHidden, &hidden, 1);
    if (hidden)
        info->put_int(checksums::Name{kIsHidden}, 0);
    return true;
}

} // namespace commands
