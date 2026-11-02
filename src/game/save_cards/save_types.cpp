// The save file types and the memory card commands' helpers (see save_types.h).

#include "game/save_cards/save_types.h"
#include "platform/code_address.h"
#include "engine/net/buddy_status.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "game/park_editing/goal_editor.h"
#include "game/park_editing/park.h"
#include "game/park_editing/park_editor.h"
#include "formats/park_map_format.h"
#include "game/goals/goal_manager.h"
#include "game/save_cards/memcard_cfuncs.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/tracker.h"
#include "game/skate/career.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "engine/sys/game_config.h"
#include "game/goals/objective.h"
#include "engine/gfx/rig.h"
#include "game/goals/create_a_trick.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace save_cards {

namespace {

using script::Struct;

constexpr uint32_t kGoalManagerParams = 0xAC7C2B62; // "GoalManagerParams"

// The game's platform (0x007CEFA8: 4 is the PC).
int os_layer() { return game_config::g_platform; }

// The card block of the platform.
int card_block()
{
    switch (os_layer()) {
    case 1:
    case 2:
    case 3:
    case 7:
        return 0x400;
    case 4:
        return 0x4000;
    case 5:
        return 0x2000;
    default:
        return 1;
    }
}

// The goal manager (0x005591B0) and its calls used here (no source yet).
goals::GoalManager* objective_board() { return goals::get_objective_board(); }

// The loaded contents kept for later (0x007CECD4) and their type (0x007CECD8).
Struct*& kept_contents() { return g_kept_params; }
uint32_t& kept_type() { return g_kept_type; }

// A summary's date: year, month, day, hour, minutes, seconds (missing ones 0).
struct SaveDate {
    std::array<int, 6> fields;
};
SaveDate read_stamped_date(Struct* summary)
{
    static const std::array<uint32_t, 6> kNames = {{
        0x447D8CC8, // "year"
        0x7149EFF9, // "month"
        0x1A5FD66F, // "day"
        0x8FE1EEB1, // "hour"
        0x5F94B55C, // "minutes"
        0xD029F619, // "seconds"
    }};
    SaveDate date = {};
    for (int i = 0; i < 6; ++i)
        summary->fetch_int_value(core::at(kNames, i), &core::at(date.fields, i), 0);
    return date;
}

} // namespace

// 0x005A5F70
char* make_file_name(const char* name, std::span<char> buffer, uint32_t type)
{
    uint32_t value = checksums::hash_from_text(name);
    for (int i = 0; i < 8; ++i) {
        core::at(buffer, i) = static_cast<char>('a' + value % 26);
        value /= 26;
    }
    switch (type) {
    case kSaveCreatedGoals:
        core::at(buffer, 7) = 'g';
        break;
    case kSaveCat:
        core::at(buffer, 7) = 'f';
        break;
    case kSavePark:
        core::at(buffer, 7) = 'c';
        break;
    case kSaveCreatedGraphic:
        core::at(buffer, 7) = 'h';
        break;
    case kSaveNetworkSettings:
        core::at(buffer, 7) = 'e';
        break;
    case kSaveOptionsAndPros:
        core::at(buffer, 7) = 'a';
        break;
    default:
        break;
    }
    core::at(buffer, 8) = '\0';
    return buffer.data();
}

// 0x005A60C0
uint32_t type_from_letter(char letter)
{
    switch (letter) {
    case 'a':
        return kSaveOptionsAndPros;
    case 'b':
        return kSaveCas;
    case 'c':
        return kSavePark;
    case 'e':
        return kSaveNetworkSettings;
    case 'f':
        return kSaveCat;
    case 'g':
        return kSaveCreatedGoals;
    case 'h':
        return kSaveCreatedGraphic;
    default:
        return 0;
    }
}

// 0x005A6180
int icon_space_required(uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals:
        return script::fetch_int_value(0x3205BC07u); // CreatedGoalsIconSpaceRequired
    case kSaveCat:
        return script::fetch_int_value(0x46B0EF40u); // CATIconSpaceRequired
    case kSavePark:
        return script::fetch_int_value(0x82AAF8DCu); // ParkIconSpaceRequired
    case kSaveCreatedGraphic:
        return script::fetch_int_value(0xD85D78F9u); // CreatedGraphicIconSpaceRequired
    case kSaveNetworkSettings:
        return script::fetch_int_value(0xF3431F63u); // NetworkSettingsIconSpaceRequired
    case kSaveOptionsAndPros:
        return script::fetch_int_value(0x4925ED9Eu); // OptionsProsIconSpaceRequired
    default:
        return 0;
    }
}

// 0x005A6210
int type_version(uint32_t type)
{
    switch (type) {
    case kSaveCreatedGoals:
        return script::fetch_int_value("VERSION_CREATEDGOALS");
    case kSaveCat:
        return script::fetch_int_value("VERSION_CAT");
    case kSavePark:
        return script::fetch_int_value("VERSION_PARK");
    case kSaveCreatedGraphic:
        return script::fetch_int_value("VERSION_CREATEDGRAPHIC");
    case kSaveNetworkSettings:
        return script::fetch_int_value("VERSION_NETWORKSETTINGS");
    case kSaveOptionsAndPros:
        return script::fetch_int_value("VERSION_OPTIONSANDPROS");
    default:
        return -1;
    }
}

namespace {
std::array<char, 0x68> g_save_file_name; // (the cutscene list's flag follows)
} // namespace

// 0x005A62A0
const char* get_type_file_name(uint32_t type, const char* name)
{
    const char* buffer = g_save_file_name.data();
    const char* extension = nullptr;
    switch (type) {
    case kSaveCat:
        extension = "CAT"; // "Save/%s.CAT"
        break;
    case kSavePark:
        extension = "PRK"; // "Save/%s.PRK"
        break;
    case kSaveReplay:
        extension = "REP"; // "Save/%s.REP"
        break;
    case kSaveCreatedGoals:
        extension = "GOA"; // "Save/%s.GOA"
        break;
    case kSaveNetworkSettings:
        extension = "NWS"; // "Save/%s.NWS"
        break;
    case kSaveOptionsAndPros:
        extension = "SKA"; // "Save/%s.SKA"
        break;
    case kSaveCreatedGraphic:
        extension = "GRF"; // "Save/%s.GRF"
        break;
    default:
        break;
    }
    if (extension) // (no limit in the original: a longer name ran on into other globals)
        text::format_to(g_save_file_name, "Save/%s.%s", name, extension);
    return buffer;
}

// 0x005A6390
int make_folder_file_name(uint32_t type, const char* name, char* buffer)
{
    std::array<char, 20> file{};
    make_file_name(name, file, type);
    const char* folder = game_config::get_path_prefix();
    return static_cast<int>(
        text::format_to(std::span(buffer, 0x100), "/%s%s", folder, file.data())); // (no limit in the original)
}

// 0x005A63E0
bool is_later(Struct* first, Struct* second)
{
    const SaveDate a = read_stamped_date(first);
    const SaveDate b = read_stamped_date(second);
    for (int i = 0; i < 6; ++i) {
        if (core::at(a.fields, i) != core::at(b.fields, i))
            return core::at(a.fields, i) > core::at(b.fields, i);
    }
    return false;
}

// 0x005A6CD0
int round_up_to_card_block(int size)
{
    const int block = card_block();
    if (size % block == 0)
        return size;
    return (size / block + 1) * block;
}

// 0x005A7340
int type_file_size(uint32_t type)
{
    int size = 0;
    switch (type) {
    case kSaveCreatedGoals:
        size = script::fetch_int_value(0x3308EFC0u);
        break; // SaveSize_CreatedGoals
    case kSaveCat:
        size = script::fetch_int_value(0x9EECDEC9u);
        break; // SaveSize_Cat
    case kSavePark:
        size = script::fetch_int_value(0x2CB071EDu);
        break; // SaveSize_Park
    case kSaveCreatedGraphic:
        size = script::fetch_int_value(0x2E3F4E50u);
        break; // SaveSize_CreatedGraphic
    case kSaveNetworkSettings:
        size = script::fetch_int_value(0x651C978Du);
        break; // SaveSize_NetworkSettings
    case kSaveOptionsAndPros:
        size = script::fetch_int_value(0xE7F51FFEu);
        break; // SaveSize_OptionsAndPros
    default:
        break;
    }
    return round_up_to_card_block(size + 0x14);
}

// 0x005A8470
int save_blocks(uint32_t type, int size)
{
    const int platform = os_layer();
    const int block = card_block();
    const int blocks = round_up_to_card_block(size) / block;
    if (platform <= 0)
        return blocks;
    if (platform <= 3)
        return blocks + icon_space_required(type) + 4;
    if (platform == 4)
        return blocks + 3;
    return blocks;
}

// 0x005A6610
void write_summary(Struct* saved, Struct* summary, uint32_t type)
{
    constexpr uint32_t kGoals = 0x38DBE1D0;          // "goals"
    constexpr uint32_t kNumEditedGoals = 0xE1EC606F; // "num_edited_goals"
    constexpr uint32_t kMaxPlayers = 0xB7E39B53;     // "MaxPlayers"
    constexpr uint32_t kName = 0xA1DC81F9;           // "name"
    constexpr uint32_t kTotalGoalsComplete = 0x959DCF6E, kTotalGoalsPossible = 0xA95F9E73;
    constexpr uint32_t kTotalScore = 0x0E145E9C, kTotalScorePotential = 0x851B0904;
    constexpr uint32_t kIsMale = 0x3F813177; // "is_male"
    const char* const empty = "";            // ""
    park_editing::ParkBuilderMode* editor = park_editing::ParkBuilderMode::sole();
    // The park map's numbers, in get_map_info's order: num_gaps, num_pieces, theme (16 bits), tod_script, width,
    // length.
    auto add_map_info = [&](const park_editing::ParkMap* map) {
        int gaps = 0;
        int pieces = 0;
        uint16_t theme = 0;
        uint32_t tod_script = 0;
        int width = 0;
        int length = 0;
        editor->get_map_info(map, &gaps, &pieces, &theme, &tod_script, &width, &length);
        summary->put_int(0xE6121ED0u, gaps);            // num_gaps
        summary->put_int(0xFFF3DC35u, pieces);          // num_pieces
        summary->put_int(0x688A18F7u, theme);           // theme
        summary->put_name_key(0x4C72ED98u, tod_script); // tod_script
        summary->put_int(0x73E5BAD0u, width);           // width
        summary->put_int(0xFE82614Du, length);          // length
    };

    if (saved) {
        switch (type) {
        case kSaveCat: {
            Struct* other = nullptr;
            saved->read_sub_struct(0x4137CB14u, &other, 1); // other_params
            const char* name = empty;
            other->fetch_text(kName, &name, 1);
            summary->append_word(kName, name);
            break;
        }
        case kSavePark: {
            Struct* goals = nullptr;
            saved->read_sub_struct(0xD8EB825Eu, &goals, 0); // Park_editor_goals
            int count = 0;
            if (goals) {
                script::Array* array = nullptr;
                goals->fetch_list(kGoals, &array, 0);
                if (array)
                    count = static_cast<int>(array->extent_);
            }
            summary->put_int(kNumEditedGoals, count);
            int max_players = 1;
            saved->fetch_int_value(kMaxPlayers, &max_players, 0);
            summary->put_int(kMaxPlayers, max_players);
            script::Array* map = nullptr;
            saved->fetch_list(0x337C5289u, &map, 1); // Park_editor_map
            // (the map's words into a map of its own: NOT ORIGINAL - the original read the array's storage as
            // a map, past the end of a short one)
            auto copy = std::make_unique<park_editing::ParkMap>();
            const std::vector<uint32_t> words = script::words_of(map);
            park_editing::park_map_from_words(copy->bytes(), words);
            add_map_info(copy.get());
            break;
        }
        case kSaveCreatedGoals: {
            script::Array* goals = nullptr;
            saved->fetch_list(kGoals, &goals, 0);
            summary->put_int(kNumEditedGoals, goals ? static_cast<int>(goals->extent_) : 0);
            break;
        }
        case kSaveOptionsAndPros: {
            Struct* story = nullptr;
            saved->read_sub_struct(0x14A9FBC7u, &story, 1); // Story
            Struct* goal_manager = nullptr;
            story->read_sub_struct(kGoalManagerParams, &goal_manager, 1);
            Struct* goal_manager_params = nullptr;                               // (read, not used)
            goal_manager->read_sub_struct(0x23D4170Au, &goal_manager_params, 1); // GoalManager_Params
            Struct* global = nullptr;
            saved->read_sub_struct(0xF55CBD13u, &global, 0); // GlobalInfo
            if (global) {
                Struct* career = nullptr;
                global->read_sub_struct(0x4DA4937Bu, &career, 0); // career
                int value = 0; // (not initialised: a missing field after the first repeats the one before)
                if (career && career->fetch_int_value(0x6C1A5E39u, &value, 0)) {
                    summary->put_int(kTotalGoalsComplete, value);
                    career->fetch_int_value(0x7A6BF912u, &value, 0);
                    summary->put_int(kTotalGoalsPossible, value);
                    career->fetch_int_value(0x0444419Au, &value, 0);
                    summary->put_int(kTotalScore, value);
                    career->fetch_int_value(0x95947B61u, &value, 0);
                    summary->put_int(kTotalScorePotential, value);
                }
            }
            Struct* custom_skater = nullptr;
            saved->read_sub_struct(0x12BFAC82u, &custom_skater, 1); // CustomSkater
            Struct* custom = nullptr;
            custom_skater->read_sub_struct(0x0A7BE964u, &custom, 1); // custom
            Struct* info = nullptr;
            custom->read_sub_struct(0x3476CEA8u, &info, 1); // info
            int male = 0;
            info->fetch_int_value(kIsMale, &male, 0);
            summary->put_int(kIsMale, male);
            const char* name = empty;
            info->fetch_text(0x2AB66CB8u, &name, 0); // display_name
            summary->append_word(kName, name);
            break;
        }
        case kSaveNetworkSettings:
            summary->append_word("network_id", empty);
            break;
        default:
            break;
        }
        return;
    }

    switch (type) {
    case kSaveCat: {
        auto* skater = core::down_cast<objects::Rider>(modules::Skate::sole()->get_rider_at(0));
        Struct* trick_record = core::at(skater->trick_slots_, 0)->other_params_;
        const char* name = empty;
        trick_record->fetch_text(kName, &name, 1);
        summary->append_word(kName, name);
        break;
    }
    case kSavePark:
    case kSaveCreatedGoals: {
        constexpr uint32_t kGoalEditor = 0x81F01058; // "GoalEditor": the object and its component
        objects::Entity* object = objects::g_tracker->look_up_object(kGoalEditor);
        objects::ObjectModule* component = core::down_cast<objects::GameEntity>(object)->fetch_module(kGoalEditor);
        // its number of edited goals (0x004E7E00, no source yet)
        const int edited = core::down_cast<park_editing::ObjectiveAuthoring>(component)->count_used_goals();
        summary->put_int(kNumEditedGoals, edited);
        if (type != kSavePark)
            break;
        summary->put_int(kMaxPlayers, park_editing::ParkBuilderMode::get_player_limit());
        park_editing::ParkBuilderMode::sole()->write_compressed_map_if_built();
        editor = park_editing::ParkBuilderMode::sole();
        add_map_info(editor->get_packed_park_data(false));
        break;
    }
    case kSaveNetworkSettings: {
        Struct* settings = &online::g_online_hub->get_online_settings()->top_entry_;
        Struct* network_id = nullptr;
        settings->read_sub_struct(0xCBED746Eu, &network_id, 0); // network_id
        const char* text = empty;
        network_id->fetch_text(0x96875C0Fu, &text, 0); // ui_string
        summary->append_word(0xCBED746Eu, text);
        break;
    }
    case kSaveOptionsAndPros: {
        Struct* own = objective_board()->goal_data_; // (the shared getter of +0x14)
        Struct* goal_manager_params = nullptr;       // (read, not used)
        own->read_sub_struct(0x23D4170Au, &goal_manager_params, 0);
        const objects::RiderStoryProgress* career = modules::Skate::sole()->career_.get();
        summary->put_int(kTotalScore, career->lifetime_funds_);
        summary->put_int(kTotalScorePotential, career->total_amount_);
        summary->put_int(kTotalGoalsComplete, career->total_counter_);
        summary->put_int(kTotalGoalsPossible, career->flag_5e_f_);
        break;
    }
    default:
        break;
    }
}

// 0x005A6120
void apply_goal_manager_params(Struct* saved)
{
    Struct* params = nullptr;
    saved->read_sub_struct(kGoalManagerParams, &params, 0);
    goals::GoalManager* goal_manager = objective_board();
    goal_manager->reset_goal_data(0); // reset the career (mode 0)
    // its parameters (the shared getter of +0x14)
    Struct* own = goal_manager->goal_data_;
    own->get_clear();
    own->append_structure_if_any(params);
    goal_manager->restore_goal_data(); // the level's goals loaded
}

// 0x005A6C80
bool apply_cat_and_custom_skater(Struct* loaded, Struct* custom_skater)
{
    objects::ProfileRoster* profiles = objects::g_profile_roster.get();
    modules::Skate* skate = modules::Skate::sole();
    // the current skater (the manager's +0x1C, through the folded getter 0x00553E90)
    const int index = profiles->cursor_pos_;
    auto* skater = core::down_cast<objects::Rider>(skate->get_rider_at(index));
    skater->load_cat_info(loaded);
    skater->load_list_44b_66a_9f(loaded);
    skater->read_chapter_info(loaded);
    return profiles->read_custom_skater(custom_skater, true);
}

// 0x005A8EB0
const uint8_t* keep_loaded_contents(const uint8_t* data, size_t size, uint32_t type)
{
    if (Struct* kept = kept_contents()) {
        kept->get_clear();
        script::delete_struct(kept);
        kept_contents() = nullptr;
    }
    gsl::owner<Struct*> contents = script::spawn_param_bag();
    if (contents)
        contents = new (contents) script::Struct;
    kept_contents() = contents;
    const uint8_t* end = script::decode_from_bytes(contents, data, size);
    kept_type() = type;
    return end;
}

} // namespace save_cards
