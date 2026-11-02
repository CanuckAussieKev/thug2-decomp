// What a save holds and how it is put back (see save_contents.h).

#include "game/save_cards/save_contents.h"
#include "platform/code_address.h"
#include "engine/net/buddy_status.h"

#include <cstdint>
#include <array>

#include "engine/core/dbg.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park_editor.h"
#include "game/goals/goal_manager.h"
#include "game/save_cards/memcard_cfuncs.h"
#include "game/save_cards/save_types.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/audio/music_player.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "game/skate/skate_module.h"
#include "game/goals/create_a_trick.h"
#include "game/rider/rider.h"
#include "engine/sys/game_config.h"
#include "game/goals/objective.h"
#include "engine/gfx/rig.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace save_cards {

namespace {

using script::Array;
using script::Script;
using script::Struct;

// The names of the save's parts.
constexpr uint32_t kOptions = 0x2FCA0578;                // "Options"
constexpr uint32_t kSplitScreenPreferences = 0xF7720C3F; // "SplitScreenPreferences"
constexpr uint32_t kSoundOptions = 0x89EB9738;           // "SoundOptions"
constexpr uint32_t kMasterGainKey = 0x6F016DFB;          // "master_loudness"
constexpr uint32_t kMusicVolume = 0xABD4A575;            // "MusicVolume"
constexpr uint32_t kMusicVolOutsideBox = 0xE3C22B01;     // "MusicVolOutsideBox"
constexpr uint32_t kForbiddenTracks1 = 0x595D2C95;       // "PlayListForbiddenTrackFlags1"
constexpr uint32_t kForbiddenTracks2 = 0xC0547D2F;       // "PlayListForbiddenTrackFlags2"
constexpr uint32_t kForbiddenTracks3 = 0xB7534DB9;       // (name unknown) the second set's high half
constexpr uint32_t kForbiddenTracks4 = 0x2937D81A;       // (name unknown) its low half
constexpr uint32_t kCurrentSoundtrack = 0xE1F7C4AE;      // "current_soundtrack"
constexpr uint32_t kRandomMode = 0x31C71B70;             // "RandomMode"
constexpr uint32_t kPadSettingsKey = 0x37BDB853;         // "ControllerPreferences"
constexpr uint32_t kVibrationOn = 0x73CEE124;            // "VibrationOn"
constexpr uint32_t kAutokickOn = 0x1EEF7085;             // "AutokickOn"
constexpr uint32_t kSpinTapsOn = 0xA483BA67;             // "SpinTapsOn"
constexpr uint32_t kTaunts = 0xE62B6586;                 // "Taunts"
constexpr uint32_t kPros = 0xC9986BAF;                   // "Pros"
constexpr uint32_t kGlobalInfo = 0xF55CBD13;             // "GlobalInfo"
constexpr uint32_t kStory = 0x14A9FBC7;                  // "Story"
constexpr uint32_t kStorySkater = 0x0DF2F448;            // "StorySkater"
constexpr uint32_t kCustomSkater = 0x12BFAC82;           // "CustomSkater"
constexpr uint32_t kGoalManagerParams = 0xAC7C2B62;      // "GoalManagerParams"
constexpr uint32_t kLayerInfos = 0x9A5FF0A3;             // "layer_infos"
constexpr uint32_t kGoalEditor = 0x81F01058;             // "GoalEditor": the object and its component
constexpr int kControllers = 8;

modules::Skate* get_skate() { return modules::Skate::sole(); }
objects::ProfileRoster* get_profiles() { return objects::g_profile_roster.get(); }
highscores::BestResults* game_records() { return highscores::g_game_records.get(); }
online::Hub* online_hub_handle() { return online::g_online_hub; }
objects::RiderStoryProgress* get_career() { return get_skate()->career_.get(); }
int os_layer() { return game_config::g_platform; }

// The skate module's split-screen preferences (0x00564D80, no source yet).
prefs::Preferences* split_screen_preferences() { return get_skate()->get_split_view_settings(); }
// A preferences object's structure.
Struct* preferences_structure(prefs::Preferences* preferences) { return &preferences->top_entry_; }

// The goal editor's component (its object's, both named GoalEditor).
objects::ObjectModule* goal_editor_component()
{
    objects::Entity* object = objects::g_tracker->look_up_object(kGoalEditor);
    return core::down_cast<objects::GameEntity>(object)->fetch_module(kGoalEditor);
}

// The skate module's controller settings: each controller's three flags (vibration, autokick, spin taps).
const modules::Skate::PlayerSettings& controller_flags(int controller)
{
    return core::at(get_skate()->participants_, controller);
}


} // namespace

// 0x005A7560
void write_global_info(Struct* global)
{
    gsl::owner<Struct*> options = script::new_struct();
    prefs::Preferences* split_screen = split_screen_preferences();
    gsl::owner<Struct*> split_screen_copy = script::new_struct();
    split_screen_copy->append_structure_if_any(preferences_structure(split_screen));
    options->append_struct_ref(kSplitScreenPreferences, split_screen_copy);

    gsl::owner<Struct*> sound = script::new_struct();
    sound->push_float(kMasterGainKey, sound::fetch_master_loudness());
    sound->push_float(kMusicVolume, script::fetch_float(kMusicVolOutsideBox));
    std::array<uint64_t, 2> tracks_off{};
    audio::get_tracks_off(&core::at(tracks_off, 0), &core::at(tracks_off, 1));
    sound->put_int(kForbiddenTracks1, static_cast<int>(core::at(tracks_off, 0) >> 32));
    sound->put_int(kForbiddenTracks2, static_cast<int>(core::at(tracks_off, 0)));
    sound->put_int(kForbiddenTracks3, static_cast<int>(core::at(tracks_off, 1) >> 32));
    sound->put_int(kForbiddenTracks4, static_cast<int>(core::at(tracks_off, 1)));
    if (os_layer() == 4)
        sound->put_name_key(kCurrentSoundtrack, script::get_symbol_value("current_soundtrack"));
    if (audio::random_track_order())
        sound->put_name_key(0u, kRandomMode);
    options->append_struct_ref(kSoundOptions, sound);

    gsl::owner<Array*> controllers = script::alloc_script_array();
    if (controllers)
        controllers = new (controllers) script::Array;
    controllers->configure_size_kind(kControllers, script::kQbvalStruct);
    for (int i = 0; i < kControllers; ++i) {
        gsl::owner<Struct*> controller = script::new_struct();
        const modules::Skate::PlayerSettings& flags = controller_flags(i);
        if (flags.auto_kick_)
            controller->put_name_key(0u, kAutokickOn);
        if (flags.spin_taps_)
            controller->put_name_key(0u, kSpinTapsOn);
        if (flags.vibration_)
            controller->put_name_key(0u, kVibrationOn);
        controllers->set_cell(static_cast<uint32_t>(i), script::Cell::of(controller));
    }
    options->append_array_ref(kPadSettingsKey, controllers);
    options->put_sub_struct(kTaunts, preferences_structure(online_hub_handle()->get_taunt_settings()));
    global->append_struct_ref(kOptions, options);

    get_career()->write_to_structure(global);
    game_records()->write_to_structure(global);
    gsl::owner<Struct*> pros = script::new_struct();
    get_profiles()->write_pros(pros);
    global->append_struct_ref(kPros, pros);
}

// 0x005A7CF0
void read_save_header(Struct* global, Script* script)
{
    Struct* options = nullptr;
    global->read_sub_struct(kOptions, &options, 0);
    Struct* split_screen = nullptr;
    options->read_sub_struct(kSplitScreenPreferences, &split_screen, 0);
    split_screen_preferences()->set_preferences(split_screen);
    Struct* taunts = nullptr;
    options->read_sub_struct(kTaunts, &taunts, 0);
    online_hub_handle()->get_taunt_settings()->set_preferences(taunts);

    Struct* sound = nullptr;
    options->read_sub_struct(kSoundOptions, &sound, 0);
    auto* sfx = sound::g_audio_mixer.get();
    float main_volume = 0.0f;
    if (sound->fetch_float(kMasterGainKey, &main_volume, 0))
        sfx->change_master_loudness(main_volume);
    float music_volume = 0.0f;
    if (sound->fetch_float(kMusicVolume, &music_volume, 0)) {
        audio::set_music_volume(music_volume);
        const script::StructPtr change = script::make_struct();
        change->push_float(kMusicVolOutsideBox, music_volume);
        script::launch_script("Change", change.get(), nullptr, false);
    }
    std::array<int, 4> tracks = {};
    if (sound->fetch_int_value(kForbiddenTracks1, &core::at(tracks, 0), 0)) {
        sound->fetch_int_value(kForbiddenTracks2, &core::at(tracks, 1), 0);
        sound->fetch_int_value(kForbiddenTracks3, &core::at(tracks, 2), 0);
        sound->fetch_int_value(kForbiddenTracks4, &core::at(tracks, 3), 0);
        const uint64_t low =
            (static_cast<uint64_t>(static_cast<uint32_t>(core::at(tracks, 0))) << 32) | static_cast<uint32_t>(core::at(tracks, 1));
        const uint64_t high =
            (static_cast<uint64_t>(static_cast<uint32_t>(core::at(tracks, 2))) << 32) | static_cast<uint32_t>(core::at(tracks, 3));
        audio::set_tracks_off(low, high);
    }
    audio::set_random_mode(sound->has_bare_flag(kRandomMode) ? 1 : 0);
    if (os_layer() == 4) {
        uint32_t soundtrack = 0xFFFFFFFF;
        sound->read_hash_value(kCurrentSoundtrack, &soundtrack, 0);
        if (script::SymbolTableEntry* symbol = script::find_symbol(kCurrentSoundtrack))
            symbol->value.set_word(soundtrack);
        const script::StructPtr change = script::make_struct();
        change->put_name_key(0x8C465905u, soundtrack);                       // trackchecksum
        script::launch_script(0xD44400C2u, change.get(), nullptr, false, nullptr); // set_loaded_soundtrack
    }

    Array* controllers = nullptr;
    if (options->fetch_list(kPadSettingsKey, &controllers, 0)) {
        modules::Skate* skate = get_skate();
        for (int i = 0; i < kControllers; ++i) {
            auto* controller = controllers->struct_at(static_cast<uint32_t>(i));
            // the skate module's setters (no source yet): vibration, autokick, spin taps
            skate->change_rumble(i, controller->has_bare_flag(kVibrationOn));
            skate->set_auto_eject(i, controller->has_bare_flag(kAutokickOn));
            skate->set_turn_tap_count(i, controller->has_bare_flag(kSpinTapsOn));
        }
    }
    game_records()->decode_from_struct(global);
    Struct* pros = nullptr;
    global->read_sub_struct(kPros, &pros, 1);
    get_profiles()->read_pros(pros);
    get_career()->decode_from_struct(global);

    constexpr uint32_t kLastLevelLoadScript = 0xE3335D2F, kLastGameMode = 0x2CC06F5E;
    Struct* career = nullptr;
    global->read_sub_struct(0x4DA4937Bu, &career, 0); // career
    uint32_t last_level = 0;
    uint32_t last_mode = 0;
    if (career) {
        career->read_hash_value(kLastLevelLoadScript, &last_level, 0);
        career->read_hash_value(kLastGameMode, &last_mode, 0);
    }
    Struct* results = script->args_.get();
    if (last_level)
        results->put_name_key(kLastLevelLoadScript, last_level);
    if (last_mode)
        results->put_name_key(kLastGameMode, last_mode);

    // the first profile: a custom skater keeps its CAS file name
    // (0x00564F80, the overload taking the params - none: not fetch_account(int))
    auto* profile = get_skate()->fetch_account(static_cast<Struct*>(nullptr));
    if (!profile->is_pro()) {
        const char* cas_file_name = "Unimplemented";        // "Unimplemented"
        global->fetch_text(0xF36C1878u, &cas_file_name, 0); // CASFileName
        profile->set_string_f_36c_1878(cas_file_name);
    }
}

// 0x005A79A0
void write_save_contents(Struct* contents, uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals:
        // the goal editor's goals written (0x004EB850, no source yet)
        core::down_cast<park_editing::ObjectiveAuthoring>(goal_editor_component())->write_goals(contents);
        return;
    case kSaveCat: {
        auto* skater = core::down_cast<objects::Rider>(get_skate()->get_rider_at(0));
        const goals::CreateATrick* trick_record = core::at(skater->trick_slots_, 0).get();
        contents->put_sub_struct("other_params", trick_record->other_params_);
        contents->append_array_value("rotation_info", trick_record->spin_list_);
        contents->append_array_value("animation_info", trick_record->animations_);
        return;
    }
    case kSavePark:
        park_editing::ParkBuilderMode::sole()->export_to_struct(contents);
        return;
    case kSaveNetworkSettings:
        contents->append_structure_if_any(preferences_structure(online_hub_handle()->get_online_settings()));
        return;
    case kSaveOptionsAndPros: {
        gsl::owner<Struct*> global = script::new_struct();
        write_global_info(global);
        contents->append_struct_ref(kGlobalInfo, global);
        gsl::owner<Struct*> story = script::new_struct();
        // the goal manager's parameters (the shared getter of +0x14)
        story->put_sub_struct(kGoalManagerParams, goals::get_objective_board()->goal_data_);
        contents->append_struct_ref(kStory, story);
        gsl::owner<Struct*> story_skater = script::new_struct();
        auto* skater = core::down_cast<objects::Rider>(get_skate()->get_rider_at(0));
        skater->write_cat_info(story_skater);
        skater->write_list_44b_66a_9f(story_skater);
        skater->write_chapter_info(story_skater);
        contents->append_struct_ref(kStorySkater, story_skater);
        gsl::owner<Struct*> custom_skater = script::new_struct();
        get_profiles()->write_custom_skaters(custom_skater);
        contents->append_struct_ref(kCustomSkater, custom_skater);
        // (on into CreatedGraphic's layer_infos, as the original does)
        contents->append_array_value(kLayerInfos, script::fetch_list(0x8EAD7D43u)); // edit_graphic_layer_infos
        return;
    }
    case kSaveCreatedGraphic:
        contents->append_array_value(kLayerInfos, script::fetch_list(0x8EAD7D43u)); // edit_graphic_layer_infos
        return;
    default:
        return;
    }
}

// 0x005A81B0
void apply_loaded_contents(uint32_t type, Struct* contents, Script* script, Struct* params)
{
    bool& loaded_custom_skater = g_loaded_custom_skater;
    loaded_custom_skater = false;
    switch (type) {
    case kSaveCreatedGoals:
        // the goal editor's goals read (0x004EA470, no source yet)
        core::down_cast<park_editing::ObjectiveAuthoring>(goal_editor_component())->read_goals(contents, 0);
        return;
    case kSaveCat: {
        // the local skater (0x00566520, no source yet)
        const objects::Rider* skater = get_skate()->get_home_rider();
        if (!skater)
            return;
        // (its fields by name: these were the original's byte offsets +4 / +8 / +0xC on a CreateATrick*, which
        // stepped whole objects past it - and the class has no vtable word here)
        goals::CreateATrick* trick_record = core::at(skater->trick_slots_, 0).get();
        Struct* other = nullptr;
        contents->read_sub_struct("other_params", &other, 1);
        trick_record->other_params_->append_structure_if_any(other);
        Array* rotation = nullptr;
        contents->fetch_list("rotation_info", &rotation, 1);
        script::duplicate_array(trick_record->spin_list_, rotation);
        Array* animation = nullptr;
        contents->fetch_list("animation_info", &animation, 1);
        script::duplicate_array(trick_record->animations_, animation);
        return;
    }
    case kSavePark:
        park_editing::ParkBuilderMode::sole()->decode_from_struct(contents, true);
        return;
    case kSaveCreatedGraphic: {
        Array* layers = nullptr;
        contents->fetch_list(kLayerInfos, &layers, 1);
        params->append_array_value(kLayerInfos, layers);
        return;
    }
    case kSaveNetworkSettings:
        online_hub_handle()->get_online_settings()->set_preferences(contents);
        return;
    case kSaveOptionsAndPros: {
        uint32_t& sections = sections_to_apply_when_loading();
        if (sections & 1) {
            debug::printf("APPLY_GLOBAL_INFO\n"); // "APPLY_GLOBAL_INFO\n"
            Struct* global = nullptr;
            contents->read_sub_struct(kGlobalInfo, &global, 1);
            read_save_header(global, script);
        }
        if (sections & 2) {
            debug::printf("APPLY_STORY\n"); // "APPLY_STORY\n"
            Struct* story = nullptr;
            contents->read_sub_struct(kStory, &story, 1);
            apply_goal_manager_params(story);
        }
        if (sections & 8) {
            debug::printf("APPLY_CUSTOM_SKATER\n"); // "APPLY_CUSTOM_SKATER\n"
            Struct* custom_skater = nullptr;
            contents->read_sub_struct(kCustomSkater, &custom_skater, 1);
            objects::ProfileRoster* profiles = get_profiles();
            // the stats kept unless the current skater is the first (the manager's +0x1C, 0x00553E90)
            const bool keep = profiles->cursor_pos_ != 0;
            profiles->read_custom_skater(custom_skater, keep);
            loaded_custom_skater = true;
        }
        if (sections & 4) {
            debug::printf("APPLY_STORY_SKATER\n"); // "APPLY_STORY_SKATER\n"
            Struct* story_skater = nullptr;
            contents->read_sub_struct(kStorySkater, &story_skater, 1);
            Struct* custom_skater = nullptr;
            contents->read_sub_struct(kCustomSkater, &custom_skater, 1);
            apply_cat_and_custom_skater(story_skater, custom_skater);
        }
        Array* layers = nullptr;
        contents->fetch_list(kLayerInfos, &layers, 1);
        params->append_array_value(kLayerInfos, layers);
        sections = 0;
        return;
    }
    default:
        return;
    }
}

} // namespace save_cards
