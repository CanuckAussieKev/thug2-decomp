// Start in a level (game/front_end/start_level.h).

#include "game/front_end/start_level.h"

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <array>

#include "base/core/checksums.h"
#include "engine/memory/memory_report.h"
#include "engine/file/file_load.h"
#include "game/front_end/front_end.h"
#include "engine/ext/extension.h"
#include "game/front_end/menu_pad_event.h"
#include "game/rider/test_view.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/script/names.h"
#include "engine/sys/editor_mode.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "platform/process_flags.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"

namespace frontend::begin_level {

namespace {

constexpr std::size_t kValueSize = 128;
constexpr std::size_t kPathSize = 160;
constexpr int kSettleFrames = 60;              // the menu runs this long before the level starts

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

// The command line as the process got it (the game's own parsing cuts the process's buffer into words later: a 0
// after the first word - platform/process_flags.h).
const std::string& command_line() { return sys::startup_command_line(); }

std::string from_ini(const char* key)
{
    std::array<char, kValueSize> value = {};
    GetPrivateProfileStringA("features", key, "", value.data(), sizeof(value), game_folder_path("thug2-decomp.ini").c_str());
    return value.data();
}

// The level and where it came from: -edit, -level, edit_level=, start_level= (the first given).
struct Request {
    std::string level;
    bool edit = false;
};
const Request& the_request()
{
    static const Request kRequest = [] {
        Request request{.level = option_value(command_line(), "edit"), .edit = true};
        if (request.level.empty()) {
            request = {.level = option_value(command_line(), "level"), .edit = false};
        }
        if (request.level.empty()) {
            request = {.level = from_ini("edit_level"), .edit = true};
        }
        if (request.level.empty()) {
            request = {.level = from_ini("start_level"), .edit = false};
        }
        return request;
    }();
    return kRequest;
}

// Whether Level_<name>'s scene can load: a level without a scene pre (scnpre) needs levels\<level>\<level>.scn.xbx
// (data folder or overlay). The DEVKIT_ONLY level Default has none on PC (only its scripts and sky ship): the game's
// scene load returns no scene for it and writes through it (0x0048C5AA). Levels with a scene pre aren't checked.
bool scene_installed_in(script::Struct* info)
{
    const char* scene = nullptr;
    if (!info || info->has_entry_named("scnpre") || !info->fetch_text("level", &scene, 0) || !scene) {
        return true;
    }
    std::array<char, kPathSize> path{};
    text::format_to(path, "levels\\%s\\%s.scn.xbx", scene, scene);
    return file::is_present(path.data());
}
bool scene_installed(const std::string& name) { return scene_installed_in(script::read_sub_struct(("Level_" + name).c_str())); }

// NOT ORIGINAL - the level structure whose load_script is `load` (levels.qb's Level_<x>, and the ported levels' that
// make_menu_level adds the same way), or none.
script::Struct* level_info_of(uint32_t load)
{
    static const uint32_t kLoadScript = checksums::hash_from_text("load_script");
    for (script::SymbolTableEntry* entry = script::get_next_symbol(nullptr); entry != nullptr;
         entry = script::get_next_symbol(entry)) {
        uint32_t its_load = 0;
        if (entry->type() == script::QbValueKind::kStruct && entry->value().structure() != nullptr &&
            entry->value().structure()->read_hash_value(kLoadScript, &its_load, 0) && its_load == load) {
            return entry->value().structure();
        }
    }
    return nullptr;
}

// What read_in and read_in_load run once the level is known: the level select's SKATE NOW with that level chosen
// (`edited_copy`: with its edited_layout flag - the level editor's copy of the level, editor/level_editor_layout.h).
void start_free_skate(uint32_t load, bool edited_copy)
{
    script::launch_script(name_hash("EnableSun"), nullptr, nullptr, false, "EnableSun");
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{0U}, name_hash("freeskate"));
    script::launch_script(name_hash("SetGameType"), params, nullptr, false, "SetGameType");
    params->get_clear();
    script::launch_script(name_hash("SetCurrentGameType"), nullptr, nullptr, false, "SetCurrentGameType");
    params->put_name_key(checksums::Name{name_hash("level")}, load);
    if (edited_copy) {
        params->put_name_key(checksums::Name{0U}, name_hash("edited_layout"));
    }
    script::spawn_script(name_hash("level_select_change_level"), params);
    params->get_clear();
    script::delete_struct(params);
}

// NOT ORIGINAL grouping - start_level.cpp's state (the original's globals). Made on first use, never destroyed
// (start_level_state()).
struct StartLevelState {
    int frames_{}; // the front end's frames with the main menu's scripts loaded
    bool done_{};
};

StartLevelState& start_level_state()
{
    static core::NoDestroy<StartLevelState> state;
    return state.get();
}

constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kLevelSettleFrames = 90;   // (in the level, unpaused, before its script pool use is logged)
// NOT ORIGINAL grouping - the requested level's load: whether it began, its script pool use logged (the original's
// globals). Made on first use, never destroyed (started_load()).
struct StartedLoad {
    bool started_{}; // the requested level's load began
    bool pools_logged_{}; // its script pool use logged (Scripts::log_script_pool_use)
    int pool_frames_{};
};

StartedLoad& started_load()
{
    static core::NoDestroy<StartedLoad> state;
    return state.get();
}

// -at / start_at: the skater put there standing once the level is up (at_update).
constexpr int kAtSettleFrames = 60; // (in the level, unpaused, before the teleport)
enum class At : uint8_t { kDisengaged, kWaitLevel, kDone };
// NOT ORIGINAL grouping - the spot to start at and the frames spent placing the skater there (the original's globals).
// Made on first use, never destroyed (start_spot()).
struct StartSpot {
    At at_ = At::kDisengaged;
    int at_frames_{};
};

StartSpot& start_spot()
{
    static core::NoDestroy<StartSpot> state;
    return state.get();
}

// The spot asked for ("" if none): -at, then start_at=.
const std::string& at_text()
{
    static const std::string kText = [] {
        const std::string at_option = option_value(command_line(), "at");
        return at_option.empty() ? from_ini("start_at") : at_option;
    }();
    return kText;
}

// True while the teleport is still to come (-edit's editor waits for it).
bool at_update(modules::FrontEnd* front_end)
{
    if (start_spot().at_ != At::kWaitLevel) {
        return false;
    }
    const bool in_level = render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
    const bool paused = (front_end != nullptr) && front_end->is_halted();
    start_spot().at_frames_ = in_level && !paused ? start_spot().at_frames_ + 1 : 0;
    if (start_spot().at_frames_ < kAtSettleFrames) {
        return true;
    }
    start_spot().at_ = At::kDone;
    Spot spot;
    parse_spot(at_text(), &spot); // (checked when it was asked for)
    test_view::Vec3 at{};
    const std::string failed = test_view::jump_to({.x = spot.x, .y = spot.y, .z = spot.z}, spot.has_heading_, spot.heading, &at);
    if (!failed.empty()) {
        get_log("start at: %s - the skater stays where the level put it", failed.c_str());
    } else {
        get_log("start at: the skater stands at (%.1f, %.1f, %.1f) in %s", at.x, at.y, at.z, get_requested());
    }
    return false;
}

} // namespace

std::string option_value(const std::string& line, const char* option)
{
    const std::string name = option;
    for (const std::string& form : {"--" + name + "=", "--" + name + " ", "-" + name + " "}) {
        for (size_t at = line.find(form); at != std::string::npos; at = line.find(form, at + 1)) {
            // (a word of its own: at the start, or after a space or a quote - not inside a path such as C:\my-level x)
            if (at > 0 && core::at(line, at - 1) != ' ' && core::at(line, at - 1) != '\t' &&
                core::at(line, at - 1) != '"') {
                continue;
            }
            const size_t start = line.find_first_not_of(' ', at + form.size());
            if (start == std::string::npos) {
                return {};
            }
            if (core::at(line, start) == '"') {
                const size_t end = line.find('"', start + 1);
                return line.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
            }
            const size_t end = line.find_first_of(" \"", start);
            return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
    }
    return {};
}

bool parse_spot(const std::string& text, Spot* out)
{
    std::string trimmed = text;
    // (the sidecar's "(x, y, z)": one pair of parentheses around the whole)
    const size_t first = trimmed.find_first_not_of(" \t");
    const size_t last = trimmed.find_last_not_of(" \t");
    if (first == std::string::npos) {
        return false;
    }
    trimmed = trimmed.substr(first, last - first + 1);
    if (!trimmed.empty() && trimmed.front() == '(' && trimmed.back() == ')') {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    std::array<float, 4> values{};
    int count = 0;
    size_t at = 0;
    for (;;) {
        at = trimmed.find_first_not_of(" \t", at);
        if (at == std::string::npos || count == 4) {
            return false;
        }
        const char* start = std::next(trimmed.c_str(), static_cast<std::ptrdiff_t>(at));
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start || !std::isfinite(v)) {
            return false;
        }
        core::at(values, count++) = static_cast<float>(v);
        at = static_cast<size_t>(end - trimmed.c_str());
        at = trimmed.find_first_not_of(" \t", at);
        if (at == std::string::npos) {
            break;
        }
        if (core::at(trimmed, at) == ',') {
            ++at; // (a comma, or only spaces, between two numbers)
        } else if (at > 0 && core::at(trimmed, at - 1) != ' ' && core::at(trimmed, at - 1) != '\t') {
            return false; // (something glued to the number: "1x")
        }
    }
    if (count < 3) {
        return false;
    }
    if (out) {
        *out = {};
        out->x = core::at(values, 0);
        out->y = core::at(values, 1);
        out->z = core::at(values, 2);
        out->has_heading_ = count == 4;
        out->heading = count == 4 ? core::at(values, 3) : 0.0F;
    }
    return true;
}

const char* get_requested() { return the_request().level.c_str(); }

// (the editor mode opens the level it starts in in the editor: -level as -edit - engine/sys/editor_mode.h)
bool edit_requested() { return (the_request().edit || sys::editor_mode::on()) && !the_request().level.empty(); }

void tick_frame(modules::FrontEnd* front_end)
{
    if (frame_loop::frames_run() == 0) {
        return;
    }
    if (*get_requested() == '\0') {
        if (!start_level_state().done_ && !at_text().empty()) {
            start_level_state().done_ = true;
            get_log("start at: %s asked for without a level (-level / start_level) - ignored", at_text().c_str());
        }
        return;
    }
    if (start_level_state().done_) {
        // (once the level is up and settled: what it costs the script pools - for the converters' load checks,
        // tools/game/load_check.ps1)
        if (!started_load().pools_logged_ && started_load().started_) {
            const bool up = render::current_level() != 0 && objects::find_object(kSkater) != nullptr &&
                            !((front_end != nullptr) && front_end->is_halted());
            started_load().pool_frames_ = up ? started_load().pool_frames_ + 1 : 0;
            if (started_load().pool_frames_ >= kLevelSettleFrames) {
                started_load().pools_logged_ = true;
                script::log_script_pool_use((std::string("in ") + get_requested()).c_str());
                memory::log_memory_use((std::string("in ") + get_requested()).c_str());
            }
        }
        if (at_update(front_end)) {
            return; // (-edit's editor once the skater stands there)
        }
        // (-edit: the editor's own steps to open in the level - editor/level_editor_open.h)
        ext::editor_opening_frame(front_end);
        return;
    }
    // In the front end, with the main menu up: its scripts loaded (not the early self-tests' stand-in symbol table,
    // which run this before the game is up), then a second more for the menu to settle.
    // (not original either: the editor mode's main menu may be the game's own - the new front end's overlay unused -,
    // whose scripts have main_menu_exit instead - engine/sys/editor_mode.h)
    const bool menu_scripts = is_script(name_hash("main_menu_skate_now")) ||
                              (sys::editor_mode::on() && is_script(name_hash("main_menu_exit")));
    if (render::current_level() != 0 || (front_end && front_end->is_halted()) || !menu_scripts) {
        return;
    }
    if (++start_level_state().frames_ < kSettleFrames) {
        return;
    }
    start_level_state().done_ = true;
    const bool loaded = read_in(get_requested(), nullptr);
    started_load().started_ = loaded;
    if (loaded && !at_text().empty()) {
        if (parse_spot(at_text(), nullptr)) {
            start_spot().at_ = At::kWaitLevel;
            start_spot().at_frames_ = 0;
            get_log("start at: the skater is put at %s standing once %s is up", at_text().c_str(), get_requested());
        } else {
            get_log("start at: \"%s\" isn't x,y,z or x,y,z,heading - ignored", at_text().c_str());
        }
    }
    if (loaded && edit_requested()) {
        ext::open_editor_when_up();
    }
}

bool read_in(const char* name, std::string* why)
{
    std::string level = name;
    if (_strnicmp(level.c_str(), "load_", kLoadPrefixLength) != 0) {
        level = "Load_" + level;
    }
    const uint32_t load = name_hash(level.c_str());
    if (!is_script(load)) {
        get_log("start level: %s isn't a level's load script - the main menu stays", level.c_str());
        if (why) {
            *why = level + " isn't a level's load script";
        }
        return false;
    }
    if (!scene_installed(level.substr(kLoadPrefixLength))) {
        get_log("start level: %s - the level's scene isn't installed (the game would crash loading it) - the main menu "
                "stays",
                level.c_str());
        if (why) {
            *why = level + ": the level's scene isn't installed";
        }
        return false;
    }
    get_log("start level: %s (free skate)", level.c_str());
    start_free_skate(load, false);
    return true;
}

bool read_in_load(uint32_t load, bool edited_copy, std::string* why)
{
    const char* name = script::look_up_hash_name(load);
    const std::string level = name != nullptr ? std::string(name) : text::format("%08X", load);
    if (!is_script(load)) {
        get_log("start level: %s isn't a level's load script", level.c_str());
        if (why) {
            *why = level + " isn't a level's load script";
        }
        return false;
    }
    if (!scene_installed_in(level_info_of(load))) {
        get_log("start level: %s - the level's scene isn't installed (the game would crash loading it)", level.c_str());
        if (why) {
            *why = level + ": the level's scene isn't installed";
        }
        return false;
    }
    get_log("start level: %s (free skate%s)", level.c_str(), edited_copy ? ", its edited copy" : "");
    start_free_skate(load, edited_copy);
    return true;
}

namespace {
class StartLevelExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* front_end, bool game_owned) override
    {
        if (!game_owned) {
            tick_frame(front_end);
        }
    }
    bool skips_intro_movies() override { return edit_requested(); }
};
} // namespace

ext::Extension& extension()
{
    static StartLevelExtension start_level;
    return start_level;
}

} // namespace frontend::begin_level
