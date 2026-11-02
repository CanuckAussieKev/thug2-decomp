// Script commands (CFunctions) 0x005B0A40-0x005B1B60: configurable tricks, records, soundtracks, controllers (see
// trick_cfuncs.h).

#include "engine/script/trick_cfuncs.h"

#include <cstdint>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "frontend/front_end.h"
#include "engine/gfx/texture_cfuncs.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity_hub.h"
#include "engine/audio/music_player.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_hub.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/rider/trick_component.h"
#include "game/skate/point_tally.h"
#include "engine/objects/compositemgr.h"
#include "game/goals/goal_manager.h"
#include "game/goals/create_a_trick.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/net/net_game_state.h"
#include <gsl/pointers>

namespace commands {

namespace {

using objects::RiderIdentity;
using script::Array;
using script::Struct;

constexpr uint32_t kProfile = 0x7EA855F0;              // "profile"
constexpr uint32_t kCat = 0x61A1BC57;                  // "cat"
constexpr uint32_t kTrickMapping = 0xD544AA2D;         // "trick_mapping"
constexpr uint32_t kTrick = 0x270F56E1;                // "trick"
constexpr uint32_t kCatNum = 0xA75B8581;               // "cat_num"
constexpr uint32_t kSpecial = 0xB394C01C;              // "special"
constexpr uint32_t kSkater = 0x5B8AB877;               // "Skater"
constexpr uint32_t kType = 0x7321A8D6;                 // "type"
constexpr uint32_t kEditableTrickList = 0x5004A967;    // "ConfigurableTricks"
constexpr uint32_t kEditableCategoryList = 0x325F0B5D; // "ConfigurableCats"
constexpr uint32_t kTrickType = 0x89FA55CE;            // "trickType"
constexpr uint32_t kScr = 0xA6D2D890;                  // "scr"
constexpr uint32_t kGrindTrick = 0x6881FCB4;           // "GrindTrick"
constexpr uint32_t kGrabTrick = 0xB9AE26D2;            // "GrabTrick"
constexpr uint32_t kFlipTrick = 0xBA15FC7F;            // "FlipTrick"
constexpr uint32_t kParams = 0x7031F10C;               // "params"
constexpr uint32_t kIsSpecial = 0x1AEEAA40;            // "IsSpecial"
constexpr uint32_t kFull = 0x1F802B5F;                 // "full"
constexpr uint32_t kLockedForOthers =
    0x7ABFD5B4;                            // (name unknown) TrickIsLocked: locked unless it's the current skater's
constexpr uint32_t kIsHidden = 0x27EB9B9D; // "is_hidden"
constexpr uint32_t kName = 0xA1DC81F9;     // "name"
constexpr uint32_t kTrickDisplayText = 0x7BFE5BE2;    // "trick_display_text"
constexpr uint32_t kExtraTricks = 0x6E855102;         // "ExtraTricks"
constexpr uint32_t kExtraTrickString = 0x3DAAF58B;    // "extra_trick_string"
constexpr uint32_t kExtraTrickName = 0xD0BCFC62;      // (name unknown) GetTrickDisplayText: the extra trick's name
constexpr uint32_t kIndex = 0x7F8C98FE;               // "index"
constexpr uint32_t kSpecialTrickSlot = 0xC7C1990C;    // "special_trickslot"
constexpr uint32_t kSpecialTrickName = 0x35ECC76D;    // "special_trickname"
constexpr uint32_t kIsCat = 0xB56A8816;               // "isCat"
constexpr uint32_t kTrickTypeOut = 0x3D929B66;        // "trick_type"
constexpr uint32_t kArray = 0x5EF31148;               // "array"
constexpr uint32_t kValue = 0xE288A7CB;               // "value"
constexpr uint32_t kLevel = 0x651533EC;               // "level"
constexpr uint32_t kTrickText = 0x3EAFA520;           // "TrickText"
constexpr uint32_t kNumberOfOccurrences = 0x3F42ADAC; // "Number_Of_Occurrences"
constexpr uint32_t kNumSoundtracks = 0xAA0B4287;      // "numsoundtracks"
constexpr uint32_t kSoundtrackName = 0x869029CA;      // "soundtrackname"
constexpr uint32_t kController = 0xB30D9965;          // "controller"
constexpr uint32_t kSkaterHeapIndex = 0xC52D1A41;     // "skater_heap_index"
constexpr uint32_t kFrontEndPad = 0xCDC3D2FD;         // "front_end_pad"
constexpr uint32_t kTricks = 0x1E26FD3E;              // "tricks"
constexpr uint32_t kButtonSequences = 0x1A4F3519;     // (name unknown) GetKeyComboArrayFromTrickArray: the key combos
constexpr uint32_t kPort = 0xBC6EA233;                // "port"
constexpr uint32_t kActuator = 0x114FC131;            // "Actuator"
constexpr uint32_t kPercent = 0x9E497FC6;             // "Percent"
constexpr uint32_t kSlot = 0x53F1DF98;                // "slot"
constexpr uint32_t kTrickName = 0xEF6FB249;           // "trick_name"
constexpr uint32_t kKeyCombo = 0xACFDB27A;            // "key_combo"

modules::Skate* get_skate() { return modules::Skate::sole(); }

pad_input::Hub* input_manager() { return pad_input::get_input_manager(); }

// The skate module's local skater (0x00566520, no source yet; null if there's none).
objects::GameEntity* local_skater() { return get_skate()->get_home_rider(); }
// Whether a skater is a local one (its byte at +0xBC, as 0x00566520 reads it).
bool is_local_skater(objects::GameEntity* skater) { return skater->is_local_skater_ != 0; }
// The skater's score (0x00580480: its score component's object).
void* skater_score(objects::GameEntity* skater) { return core::down_cast<objects::Rider>(skater)->get_points_total(); }

// 0x00564F80 (no source yet; a skate module method that ignores the module): the profile of `params`' Skater, or the
// current one (no params or no Skater).
RiderIdentity* profile_for(Struct* params) { return get_skate()->fetch_account(params); }

// 0x005A9D90 (commands::find_name_of_value): the key combo in `mapping` bound to `trick` (0 if none).
uint32_t key_combo_for_trick(Struct* mapping, uint32_t trick)
{
    return commands::find_name_of_value(mapping, trick, 0);
}

// 0x005A9E30 (commands::find_name_of_integer; in the original the structure goes in EBX): the key combo in `mapping` bound
// to the created trick `cat` (the name of its last integer component of that value; 0 if none).
uint32_t key_combo_for_cat(Struct* mapping, int cat)
{
    return commands::find_name_of_integer(mapping, static_cast<uint32_t>(cat));
}

// `new Scripts::CArray` (the array pool, constructed).

// The profile manager's template table: its count (0x00469490, single::PointerMember::get_pull folded) and the template at
// `index` (0x00557FB0, no source yet).
int template_count(objects::ProfileRoster* manager) { return static_cast<int>(manager->templates_.num_); }
RiderIdentity* template_at(objects::ProfileRoster* manager, int index)
{
    return manager->templates_.get_by_index(index, nullptr); // (the original: the goal table's code, shared)
}

// A trick structure's type: its trickType, else its scr, else GrindTrick.
uint32_t trick_type_of(Struct* trick, uint32_t* type)
{
    if (!trick->read_hash_value(kTrickType, type, 0) && !trick->read_hash_value(kScr, type, 0))
        *type = kGrindTrick;
    return *type;
}

} // namespace

// 0x005B0A40: FlushDeadObjects: the object manager's dead objects deleted now. Always true.
bool cfunc_flush_dead_objects(script::Struct*, script::Script*)
{
    static_cast<objects::MiscHub*>(objects::object_hub_slot().get())->flush_dead_objects();
    return true;
}

// 0x005B0A50: BindTrickToKeyCombo key_combo=<keys> trick=<name or CAT number> [update_mappings=0] [profile=<name>]
// [special index=n [cat]]: in the profile (the named template, else the current one) either the special trick slot
// `index` set (the trick, the keys, whether it's a CAT; false without index) or the trick_mapping's key combo bound
// to the trick. True otherwise.
bool cfunc_bind_trick_to_key_combo(script::Struct* params, script::Script*)
{
    uint32_t key_combo = 0;
    params->read_hash_value("key_combo", &key_combo, 1);
    uint32_t trick_name = 0;
    params->read_hash_value("trick", &trick_name, 0);
    int trick_number = 0;
    const bool is_number = params->fetch_int_value("trick", &trick_number, 0);
    bool update = true;
    int update_mappings = 1;
    params->fetch_int_value("update_mappings", &update_mappings, 0);
    if (update_mappings == 0)
        update = false;

    RiderIdentity* profile = nullptr;
    if (params->has_entry_named(kProfile)) {
        uint32_t name = 0;
        params->read_hash_value(kProfile, &name, 1);
        profile = objects::profile_roster()->get_template(name);
    } else {
        profile = profile_for(nullptr);
    }

    if (params->has_bare_flag("special")) {
        int index = 0;
        if (!params->fetch_int_value("index", &index, 0))
            return false;
        RiderIdentity::SpecialTrick trick;
        trick.keys = key_combo;
        trick.trick = is_number ? static_cast<uint32_t>(trick_number) : trick_name;
        trick.flag = params->has_bare_flag(kCat);
        profile->set_special_trick(index, &trick, update);
        return true;
    }
    Struct* mapping = profile->fetch_details_params(kTrickMapping);
    mapping->erase_module(key_combo);
    if (is_number)
        mapping->put_int(key_combo, trick_number);
    else
        mapping->put_name_key(key_combo, trick_name);
    return true;
}

// 0x005B0C00: GetKeyComboBoundToTrick trick=<name> [special] | cat_num=n [profile=<name>]: the key combo bound to
// the trick (the profile's trick_mapping, or with `special` its special trick slots - then also current_index, the
// slot) or to the created trick cat_num, as current_key_combo. False if none.
bool cfunc_get_key_combo_bound_to_trick(script::Struct* params, script::Script* script)
{
    uint32_t trick = 0;
    int cat_number = 0; // (uninitialised in the original when cat_num is missing too: an asserting get)
    if (!params->read_hash_value(kTrick, &trick, 0))
        params->fetch_int_value(kCatNum, &cat_number, 1);
    RiderIdentity* profile = nullptr;
    if (params->has_entry_named(kProfile)) {
        uint32_t name = 0;
        params->read_hash_value(kProfile, &name, 1);
        profile = objects::profile_roster()->get_template(name);
    } else {
        profile = profile_for(nullptr);
    }

    bool found_special = false;
    int slot = 0;
    uint32_t key_combo = 0;
    if (trick) {
        if (!params->has_bare_flag(kSpecial)) {
            Struct* mapping = profile->fetch_details_params(kTrickMapping);
            key_combo = key_combo_for_trick(mapping, trick);
            uint32_t bound = 0;
            if (!mapping->read_hash_value(key_combo, &bound, 0) || bound != trick)
                return false;
        } else {
            if (!params->has_bare_flag(kSpecial))
                return false;
            const int count = profile->get_max_specials();
            if (count <= 0)
                return false;
            RiderIdentity::SpecialTrick special;
            for (slot = 0;; ++slot) {
                if (slot >= count)
                    return false;
                profile->get_special_trick(&special, slot);
                if (special.trick == trick)
                    break;
            }
            debug::printf(
                "GetKeyComboBoundToTrick found a special trick\n"); // "GetKeyComboBoundToTrick found a special trick\n"
            key_combo = special.keys;
            found_special = true;
        }
    } else {
        Struct* mapping = profile->fetch_details_params(kTrickMapping);
        key_combo = key_combo_for_cat(mapping, cat_number);
    }
    if (!key_combo)
        return false;
    script->args()->put_name_key("current_key_combo", key_combo);
    if (found_special) {
        debug::printf("current_index: %i\n", slot); // "current_index: %i\n"
        script->args()->put_int("current_index", slot);
    }
    return true;
}

// 0x005B0DA0: UpdateTrickMappings [Skater=n]: the skater's trick component given its profile's mappings - online the
// local skater and the first profile (not checked for null), otherwise skater n's and profile n's (nothing if either
// is missing). Always true.
bool cfunc_update_trick_mappings(script::Struct* params, script::Script*)
{
    modules::Skate* skate = get_skate();
    objects::ProfileRoster* manager = objects::profile_roster();
    objects::GameEntity* skater = nullptr;
    RiderIdentity* profile = nullptr;
    if (online::net_game_state().online_hub()->in_net_game()) {
        skater = local_skater();
        profile = manager->fetch_account(0);
    } else {
        int index = 0;
        params->fetch_int_value(kSkater, &index, 1);
        skater = skate->get_rider_at(index);
        profile = manager->fetch_account(index);
        if (!skater || !profile)
            return true;
    }
    core::down_cast<objects::MoveExecutor>(skater->fetch_module(kTrick))->apply_trick_bindings(profile);
    return true;
}

// 0x005B0E30: GetConfigurableTricksFromType [type=<trick type>] [special]: ConfigurableTricks, the names of the global
// ConfigurableTricks whose type (trickType, else scr, else GrindTrick) is `type` (with `special` and no type: any)
// and whose params are IsSpecial or not as `special` asks; for grab and flip tricks also ConfigurableCats, the local
// skater's created trick slots (1-10) that are full. False if no trick matched.
bool cfunc_get_configurable_tricks_from_type(script::Struct* params, script::Script* script)
{
    std::array<uint32_t, 128> tricks{};
    std::array<int, 128> cats{};
    uint32_t type = 0;
    params->read_hash_value(kType, &type, 0);
    const bool special = params->has_bare_flag(kSpecial);
    int trick_count = 0;
    int cat_count = 0;
    Array* configurable = script::fetch_list(kEditableTrickList);
    const int size = static_cast<int>(configurable->extent_);
    uint32_t trick_type = 0;
    for (int i = 0; i < size; ++i) {
        const uint32_t name = configurable->fetch_part(i);
        Struct* trick = script::read_sub_struct(name);
        if (!trick)
            continue;
        trick_type_of(trick, &trick_type);
        if ((!special || type) && trick_type != type)
            continue;
        Struct* trick_params = nullptr;
        trick->read_sub_struct(kParams, &trick_params, 1);
        if (trick_params->has_bare_flag(kIsSpecial) != special)
            continue;
        core::at(tricks, trick_count++) = name;
    }
    if (type == kGrabTrick || type == kFlipTrick) {
        objects::GameEntity* skater = local_skater();
        if (skater) {
            // the skater's created tricks 1-10
            const auto& created = core::down_cast<objects::Rider>(skater)->trick_slots_;
            for (int slot = 1; slot < 11; ++slot) {
                int full = 0;
                if (core::at(created, slot)->other_params_->fetch_int_value(kFull, &full, 0) && full)
                    core::at(cats, cat_count++) = slot;
            }
        }
    }
    if (trick_count == 0)
        return false;
    gsl::owner<Array*> trick_array = script::new_array();
    trick_array->configure_size_kind(trick_count, script::kQbvalName);
    for (int i = 0; i < trick_count; ++i)
        trick_array->set_element(i, core::at(tricks, i));
    script->args()->append_array_ref(kEditableTrickList, trick_array);
    if (cat_count > 0) {
        gsl::owner<Array*> cat_array = script::new_array();
        cat_array->configure_size_kind(cat_count, script::kQbvalInteger);
        for (int i = 0; i < cat_count; ++i)
            cat_array->set_element(i, static_cast<uint32_t>(core::at(cats, i)));
        script->args()->append_array_ref(kEditableCategoryList, cat_array);
    }
    return true;
}

// 0x005B1140: TrickIsLocked trick=<name>: whether the trick (a global structure whose params name a Skater) is
// locked: with the flag 0x7ABFD5B4 unless the current profile is that skater; otherwise if that skater's template is
// hidden (is_hidden). False without the structure, its params or Skater, or with no such template.
bool cfunc_trick_is_locked(script::Struct* params, script::Script*)
{
    uint32_t trick = 0;
    params->read_hash_value(kTrick, &trick, 1);
    Struct* structure = script::read_sub_struct(trick);
    if (!structure)
        return false;
    Struct* trick_params = nullptr; // (uninitialised in the original: an asserting get)
    structure->read_sub_struct(kParams, &trick_params, 1);
    if (!trick_params)
        return false;
    uint32_t skater = 0;
    if (!trick_params->read_hash_value(kSkater, &skater, 0))
        return false;
    objects::ProfileRoster* manager = objects::profile_roster();
    if (trick_params->has_bare_flag(kLockedForOthers))
        return manager->fetch_active_account()->fetch_title() != skater;
    const int count = template_count(manager);
    for (int i = 0; i < count; ++i) {
        RiderIdentity* profile = template_at(manager, i);
        if (profile->fetch_title() == skater) {
            int hidden = 0;
            profile->details_params()->fetch_int_value(kIsHidden, &hidden, 0);
            return hidden != 0;
        }
    }
    return false;
}

// 0x005B1250: GetTrickDisplayText trick=<name>: trick_display_text, its params' name; if it has ExtraTricks (a name,
// or an array's first) also extra_trick_string (the first extra trick's params' name) and 0xD0BCFC62 (the extra
// tricks' name). False without the trick structure.
bool cfunc_get_trick_display_text(script::Struct* params, script::Script* script)
{
    uint32_t trick = 0;
    params->read_hash_value(kTrick, &trick, 1);
    Struct* structure = script::read_sub_struct(trick);
    if (!structure)
        return false;
    Struct* trick_params = nullptr;
    structure->read_sub_struct(kParams, &trick_params, 1);
    const char* name = nullptr;
    trick_params->fetch_text(kName, &name, 1);
    script->args()->append_word(kTrickDisplayText, name);
    uint32_t extra = 0;
    if (!trick_params->read_hash_value(kExtraTricks, &extra, 0)) {
        Array* extras = nullptr;
        if (trick_params->fetch_list(kExtraTricks, &extras, 0))
            extra = extras->fetch_part(0);
    }
    if (extra) {
        auto* first = script::fetch_list(extra)->struct_at(0);
        Struct* extra_params = nullptr;
        first->read_sub_struct(kParams, &extra_params, 1);
        const char* extra_name = nullptr;
        extra_params->fetch_text(kName, &extra_name, 1);
        script->args()->append_word(kExtraTrickString, extra_name);
        script->args()->put_name_key(kExtraTrickName, extra);
    }
    return true;
}

// 0x005B1380: GetSpecialTrickInfo index=n: the current profile's special trick slot n: special_trickslot (its keys),
// special_trickname (a CAT's number as an integer, else the trick's name) and isCat. Always true.
bool cfunc_get_special_trick_info(script::Struct* params, script::Script* script)
{
    int index = 0;
    params->fetch_int_value(kIndex, &index, 1);
    RiderIdentity::SpecialTrick trick;
    profile_for(nullptr)->get_special_trick(&trick, index);
    script->args()->put_name_key(kSpecialTrickSlot, trick.keys);
    if (trick.flag)
        script->args()->put_int(kSpecialTrickName, static_cast<int>(trick.trick));
    else
        script->args()->put_name_key(kSpecialTrickName, trick.trick);
    script->args()->put_int(kIsCat, trick.flag);
    return true;
}

// 0x005B1420: GetTrickType trick=<name>: trick_type, the trick structure's type (trickType, else scr, else
// GrindTrick). Always true.
bool cfunc_get_trick_type(script::Struct* params, script::Script* script)
{
    uint32_t trick = 0;
    params->read_hash_value(kTrick, &trick, 1);
    uint32_t type = 0;
    trick_type_of(script::read_sub_struct(trick), &type);
    script->args()->put_name_key(kTrickTypeOut, type);
    return true;
}

// 0x005B14A0: GetIndexOfItemContaining array=<global array> name=<component> value=<checksum> [index=start]: index,
// the first structure from `start` whose component `name` is `value`. False if none.
bool cfunc_get_index_of_item_containing(script::Struct* params, script::Script* script)
{
    uint32_t array_name = 0;
    params->read_hash_value(kArray, &array_name, 1);
    Array* array = script::fetch_list(array_name);
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1);
    int index = 0;
    params->fetch_int_value(kIndex, &index, 0);
    uint32_t value = 0;
    params->read_hash_value(kValue, &value, 1);
    const int size = static_cast<int>(array->extent_);
    for (; index < size; ++index) {
        auto* item = array->struct_at(index);
        if (!item->has_entry_named(name))
            continue;
        uint32_t found = 0;
        item->read_hash_value(name, &found, 1);
        if (value == found) {
            script->args()->put_int(kIndex, index);
            return true;
        }
    }
    return false;
}

// 0x005B1590: GetLevelRecords [level=n]: the level's records (default: the career's level) written to the script's
// parameters. Always true.
bool cfunc_get_level_records(script::Struct* params, script::Script* script)
{
    const modules::Skate* skate = get_skate();
    int level = 0;
    if (!params->fetch_int_value(kLevel, &level, 0))
        level = skate->career()->fetch_stage();
    highscores::game_records()->get_level_records(level)->write_to_structure(script->args().get());
    return true;
}

// 0x005B15F0: ResetComboRecords: the local skater's score's three combo records (0x00561400: +0x2C, +0x30, +0x34)
// zeroed. Always true.
bool cfunc_reset_combo_records(script::Struct*, script::Script*)
{
    objects::GameEntity* skater = local_skater();
    if (skater)
        static_cast<modules::PointTally*>(skater_score(skater))->reset_bests();
    return true;
}

// 0x005B1610: GetNumberOfTrickOccurrences TrickText="text": Number_Of_Occurrences, how often the local skater's score
// has the trick (0x005624C0 with the text's checksum, 0, 0, 1; no local skater is not checked). Always true.
bool cfunc_get_number_of_trick_occurrences(script::Struct* params, script::Script* script)
{
    const char* text = nullptr;
    params->fetch_text(kTrickText, &text, 1);
    const uint32_t trick = checksums::hash_from_text(text);
    void* score = skater_score(local_skater());
    const int count = static_cast<modules::PointTally*>(score)->count_trick_after_taps(trick, 0, false, 1);
    script->args()->put_int(kNumberOfOccurrences, count);
    return true;
}

// 0x005B1670: GetNumSoundtracks: numsoundtracks. Always true.
bool cfunc_get_num_soundtracks(script::Struct*, script::Script* script)
{
    script->args()->put_int(kNumSoundtracks, audio::get_num_soundtracks());
    return true;
}

// 0x005B1690: GetSoundtrackName <n>: soundtrackname, user soundtrack n's name. Always true.
bool cfunc_get_soundtrack_name(script::Struct* params, script::Script* script)
{
    int index = 0;
    params->fetch_int_value(0u, &index, 1);
    script->args()->append_word(kSoundtrackName, audio::get_soundtrack_name(index));
    return true;
}

// 0x005B16D0: BindControllerToSkater controller=c skater_heap_index=n: player n reads controller c; the skate module's
// local skaters' input components told (0x00567090). Always true.
bool cfunc_bind_controller_to_skater(script::Struct* params, script::Script*)
{
    int controller = 0;
    params->fetch_int_value(kController, &controller, 1);
    int skater = 0;
    params->fetch_int_value(kSkaterHeapIndex, &skater, 1);
    modules::Skate* skate = get_skate();
    input_manager()->set_player_controller(skater, controller);
    skate->bind_controllers();
    return true;
}

// 0x005B1730: BindFrontEndToController controller=c front_end_pad=n: the front end's pad n listens to controller c
// (the pad that had c takes n's old controller); its input mappings updated. Always true.
bool cfunc_bind_front_end_to_controller(script::Struct* params, script::Script*)
{
    int controller = 0;
    params->fetch_int_value(kController, &controller, 1);
    int pad = 0;
    params->fetch_int_value(kFrontEndPad, &pad, 1);
    // "attempting to bind front end %i to controller %i\n"
    debug::printf("attempting to bind front end %i to controller %i\n", pad, controller);
    modules::FrontEnd* front_end = modules::front_end_instance().get();
    auto& map = front_end->device_server_map_;
    const int old = core::at(map, pad);
    if (core::at(map, 0) == controller)
        core::at(map, 0) = old;
    if (core::at(map, 1) == controller)
        core::at(map, 1) = old;
    core::at(map, pad) = controller;
    front_end->update_input_mappings();
    return true;
}

// 0x005B17C0: ControllerBoundToDifferentSkater Skater=n controller=c: whether a local skater other than n (0-7) reads
// controller c.
bool cfunc_controller_bound_to_different_skater(script::Struct* params, script::Script*)
{
    int skater = 0;
    params->fetch_int_value(kSkater, &skater, 1);
    int controller = 0;
    params->fetch_int_value(kController, &controller, 1);
    modules::Skate* skate = get_skate();
    for (int i = 0; i < 8; ++i) {
        if (i == skater)
            continue;
        objects::GameEntity* other = skate->get_rider_at(i);
        if (!other || !is_local_skater(other))
            continue;
        if (input_manager()->get_player_controller(i) == controller)
            return true;
    }
    return false;
}

// 0x005B1840: ControllerBoundToSkater controller=c Skater=n: whether player n reads controller c.
bool cfunc_controller_bound_to_skater(script::Struct* params, script::Script*)
{
    int controller = 0;
    params->fetch_int_value(kController, &controller, 1);
    int skater = 0;
    params->fetch_int_value(kSkater, &skater, 1);
    return input_manager()->get_player_controller(skater) == controller;
}

// 0x005B1890: GetKeyComboArrayFromTrickArray tricks=[...]: 0x1A4F3519, the key combo of each trick (the current
// profile's trick_mapping, else its special trick slots; 0 if none) - a copy: the array made for it isn't freed (a
// leak in the original). Always true.
bool cfunc_get_key_combo_array_from_trick_array(script::Struct* params, script::Script* script)
{
    Array* tricks = nullptr;
    params->fetch_list(kTricks, &tricks, 1);
    RiderIdentity* profile = profile_for(nullptr);
    Struct* mapping = profile->fetch_details_params(kTrickMapping);
    const int size = static_cast<int>(tricks->extent_);
    gsl::owner<Array*> key_combos = script::new_array();
    key_combos->configure_size_kind(size, script::kQbvalName);
    for (int i = 0; i < size; ++i) {
        const uint32_t trick = tricks->fetch_part(i);
        uint32_t key_combo = key_combo_for_trick(mapping, trick);
        if (!key_combo) {
            const int count = profile->get_max_specials();
            for (int slot = 0; slot < count; ++slot) {
                RiderIdentity::SpecialTrick special;
                profile->get_special_trick(&special, slot);
                if (special.trick == trick) {
                    key_combo = special.keys;
                    break;
                }
            }
        }
        key_combos->set_element(i, key_combo);
    }
    script->args()->append_array_value(kButtonSequences, key_combos);
    return true;
}

// 0x005B1A00: FirstInputReceived: the skate module's flag (+0x3C) set (0x00564DA0). Always true.
bool cfunc_first_input_received(script::Struct*, script::Script*)
{
    get_skate()->scan_pad_connections();
    return true;
}

// 0x005B1A10: VibrateController port=p Actuator=a Percent=n: the device's actuator a at n percent. False without the
// device.
bool cfunc_vibrate_controller(script::Struct* params, script::Script*)
{
    int port = 0;
    params->fetch_int_value(kPort, &port, 1);
    int actuator = 0;
    params->fetch_int_value(kActuator, &actuator, 1);
    int percent = 0;
    params->fetch_int_value(kPercent, &percent, 1);
    sio::InputPort* device = sio::manager_instance()->fetch_gadget(port, 0);
    if (!device)
        return false;
    device->fire_actuator(actuator, percent);
    return true;
}

// 0x005B1A90: LockCurrentSkaterProfileIndex [<n> (1)]: the profile manager's lock set (n != 0). Always true.
bool cfunc_lock_current_skater_profile_index(script::Struct* params, script::Script*)
{
    int lock = 1;
    params->fetch_int_value(0u, &lock, 1);
    objects::profile_roster()->set_locked(lock != 0);
    return true;
}

// 0x005B1AD0: SetSpecialTrickInfo slot=n trick_name=<name> key_combo=<keys>: the current profile's special trick slot
// n set (not a CAT; the skater not told). False without a current profile.
bool cfunc_set_special_trick_info(script::Struct* params, script::Script*)
{
    int slot = 0;
    params->fetch_int_value(kSlot, &slot, 1);
    uint32_t name = 0;
    params->read_hash_value(kTrickName, &name, 1);
    uint32_t keys = 0;
    params->read_hash_value(kKeyCombo, &keys, 1);
    RiderIdentity* profile = objects::profile_roster()->fetch_active_account();
    if (!profile)
        return false;
    RiderIdentity::SpecialTrick trick;
    trick.trick = name;
    trick.keys = keys;
    profile->set_special_trick(slot, &trick, false);
    return true;
}

} // namespace commands
