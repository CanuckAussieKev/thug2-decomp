// Start in a level (frontend/start_level.h).

#include "frontend/start_level.h"

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

#include "engine/core/checksums.h"
#include "engine/core/memory_report.h"
#include "engine/file/file_load.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "editor/level_editor_layout.h"
#include "frontend/mouse_menus.h"
#include "frontend/test_view.h"
#include "engine/gfx/screen.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "platform/winmain.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace frontend::begin_level {

namespace {

constexpr std::size_t kValueSize = 128;
constexpr std::size_t kPathSize = 160;
constexpr int kSettleFrames = 60;              // the menu runs this long before the level starts

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return symbol && symbol->type() == script::kQbvalScriptBody;
}

// The command line as the process got it (the game's own parsing cuts the process's buffer into words later: a 0
// after the first word - platform/winmain.h).
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
        Request r;
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): set and tested in one step, on purpose
        if (!(r.level = option_value(command_line(), "edit")).empty())
            // NOLINTNEXTLINE(bugprone-branch-clone): the cases kept apart, one per kind
            r.edit = true;
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): set and tested in one step, on purpose
        else if (!(r.level = option_value(command_line(), "level")).empty())
            r.edit = false;
        // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): set and tested in one step, on purpose
        else if (!(r.level = from_ini("edit_level")).empty())
            r.edit = true;
        else
            r.level = from_ini("start_level");
        return r;
    }();
    return kRequest;
}

// Whether Level_<name>'s scene can load: a level without a scene pre (scnpre) needs levels\<level>\<level>.scn.xbx
// (data folder or overlay). The DEVKIT_ONLY level Default has none on PC (only its scripts and sky ship): the game's
// scene load returns no scene for it and writes through it (0x0048C5AA). Levels with a scene pre aren't checked.
bool scene_installed(const std::string& name)
{
    script::Struct* info = script::read_sub_struct(("Level_" + name).c_str());
    const char* scene = nullptr;
    if (!info || info->has_entry_named("scnpre") || !info->fetch_text("level", &scene, 0) || !scene)
        return true;
    std::array<char, kPathSize> path{};
    text::format_to(path, "levels\\%s\\%s.scn.xbx", scene, scene);
    return file::is_present(path.data());
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

// -edit: the editor opened once the level is up (edit_update).
constexpr uint32_t kSkater = 0x5B8AB877; // Skater (the local skater's alias)
constexpr int kLevelSettleFrames = 90;   // (in the level, unpaused, before Start is pressed)
constexpr int kPauseWaitFrames = 120;    // (for the pause menu, before Start is pressed again)
constexpr int kPauseMenuFrames = 20;     // (the pause menu up this long before LEVEL EDITOR is chosen)
constexpr int kStartPresses = 5;
enum class Edit : uint8_t { kDisengaged, kWaitLevel, kWaitPause, kDone };
// NOT ORIGINAL grouping - the editor start: its presses and frames, whether it started, the pools' log (the original's
// globals). Made on first use, never destroyed (edit_start()).
struct EditStart {
    Edit edit_ = Edit::kDisengaged;
    int edit_frames_{};
    int presses_{};
    bool started_{}; // the requested level's load began
    bool pools_logged_{}; // its script pool use logged (Scripts::log_script_pool_use)
    int pool_frames_{};
};

EditStart& edit_start()
{
    static core::NoDestroy<EditStart> state;
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
        const std::string t = option_value(command_line(), "at");
        return t.empty() ? from_ini("start_at") : t;
    }();
    return kText;
}

// True while the teleport is still to come (-edit's editor waits for it).
bool at_update(modules::FrontEnd* front_end)
{
    if (start_spot().at_ != At::kWaitLevel)
        return false;
    const bool in_level = render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
    const bool paused = front_end && front_end->is_halted();
    start_spot().at_frames_ = in_level && !paused ? start_spot().at_frames_ + 1 : 0;
    if (start_spot().at_frames_ < kAtSettleFrames)
        return true;
    start_spot().at_ = At::kDone;
    Spot spot;
    parse_spot(at_text(), &spot); // (checked when it was asked for)
    test_view::Vec3 at{};
    const std::string failed = test_view::jump_to({.x = spot.x, .y = spot.y, .z = spot.z}, spot.has_heading_, spot.heading, &at);
    if (!failed.empty())
        get_log("start at: %s - the skater stays where the level put it", failed.c_str());
    else
        get_log("start at: the skater stands at (%.1f, %.1f, %.1f) in %s", at.x, at.y, at.z, get_requested());
    return false;
}

void edit_update(modules::FrontEnd* front_end)
{
    const bool in_level = render::current_level() != 0 && objects::find_object(kSkater) != nullptr;
    const bool paused = front_end && front_end->is_halted();
    if (edit_start().edit_ == Edit::kWaitLevel) {
        edit_start().edit_frames_ = in_level && !paused ? edit_start().edit_frames_ + 1 : 0;
        if (edit_start().edit_frames_ < kLevelSettleFrames)
            return;
        if (!level_editor::is_switched_on()) {
            get_log("edit level: the level editor is off ([features] level_editor=0) - free skate in %s, as -level",
                    get_requested());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        if (!is_script(name_hash("level_editor_enter"))) {
            get_log("edit level: no level_editor_enter script (the data overlay isn't installed - "
                    "tools/pipeline/menu_level/make_menu_level.ts) - free skate in %s, as -level",
                    get_requested());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        // (the level is loaded in free skate - Load - so this is only a safeguard: never in a network session or a
        // career-type mode - level_editor_layout.h)
        std::string why;
        if (level_editor::layout::get_refused(&why)) {
            get_log("edit level: the level editor isn't offered here (%s) - %s as it is", why.c_str(), get_requested());
            level_editor::layout::panel_message(("The level editor isn't offered here - " + why).c_str());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
        get_log("edit level: %s is up - pausing (Start) to open the level editor", get_requested());
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++edit_start().presses_;
        edit_start().edit_ = Edit::kWaitPause;
        edit_start().edit_frames_ = 0;
        return;
    }
    if (edit_start().edit_ != Edit::kWaitPause)
        return;
    if (!in_level) {
        edit_start().edit_frames_ = 0;
        return;
    }
    if (!paused) {
        // (not taken - a level's intro camera, menu input blocked a moment: pressed again a little later)
        if (++edit_start().edit_frames_ < kPauseWaitFrames)
            return;
        if (edit_start().presses_ >= kStartPresses) {
            get_log("edit level: the game didn't pause after %d presses of Start - free skate in %s (the pause menu's "
                    "LEVEL EDITOR opens the editor)",
                    edit_start().presses_, get_requested());
            edit_start().edit_ = Edit::kDone;
            return;
        }
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++edit_start().presses_;
        edit_start().edit_frames_ = 0;
        return;
    }
    if (++edit_start().edit_frames_ < kPauseMenuFrames)
        return;
    edit_start().edit_ = Edit::kDone;
    get_log("edit level: running level_editor_enter (the pause menu's LEVEL EDITOR)");
    script::launch_script(name_hash("level_editor_enter"), nullptr, nullptr, false, "level_editor_enter");
}

} // namespace

std::string option_value(const std::string& line, const char* option)
{
    const std::string name = option;
    for (const std::string& form : {"--" + name + "=", "--" + name + " ", "-" + name + " "}) {
        for (size_t at = line.find(form); at != std::string::npos; at = line.find(form, at + 1)) {
            // (a word of its own: at the start, or after a space or a quote - not inside a path such as C:\my-level x)
            if (at > 0 && core::at(line, at - 1) != ' ' && core::at(line, at - 1) != '\t' && core::at(line, at - 1) != '"')
                continue;
            const size_t start = line.find_first_not_of(' ', at + form.size());
            if (start == std::string::npos)
                return {};
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
    std::string s = text;
    // (the sidecar's "(x, y, z)": one pair of parentheses around the whole)
    const size_t first = s.find_first_not_of(" \t");
    const size_t last = s.find_last_not_of(" \t");
    if (first == std::string::npos)
        return false;
    s = s.substr(first, last - first + 1);
    if (!s.empty() && s.front() == '(' && s.back() == ')')
        s = s.substr(1, s.size() - 2);
    std::array<float, 4> values{};
    int count = 0;
    size_t at = 0;
    for (;;) {
        at = s.find_first_not_of(" \t", at);
        if (at == std::string::npos || count == 4)
            return false;
        const char* start = std::next(s.c_str(), static_cast<std::ptrdiff_t>(at));
        char* end = nullptr;
        const double v = std::strtod(start, &end);
        if (end == start || !std::isfinite(v))
            return false;
        core::at(values, count++) = static_cast<float>(v);
        at = static_cast<size_t>(end - s.c_str());
        at = s.find_first_not_of(" \t", at);
        if (at == std::string::npos)
            break;
        if (core::at(s, at) == ',')
            ++at; // (a comma, or only spaces, between two numbers)
        else if (at > 0 && core::at(s, at - 1) != ' ' && core::at(s, at - 1) != '\t')
            return false; // (something glued to the number: "1x")
    }
    if (count < 3)
        return false;
    if (out) {
        *out = {};
        out->x = core::at(values, 0);
        out->y = core::at(values, 1);
        out->z = core::at(values, 2);
        out->has_heading_ = count == 4;
        out->heading = count == 4 ? core::at(values, 3) : 0.0f;
    }
    return true;
}

const char* get_requested() { return the_request().level.c_str(); }

bool edit_requested() { return the_request().edit && !the_request().level.empty(); }

void tick_frame(modules::FrontEnd* front_end)
{
    if (frame_loop::frames_run() == 0)
        return;
    if (!*get_requested()) {
        if (!start_level_state().done_ && !at_text().empty()) {
            start_level_state().done_ = true;
            get_log("start at: %s asked for without a level (-level / start_level) - ignored", at_text().c_str());
        }
        return;
    }
    if (start_level_state().done_) {
        // (once the level is up and settled: what it costs the script pools - for the converters' load checks,
        // tools/game/load_check.ps1)
        if (!edit_start().pools_logged_ && edit_start().started_) {
            const bool up = render::current_level() != 0 && objects::find_object(kSkater) != nullptr &&
                            !(front_end && front_end->is_halted());
            edit_start().pool_frames_ = up ? edit_start().pool_frames_ + 1 : 0;
            if (edit_start().pool_frames_ >= kLevelSettleFrames) {
                edit_start().pools_logged_ = true;
                script::log_script_pool_use((std::string("in ") + get_requested()).c_str());
                memory::log_memory_use((std::string("in ") + get_requested()).c_str());
            }
        }
        if (at_update(front_end))
            return; // (-edit's editor once the skater stands there)
        if (edit_start().edit_ != Edit::kDisengaged && edit_start().edit_ != Edit::kDone)
            edit_update(front_end);
        return;
    }
    // In the front end, with the main menu up: its scripts loaded (not the early self-tests' stand-in symbol table,
    // which run this before the game is up), then a second more for the menu to settle.
    if (render::current_level() != 0 || (front_end && front_end->is_halted()) ||
        !is_script(name_hash("main_menu_skate_now")))
        return;
    if (++start_level_state().frames_ < kSettleFrames)
        return;
    start_level_state().done_ = true;
    const bool loaded = read_in(get_requested(), nullptr);
    edit_start().started_ = loaded;
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
        edit_start().edit_ = Edit::kWaitLevel;
        edit_start().edit_frames_ = 0;
        edit_start().presses_ = 0;
        get_log("edit level: the level editor opens once %s is up", get_requested());
    }
}

bool read_in(const char* name, std::string* why)
{
    std::string level = name;
    if (_strnicmp(level.c_str(), "load_", kLoadPrefixLength) != 0)
        level = "Load_" + level;
    const uint32_t load = name_hash(level.c_str());
    if (!is_script(load)) {
        get_log("start level: %s isn't a level's load script - the main menu stays", level.c_str());
        if (why)
            *why = level + " isn't a level's load script";
        return false;
    }
    if (!scene_installed(level.substr(kLoadPrefixLength))) {
        get_log("start level: %s - the level's scene isn't installed (the game would crash loading it) - the main menu "
                "stays",
                level.c_str());
        if (why)
            *why = level + ": the level's scene isn't installed";
        return false;
    }
    get_log("start level: %s (free skate)", level.c_str());
    script::launch_script(name_hash("EnableSun"), nullptr, nullptr, false, "EnableSun");
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{0u}, name_hash("freeskate"));
    script::launch_script(name_hash("SetGameType"), params, nullptr, false, "SetGameType");
    params->get_clear();
    script::launch_script(name_hash("SetCurrentGameType"), nullptr, nullptr, false, "SetCurrentGameType");
    params->put_name_key(checksums::Name{name_hash("level")}, load);
    script::spawn_script(name_hash("level_select_change_level"), params);
    params->get_clear();
    script::delete_struct(params);
    return true;
}

} // namespace frontend::begin_level
