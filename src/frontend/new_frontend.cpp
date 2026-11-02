// NOT ORIGINAL - the new front end (ours): see new_frontend.h. The glue to the game: when it's up and over which
// original screen, its input, what its menus ask for (hand-offs, the ini, levels), its pictures and fonts, its ImGui
// context, the automation channel's commands.

#include "frontend/new_frontend.h"
#include "platform/keyboard.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/file/file_load.h"
#include "frontend/automation.h"
#include "frontend/data_overlay.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_ui_backend.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "frontend/restart_tester.h"
#include "engine/audio/continue_music.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "frontend/sprite_element.h"
#include "frontend/start_level.h"
#include "frontend/text_element.h"
#include "engine/gfx/font.h"
#include "engine/gfx/glyphs.h"
#include "engine/gfx/hd_ui.h"
#include "engine/net/online_identity.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/pad_input/device.h"
#include "engine/core/str.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/thaw_bmx.h"
#include "platform/platform_config.h"
#include "platform/platform_system.h"
#include "platform/sdl_events.h"
#include "platform/sdl_window.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "engine/gfx/device.h"
#include "engine/gfx/engine_glue.h"
#include "game/rider/skater_state.h"
#include "game/rider/skater_score.h"
#include "game/skate/point_tally.h"
#include "engine/util/unique_handle.h"
#include "engine/core/parse_number.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace frontend::new_frontend {

namespace {
void continue_last_level(modules::FrontEnd* front_end); // (below)

namespace ui_backend = frontend::level_editor::ui_backend;
using game_screens::Anchor;

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)
constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;  // SkaterState
constexpr uint32_t kSkaterScore = 0xD262802F;  // SkaterScore

// --- the game's state ---

modules::Skate* skate_module() { return modules::g_skate_module.get(); }
// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t current_load_state() { return skate_module() ? modules::g_skate_module->level_ : 0; }
bool in_front_end() { return current_load_state() == kLoadMainMenu; }
// The skate module's career (+0x20; null before the module is up).
objects::RiderStoryProgress* get_career() { return skate_module() ? modules::g_skate_module->career_.get() : nullptr; }
modules::FrontEnd* the_front_end() { return modules::g_front_end_instance.get(); }
bool get_paused() { return the_front_end() && the_front_end()->is_halted_; }
bool loading_screen_up()
{
    return render::g_loading_screen_shown != 0 || render::g_loading_bar_timer != 0 ||
           static_cast<uint8_t>(render::g_loading_screen_up) != 0;
}

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return symbol && symbol->type == script::kQbvalScriptBody;
}

// A script integer by name (-1: none).
int script_integer(const std::string& name)
{
    if (name.empty())
        return -1;
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name_hash(name.c_str()));
    return symbol && symbol->type == script::kQbvalInteger ? symbol->value.integer() : -1;
}

// --- the setting and the files ---

std::string ini_path() { return game_folder_path(build_info::kFileName); }

std::string ini_string(const char* key, const char* fallback)
{
    std::array<char, 512> value = {};
    GetPrivateProfileStringA("frontend", key, fallback, value.data(), sizeof(value), ini_path().c_str());
    return value.data();
}

bool is_file_present(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string windows_fonts()
{
    std::array<char, MAX_PATH> windows = {};
    const UINT n = GetWindowsDirectoryA(windows.data(), MAX_PATH);
    return std::string(windows.data(), n) + "\\Fonts\\";
}

// A font named in the ini (or the default list): a full path as it is, else the overlay's frontend\fonts, else
// Windows' fonts folder. "" if none of the names is there.
std::string find_font(const std::vector<std::string>& names)
{
    const std::string ours = data_overlay::get_folder() + R"(\frontend\fonts\)";
    const std::string windows = windows_fonts();
    for (const std::string& name : names) {
        if (name.empty())
            continue;
        if (name.find(':') != std::string::npos || name.starts_with("\\\\")) {
            if (is_file_present(name))
                return name;
            continue;
        }
        if (is_file_present(ours + name))
            return ours + name;
        if (is_file_present(windows + name))
            return windows + name;
    }
    return {};
}

paint::FontFiles resolve_fonts()
{
    paint::FontFiles files;
    files.display = find_font({ini_string("display_font", ""), "Anton-Regular.ttf", "bahnschrift.ttf", "impact.ttf"});
    files.mono = find_font({ini_string("mono_font", ""), "IBMPlexMono-Regular.ttf", "consola.ttf"});
    files.body = find_font({ini_string("body_font", ""), "IBMPlexSans-Regular.ttf", "segoeui.ttf"});
    // Bahnschrift (Windows' DIN): the default instance of its variable font is regular width and weight - drawn
    // narrower and heavier to come nearer Anton; Anton and Impact are already condensed and heavy.
    std::string lower = files.display;
    for (char& ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool bahnschrift = lower.find("bahnschrift") != std::string::npos;
    files.condense =
        static_cast<float>(text::to_int(ini_string("display_condense", bahnschrift ? "84" : "100").c_str())) / 100.0f;
    files.bold = text::to_int(ini_string("display_bold", bahnschrift ? "1" : "0").c_str()) ? 0.022f : 0.0f;
    get_log("new front end: fonts - display %s (condensed %.0f%%%s), mono %s, body %s",
            files.display.empty() ? "(none)" : files.display.c_str(), files.condense * 100.0f,
            files.bold > 0 ? ", heavier" : "", files.mono.empty() ? "(none)" : files.mono.c_str(),
            files.body.empty() ? "(none)" : files.body.c_str());
    return files;
}

std::string read_text(const std::string& path)
{
    const util::UniqueFile file = util::open_file(path.c_str(), "rb");
    if (!file)
        return {};
    std::string text;
    std::array<char, 4096> buffer{};
    for (size_t n = 0; (n = std::fread(buffer.data(), 1, sizeof(buffer), file.get())) > 0;)
        text.append(buffer.data(), n);
    return text;
}

// The text written next to `path` and moved over it in one step (a crash part-way leaves the old file).
bool write_atomically(const std::string& path, const std::string& text)
{
    const std::string temporary = path + ".new";
    util::UniqueFile file = util::open_file(temporary.c_str(), "wb");
    if (!file)
        return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), file.get()) == text.size();
    ok = std::fflush(file.get()) == 0 && ok;
    file.reset(); // (closed before it's moved)
    if (!ok || !MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

std::string state_path() { return game_folder_path("thug2-decomp-frontend.txt"); }
// The level select's chosen picture per level (Q / E there): `<load script> <number>` lines.
std::string art_choices_path() { return game_folder_path("thug2-decomp-art.txt"); }

// --- the state ---

enum class State : uint8_t {
    kIdle,      // not up (a level being played, the game's screens restyled or as they are)
    kShown,     // our screen a menu's item asked for: drawn, with the input
    kAway,      // the game's screens up (restyled or as they are)
    kLaunching, // a level started: the art up until it loads
};
const char* get_state_name(State state)
{
    switch (state) {
    case State::kIdle:
        return "idle";
    case State::kShown:
        return "shown";
    case State::kAway:
        return "away";
    case State::kLaunching:
        return "launching";
    }
    return "?";
}
State s_state = State::kIdle;
float s_state_time = 0.0f;
Anchor s_anchor = Anchor::kNone; // the game's menu up when ours came up (the log, the status)
// Our screen a menu's item asked for (NewFrontend_Open: the overlay's CHANGE LEVEL / OPTIONS / LEVEL EDITOR items, the
// skate shop's Play Level) - shown once, over whatever is up, on our root (Main in the front end, Pause in a level);
// back at the root, ours goes aside again (the restyled menu it came from under it, or `s_open_back` spawned when that
// menu went - the skate shop's).
Screen s_open_screen = Screen::kMain; // (Main: none asked for)
LevelsFor s_open_levels = LevelsFor::kFreeSkate;
uint32_t s_open_back = 0;  // (a script spawned when it is closed with Back; 0: none)
uint32_t s_shown_back = 0; // (the one of the screen shown now)
Screen s_open_root = Screen::kMain;
bool s_root_hidden = false;  // the original's screen elements hidden under our pause menu
uint32_t s_launch_level = 0; // the level up when a launch started (in a level: done when it changes)

Catalogue s_catalogue;
bool s_catalogue_tried = false, s_catalogue_ok = false;
Menu& menu_state()
{
    static Menu value;
    return value;
}
SavedState s_saved;
uint32_t s_last_load = 0;

std::vector<SDL_Event> s_events;
std::vector<paint::Hit> s_hits;
bool s_guard = false; // (the game's input held back until the keys that chose are let go)
float s_guard_time = 0.0f;
int s_buttons_down = 0;
std::string s_skater_name;

// Controller directions held (the d-pad, the left stick): repeated as the keyboard repeats.
struct Repeat {
    int direction = -1; // an Input (Up / Down / Left / Right), -1 none
    float wait = 0.0f;
};
Repeat s_repeat;
int s_stick_x = 0, s_stick_y = 0;
constexpr float kRepeatDelay = 0.35f, kRepeatEvery = 0.09f;

std::chrono::steady_clock::time_point s_last_tick;
bool s_ticked = false;

// The Level Editor chosen on the Create screen: the level loads, then Start pauses it and the pause menu's LEVEL
// EDITOR opens the editor (as -edit does - frontend/start_level.h).
enum class Edit : uint8_t { kDisengaged, kWaitLevel, kWaitPause };
Edit s_edit = Edit::kDisengaged;
float s_edit_time = 0.0f;
int s_edit_frames = 0, s_edit_presses = 0;

// The options' state: what the ini said at start-up (a Restart option changed since is "pending").
std::vector<std::string> s_start_values;
std::vector<bool> s_restart_pending;
std::string s_options_note;

// --- the session in a level (the pause menu's panel) ---

struct SessionState {
    uint32_t level = 0;
    float seconds = 0.0f;
    int best_combo_ = 0;
    std::set<std::string> gaps_landed_;
    int gaps_total_ = 0;
    level_editor::play_test::ComboTracker combo;
    level_editor::play_test::GapTracker gaps;
};
SessionState& session_state()
{
    static SessionState value;
    return value;
}

void update_session(float dt)
{
    const uint32_t level = current_load_state();
    if (level != session_state().level) {
        session_state() = SessionState{};
        session_state().level = level;
    }
    if (!level || level == kLoadMainMenu || get_paused() || level_editor::live_now())
        return;
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater)
        return;
    session_state().seconds += dt;
    // The combo's points pending (the score's pot times its multiplier - level_editor_playtest.h), bails.
    int pending = 0;
    bool bailing = false;
    if (objects::ObjectModule* state = skater->fetch_module(kSkaterState))
        bailing = core::down_cast<objects::RiderPoseState>(state)->is_on(objects::PoseFlag::kBailing) != 0;
    if (objects::ObjectModule* score = skater->fetch_module(kSkaterScore))
        if (const modules::PointTally* s = core::down_cast<objects::RiderPointKeeper>(score)->points_held_) {
            const double points = static_cast<double>(s->pot_) * s->multiplier_;
            if (points > 0.0 && points < 2.0e9)
                pending = static_cast<int>(points);
        }
    level_editor::play_test::ComboEnd end;
    if (session_state().combo.tick_frame(pending, bailing, &end) && end.landed)
        session_state().best_combo_ = std::max(session_state().best_combo_, end.points);
    // The gaps (the skate module's checklist: the level's).
    std::vector<level_editor::play_test::GapRead> read;
    modules::Skate* skate = skate_module();
    if (const goals::GapTally* list =
            skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list())
                  : nullptr) {
        int guard = 0;
        for (const memory::ListNode* node = list->gaps_.next; node && node != &list->gaps_ && ++guard < 4096;
             node = node->next) {
            if (node->data == memory::ListNode::head_marker())
                break;
            auto* gap = static_cast<goals::GapEntry*>(node->data);
            read.push_back(
                {gap->label_.c_str() ? gap->label_.c_str() : "", gap->pending_, gap->num_, gap->points_held_});
        }
    }
    session_state().gaps_total_ = static_cast<int>(read.size());
    for (const level_editor::play_test::GapChange& change : session_state().gaps.tick_frame(read))
        if (change.kind == level_editor::play_test::GapChange::Kind::kLanded)
            session_state().gaps_landed_.insert(change.name);
}

// --- the pictures ---

struct CachedArt {
    paint::Art art;
    uint32_t used = 0;
    bool failed = false;
};
std::map<std::string, CachedArt>& art_state()
{
    static std::map<std::string, CachedArt> value;
    return value;
}
uint32_t s_art_clock = 0;
constexpr size_t kArtCached = 10;

void free_art()
{
    for (auto& [name, cached] : art_state())
        ui_backend::release_texture(cached.art.texture);
    art_state().clear();
}

// The image file (the game's .img.xbx: a 32-byte header, a palette for 8-bit images, the texels - swizzled when both
// sides are powers of two; an upscaled loading screen may be DXT1 blocks - engine/gfx/hd_ui.h) as a texture.
paint::Art load_image_file(const std::string& name)
{
    const std::string path = "images\\" + name + ".img.xbx";
    file::Hdl file = file::unseal(path.c_str(), "rb");
    if (!file)
        return {};
    const uint32_t size = file::get_file_size(file);
    std::vector<uint8_t> data(size);
    const bool read = size > 0x20 && file::get_take_in(data.data(), static_cast<int>(size), 1, file) > 0;
    file::shut(file);
    if (!read)
        return {};
    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        std::memcpy(&v, std::span<const uint8_t>(data).subspan(at, 4).data(), 4);
        return v;
    };
    const uint32_t width = u32(8);
    const uint32_t height = u32(12);
    const uint32_t format = u32(16);
    const uint32_t marker = u32(20);
    const uint32_t palette_size = u32(28);
    if (width == 0 || height == 0 || width > 8192 || height > 8192)
        return {};
    std::span<const uint8_t> texels = std::span<const uint8_t>(data).subspan(0x20);
    size_t left = data.size() - 0x20;
    paint::Art art;
    art.width = static_cast<float>(width);
    art.height = static_cast<float>(height);
    // (a loading screen's rows are stored bottom first: render::draw_loading_screen_picture puts v 1 at the screen's top)
    art.bottom_up_ = true;
    if (format == render::hd_ui::kFormatDxt1 && render::hd_ui::image_scale(marker) != 1 && width % 4 == 0 &&
        height % 4 == 0) {
        const int row_bytes = static_cast<int>(width / 4 * 8);
        const int rows = static_cast<int>(height / 4);
        if (left < static_cast<size_t>(row_bytes) * rows)
            return {};
        art.texture = ui_backend::create_texture(static_cast<int>(width), static_cast<int>(height),
                                                 ui_backend::PixelLayout::kDxt1, texels.data(), row_bytes, rows);
        return art;
    }
    const uint32_t bits = format == 0 ? 32 : format == 2 ? 16 : format == 0x13 ? 8 : format;
    const bool palettized = format == 0x13 || (format != 0 && format != 2 && format < 0x10);
    if (bits != 32 && !(palettized && bits == 8))
        return {}; // (16-bit and 4-bit images: no loading screen is one)
    // (the palette: the colours index the rest of the file, as the original's read did - an index past a short
    // palette reads the texels that follow)
    std::span<const uint8_t> palette;
    if (palettized) {
        const size_t palette_bytes = palette_size ? palette_size : 0x400;
        if (left < palette_bytes)
            return {};
        palette = texels;
        texels = texels.subspan(palette_bytes);
        left -= palette_bytes;
    }
    const size_t count = static_cast<size_t>(width) * height;
    if (left < count * (bits / 8))
        return {};
    const bool powers = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    std::vector<uint8_t> plain;
    std::span<const uint8_t> rows_data = texels;
    if (powers) {
        plain.resize(count * (bits / 8));
        os_layer::unswizzle(plain, texels.first(plain.size()), static_cast<int>(width),
                            static_cast<int>(height), static_cast<int>(bits / 8));
        rows_data = plain;
    }
    std::vector<uint8_t> bgra(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const std::span<const uint8_t> px =
            palettized ? palette.subspan(size_t{core::at(rows_data, i)} * 4) : rows_data.subspan(i * 4);
        core::at(bgra, i * 4 + 0) = core::at(px, 0);
        core::at(bgra, i * 4 + 1) = core::at(px, 1);
        core::at(bgra, i * 4 + 2) = core::at(px, 2);
        core::at(bgra, i * 4 + 3) = 0xFF; // (a full-bleed picture: opaque - the files' alpha is the PS2's half scale)
    }
    art.texture =
        ui_backend::create_texture(static_cast<int>(width), static_cast<int>(height), ui_backend::PixelLayout::kBgra32,
                                   bgra.data(), static_cast<int>(width * 4), static_cast<int>(height));
    return art;
}

paint::Art art_for(const std::string& image)
{
    auto it = art_state().find(image);
    if (it == art_state().end()) {
        if (art_state().size() >= kArtCached) {
            auto oldest = art_state().begin();
            for (auto i = art_state().begin(); i != art_state().end(); ++i)
                if (i->second.used < oldest->second.used)
                    oldest = i;
            ui_backend::release_texture(oldest->second.art.texture);
            art_state().erase(oldest);
        }
        CachedArt cached;
        cached.art = load_image_file(image);
        cached.failed = !cached.art.texture;
        if (cached.failed)
            get_log("new front end: no picture images\\%s.img.xbx", image.c_str());
        it = art_state().emplace(image, cached).first;
    }
    it->second.used = ++s_art_clock;
    return it->second.art;
}

paint::Highscores records_for(const StageMap& level)
{
    paint::Highscores records;
    const int number = level.level_index_ >= 0 ? level.level_index_ : script_integer(level.level_num_name_);
    objects::RiderStoryProgress* career = get_career();
    // (the career's records are THUG2's own levels': 21 of them)
    if (!career || number < 0 || number >= objects::RiderStoryProgress::kLevels || !level.game_tag_.empty() ||
        level.level_num_name_.empty())
        return records;
    const int goals = career->count_goals(number);
    if (goals > 0)
        records.goals = goals;
    if (goals::GapTally* gaps = career->fetch_gap_list(number)) {
        const int total = gaps->total_of();
        if (total > 0) {
            int got = 0;
            for (int i = 0; i < total; ++i)
                got += gaps->count_gaps_found(i) > 0 ? 1 : 0;
            records.gaps_total_ = total;
            records.gaps_got_ = got;
        }
    }
    return records;
}

// [frontend] respect_locks (0, the default: every level open in Free Skate - the user's wish; 1: the game's level
// select's rules).
bool respect_locks() { return GetPrivateProfileIntA("frontend", "respect_locks", 0, ini_path().c_str()) != 0; }

// Whether a level is unlocked, as the game's level select decides it outside a career (level_select_menu_add_item,
// gamemenu_levelselect.qb): every level when all_levels_unlocked is 1 (the cheat); else its seen_flag when it has one
// (level_select_should_use_seen_flags - free skate and single session use the seen flags), else its flag; no flag:
// open. Every level when respect_locks is off - except for a network game (`game_rules`): its level select keeps the
// game's rules always, as the game's own does.
bool get_unlocked(const StageMap& level, bool game_rules)
{
    if ((!game_rules && !respect_locks()) || script_integer("all_levels_unlocked") == 1)
        return true;
    const int flag = script_integer(level.seen_flag_.empty() ? level.flag : level.seen_flag_);
    objects::RiderStoryProgress* career = get_career();
    return flag < 0 || !career || career->get_global_flag(flag);
}

std::vector<SectionView> get_views(bool game_rules = false)
{
    std::vector<SectionView> views;
    for (const Section& section : s_catalogue.sections) {
        SectionView view;
        for (size_t i = 0; i < section.levels.size(); ++i) {
            const StageMap& level = core::at(section.levels, i);
            const bool unlocked = get_unlocked(level, game_rules);
            // (the game's select drops hide_when_locked outside a career when it uses the seen flags: listed, locked)
            if (!unlocked && level.hide_when_locked_ && level.seen_flag_.empty())
                continue;
            view.levels.push_back(static_cast<int>(i));
            view.locked.push_back(!unlocked);
        }
        views.push_back(std::move(view));
    }
    return views;
}

void load_catalogue()
{
    if (s_catalogue_tried)
        return;
    s_catalogue_tried = true;
    const std::string path = data_overlay::get_folder() + "\\frontend\\levels.json";
    const std::string text = read_text(path);
    std::string error;
    if (text.empty()) {
        get_log("new front end: no %s (make_menu_level.ts makes it) - Free Skate goes to the original level select",
                path.c_str());
    } else if (!parse_catalogue(text, &s_catalogue, &error)) {
        get_log("new front end: %s - %s; Free Skate goes to the original level select", path.c_str(), error.c_str());
        s_catalogue = Catalogue{};
    } else {
        s_catalogue_ok = true;
        size_t levels = 0;
        for (const Section& s : s_catalogue.sections)
            levels += s.levels.size();
        get_log("new front end: %zu sections, %zu levels from %s", s_catalogue.sections.size(), levels, path.c_str());
    }
    s_saved = parse_state(read_text(state_path()));
    s_catalogue.apply_art_choices(parse_art_choices(read_text(art_choices_path())));
}

// Q / E on the level select: the highlighted level's next / previous picture, kept for the next start.
bool cycle_level_art(int step)
{
    const StageMap* level = menu_state().selected_level();
    if (!level || !s_catalogue.cycle_art(level->load, step))
        return false;
    write_atomically(art_choices_path(), format_art_choices(s_catalogue.art_choices()));
    return true;
}

void save_state() { write_atomically(state_path(), format_state(s_saved)); }

// The last level played: whichever menu started it, once it's the level loaded.
void note_level_loaded()
{
    const uint32_t load = current_load_state();
    if (load == s_last_load)
        return;
    s_last_load = load;
    if (!load || load == kLoadMainMenu || !s_catalogue_ok)
        return;
    for (const Section& section : s_catalogue.sections)
        for (const StageMap& level : section.levels)
            if (name_hash(level.load.c_str()) == load) {
                if (s_saved.last_level_ != level.load) {
                    s_saved.last_level_ = level.load;
                    save_state();
                }
                return;
            }
}

std::string get_skater_name()
{
    const uint32_t script = name_hash("select_skater_get_current_skater_name");
    if (!is_script(script))
        return {};
    script::Struct results;
    script::run_script_once(script, nullptr, nullptr, &results);
    const char* name = nullptr;
    std::string out;
    // (the profile's info - GetSkaterProfileInfo's display_name - is among the script's parameters after it)
    if ((results.fetch_text("display_name", &name, 0) || results.fetch_text("current_skater", &name, 0)) && name)
        out = name;
    results.get_clear();
    return out;
}

void get_sound(const char* script, const char* flag = nullptr)
{
    const uint32_t name = name_hash(script);
    if (!is_script(name))
        return;
    script::Struct params;
    if (flag)
        params.put_name_key(0u, name_hash(flag));
    script::launch_script(name, &params, nullptr, false, script);
    params.get_clear();
}

// --- the options ---

void load_option_values()
{
    const std::string text = read_text(ini_path());
    const std::vector<options::Spec>& specs = options::get_specs();
    std::vector<int> choices(specs.size());
    const bool first = s_start_values.empty();
    if (first) {
        s_start_values.resize(specs.size());
        s_restart_pending.assign(specs.size(), false);
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        std::string value;
        const bool present = options::ini_get(text, core::at(specs, i).section, core::at(specs, i).key, &value);
        core::at(choices, i) = options::choice_of(core::at(specs, i), value, present);
        if (first)
            core::at(s_start_values, i) = core::at(core::at(specs, i).choices, core::at(choices, i)).value;
    }
    menu_state().set_option_values(std::move(choices));
}

void apply_option(int option, int choice)
{
    const std::vector<options::Spec>& specs = options::get_specs();
    if (option < 0 || std::cmp_greater_equal(option, specs.size()))
        return;
    const options::Spec& spec = core::at(specs, option);
    const char* value = core::at(spec.choices, choice).value;
    const std::string path = ini_path();
    const std::string text = read_text(path);
    const std::string changed = options::ini_set(text, spec.section, spec.key, value);
    if (!write_atomically(path, changed)) {
        get_log("new front end: couldn't write %s ([%s] %s=%s)", path.c_str(), spec.section, spec.key, value);
        s_options_note = text_tools::kCouldntSave;
        return;
    }
    get_log("new front end: options - [%s] %s=%s written to %s", spec.section, spec.key, value, path.c_str());
    s_options_note.clear();
    if (spec.apply == options::Apply::kLive) {
        const bool on = std::strcmp(value, "0") != 0;
        if (std::strcmp(spec.key, "mouse_menus") == 0)
            mouse_menus::is_switched_on() = on;
        else if (std::strcmp(spec.key, "restart_tester") == 0)
            restart_tester::is_switched_on() = on;
        else if (std::strcmp(spec.key, "level_editor") == 0)
            level_editor::is_switched_on() = on;
        else if (std::strcmp(spec.key, "continue_music") == 0)
            audio::continue_music::is_switched_on() = on;
        else if (std::strcmp(spec.key, "respect_locks") == 0)
            menu_state().set_views(get_views(menu_state().levels_purpose() == LevelsFor::kNet));
    } else if (std::cmp_less(option, s_restart_pending.size())) {
        core::at(s_restart_pending, option) = core::at(s_start_values, option) != value;
    }
}

// The level being played, as the data names it (null if it isn't there).
const StageMap* level_played(const Section** section)
{
    const uint32_t load = current_load_state();
    for (const Section& s : s_catalogue.sections)
        for (const StageMap& level : s.levels)
            if (name_hash(level.load.c_str()) == load) {
                if (section)
                    *section = &s;
                return &level;
            }
    return nullptr;
}

// --- the state machine ---

void change_phase(State state)
{
    if (s_state == state)
        return;
    get_log("new front end: %s -> %s%s%s", get_state_name(s_state), get_state_name(state),
            state == State::kShown ? " over the " : "",
            state == State::kShown ? game_screens::anchor_name(s_anchor) : "");
    const bool was_up = s_state == State::kShown || s_state == State::kLaunching;
    const bool up = state == State::kShown || state == State::kLaunching;
    if (was_up && !up) {
        s_guard = true;
        s_guard_time = 0.0f;
    }
    s_state = state;
    s_state_time = 0.0f;
    s_events.clear();
    s_repeat = {};
}

// The original's elements hidden (or shown again): the game's own hide_root_window / unhide_root_window (root_window
// scaled to 0 / 1) and root_window's alpha 0 / 1 at once - scaled to 0 alone, some elements still left marks: specks
// behind the restyled dialogs and a thin bar at the top centre of the pause screens (pause_menu_gradient's sprite
// squeezed to a line) - the user's report of 2026-10-25. An element's alpha is its parents' times its own, so nothing
// under root_window is drawn.
void hide_root(bool hide, bool force = false)
{
    if (hide == s_root_hidden && !force)
        return;
    const char* script = hide ? "hide_root_window" : "unhide_root_window";
    if (!is_script(name_hash(script)))
        return;
    script::launch_script(name_hash(script), nullptr, nullptr, false, script);
    if (UiWidget* root = game_screens::get_find(name_hash("root_window")))
        root->change_opacity(hide ? 0.0f : 1.0f, 1);
    s_root_hidden = hide;
}

// While ours hides the original's elements, whatever script scales root_window back (create_pause_menu's
// unhide_root_window - a script making the pause menu again, the BMX's toggle, a dialog...) is undone at once: the
// original never shows under ours.
void keep_root_hidden()
{
    if (!s_root_hidden)
        return;
    const UiWidget* root = game_screens::get_find(name_hash("root_window"));
    if (!root || (root->aim_point_.scale_x_ < 0.001f && root->aim_point_.alpha < 0.001f))
        return;
    static int s_logged = 0;
    if (s_logged < 20) {
        ++s_logged;
        get_log("new front end: the original's elements were shown again under ours (a script scaled root_window) - "
                "hidden");
    }
    hide_root(true, true);
}

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

uint32_t type_of(const UiWidget* element) { return static_cast<const objects::Entity*>(element)->kind_tag_; }
uint32_t id_of_element(const UiWidget* element) { return static_cast<const objects::Entity*>(element)->ident_; }
UiWidget* first_child(const UiWidget* element) { return element->eldest_.get_pull(); }
UiWidget* get_next(const UiWidget* element) { return element->following_.get_pull(); }
bool element_hidden(const UiWidget* element)
{
    return (static_cast<const objects::Entity*>(element)->object_flags_ & UiWidget::kHidden) != 0;
}
bool get_visible(const UiWidget* element)
{
    return !element_hidden(element) && element->aim_point_.alpha > 0.02f && core::at(element->aim_point_.rgba, 3) > 2;
}

uint32_t text_type()
{
    static const uint32_t kType = name_hash("TextElement");
    return kType;
}
uint32_t sprite_type()
{
    static const uint32_t kType = name_hash("SpriteElement");
    return kType;
}

UiWidget* root_element()
{
    const UiTree* manager = UiTree::sole();
    return manager ? manager->tree_root_.get_pull() : nullptr;
}

// The menu that takes the pad events now: of the focused menus, one in a dialog box first, else the last in the tree
// (the newest screen). `inDialog`: it is in dialog_box_container. `coming`: a menu not taking them yet counts too, when
// it is shown - the screen being made (the pause menu animates in for 13 frames before it takes the pad).
UiWidget* active_menu(bool* in_dialog = nullptr, bool coming = false)
{
    const UiWidget* root = root_element();
    if (!root)
        return nullptr;
    static const uint32_t kDialogContainer = name_hash("dialog_box_container");
    UiWidget* found = nullptr;
    UiWidget* dialog = nullptr;
    int budget = 8192;
    // (depth first, without recursion: the element, its first child, its next sibling, back up through the parents)
    struct Walk {
        UiWidget* element;
        bool in_dialog_;
    };
    std::vector<Walk> stack;
    stack.push_back({first_child(root), false});
    while (!stack.empty() && --budget > 0) {
        Walk& top = stack.back();
        UiWidget* e = top.element;
        if (!e) {
            stack.pop_back();
            continue;
        }
        const bool here = top.in_dialog_ || id_of_element(e) == kDialogContainer;
        top.element = get_next(e);
        if (mouse_menus::is_menu(e) && (coming ? get_visible(e) : mouse_menus::menu_gets_pad_events(e))) {
            found = e;
            if (here)
                dialog = e;
        }
        if (UiWidget* child = first_child(e))
            stack.push_back({child, here});
    }
    if (in_dialog)
        *in_dialog = dialog != nullptr;
    return dialog ? dialog : found;
}

// The strings of the text elements in `element` (itself included), in tree order; `skip`: subtrees left out.
void collect_texts(const UiWidget* element, std::vector<std::string>* out, const std::vector<const UiWidget*>& skip,
                   int* budget, bool visible_only)
{
    if (!element || --*budget <= 0)
        return;
    if (std::ranges::find(skip, element) != skip.end())
        return;
    if (visible_only && !get_visible(element))
        return;
    if (type_of(element) == text_type()) {
        const char* text = core::down_cast<const LabelWidget>(element)->chars_;
        if (text && *text)
            out->emplace_back(text);
    }
    for (const UiWidget* child = first_child(element); child && *budget > 0; child = get_next(child))
        collect_texts(child, out, skip, budget, visible_only);
}

// An element's height on the 640 x 480 layout: its position and its parents' added up (justification and scale aside -
// enough to tell rows apart).
float absolute_y(const UiWidget* element)
{
    float y = 0.0f;
    for (int guard = 0; element && guard < 64; ++guard, element = element->container_.get_pull())
        y += element->aim_point_.y;
    return y;
}

// The visible texts in `element` (itself included) with their heights, in tree order; `skip`: subtrees left out.
void collect_texts_at(const UiWidget* element, std::vector<std::pair<std::string, float>>* out,
                      const std::vector<const UiWidget*>& skip, int* budget)
{
    if (!element || --*budget <= 0 || !get_visible(element))
        return;
    if (std::ranges::find(skip, element) != skip.end())
        return;
    if (type_of(element) == text_type()) {
        const char* text = core::down_cast<const LabelWidget>(element)->chars_;
        if (text && *text)
            out->emplace_back(text, absolute_y(element));
    }
    for (const UiWidget* child = first_child(element); child && *budget > 0; child = get_next(child))
        collect_texts_at(child, out, skip, budget);
}

// A visible sprite in `element` that isn't a highlight bar (those are drawn at alpha 0 until their item is focused).
bool has_visible_sprite(const UiWidget* element, int* budget)
{
    if (!element || --*budget <= 0 || element_hidden(element) || element->aim_point_.alpha <= 0.02f)
        return false;
    if (type_of(element) == sprite_type() && core::at(element->aim_point_.rgba, 3) > 2 &&
        std::fabs(element->aim_point_.scale_x_) > 0.001f)
        return true;
    for (const UiWidget* child = first_child(element); child; child = get_next(child))
        if (has_visible_sprite(child, budget))
            return true;
    return false;
}

// An item's checkbox ("checkbox" sprite shown: ticked when its "checkmark" is) and whether its value changes with Left /
// Right (left_arrow / right_arrow sprites in it, shown on the focused item).
//
// A slider item (sliderbar_add_item: the CAS colour menus' HUE / SAT. / VAL., the scaling menus' bars) has no left_arrow
// sprites: a bar picture and an up_arrow / down_arrow marker pair that its scripts slide along the bar (SetScreenElementProps
// pos=(x_val, 0), x_val between the menu script's arrow_left and arrow_right) after every change - Left / Right on the
// focused item are its pad_left / pad_right handlers. So the marker's x is the value.
void read_value_sprites(const UiWidget* element, mirror::ItemRead* item, int* budget)
{
    static const uint32_t kCheckbox = name_hash("checkbox"), kCheckmark = name_hash("checkmark");
    static const uint32_t kLeft = name_hash("left_arrow"), kRight = name_hash("right_arrow");
    static const uint32_t kUp = name_hash("up_arrow");
    static const uint32_t kHueArrow = name_hash("hue_up_arrow");
    static const uint32_t kStar = name_hash("star"), kCircle = name_hash("circle");
    static const auto kColourArrows = std::to_array<uint32_t>({name_hash("hue_up_arrow"), name_hash("saturation_up_arrow"),
                                             name_hash("value_up_arrow"), name_hash("alpha_up_arrow")});
    for (const UiWidget* e = element; e && --*budget > 0; e = get_next(e)) {
        if (type_of(e) == sprite_type()) {
            const uint32_t texture = core::down_cast<const ImageWidget>(e)->texture_;
            if (texture == kLeft || texture == kRight)
                item->adjustable = true;
            if (texture == kUp) {
                // (colormenu_nowrap_arrow_left / right -53 .. 80 for the colour menus' markers, scalingmenu_arrow_left /
                // right -49 .. 76 for the scaling ones)
                const uint32_t id = id_of_element(e);
                float left = -49.0f, right = 76.0f;
                for (const uint32_t colour : kColourArrows)
                    if (id == colour) {
                        left = -53.0f;
                        right = 80.0f;
                    }
                item->adjustable = true;
                item->slider_at_ = std::clamp((e->aim_point_.x - left) / (right - left), 0.0f, 1.0f);
                item->slider_hue_ = id == kHueArrow;
                item->slider_kind_ = id == core::at(kColourArrows, 0)   ? 1
                                     : id == core::at(kColourArrows, 1) ? 2
                                     : id == core::at(kColourArrows, 2) ? 3
                                     : id == core::at(kColourArrows, 3) ? 4
                                                              : 0;
            }
            if (item->stat_index_ >= 0 && (texture == kStar || texture == kCircle)) {
                // A stat's star (build_stat_slider: ten of them; stats_menu_update_row lights the first `value` in
                // stats_menu_star_highlight [85 85 0], the rest stats_menu_star_lowlight [10 10 10] - the story's earned
                // but unspent ones lit at alpha 0.3)
                item->stat_value_ = std::max(item->stat_value_, 0);
                item->stat_cap_ = std::max(item->stat_cap_, 0);
                if (core::at(e->aim_point_.rgba, 0) >= 40) {
                    ++item->stat_cap_;
                    if (e->aim_point_.alpha > 0.6f)
                        ++item->stat_value_;
                }
            }
            if (texture == kCheckbox && get_visible(e)) {
                item->checkbox = 0;
                for (const UiWidget* mark = first_child(e); mark; mark = get_next(mark))
                    if (type_of(mark) == sprite_type() && get_visible(mark) &&
                        core::down_cast<const ImageWidget>(mark)->texture_ == kCheckmark)
                        item->checkbox = 1;
            }
        }
        if (const UiWidget* child = first_child(e))
            read_value_sprites(child, item, budget);
    }
}

objects::ProfileRoster* roster_handle() { return objects::g_profile_roster.get(); }

// The skater select's stats for the template `name`: stat_names' short_string (else string) and the template's value.
std::vector<std::pair<std::string, int>> skater_stats(uint32_t name)
{
    std::vector<std::pair<std::string, int>> stats;
    objects::ProfileRoster* manager = roster_handle();
    objects::RiderIdentity* profile = manager ? manager->get_template(name) : nullptr;
    script::Array* names = script::fetch_list(name_hash("stat_names"));
    if (!profile || !names || names->kind_tag_ != script::kQbvalStruct)
        return stats;
    for (uint32_t i = 0; i < names->extent_ && i < 16; ++i) {
        auto* entry = names->struct_at(i);
        uint32_t stat = 0;
        const char* label = nullptr;
        if (!entry || !entry->read_hash_value(name_hash("name"), &stat, 0))
            continue;
        if (!entry->fetch_text(name_hash("short_string"), &label, 0))
            entry->fetch_text(name_hash("string"), &label, 0);
        stats.emplace_back(label ? label : "?", profile->fetch_tally(stat));
    }
    return stats;
}

mirror::MenuRead read_menu(UiWidget* menu, bool in_dialog)
{
    mirror::MenuRead read;
    if (!menu)
        return read;
    read.found = true;
    read.menu_id_ = id_of_element(menu);
    read.menu_key_ = menu;
    read.vertical = mouse_menus::menu_runs_top_to_bottom(menu);
    read.selected_index_ = mouse_menus::selected_index(menu);
    read.in_dialog_ = in_dialog;
    read.in_level_ = !in_front_end();
    read.graphic_editor_ = script::fetch_int_value("EG_in_create_a_graphic") == 1;
    static const uint32_t kSkaterMenu = name_hash("select_skater_hmenu");
    const bool skaters = read.menu_id_ == kSkaterMenu;
    int budget = 4096;
    int index = 0;
    for (const UiWidget* child = first_child(menu); child && index < 80; child = get_next(child), ++index) {
        mirror::ItemRead item;
        item.id = id_of_element(child);
        item.focusable = mouse_menus::item_focusable(child);
        collect_texts(child, &item.texts, {}, &budget, false);
        if (!skaters && index != read.selected_index_) {
            int sprite_budget = 256;
            item.graphic_value_ = has_visible_sprite(child, &sprite_budget);
        }
        if (!skaters) {
            int value_budget = 256;
            if (const UiWidget* first = first_child(child))
                read_value_sprites(first, &item, &value_budget);
        }
        if (skaters) {
            objects::ProfileRoster* manager = roster_handle();
            objects::RiderIdentity* profile = manager ? manager->get_template(item.id) : nullptr;
            const char* name = profile ? profile->fetch_shown_name() : nullptr;
            item.display = name ? name : "";
        }
        read.items.push_back(std::move(item));
    }
    if (in_dialog) {
        // The title: the menu's first text; the text: the text block's lines (its children).
        static const uint32_t kTextBlock = name_hash("TextBlockElement");
        for (UiWidget* child = first_child(menu); child; child = get_next(child)) {
            if (read.dialog_title_.empty() && type_of(child) == text_type()) {
                const char* text = core::down_cast<LabelWidget>(child)->chars_;
                read.dialog_title_ = text ? text : "";
            } else if (type_of(child) == kTextBlock) {
                std::vector<std::string> lines;
                collect_texts(child, &lines, {}, &budget, false);
                std::string joined;
                for (const std::string& line : lines)
                    joined += (joined.empty() ? "" : " ") + line;
                read.dialog_lines_.push_back(joined);
            }
        }
        return read;
    }
    if (skaters) {
        if (UiWidget* name = game_screens::get_find(name_hash("select_skater_name")))
            if (type_of(name) == text_type() && core::down_cast<LabelWidget>(name)->chars_)
                read.skater_name_ = core::down_cast<LabelWidget>(name)->chars_;
        if (read.selected_index_ >= 0 && std::cmp_less(read.selected_index_, read.items.size()))
            read.stats = skater_stats(core::at(read.items, read.selected_index_).id);
        return read;
    }
    // A list: its title (title_text, the themed menus'), the other words on its screen (under the same child of the
    // root: not the menu, its title or the helper text).
    if (UiWidget* title = game_screens::get_find(name_hash("title_text")))
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars_ && get_visible(title))
            read.title = core::down_cast<LabelWidget>(title)->chars_;
    const UiWidget* root = root_element();
    const UiWidget* screen = menu;
    for (int guard = 0; screen && guard < 64; ++guard) {
        const UiWidget* parent = screen->container_.get_pull();
        if (!parent || parent == root)
            break;
        screen = parent;
    }
    std::vector<const UiWidget*> skip = {menu};
    for (const char* id : {"helper_text_anchor", "submenu_title_anchor", "title_text"})
        if (const UiWidget* e = game_screens::get_find(name_hash(id)))
            skip.push_back(e);
    if (screen && screen != menu) {
        // A column beside the menu (the grab tricks' names beside their key combos): a word at a row's height is that
        // row's value; the rest are the screen's other words.
        std::vector<std::pair<std::string, float>> beside;
        collect_texts_at(screen, &beside, skip, &budget);
        std::vector<float> row_y;
        for (const UiWidget* child = first_child(menu); child; child = get_next(child)) {
            float y = absolute_y(child);
            for (const UiWidget* t = first_child(child); t; t = get_next(t))
                if (type_of(t) == text_type()) {
                    y = absolute_y(t);
                    break;
                }
            row_y.push_back(y);
        }
        // (the nearest row, within half the rows' spacing)
        float spacing = 24.0f;
        for (size_t i = 1; i < row_y.size(); ++i)
            if (core::at(row_y, i) - core::at(row_y, i - 1) > 2.0f)
                spacing = std::min(spacing, core::at(row_y, i) - core::at(row_y, i - 1));
        for (const auto& [text, y] : beside) {
            size_t row = row_y.size();
            float best = spacing * 0.5f;
            for (size_t i = 0; i < row_y.size() && i < read.items.size(); ++i)
                if (std::fabs(core::at(row_y, i) - y) < best && core::at(read.items, i).focusable) {
                    best = std::fabs(core::at(row_y, i) - y);
                    row = i;
                }
            if (row < row_y.size())
                core::at(read.items, row).texts.push_back(text);
            else
                read.extra_texts_.push_back(text);
        }
    }
    return read;
}

// --- the stats screen (EDIT STATS, the story's STATS) ---

// The stat a stats screen's row stands for (stats_menu_add_item: its id stats_menu_names_<n>); -1 for any other element.
int stat_row_index(uint32_t id)
{
    static const std::array<uint32_t, 16> kIds = [] {
        std::array<uint32_t, 16> ids{};
        for (size_t i = 0; i < ids.size(); ++i) {
            std::array<char, 32> name{};
            std::snprintf(name.data(), name.size(), "stats_menu_names_%u", static_cast<unsigned>(i));
            core::at(ids, i) = name_hash(name.data());
        }
        return ids;
    }();
    for (size_t i = 0; i < kIds.size(); ++i)
        if (core::at(kIds, i) == id)
            return static_cast<int>(i);
    return -1;
}

bool has_stat_rows(const UiWidget* menu)
{
    for (const UiWidget* child = menu ? first_child(menu) : nullptr; child; child = get_next(child))
        if (stat_row_index(id_of_element(child)) >= 0)
            return true;
    return false;
}

// Whether the stats screen is up: its menu takes the pad (classic_stats_vmenu, the story's sub_vmenu / sub_vmenu2), or
// the story's stat is being changed (stats_menu_build_change_menu: its change_stat_sub_anchor takes the pad - no stats
// menu does then).
bool stats_screen_up(const UiWidget* menu)
{
    return has_stat_rows(menu) || game_screens::get_find(name_hash("change_stat_sub_anchor")) != nullptr;
}

// The stats screen read: every stat row of its menus (the story's two columns both), what the stars show, the row
// selected (the one with the pad, or the one being changed), the screen's other words. The game's scripts do the rest:
// Left / Right reach its own handlers (stats_change_menu_left / right, the story's column change), Back its pad_back.
mirror::MenuRead read_stats(UiWidget* active)
{
    mirror::MenuRead read;
    read.found = true;
    read.stats_screen_ = true;
    read.in_level_ = !in_front_end();
    const UiWidget* editing = game_screens::get_find(name_hash("change_stat_sub_anchor"));
    read.stats_editing_ = editing != nullptr;
    const UiWidget* editing_row = editing ? editing->container_.get_pull() : nullptr;
    static const uint32_t kColumn1 = name_hash("sub_vmenu"), kColumn2 = name_hash("sub_vmenu2");
    std::vector<const UiWidget*> menus;
    if (has_stat_rows(active) && id_of_element(active) != kColumn1 && id_of_element(active) != kColumn2) {
        menus.push_back(active);
    } else {
        for (const uint32_t column : {kColumn1, kColumn2})
            if (const UiWidget* m = game_screens::get_find(column); m && has_stat_rows(m))
                menus.push_back(m);
    }
    if (menus.empty())
        return read;
    read.menu_id_ = id_of_element(menus.front());
    read.menu_key_ = menus.front();
    int budget = 4096;
    int selected_stat = -1;
    for (size_t column = 0; column < menus.size(); ++column) {
        const UiWidget* menu = core::at(menus, column);
        const bool takes_pad = menu == active && !editing;
        const int selected = takes_pad ? mouse_menus::selected_index(menu) : -1;
        int index = 0;
        for (const UiWidget* child = first_child(menu); child && index < 32; child = get_next(child), ++index) {
            const int stat = stat_row_index(id_of_element(child));
            if (stat < 0)
                continue;
            mirror::ItemRead item;
            item.id = id_of_element(child);
            item.focusable = mouse_menus::item_focusable(child);
            item.stat_index_ = stat;
            item.stat_column_ = static_cast<int>(column);
            item.stat_child_ = takes_pad ? index : -1;
            collect_texts(child, &item.texts, {}, &budget, false);
            int value_budget = 256;
            if (const UiWidget* first = first_child(child))
                read_value_sprites(first, &item, &value_budget);
            if ((takes_pad && index == selected) || child == editing_row)
                selected_stat = stat;
            read.items.push_back(std::move(item));
        }
    }
    std::ranges::stable_sort(read.items, {}, &mirror::ItemRead::stat_index_);
    for (size_t i = 0; i < read.items.size(); ++i)
        if (core::at(read.items, i).stat_index_ == selected_stat)
            read.stats_selected_ = static_cast<int>(i);
    // The other words: the screen's texts (the points left, the description, the story's goals) - the stats menus and the
    // helper text left out.
    if (UiWidget* title = game_screens::get_find(name_hash("title_text")))
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars_ && get_visible(title))
            read.title = core::down_cast<LabelWidget>(title)->chars_;
    const UiWidget* root = root_element();
    const UiWidget* screen = menus.front();
    for (int guard = 0; screen && guard < 64; ++guard) {
        const UiWidget* parent = screen->container_.get_pull();
        if (!parent || parent == root)
            break;
        screen = parent;
    }
    std::vector<const UiWidget*> skip(menus.begin(), menus.end());
    for (const char* id : {"helper_text_anchor", "title_text"})
        if (const UiWidget* e = game_screens::get_find(name_hash(id)))
            skip.push_back(e);
    if (screen)
        collect_texts(screen, &read.extra_texts_, skip, &budget, true);
    return read;
}

// [frontend] debug_restyle=1: a menu left as it is (not restyled) logged once with its screen's element tree - what
// the reader would have to understand to restyle it (each element: type, id, text, texture, position, hidden).
const char* current_type_name(uint32_t type)
{
    static const auto kTypes = std::to_array<const char*>({
        "TextElement",    "SpriteElement",    "ContainerElement", "VMenu",         "HMenu", "VScrollingMenu",
        "HScrollingMenu", "TextBlockElement", "Element3d",        "WindowElement", "Menu"});
    for (const char* name : kTypes)
        if (name_hash(name) == type)
            return name;
    return nullptr;
}

void dump_tree(const UiWidget* element, int depth, int* budget)
{
    for (; element && --*budget > 0; element = get_next(element)) {
        std::array<char, 512> line{};
        const char* type = current_type_name(type_of(element));
        const int at = std::snprintf(line.data(), sizeof(line), "%*s%s %08X (%.0f, %.0f)%s", depth * 2, "", type ? type : "?",
                                     id_of_element(element), element->aim_point_.x, element->aim_point_.y,
                                     get_visible(element) ? "" : " hidden");
        if (type_of(element) == text_type()) {
            const char* text = core::down_cast<const LabelWidget>(element)->chars_;
            const std::span<char> rest = std::span<char>(line).subspan(static_cast<size_t>(at));
            std::snprintf(rest.data(), rest.size(), " \"%.80s\"", text ? text : "");
        } else if (type_of(element) == sprite_type()) {
            std::snprintf(std::span<char>(line).subspan(static_cast<size_t>(at)).data(), line.size() - static_cast<size_t>(at), " texture %08X",
                          core::down_cast<const ImageWidget>(element)->texture_);
        }
        get_log("new front end: debug_restyle %s", line.data());
        if (depth < 8)
            dump_tree(first_child(element), depth + 1, budget);
    }
}

void dump_left_menu(UiWidget* menu, const std::string& why)
{
    static const bool kOn = GetPrivateProfileIntA("frontend", "debug_restyle", 0, ini_path().c_str()) != 0;
    // (each menu once by what it holds: menus share ids and are rebuilt in the same element)
    static std::set<std::string> logged;
    std::vector<std::string> texts;
    int text_budget = 400;
    collect_texts(menu, &texts, {}, &text_budget, false);
    std::string key = std::to_string(id_of_element(menu));
    for (size_t i = 0; i < texts.size() && i < 6; ++i)
        key += "|" + core::at(texts, i);
    if (!kOn || !menu || !logged.insert(key).second)
        return;
    const UiWidget* root = root_element();
    const UiWidget* screen = menu;
    for (int guard = 0; screen && guard < 64; ++guard) {
        const UiWidget* parent = screen->container_.get_pull();
        if (!parent || parent == root)
            break;
        screen = parent;
    }
    get_log("new front end: debug_restyle - the menu %08X (%s), its screen %08X:", id_of_element(menu), why.c_str(),
            id_of_element(screen));
    int budget = 600;
    const UiWidget* one = screen;
    std::array<char, 160> line{};
    std::snprintf(line.data(), sizeof(line), "%s %08X",
                  current_type_name(type_of(one)) ? current_type_name(type_of(one)) : "?", id_of_element(one));
    get_log("new front end: debug_restyle %s", line.data());
    dump_tree(first_child(screen), 1, &budget);
}

uint32_t v_menu_type()
{
    static const uint32_t kType = name_hash("VMenu");
    return kType;
}
uint32_t h_menu_type()
{
    static const uint32_t kType = name_hash("HMenu");
    return kType;
}

// The on-screen keyboard around `menu` (a row of keys - an HMenu - in a VMenu of such rows): the VMenu, else null.
UiWidget* keyboard_of(UiWidget* menu)
{
    if (!menu || type_of(menu) != h_menu_type())
        return nullptr;
    UiWidget* parent = menu->container_.get_pull();
    if (!parent || type_of(parent) != v_menu_type())
        return nullptr;
    int rows = 0, singles = 0;
    for (const UiWidget* row = first_child(parent); row; row = get_next(row)) {
        if (type_of(row) != h_menu_type())
            continue;
        ++rows;
        for (const UiWidget* key = first_child(row); key; key = get_next(key)) {
            std::vector<std::string> texts;
            int budget = 256;
            collect_texts(key, &texts, {}, &budget, false);
            for (const std::string& t : texts)
                singles += t.size() == 1 ? 1 : 0;
        }
    }
    return rows >= 3 && singles >= 20 ? parent : nullptr;
}

// The keyboard read: its rows of keys (a key's words: the last that isn't a button tag, e.g. "Backspace"), the key
// selected, the text typed (the first row that isn't keys - its text, the cursor's "_" left in).
mirror::MenuRead read_keyboard(UiWidget* keys, UiWidget* active)
{
    mirror::MenuRead read;
    read.found = true;
    read.keyboard = true;
    read.menu_id_ = id_of_element(keys);
    read.menu_key_ = keys;
    read.in_level_ = !in_front_end();
    int row = 0;
    bool entry_read = false;
    for (const UiWidget* child = first_child(keys); child; child = get_next(child)) {
        if (type_of(child) != h_menu_type()) {
            if (!entry_read) {
                std::vector<std::string> texts;
                int budget = 32;
                collect_texts(child, &texts, {}, &budget, false);
                if (!texts.empty())
                    read.entry = texts.front();
                entry_read = true;
            }
            continue;
        }
        std::vector<std::string> key_row;
        for (const UiWidget* key = first_child(child); key; key = get_next(key)) {
            std::vector<std::string> texts;
            int budget = 256; // (a wide key: a container of a score of cap sprites before its words)
            collect_texts(key, &texts, {}, &budget, false);
            std::string label;
            for (const std::string& t : texts)
                if (!(t.size() >= 2 && core::at(t, 0) == '\\'))
                    label = t;
            key_row.push_back(label);
        }
        if (child == active) {
            read.key_row_ = row;
            read.key_col_ = mouse_menus::selected_index(active);
        }
        read.keys.push_back(std::move(key_row));
        ++row;
    }
    if (UiWidget* title = game_screens::get_find(name_hash("title_text")))
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars_ && get_visible(title))
            read.title = core::down_cast<LabelWidget>(title)->chars_;
    return read;
}

// A button tag as the game spells it (gfx/text.cpp's): the explicit buttons through the controller map when one is in
// use, the meta buttons through the meta map; the arrow glyphs Up / Down / Left / Right, else the key's name.
std::string pad_button_label_for(char tag, char digit)
{
    int button = 0;
    if (tag == 'm' || tag == 'M') {
        button = text::hex_digit_value(render::button_glyph(digit));
    } else {
        button = static_cast<int>(render::button_for_tag(digit));
    }
    switch (button) {
    case 4:
        return text_tools::kKeyDown;
    case 5:
        return text_tools::kKeyRight;
    case 6:
        return text_tools::kKeyLeft;
    case 7:
        return text_tools::kKeyUp;
    case 10:
        return text_tools::
            kKeyDownRight; // (the grab tricks' order: Down, Down-Left \bc, Down-Right \ba, ... Up-Left \bb,
    case 11:
        return text_tools::kKeyUpLeft; // Up-Right \bd)
    case 12:
        return text_tools::kKeyDownLeft;
    case 13:
        return text_tools::kKeyUpRight;
    default:
        break;
    }
    const char* name = sio::get_button_key_name(button);
    return name ? name : "";
}

mirror::Tracker s_mirror;
std::vector<paint::Hit> s_mirror_hits;
std::vector<SDL_Event> s_mirror_events;
float s_mirror_scroll = 0.0f;
// Set while the original's screens are those of a hand-off left to them entirely: the create modes' editors (CAS,
// CAT, CAG, the park editor, graphics) and online - nothing restyled until ours is up again.
const char* s_left_original = nullptr;
std::string s_mirror_logged;

bool dialog_up()
{
    bool in_dialog = false;
    active_menu(&in_dialog);
    return in_dialog;
}

// --- a network game's level select ---
// The game's own level select made for a network game (gamemenu_levelselect.qb's create_level_select_menu): from the
// host options' Level item in the front end (create_network_host_options_menu sets in_server_options to 1; a choice
// is level_select_menu_exit, which sets the network preference "level" - select_host_option - and makes the host
// options again; Back is network_level_select_pad_back), or from a network lobby's CHANGE LEVEL (the host's pause menu,
// in_server_options 0: a choice is level_select_change_level - in a network game ChangeLevel for everyone, with
// show_warning; Back is level_select_menu_exit, the pause menu again). Its levels are the same lists as free skate's
// (level_select_get_section_list: only the edited levels' section left out - never in our data), its locks the
// seen flags (level_select_should_use_seen_flags: not a career). Ours takes it while it is up with nothing over it:
// level_select_anchor and its level_menu there, neither the created park's menu nor the pre-made parks' list (the
// game's own, for CREATED PARK), no dialog. The original select stays under ours, hidden, until its own scripts take
// it down.
bool net_level_select_up()
{
    if (!s_catalogue_ok || s_catalogue.sections.empty() || !current_load_state() || loading_screen_up())
        return false;
    if (!game_screens::get_find(name_hash("level_select_anchor")) || !game_screens::get_find(name_hash("level_menu")))
        return false;
    if (game_screens::get_find(name_hash("created_park_menu")) || game_screens::get_find(name_hash("premade_park_menu")))
        return false;
    if (script_integer("in_server_options") != 1 && !game_screens::net_session())
        return false;
    return !dialog_up();
}

mirror::Policy mirror_policy()
{
    mirror::Policy policy;
    const std::string ini = ini_path();
    policy.lists = GetPrivateProfileIntA("frontend", "restyle_menus", 1, ini.c_str()) != 0;
    policy.dialogs = GetPrivateProfileIntA("frontend", "restyle_dialogs", 1, ini.c_str()) != 0;
    policy.skaters = GetPrivateProfileIntA("frontend", "restyle_skater_select", 1, ini.c_str()) != 0;
    policy.online = GetPrivateProfileIntA("frontend", "restyle_online", 1, ini.c_str()) != 0;
    const std::string mode = game_screens::active_ruleset_label();
    policy.editor = s_left_original != nullptr || mode == "parkeditor" || mode == "creategoals" ||
                    script_integer("in_cat_preview_mode") == 1;
    return policy;
}

// What to restyle now (kind None: nothing).
mirror::View mirror_now()
{
    static mirror::Policy policy;
    static int g_policy_frames = 0;
    if (g_policy_frames-- <= 0) { // (the ini read twice a second)
        policy = mirror_policy();
        g_policy_frames = 30;
        mirror::set_button_namer(pad_button_label_for);
    }
    // (online: restyled too unless [frontend] restyle_online=0 - tested by the user by hand, never by a tool)
    if (!current_load_state() || loading_screen_up() || (game_screens::net_session() && !policy.online) ||
        level_editor::live_now())
        return {};
    // (a network game's level select: ours, never restyled as a list)
    if (net_level_select_up())
        return {};
    bool in_dialog = false;
    UiWidget* menu = active_menu(&in_dialog);
    // (none takes the pad yet, paused, in the front end or with a menu's anchor up: the menu being made - restyled from
    // its first frame, never drawn as the game makes it while it animates in; half a second at most for each menu - one
    // that still doesn't take the pad belongs to a screen worked some other way, left as the game made it. The anchors
    // - current_menu_anchor, dialog_box_anchor, the game's own "a menu is up" (FrontEnd::tick_frame) - cover the menus
    // made while the game runs on: a network game's pause menu (handle_start_pressed doesn't pause in a network game)
    // and the screens it opens, which were drawn as the game made them until they took the pad - the user's report of
    // 2026-11-02, the original pause menu for the first frames)
    static const UiWidget* s_coming = nullptr;
    static int s_coming_frames = 0;
    constexpr int kComingAtMost = 30;
    if (!menu && (get_paused() || in_front_end() || game_screens::get_find(name_hash("current_menu_anchor")) ||
                  game_screens::get_find(name_hash("dialog_box_anchor")))) {
        UiWidget* coming = active_menu(&in_dialog, true);
        s_coming_frames = coming && coming == s_coming ? s_coming_frames + 1 : 1;
        s_coming = coming;
        if (s_coming_frames <= kComingAtMost)
            menu = coming;
    } else {
        s_coming = nullptr;
    }
    if (!in_dialog && stats_screen_up(menu)) {
        // (the stats screen - its menus, or the story's stat being changed with none of them taking the pad)
        const mirror::View stats = mirror::get_build(read_stats(menu), policy);
        if (s_mirror_logged != "stats") {
            s_mirror_logged = "stats";
            get_log("new front end: restyled - the stats screen \"%s\", %zu stats", stats.title.c_str(), stats.rows.size());
        }
        return stats;
    }
    if (!menu)
        return {};
    // A create mode's editor chosen on the restyled CREATE-A-MODES (its item selected when that menu went): its menus
    // are left as they are - until the main menu or CREATE-A-MODES is back.
    static uint32_t s_last_menu = 0, s_last_item = 0;
    static const char* const kEditor = "a create mode's editor";
    const uint32_t menu_id = id_of_element(menu);
    const bool create_modes = game_screens::get_find(name_hash("menu_cas")) != nullptr;
    if (s_left_original == kEditor && (menu_id == name_hash("main_vmenu") || create_modes))
        s_left_original = nullptr;
    if (!s_left_original && menu_id != s_last_menu && s_last_menu == name_hash("main_menu") && !create_modes &&
        menu_id != name_hash("main_vmenu")) { // (Back to the main menu isn't an editor)
        for (const char* editor : {"menu_cas", "menu_cat", "menu_creategoals", "menu_parked", "menu_cagr"})
            if (s_last_item == name_hash(editor)) {
                get_log("new front end: %s - its menus left as they are", editor);
                s_left_original = kEditor;
            }
    }
    s_last_menu = menu_id;
    {
        const int selected = mouse_menus::selected_index(menu);
        const UiWidget* item = first_child(menu);
        for (int i = 0; item && i < selected; ++i)
            item = get_next(item);
        s_last_item = item && selected >= 0 ? id_of_element(item) : 0;
    }
    mirror::Policy now = policy;
    now.editor = now.editor || s_left_original != nullptr;
    UiWidget* keyboard = in_dialog ? nullptr : keyboard_of(menu);
    mirror::View view = mirror::get_build(keyboard ? read_keyboard(keyboard, menu) : read_menu(menu, in_dialog), now);
    // (each menu's decision logged once)
    std::array<char, 96> key{};
    std::snprintf(key.data(), sizeof(key), "%08X %s %s", id_of_element(menu), mirror::kind_name(view.kind), view.why.c_str());
    if (s_mirror_logged != key.data()) {
        s_mirror_logged = key.data();
        if (view.kind != mirror::Kind::kNone)
            get_log("new front end: restyled - the original's %s (%08X) \"%s\", %zu rows", mirror::kind_name(view.kind),
                    id_of_element(menu), view.title.c_str(), view.rows.size());
        else
            get_log("new front end: the original's menu %08X left as it is: %s", id_of_element(menu),
                    view.why.empty() ? "not restyled" : view.why.c_str());
    }
    if (view.kind == mirror::Kind::kNone)
        dump_left_menu(menu, view.why);
    return view;
}

// The mouse over the restyled screen: the original menu's selection stepped to the row (its own pad events), a click
// chooses it, the right button / Back is pad_back, the wheel steps.
struct StepContext {
    modules::FrontEnd* front_end_;
    bool horizontal;
};
int read_selection(void*)
{
    const UiWidget* menu = active_menu();
    return menu ? mouse_menus::selected_index(menu) : -1;
}
void send_step(void* context, int direction)
{
    auto* step = static_cast<StepContext*>(context);
    const uint32_t event = step->horizontal
                               ? (direction < 0 ? modules::FrontEnd::kPadLeft : modules::FrontEnd::kPadRight)
                               : (direction < 0 ? modules::FrontEnd::kPadUp : modules::FrontEnd::kPadDown);
    mouse_menus::send_menu_pad_event(step->front_end_, event);
}
bool step_to(modules::FrontEnd* front_end, int row)
{
    const mirror::View& v = s_mirror.get_shown();
    const UiWidget* menu = active_menu();
    if (!menu || row < 0 || std::cmp_greater_equal(row, v.rows.size()))
        return false;
    const int target = core::at(v.rows, row).index;
    if (target < 0)
        return false; // (a row of a menu not taking the pad: the story's other stats column)
    StepContext context{front_end, v.horizontal};
    mouse_menus::step_selection(target, mouse_menus::child_count(menu), mouse_menus::menu_wraps(menu), read_selection,
                                send_step, &context);
    return read_selection(nullptr) == target;
}

// The keyboard's key (row, col) selected with the game's own pad events (each step read back), then chosen.
void choose_key(modules::FrontEnd* front_end, int row, int col)
{
    for (int guard = 0; guard < 64; ++guard) {
        UiWidget* active = active_menu();
        UiWidget* keys = keyboard_of(active);
        if (!keys)
            return;
        const mirror::MenuRead now = read_keyboard(keys, active);
        if (now.key_row_ != row) {
            mouse_menus::send_menu_pad_event(front_end, now.key_row_ < row ? modules::FrontEnd::kPadDown
                                                                           : modules::FrontEnd::kPadUp);
            continue;
        }
        if (now.key_col_ != col) {
            mouse_menus::send_menu_pad_event(front_end, now.key_col_ < col ? modules::FrontEnd::kPadRight
                                                                           : modules::FrontEnd::kPadLeft);
            continue;
        }
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
        return;
    }
}

// The stats screen's row `row` selected with the game's own pad events: in the menu taking the pad, stepped there; in the
// story's other column, Left / Right toward it first (its column change keeps the row's place - the next steps reach
// the row). Nothing while a stat is being changed. Returns whether the row is selected now.
bool step_to_stat(modules::FrontEnd* front_end, int row)
{
    const mirror::View& v = s_mirror.get_shown();
    if (row < 0 || std::cmp_greater_equal(row, v.rows.size()))
        return false;
    if (row == v.selected)
        return true;
    if (v.stats_editing_)
        return false;
    const mirror::Row& r = core::at(v.rows, row);
    if (r.index >= 0)
        return step_to(front_end, row);
    if (v.selected >= 0)
        mouse_menus::send_menu_pad_event(front_end, r.stat_column_ < core::at(v.rows, v.selected).stat_column_
                                                        ? modules::FrontEnd::kPadLeft
                                                        : modules::FrontEnd::kPadRight);
    return false;
}

// A click on the stats screen: a row's name selects it (the story's selected row: chosen - a stat to change, or done);
// its blocks select it and step its value - under the value Left, past it Right (the story's row is chosen first: its
// Left / Right change the stat only while it is being changed).
void stats_click(modules::FrontEnd* front_end, const paint::Hit& hit)
{
    const mirror::View& v = s_mirror.get_shown();
    const bool story = v.stat_columns_ > 1;
    if (hit.kind == paint::Hit::Kind::kItem) {
        const bool was_selected = hit.index == v.selected;
        if (step_to_stat(front_end, hit.index) && was_selected && story)
            mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
        return;
    }
    if (hit.kind != paint::Hit::Kind::kChoice || !step_to_stat(front_end, hit.index / 16))
        return;
    if (story && !v.stats_editing_)
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
    mouse_menus::send_menu_pad_event(front_end, hit.index % 16 == 0 ? modules::FrontEnd::kPadLeft
                                                                    : modules::FrontEnd::kPadRight);
}

void mirror_mouse(modules::FrontEnd* front_end)
{
    std::vector<SDL_Event> events;
    events.swap(s_mirror_events);
    if (!s_mirror.get_up())
        return;
    int w = 0, h = 0;
    if (SDL_Window* window = sys::sdl_window())
        SDL_GetWindowSize(window, &w, &h);
    TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(h));
    auto hit_at = [&frame](float wx, float wy) -> const paint::Hit* {
        const float x = frame.design_x(wx);
        const float y = frame.design_y(wy);
        for (const paint::Hit& hit : s_mirror_hits)
            if (hit.get_holds(x, y))
                return &hit;
        return nullptr;
    };
    const bool skaters = s_mirror.get_shown().kind == mirror::Kind::kSkaters;
    const bool stats = s_mirror.get_shown().kind == mirror::Kind::kStats;
    for (const SDL_Event& e : events) {
        switch (e.type) {
        case SDL_MOUSEMOTION:
            if (stats) {
                if (const paint::Hit* hit = hit_at(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y))) {
                    const int row = hit->kind == paint::Hit::Kind::kItem     ? hit->index
                                    : hit->kind == paint::Hit::Kind::kChoice ? hit->index / 16
                                                                             : -1;
                    if (row >= 0 && row != s_mirror.get_shown().selected)
                        step_to_stat(front_end, row);
                }
                break;
            }
            // (the skater select: each step loads a skater - only a click selects there)
            if (!skaters)
                if (const paint::Hit* hit = hit_at(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y)))
                    if (hit->kind == paint::Hit::Kind::kItem && hit->index != s_mirror.get_shown().selected)
                        step_to(front_end, hit->index);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_RIGHT) {
                mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadBack);
            } else if (e.button.button == SDL_BUTTON_LEFT) {
                const paint::Hit* hit = hit_at(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
                if (!hit)
                    break;
                if (hit->kind == paint::Hit::Kind::kBack) {
                    mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadBack);
                } else if (stats) {
                    stats_click(front_end, *hit);
                } else if (hit->kind == paint::Hit::Kind::kStageMap &&
                           s_mirror.get_shown().kind == mirror::Kind::kKeyboard) {
                    choose_key(front_end, hit->index / 64, hit->index % 64);
                } else if (hit->kind == paint::Hit::Kind::kContinue) {
                    continue_last_level(front_end);
                } else if (hit->kind == paint::Hit::Kind::kItem) {
                    const bool was_selected = hit->index == s_mirror.get_shown().selected;
                    if (step_to(front_end, hit->index) && (!skaters || was_selected)) {
                        // (a row whose value Left / Right change - a slider, an option: its sides do that, as the
                        // game's own mouse control sends them - mouse_menus.cpp option_side; the middle chooses)
                        const mirror::View& shown = s_mirror.get_shown();
                        const float click_x = frame.design_x(static_cast<float>(e.button.x));
                        const float width = hit->x1 - hit->x0;
                        int side = 0;
                        if (shown.kind == mirror::Kind::kLinkChain && hit->index >= 0 &&
                            std::cmp_less(hit->index, shown.rows.size()) && core::at(shown.rows, hit->index).adjustable)
                            side = click_x < hit->x0 + width * 0.3f ? -1 : click_x >= hit->x1 - width * 0.3f ? 1 : 0;
                        mouse_menus::send_menu_pad_event(front_end, side < 0   ? modules::FrontEnd::kPadLeft
                                                                    : side > 0 ? modules::FrontEnd::kPadRight
                                                                               : modules::FrontEnd::kPadChoose);
                    }
                }
                return; // (the screen may be another now)
            }
            break;
        case SDL_MOUSEWHEEL:
            if (e.wheel.y) {
                StepContext context{front_end, s_mirror.get_shown().horizontal};
                send_step(&context, e.wheel.y > 0 ? -1 : 1);
            }
            break;
        // (the main menu's Continue - ours: the game's menu has no such item; every other key the game's menu takes)
        case SDL_KEYDOWN:
            if (s_mirror.get_shown().main_menu_ && !e.key.repeat &&
                (e.key.keysym.scancode == SDL_SCANCODE_X || e.key.keysym.scancode == SDL_SCANCODE_R)) {
                continue_last_level(front_end);
                return;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (s_mirror.get_shown().main_menu_ && e.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                continue_last_level(front_end);
                return;
            }
            break;
        default:
            break;
        }
    }
}

// The cover over a hand-off's end (Cover in new_frontend_model.h): what it draws meanwhile - ours (menu_state() as it was)
// or the restyled screen last up (a copy: the tracker forgets it as it goes).
Cover s_cover;
mirror::Tracker s_cover_tracker;
float s_cover_scroll = 0.0f;
// A dialog box in the front end (QUIT?, the warnings): the restyled screen it came up over, drawn under it (dimmed by the
// dialog's own ground) - the front end's 3D scene (the skate shop, the skater standing) is never seen through a dialog
// (the user's report, 2026-11-02: Quit showed the skate shop).
mirror::Tracker s_dialog_under;
bool s_dialog_under_on = false;
float s_dialog_under_scroll = 0.0f;
bool s_mirror_would_restyle = false; // (this frame's decision: the menu up is one to restyle)
// The original's elements held hidden though nothing is restyled yet: frames left while a menu that would be (or is
// still being built - View::unsettled) went; frames a menu has stayed unsettled (shown as the game made it after
// kFramesUnsettled).
int s_hold_frames = 0;
int s_unsettled_frames = 0;
// The front end come up (loaded, the loading screen gone) and none of its menus restyled or left original yet: ours
// draws its ground and the main menu's art meanwhile (Paint::Waiting) - the front end's 3D scene never shows bare.
bool s_front_end_was = false;
bool s_awaiting_front_end = false;
float s_await_time = 0.0f;
constexpr float kAwaitAtMost = 8.0f; // (seconds: then the front end as it is)

void start_cover(Cover::Over over, const char* why)
{
    if (s_cover.get_on() && s_cover.get_drawn() == over)
        return;
    get_log("new front end: covering - %s (%s drawn until the next screen is ready)", why,
            over == Cover::Over::kOurs ? "ours" : "the restyled screen");
    s_cover.kickoff(over);
    hide_root(true);
}

// Each frame (tick_frame and before_screen_layout): the game's level select made for a network game asks for ours -
// once each time it comes up (again after the created park's menu over it went); hidden from that frame on.
void watch_net_level_select()
{
    static bool s_was_up = false;
    const bool up = net_level_select_up();
    const bool came_up = up && !s_was_up;
    s_was_up = up;
    if (!up && s_open_screen == Screen::kLevels && s_open_levels == LevelsFor::kNet) {
        s_open_screen = Screen::kMain; // (it went before ours could come up)
        s_open_back = 0;
    }
    if (!came_up || s_state == State::kShown || s_state == State::kLaunching || s_open_screen != Screen::kMain)
        return;
    const bool server_options = script_integer("in_server_options") == 1;
    s_open_screen = Screen::kLevels;
    s_open_levels = LevelsFor::kNet;
    s_open_back = name_hash(server_options ? "network_level_select_pad_back" : "level_select_menu_exit");
    get_log("new front end: the game's level select for a network game (%s) - ours over it",
            server_options ? "the host options" : "a network game's change of level");
    hide_root(true);
}

// A level chosen on ours over a network game's level select: what that select's item does with it
// (level_select_valid_choice's level_callback_script, given the item's level_info - its level and text). CREATED
// PARK opens the game's own created park menu over the select (Play Park / Load Park / Pre-Made Parks, the player
// count checked there), ours back once it goes.
void choose_net_level(const std::string& load)
{
    int section = -1, index = -1;
    const StageMap* level = s_catalogue.get_find(load, &section, &index)
                                ? &core::at(core::at(s_catalogue.sections, section).levels, index)
                                : nullptr;
    change_phase(State::kAway);
    s_shown_back = 0;
    if (!level || !net_level_select_up()) {
        get_log("new front end: %s - the game's level select for it is gone; nothing chosen", load.c_str());
        return;
    }
    if (_stricmp(level->level_num_name_.c_str(), "LevelNum_Sk5ed") == 0) {
        get_log("new front end: the network game's level - the created park: the game's own menu for it");
        start_cover(Cover::Over::kOurs, "the created park's menu");
        script::spawn_script(name_hash("level_select_created_park_menu"), nullptr, 0, nullptr, 0, 0, false, false,
                             false, false);
        return;
    }
    const bool server_options = script_integer("in_server_options") == 1;
    const char* script = server_options ? "level_select_menu_exit" : "level_select_change_level";
    get_log("new front end: the network game's level - %s (%s)", load.c_str(), script);
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(name_hash("level"), name_hash(load.c_str()));
    params->append_word(name_hash("text"), level->name.c_str());
    script::spawn_script(name_hash(script), params, 0, nullptr, 0, 0, false, false, false, false);
    params->get_clear();
    script::delete_struct(params);
    start_cover(Cover::Over::kOurs, "a level chosen for the network game");
}

// Once a frame while ours isn't drawn (before_screen_layout): the restyled screen up or down, the original's elements
// hidden under it.
void update_mirror(float dt, bool allowed)
{
    const mirror::View now = allowed ? mirror_now() : mirror::View{};
    s_mirror_would_restyle = now.kind != mirror::Kind::kNone;
    // (a menu that will be restyled, or one still being built: its elements hidden from the frame it is first seen -
    // the tracker brings ours up a frame or so later; meanwhile nothing of the menu is drawn, never the original)
    s_unsettled_frames = now.kind == mirror::Kind::kNone && now.unsettled ? s_unsettled_frames + 1 : 0;
    if (s_mirror_would_restyle || (s_unsettled_frames > 0 && s_unsettled_frames <= mirror::kFramesUnsettled))
        s_hold_frames = mirror::kFramesToHide;
    else if (s_hold_frames > 0)
        --s_hold_frames;
    const bool was_up = s_mirror.get_up();
    mirror::Tracker before;
    if (was_up)
        before = s_mirror;
    const bool up = s_mirror.tick_frame(now, dt);
    if (up && !was_up) {
        get_log("new front end: restyling the original's %s", mirror::kind_name(s_mirror.get_shown().kind));
        s_mirror_scroll = 0.0f;
    }
    if (!up && was_up) {
        get_log("new front end: the restyled screen gone");
        s_mirror_hits.clear();
        // (what comes next - ours, another restyled screen, an original one - may take a few frames: the restyled
        // screen stays drawn meanwhile, never the original under it)
        if (allowed && current_load_state() && !loading_screen_up()) {
            s_cover_tracker = before;
            s_cover_scroll = s_mirror_scroll;
            start_cover(Cover::Over::kRestyled, "the restyled screen went");
        }
    }
    if (up || s_hold_frames > 0)
        hide_root(true);
    // (the screen under a dialog: the restyled one it replaced; kept while the dialog - or the cover drawing it - is up)
    const bool dialog_now = up && s_mirror.get_shown().kind == mirror::Kind::kDialog;
    if (dialog_now && was_up && before.get_shown().kind != mirror::Kind::kDialog) {
        s_dialog_under = before;
        s_dialog_under_scroll = s_mirror_scroll;
        s_dialog_under_on = true;
    } else if (dialog_now && !was_up && s_cover.get_drawn() == Cover::Over::kRestyled &&
               s_cover_tracker.get_shown().kind != mirror::Kind::kDialog) {
        s_dialog_under = s_cover_tracker;
        s_dialog_under_scroll = s_cover_scroll;
        s_dialog_under_on = true;
    } else if ((up && !dialog_now) || (!up && s_cover.get_drawn() != Cover::Over::kRestyled)) {
        s_dialog_under_on = false;
    }
}

// Once a frame while a cover is on (and ours isn't up): off once the screen the hand-off went to is ready. When it ends
// with nothing restyled, the original's elements come back the same frame (before_screen_layout).
void update_cover(float dt)
{
    if (!s_cover.get_on())
        return;
    bool in_dialog = false;
    const UiWidget* active = active_menu(&in_dialog);
    Cover::Now now;
    now.target_ready_ = s_mirror.get_up();
    now.gameplay = !current_load_state() || loading_screen_up() || level_editor::live_now() ||
                   (!in_front_end() && !get_paused() && !active);
    now.screen = active;
    now.left_original_ = active && !s_mirror_would_restyle;
    const char* why = nullptr;
    if (!s_cover.tick_frame(now, dt, &why))
        get_log("new front end: cover off - %s (%.2f s)", why ? why : "?", s_cover.get_instant());
}

void free_device_objects();

// Ours up: the screen a menu's item asked for (NewFrontend_Open), on its root - Main in the front end, Pause in a level
// (never shown: back at it, ours goes aside). `anchor`: the original screen up under it (or None).
void get_show(Anchor anchor)
{
    load_catalogue();
    if (s_open_screen == Screen::kMain) {
        change_phase(State::kAway);
        return;
    }
    const Screen root = in_front_end() ? Screen::kMain : Screen::kFreeze;
    const std::vector<Screen> stack = {root, s_open_screen};
    const LevelsFor levels = s_open_levels;
    s_open_root = root;
    s_shown_back = s_open_back;
    s_open_screen = Screen::kMain;
    s_open_back = 0;
    hide_root(true);
    s_anchor = anchor;
    s_left_original = nullptr;
    if (s_cover.get_on())
        get_log("new front end: cover off - ours up");
    s_cover.halt();
    if (s_mirror.get_up())
        get_log("new front end: the restyled screen gone (ours up)");
    s_mirror.get_reset();
    s_mirror_hits.clear();
    menu_state().get_reset(&s_catalogue, get_views(levels == LevelsFor::kNet), s_saved.last_level_, stack, levels);
    load_option_values();
    s_skater_name = root == Screen::kFreeze ? std::string() : get_skater_name();
    s_hits.clear();
    change_phase(State::kShown);
}

void run_action(modules::FrontEnd* front_end, const ActStep& action)
{
    (void)front_end;
    switch (action.kind) {
    case ActionKind::kNone:
        return;
    case ActionKind::kMoveSound:
        get_sound("generic_menu_up_or_down_sound", "up");
        return;
    case ActionKind::kChooseSound:
        get_sound("generic_menu_pad_choose_sound");
        return;
    case ActionKind::kBackSound:
        get_sound("generic_menu_pad_back_sound");
        return;
    case ActionKind::kGameOptions: {
        get_sound("generic_menu_pad_choose_sound");
        {
            // (GAME OPTIONS as the pause menu's / main menu's own item opens it - its script, the screen it makes
            // restyled; ours aside)
            const char* script = in_front_end() ? "create_setup_options_menu" : "create_options_menu";
            get_log("new front end: the game options (%s)", script);
            change_phase(State::kAway);
            s_shown_back = 0;
            script::spawn_script(name_hash(script), nullptr, 0, nullptr, 0, 0, false, false, false, false);
            // (ours stays drawn until the game's options screen is up: no frame of the root menu behind - the user's
            // report, 2026-11-02)
            start_cover(Cover::Over::kOurs, "the game options");
            return;
        }
    }
    case ActionKind::kOption:
        apply_option(action.option, action.value);
        get_sound("generic_menu_up_or_down_sound", "up");
        return;
    case ActionKind::kSkate: {
        std::string why;
        if (action.levels == LevelsFor::kNet) {
            choose_net_level(action.load);
            return;
        }
        if (action.levels == LevelsFor::kFreeze) {
            // (the pause menu's change of level, as its level select does it)
            get_log("new front end: change level to %s", action.load.c_str());
            hide_root(false);
            gsl::owner<script::Struct*> params = script::new_struct();
            params->put_name_key(name_hash("level"), name_hash(action.load.c_str()));
            script::spawn_script(name_hash("level_select_change_level"), params, 0, nullptr, 0, 0, false, false, false,
                                 false);
            params->get_clear();
            script::delete_struct(params);
            s_launch_level = current_load_state();
            change_phase(State::kLaunching);
            return;
        }
        if (begin_level::read_in(action.load.c_str(), &why)) {
            s_saved.last_level_ = action.load;
            menu_state().set_last_level(action.load);
            save_state();
            s_launch_level = current_load_state();
            if (action.levels == LevelsFor::kEdit) {
                s_edit = Edit::kWaitLevel;
                s_edit_time = 0.0f;
                s_edit_frames = s_edit_presses = 0;
                get_log("new front end: the level editor opens once %s is up", action.load.c_str());
            }
            change_phase(State::kLaunching);
        } else {
            get_log("new front end: %s didn't start: %s", action.load.c_str(), why.c_str());
            menu_state().cancel_launch();
        }
        return;
    }
    }
}

// --- input ---

bool get_key_down(SDL_Scancode key) { return core::at(platform::keyboard_keys(), key) != 0; }

void press_direction(modules::FrontEnd* front_end, Intake input)
{
    run_action(front_end, menu_state().get_press(input));
}

void handle_queued(modules::FrontEnd* front_end, const SDL_Event& e)
{
    int w = 0, h = 0;
    if (SDL_Window* window = sys::sdl_window())
        SDL_GetWindowSize(window, &w, &h);
    TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(h));
    auto hit_at = [&frame](float wx, float wy) -> const paint::Hit* {
        const float x = frame.design_x(wx);
        const float y = frame.design_y(wy);
        for (const paint::Hit& hit : s_hits)
            if (hit.get_holds(x, y))
                return &hit;
        return nullptr;
    };
    switch (e.type) {
    case SDL_KEYDOWN: {
        const bool repeat = e.key.repeat != 0;
        const bool shift = (e.key.keysym.mod & KMOD_SHIFT) != 0;
        switch (e.key.keysym.scancode) {
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_W:
            press_direction(front_end, Intake::kUp);
            break;
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_S:
            press_direction(front_end, Intake::kDown);
            break;
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_A:
            press_direction(front_end, Intake::kLeft);
            break;
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_D:
            press_direction(front_end, Intake::kRight);
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (!repeat)
                run_action(front_end, menu_state().get_press(Intake::kConfirm));
            break;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE:
            if (!repeat)
                run_action(front_end, menu_state().get_press(Intake::kBack));
            break;
        case SDL_SCANCODE_Q:
            // (on the level select Q / E change the picture; the tabs keep Page Up / Down, 1 / 2, Tab and the shoulders)
            if (menu_state().get_shown() == Screen::kLevels)
                cycle_level_art(-1);
            else
                run_action(front_end, menu_state().get_press(Intake::kPrevTab));
            break;
        case SDL_SCANCODE_PAGEUP:
        case SDL_SCANCODE_1:
            run_action(front_end, menu_state().get_press(Intake::kPrevTab));
            break;
        case SDL_SCANCODE_E:
            if (menu_state().get_shown() == Screen::kLevels)
                cycle_level_art(1);
            else
                run_action(front_end, menu_state().get_press(Intake::kNextTab));
            break;
        case SDL_SCANCODE_PAGEDOWN:
        case SDL_SCANCODE_2:
            run_action(front_end, menu_state().get_press(Intake::kNextTab));
            break;
        case SDL_SCANCODE_TAB:
            run_action(front_end, menu_state().get_press(shift ? Intake::kPrevTab : Intake::kNextTab));
            break;
        default:
            break;
        }
        return;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            press_direction(front_end, Intake::kUp);
            s_repeat = {static_cast<int>(Intake::kUp), kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            press_direction(front_end, Intake::kDown);
            s_repeat = {static_cast<int>(Intake::kDown), kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            press_direction(front_end, Intake::kLeft);
            s_repeat = {static_cast<int>(Intake::kLeft), kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            press_direction(front_end, Intake::kRight);
            s_repeat = {static_cast<int>(Intake::kRight), kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_A:
            run_action(front_end, menu_state().get_press(Intake::kConfirm));
            break;
        case SDL_CONTROLLER_BUTTON_START:
            // (Start on the pause menu resumes, as the original's; elsewhere it chooses)
            run_action(front_end, menu_state().get_press(menu_state().get_shown() == Screen::kFreeze &&
                                                                 menu_state().deepness() <= 1
                                                             ? Intake::kBack
                                                             : Intake::kConfirm));
            break;
        case SDL_CONTROLLER_BUTTON_B:
            run_action(front_end, menu_state().get_press(Intake::kBack));
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            run_action(front_end, menu_state().get_press(Intake::kPrevTab));
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            run_action(front_end, menu_state().get_press(Intake::kNextTab));
            break;
        default:
            break;
        }
        return;
    case SDL_CONTROLLERBUTTONUP:
        if (e.cbutton.button >= SDL_CONTROLLER_BUTTON_DPAD_UP && e.cbutton.button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT)
            s_repeat = {};
        return;
    case SDL_CONTROLLERAXISMOTION: {
        // The left stick as a d-pad: past half way (back inside a third: let go).
        int* axis = e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX   ? &s_stick_x
                    : e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? &s_stick_y
                                                                : nullptr;
        if (!axis)
            return;
        const int value = e.caxis.value;
        const int now = value > 16000 ? 1 : value < -16000 ? -1 : (std::abs(value) < 11000 ? 0 : *axis);
        if (now == *axis)
            return;
        *axis = now;
        if (now == 0) {
            s_repeat = {};
            return;
        }
        const Intake input =
            axis == &s_stick_x ? (now > 0 ? Intake::kRight : Intake::kLeft) : (now > 0 ? Intake::kDown : Intake::kUp);
        press_direction(front_end, input);
        s_repeat = {static_cast<int>(input), kRepeatDelay};
        return;
    }
    case SDL_MOUSEMOTION:
        if (const paint::Hit* hit = hit_at(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y))) {
            if (hit->kind == paint::Hit::Kind::kItem)
                run_action(front_end, menu_state().hover_item(hit->index));
            else if (hit->kind == paint::Hit::Kind::kStageMap)
                run_action(front_end, menu_state().hover_level(hit->index));
        }
        return;
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_RIGHT) {
            run_action(front_end, menu_state().get_press(Intake::kBack));
            return;
        }
        if (e.button.button != SDL_BUTTON_LEFT)
            return;
        if (const paint::Hit* hit = hit_at(static_cast<float>(e.button.x), static_cast<float>(e.button.y))) {
            const paint::Hit clicked = *hit;
            switch (clicked.kind) {
            case paint::Hit::Kind::kItem:
                run_action(front_end, menu_state().click_item(clicked.index));
                break;
            case paint::Hit::Kind::kStageMap:
                run_action(front_end, menu_state().click_level(clicked.index));
                break;
            case paint::Hit::Kind::kTab:
                run_action(front_end, menu_state().click_tab(clicked.index));
                break;
            case paint::Hit::Kind::kBack:
                run_action(front_end, menu_state().click_back());
                break;
            case paint::Hit::Kind::kSkate:
                run_action(front_end, menu_state().get_shown() == Screen::kOptions
                                          ? menu_state().get_press(Intake::kConfirm)
                                          : menu_state().click_skate());
                break;
            case paint::Hit::Kind::kContinue:
                break; // (the restyled main menu's - mirror_mouse)
            case paint::Hit::Kind::kChoice:
                run_action(front_end, menu_state().click_choice(clicked.index / 16, clicked.index % 16));
                break;
            }
        }
        return;
    case SDL_MOUSEWHEEL:
        if (e.wheel.y)
            run_action(front_end, menu_state().get_wheel(e.wheel.y));
        return;
    default:
        return;
    }
}

void handle_event(SDL_Event* event)
{
    if (!is_switched_on())
        return;
    switch (event->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        ++s_buttons_down;
        break;
    case SDL_CONTROLLERBUTTONUP:
        s_buttons_down = std::max(0, s_buttons_down - 1);
        break;
    default:
        break;
    }
    if (s_state != State::kShown) {
        // (a restyled original screen: the mouse is ours - its keys and buttons go on to the original menu)
        if (s_mirror.get_up() && s_mirror_events.size() < 64 &&
            (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL ||
             event->type == SDL_KEYDOWN || event->type == SDL_CONTROLLERBUTTONDOWN))
            s_mirror_events.push_back(*event);
        return;
    }
    switch (event->type) {
    case SDL_KEYDOWN:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERAXISMOTION:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEWHEEL:
        if (s_events.size() < 256)
            s_events.push_back(*event);
        break;
    default:
        break;
    }
}

// --- the Level Editor from the Create screen ---

void edit_update(modules::FrontEnd* front_end, float dt)
{
    if (s_edit == Edit::kDisengaged)
        return;
    s_edit_time += dt;
    if (s_edit_time > 60.0f) {
        get_log("new front end: the level editor didn't open in 60 s - free skate as it is");
        s_edit = Edit::kDisengaged;
        return;
    }
    const bool in_level =
        current_load_state() && !in_front_end() && objects::find_object(kSkater) && !loading_screen_up();
    if (!in_level) {
        s_edit_frames = 0;
        return;
    }
    if (s_edit == Edit::kWaitLevel) {
        s_edit_frames = get_paused() ? 0 : s_edit_frames + 1;
        if (s_edit_frames < 90)
            return;
        // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++s_edit_presses;
        s_edit = Edit::kWaitPause;
        s_edit_frames = 0;
        return;
    }
    // (kWaitPause) the pause menu up with its LEVEL EDITOR: chosen.
    if (!get_paused()) {
        if (++s_edit_frames >= 120 && s_edit_presses < 3) {
            mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
            ++s_edit_presses;
            s_edit_frames = 0;
        }
        return;
    }
    if (++s_edit_frames < 20)
        return;
    s_edit = Edit::kDisengaged;
    if (UiWidget* item = game_screens::get_find(name_hash("menu_level_editor"))) {
        get_log("new front end: the pause menu's LEVEL EDITOR chosen");
        game_screens::launch_pad(front_end, modules::FrontEnd::kPadChoose, item);
    } else {
        get_log("new front end: no LEVEL EDITOR in the pause menu (refused here?) - the pause menu as it is");
    }
}

// --- the ImGui context ---

ImGuiContext* s_context = nullptr;
bool s_context_failed = false;
paint::FontFiles s_fonts;
bool s_fonts_resolved = false;

bool begin_context(ImGuiContext** previous)
{
    *previous = ImGui::GetCurrentContext();
    if (!s_context) {
        if (s_context_failed)
            return false;
        s_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(s_context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        // (the cursor is the game's - mouse_menus shows it in the front end; ImGui leaves it alone)
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        if (!ui_backend::prepare_all()) {
            ImGui::DestroyContext(s_context);
            s_context = nullptr;
            s_context_failed = true;
            ImGui::SetCurrentContext(*previous);
            get_log("new front end: ImGui couldn't start on the game's window / device - not drawn");
            return false;
        }
    }
    ImGui::SetCurrentContext(s_context);
    return true;
}

void end_context(ImGuiContext* previous) { ImGui::SetCurrentContext(previous); }

void free_device_objects()
{
    free_art();
    if (!s_context)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_context);
    ui_backend::invalidate_context_objects();
    ImGui::SetCurrentContext(previous);
}

// --- the automation channel's commands ---

struct Awaiting {
    bool active = false;
    bool confirm = false;
    Screen screen = Screen::kMain;
    std::string item;
    std::string result; // (set once done)
};
Awaiting s_pending;

bool shown_and_idle(Screen screen)
{
    return s_state == State::kShown && !menu_state().occupied() && menu_state().get_shown() == screen;
}

// The things the channel never does through ours: anything online (none of ours goes online now - the game's ONLINE
// item is the game's; kept for the commands' shape).
bool get_refused(Screen screen, const std::string& item)
{
    (void)screen;
    (void)item;
    return false;
}

} // namespace

bool& is_switched_on()
{
    static bool enabled = config_int("features", "new_frontend", 0) != 0;
    return enabled;
}

void prepare_all()
{
    if (!is_switched_on())
        return;
    os_layer::register_event_handler(handle_event);
    get_log("new front end: on ([features] new_frontend=1)");
}

bool showing() { return s_state == State::kShown || s_state == State::kLaunching; }

// (while a cover is on, the screen under it is hidden: no presses reach it blind - a few frames, 3 s at most)
bool blocks_game_input() { return is_switched_on() && (showing() || s_guard || s_cover.get_on()); }

void tick_frame(modules::FrontEnd* front_end)
{
    if (!is_switched_on())
        return;
    auto now = std::chrono::steady_clock::now();
    float dt = s_ticked ? std::chrono::duration<float>(now - s_last_tick).count() : 0.0f;
    s_last_tick = now;
    s_ticked = true;
    dt = std::clamp(dt, 0.0f, 0.25f);
    s_state_time += dt;
    load_catalogue();
    note_level_loaded();
    update_session(dt);
    edit_update(front_end, dt);

    if (s_guard) {
        s_guard_time += dt;
        const bool held = get_key_down(SDL_SCANCODE_RETURN) || get_key_down(SDL_SCANCODE_KP_ENTER) ||
                          get_key_down(SDL_SCANCODE_SPACE) || get_key_down(SDL_SCANCODE_ESCAPE) ||
                          get_key_down(SDL_SCANCODE_BACKSPACE) || s_buttons_down > 0 ||
                          (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK);
        if (!held || s_guard_time > 2.0f)
            s_guard = false;
    }

    // A tool's own UI has the screen (the level editor - its Dear ImGui screens, or its old HUD of screen elements):
    // ours steps aside completely - not drawn, nothing restyled, nothing covered, the original's elements as the tool
    // left them - and comes back only once the pause menu really is back (the user's report of 2026-10-25: our pause
    // menu stayed drawn over the level editor - its hand-off never finished, as this isn't called while the editor
    // runs... mouse_menus::tick_frame calls it then too, for this).
    if (level_editor::live_now()) {
        if (s_state != State::kIdle && s_state != State::kAway) {
            get_log("new front end: the level editor is up - ours aside");
            change_phase(State::kAway);
        }
        if (s_cover.get_on())
            get_log("new front end: cover off - the level editor is up");
        s_cover.halt();
        if (s_mirror.get_up()) {
            s_mirror.get_reset();
            s_mirror_hits.clear();
        }
        s_events.clear();
        s_mirror_events.clear();
        hide_root(false);
        return;
    }

    watch_net_level_select();
    Anchor anchor = game_screens::at_hand(front_end);
    // The menu the pad reaches now: a dialog box over an anchor, or another menu over the pause menu (a sub-screen that
    // keeps pause_vmenu - VIEW GOALS, the level select...), and ours isn't over it (the dialog / screen is restyled, or
    // left as it is).
    bool in_dialog = false;
    const UiWidget* active = active_menu(&in_dialog);
    const bool pause_active = !active || id_of_element(active) == name_hash("pause_vmenu");
    (void)pause_active;
    // (every menu is the game's, restyled - ours only for a screen a menu's item asked for, while that is up)
    const bool usable = current_load_state() && !loading_screen_up() && !in_dialog && s_edit == Edit::kDisengaged &&
                        (s_open_screen != Screen::kMain || s_state == State::kShown);

    switch (s_state) {
    case State::kIdle:
    case State::kAway:
        if (!current_load_state() ||
            (!in_front_end() && anchor == Anchor::kNone && s_state == State::kAway && !get_paused())) {
            if (s_state != State::kIdle) {
                change_phase(State::kIdle);
                free_device_objects();
            }
        }
        if (usable) {
            get_show(anchor);
            keep_root_hidden();
            return;
        }
        // (the restyled screen and the cover are kept by before_screen_layout; the mouse over the restyled screen here)
        if (s_mirror.get_up())
            mirror_mouse(front_end);
        else
            s_mirror_events.clear();
        keep_root_hidden();
        return;
    case State::kLaunching:
        menu_state().tick(dt);
        if ((s_launch_level == kLoadMainMenu && !in_front_end()) ||
            (s_launch_level != kLoadMainMenu && current_load_state() != s_launch_level) ||
            (s_launch_level != kLoadMainMenu && loading_screen_up())) {
            change_phase(State::kIdle);
            free_device_objects();
        } else if (s_state_time > 30.0f) {
            get_log("new front end: the level didn't start in 30 seconds - back to the menus");
            menu_state().cancel_launch();
            change_phase(State::kIdle);
        }
        return;
    case State::kShown:
        break;
    }
    keep_root_hidden();
    if (menu_state().get_shown() == s_open_root && !menu_state().occupied()) {
        // (back from the screen a menu's item asked for: that menu - the game's, restyled - again, or the script it
        // gave for Back when its menu went - the skate shop's)
        get_log("new front end: back from our screen%s", s_shown_back ? " - its back script" : "");
        change_phase(State::kAway);
        if (menu_state().levels_purpose() == LevelsFor::kNet) // (the game's select under ours until its script goes)
            start_cover(Cover::Over::kOurs, "back from a network game's level select");
        if (s_shown_back)
            script::spawn_script(s_shown_back, nullptr, 0, nullptr, 0, 0, false, false, false, false);
        s_shown_back = 0;
        return;
    }
    if (!usable) {
        // (the original's screen changed on its own: ours aside - back when an anchor is up)
        if (anchor == Anchor::kNone && !in_front_end() && !get_paused()) {
            change_phase(State::kIdle);
            free_device_objects();
        } else if (usable && anchor != Anchor::kNone) {
            get_show(anchor);
        } else {
            change_phase(State::kAway);
            // (a dialog over the anchor, a sub-screen over the pause menu...: ours stays on screen until it is ready)
            if (current_load_state() && !loading_screen_up())
                start_cover(Cover::Over::kOurs, "the original's screen changed under ours");
        }
        return;
    }
    std::vector<SDL_Event> events;
    events.swap(s_events);
    for (const SDL_Event& e : events) {
        handle_queued(front_end, e);
        if (s_state != State::kShown)
            return;
    }
    if (s_repeat.direction >= 0) {
        s_repeat.wait -= dt;
        if (s_repeat.wait <= 0.0f) {
            s_repeat.wait += kRepeatEvery;
            press_direction(front_end, static_cast<Intake>(s_repeat.direction));
        }
    }
    // The automation channel's command waiting for its screen.
    if (s_pending.active && shown_and_idle(s_pending.screen)) {
        s_pending.active = false;
        if (menu_state().select_id(s_pending.item)) {
            get_log("new front end: automation - %s/%s %s", get_screen_name(s_pending.screen), s_pending.item.c_str(),
                    s_pending.confirm ? "chosen" : "selected");
            s_pending.result = "ok";
            if (s_pending.confirm)
                run_action(front_end, menu_state().get_press(Intake::kConfirm));
        } else {
            s_pending.result = "error: no item " + s_pending.item + " on " + get_screen_name(s_pending.screen);
            get_log("new front end: automation - %s", s_pending.result.c_str());
        }
    }
    if (s_state == State::kShown)
        run_action(front_end, menu_state().tick(dt));
}

bool owns_mouse() { return is_switched_on() && !showing() && s_mirror.get_up(); }

void before_screen_layout()
{
    // (the level editor: Update steps ours aside and gives the original's elements back)
    if (!is_switched_on() || level_editor::live_now())
        return;
    static std::chrono::steady_clock::time_point s_last;
    static bool s_any = false;
    auto clock = std::chrono::steady_clock::now();
    const float dt = s_any ? std::clamp(std::chrono::duration<float>(clock - s_last).count(), 0.0f, 0.25f) : 0.0f;
    s_last = clock;
    s_any = true;
    const bool front_end = in_front_end() && !loading_screen_up();
    if (front_end && !s_front_end_was) {
        s_awaiting_front_end = true;
        s_await_time = 0.0f;
    }
    s_front_end_was = front_end;
    if (s_state == State::kLaunching)
        return; // (a level starting: the original's elements as the launch left them)
    watch_net_level_select();
    if (s_state == State::kIdle || s_state == State::kAway) {
        update_mirror(dt, true);
        update_cover(dt);
    }
    if (s_awaiting_front_end) {
        s_await_time += dt;
        const bool left_original = active_menu() && !s_mirror_would_restyle && s_hold_frames == 0;
        if (!front_end || s_mirror.get_up() || showing() || left_original || s_await_time > kAwaitAtMost) {
            get_log("new front end: the front end's first screen is up after %.2f s (%s)", s_await_time,
                    s_mirror.get_up() || showing() ? "ours"
                    : left_original                ? "an original screen"
                                                   : "none");
            s_awaiting_front_end = false;
        }
    }
    // Decided here, before the screen elements are laid out for this frame's drawing: whatever the scripts made since
    // the last frame (the pause menu, a dialog, the main menu) is hidden before it is ever drawn.
    const bool net_levels_asked = s_open_screen == Screen::kLevels && s_open_levels == LevelsFor::kNet;
    if (s_state == State::kShown || s_mirror.get_up() || s_cover.get_on() || s_hold_frames > 0 ||
        s_awaiting_front_end || net_levels_asked) {
        hide_root(true);
        keep_root_hidden(); // (a script that scaled root_window back since - create_pause_menu's unhide_root_window)
    } else if (s_root_hidden) {
        hide_root(false); // (nothing of ours over the original's elements any more: they're back)
    }
}

namespace {
// The pause menus' level and session (the level as the data names it; the session so far).
paint::Extras pause_extras()
{
    paint::Extras extras;
    const Section* section = nullptr;
    if (const StageMap* level = level_played(&section)) {
        extras.level_name_ = level->name;
        extras.level_kicker_ = section->tab;
        extras.level_colour_ = section->colour;
    }
    extras.session.seconds = session_state().seconds;
    extras.session.best_combo_ = session_state().best_combo_;
    if (session_state().gaps_total_ > 0) {
        extras.session.gaps = static_cast<int>(session_state().gaps_landed_.size());
        extras.session.gaps_total_ = session_state().gaps_total_;
    }
    return extras;
}

// The last level played, as the data names it ("": none, or not in the data) - the main menu's Continue.
std::string continue_level_name()
{
    int section = -1, level = -1;
    if (s_saved.last_level_.empty() || !s_catalogue_ok || !s_catalogue.get_find(s_saved.last_level_, &section, &level))
        return {};
    return core::at(core::at(s_catalogue.sections, section).levels, level).name;
}

// Continue (the restyled main menu's X / R, or its hint clicked): the last level played, in free skate.
void continue_last_level(modules::FrontEnd* front_end)
{
    if (continue_level_name().empty())
        return;
    get_sound("generic_menu_pad_choose_sound");
    ActStep skate{ActionKind::kSkate};
    skate.levels = LevelsFor::kFreeSkate;
    skate.load = s_saved.last_level_;
    get_log("new front end: continue - %s", skate.load.c_str());
    hide_root(false);
    run_action(front_end, skate);
}

// What a restyled screen needs besides the menu read: the pause menus' level and session, the levels' data for the
// items' art, the main menu's Continue level.
paint::Extras restyle_extras()
{
    paint::Extras extras = pause_extras();
    extras.art = art_for;
    extras.catalogue = s_catalogue_ok ? &s_catalogue : nullptr;
    extras.continue_name_ = continue_level_name();
    return extras;
}

// --- the script commands (the overlay's menu items) ---

// NewFrontend_Item item=on | restart | levels | options | editor: whether the overlay's menus make that item of ours -
// on: the new front end's main menu (its items, our Career menu); RESTART (free skate's pause menu); CHANGE LEVEL /
// the skate shop's Play Level opening our level select (free skate, with the levels' data); OPTIONS (ours); LEVEL
// EDITOR (Create-a-modes: [features] level_editor and its scripts). False while the new front end is off, and in
// network games: the menus as they were.
bool cfunc_item(script::Struct* params, script::Script*)
{
    if (!is_switched_on() || game_screens::net_session())
        return false;
    uint32_t item = 0;
    if (params)
        params->read_hash_value(name_hash("item"), &item, 0);
    const std::string mode = game_screens::active_ruleset_label();
    const bool free_skate = mode == "freeskate" || mode == "singlesession";
    const bool levels = s_catalogue_ok && !s_catalogue.sections.empty();
    if (item == name_hash("on"))
        return true;
    if (item == name_hash("restart"))
        return !in_front_end() && free_skate;
    if (item == name_hash("levels"))
        return free_skate && levels;
    if (item == name_hash("options"))
        return true;
    if (item == name_hash("editor"))
        return in_front_end() && levels && level_editor::is_switched_on() && is_script(name_hash("level_editor_enter"));
    return false;
}

// NewFrontend_Open screen=levels | editlevels | options [back=<script>]: our screen (the menu's item chose it: its own
// sound played) - shown the next frame over whatever is up; closed with Back to the restyled menu it came from, or
// `back` spawned (its menu went: the skate shop's).
bool cfunc_show_new_frontend(script::Struct* params, script::Script*)
{
    uint32_t screen = 0, back = 0;
    if (!is_switched_on() || !params || !params->read_hash_value(name_hash("screen"), &screen, 0))
        return false;
    params->read_hash_value(name_hash("back"), &back, 0);
    const bool levels = s_catalogue_ok && !s_catalogue.sections.empty();
    if (screen == name_hash("levels") && levels) {
        s_open_screen = Screen::kLevels;
        s_open_levels = in_front_end() ? LevelsFor::kFreeSkate : LevelsFor::kFreeze;
    } else if (screen == name_hash("editlevels") && levels) {
        s_open_screen = Screen::kLevels;
        s_open_levels = LevelsFor::kEdit;
    } else if (screen == name_hash("options")) {
        s_open_screen = Screen::kOptions;
    } else {
        return false;
    }
    s_open_back = back;
    get_log("new front end: %s asked for%s", get_screen_name(s_open_screen), back ? " (with a back script)" : "");
    return true;
}
} // namespace

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {"NewFrontend_Item", &cfunc_item},
        {"NewFrontend_Open", &cfunc_show_new_frontend},
    });
    script::install_script_functions(kFunctions);
}

void blit()
{
    // (never over a tool's own UI: the level editor - Update steps ours aside)
    if (!is_switched_on() || level_editor::live_now() ||
        (!showing() && !s_mirror.get_up() && !s_cover.get_on() && !s_awaiting_front_end))
        return;
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous))
        return;
    int w = 0, h = 0;
    ui_backend::window_size(&w, &h);
    if (w > 0 && h > 0) {
        const TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(h));
        const float scale = std::round(frame.scale * 100.0f) / 100.0f;
        if (!s_fonts_resolved) {
            s_fonts = resolve_fonts();
            s_fonts_resolved = true;
        }
        if (scale != paint::built_scale()) {
            paint::build_fonts(s_fonts, scale);
            ui_backend::invalidate_context_objects(); // (the font texture made again)
            if (ImGui::GetIO().Fonts->Fonts.Size == 0) {
                // (no font at all could be read: nothing can be drawn - off for this run, the original menus back)
                get_log("new front end: no font could be loaded - off for this run");
                end_context(previous);
                hide_root(false);
                change_phase(State::kIdle);
                is_switched_on() = false;
                return;
            }
        }
        ui_backend::new_frame();
        ImGui::NewFrame();
        if (!showing() && !s_mirror.get_up() && !s_cover.get_on()) {
            // (the front end come up, its first menu not yet: its ground and the main menu's art)
            paint::get_waiting(frame, static_cast<float>(w), static_cast<float>(h), restyle_extras(), "career");
            ImGui::Render();
            ui_backend::render();
            end_context(previous);
            return;
        }
        if (!showing() && (s_mirror.get_up() || s_cover.get_drawn() == Cover::Over::kRestyled)) {
            // (an original screen restyled - phase 3; or, under a cover, the one last up)
            const paint::Extras pause = restyle_extras();
            const mirror::Tracker& drawn = s_mirror.get_up() ? s_mirror : s_cover_tracker;
            if (drawn.get_shown().kind == mirror::Kind::kDialog && !drawn.get_shown().over_game_) {
                // (a dialog in the front end: over the screen it came from, else over the front end's ground and art -
                // never over the 3D scene)
                if (s_dialog_under_on)
                    paint::restyled(s_dialog_under, frame, static_cast<float>(w), static_cast<float>(h), nullptr,
                                    &s_dialog_under_scroll, &pause);
                else
                    paint::get_waiting(frame, static_cast<float>(w), static_cast<float>(h), pause, "career");
            }
            if (s_mirror.get_up())
                paint::restyled(s_mirror, frame, static_cast<float>(w), static_cast<float>(h), &s_mirror_hits,
                                &s_mirror_scroll, &pause);
            else {
                std::vector<paint::Hit> unused;
                paint::restyled(s_cover_tracker, frame, static_cast<float>(w), static_cast<float>(h), &unused,
                                &s_cover_scroll, &pause);
            }
            ImGui::Render();
            ui_backend::render();
            end_context(previous);
            return;
        }
        paint::Extras extras;
        extras.skater_name_ = s_skater_name;
        extras.art = art_for;
        extras.records = records_for;
        extras.levels_missing_ = !s_catalogue_ok;
        extras.restart_pending_ = s_restart_pending;
        extras.options_note_ = s_options_note;
        static const bool kShowIniKeys = text::to_int(ini_string("show_ini_keys", "0").c_str()) != 0;
        extras.show_ini_keys_ = kShowIniKeys;
        if (menu_state().over_pause()) {
            const paint::Extras pause = pause_extras();
            extras.level_name_ = pause.level_name_;
            extras.level_kicker_ = pause.level_kicker_;
            extras.level_colour_ = pause.level_colour_;
            extras.session = pause.session;
        }
        int rows = 0;
        paint::get_screen(menu_state(), frame, static_cast<float>(w), static_cast<float>(h), extras, &s_hits, &rows);
        if (rows > 0)
            menu_state().set_visible_rows(rows);
        ImGui::Render();
        ui_backend::render();
    }
    end_context(previous);
}

void device_lost()
{
    if (!s_context)
        return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(s_context);
    ui_backend::invalidate_context_objects();
    ImGui::SetCurrentContext(previous);
}

std::string automation_command(const std::string& line, bool* done)
{
    *done = true;
    if (!is_switched_on())
        return "error: the new front end is off ([features] new_frontend=0)";
    // fe select <screen>/<item> [seconds] | fe focus <screen>/<item> [seconds] | fe back | fe press <input>
    std::vector<std::string> words;
    for (size_t at = 0; at < line.size();) {
        const size_t start = line.find_first_not_of(" \t", at);
        if (start == std::string::npos)
            break;
        const size_t end = line.find_first_of(" \t", start);
        words.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        at = end == std::string::npos ? line.size() : end;
    }
    if (words.empty())
        return "error: fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>";
    std::string verb = core::at(words, 0);
    for (char& ch : verb)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (verb == "select" || verb == "focus") {
        if (words.size() < 2 || core::at(words, 1).find('/') == std::string::npos)
            return "error: fe " + verb + " <screen>/<item> [seconds]";
        const std::string screen_name = core::at(words, 1).substr(0, core::at(words, 1).find('/'));
        const std::string item = core::at(words, 1).substr(core::at(words, 1).find('/') + 1);
        Screen screen{};
        if (!screen_from_name(screen_name, &screen))
            return "error: no screen " + screen_name;
        if (get_refused(screen, item))
            return "refused: online (" + core::at(words, 1) + ")";
        s_pending = {};
        s_pending.active = true;
        s_pending.confirm = verb == "select";
        s_pending.screen = screen;
        s_pending.item = item;
        *done = false;
        return "";
    }
    if (verb == "back") {
        if (s_state != State::kShown || menu_state().occupied())
            return "error: the new front end isn't up";
        run_action(the_front_end(), menu_state().get_press(Intake::kBack));
        return "ok";
    }
    if (verb == "press") {
        static const auto kInputs = std::to_array<std::pair<const char*, Intake>>({
            {"up", Intake::kUp},           {"down", Intake::kDown},       {"left", Intake::kLeft},
            {"right", Intake::kRight},     {"confirm", Intake::kConfirm}, {"back", Intake::kBack},
            {"prevtab", Intake::kPrevTab}, {"nexttab", Intake::kNextTab}});
        if (words.size() < 2)
            return "error: fe press <up|down|left|right|confirm|back|resume|prevtab|nexttab>";
        for (const auto& [name, input] : kInputs)
            if (_stricmp(core::at(words, 1).c_str(), name) == 0) {
                if (s_state != State::kShown || menu_state().occupied())
                    return "error: the new front end isn't up";
                if (input == Intake::kConfirm && get_refused(menu_state().get_shown(), menu_state().chosen_item_id()))
                    return "refused: online";
                run_action(the_front_end(), menu_state().get_press(input));
                return "ok";
            }
        return "error: no input " + core::at(words, 1);
    }
    return "error: fe " + verb + "? (select, focus, back, press)";
}

std::string automation_result()
{
    if (s_pending.active)
        return {};
    return s_pending.result.empty() ? std::string("ok") : s_pending.result;
}

void automation_cancel() { s_pending.active = false; }

bool automation_screen_up(const std::string& name)
{
    // (the restyled original screens: dialog, skaters, list)
    if (is_switched_on() && !showing() && s_mirror.get_up() &&
        _stricmp(name.c_str(), mirror::kind_name(s_mirror.get_shown().kind)) == 0)
        return true;
    Screen screen{};
    return is_switched_on() && screen_from_name(name, &screen) && shown_and_idle(screen);
}

std::string automation_status()
{
    if (!is_switched_on())
        return {};
    std::string text = std::string(get_state_name(s_state));
    if (s_state == State::kShown || s_state == State::kLaunching) {
        text += std::string(" ") + get_screen_name(menu_state().get_shown());
        const std::string item = menu_state().chosen_item_id();
        if (!item.empty())
            text += "/" + item;
    }
    if (s_anchor != Anchor::kNone && s_state == State::kShown)
        text += std::string(" over the ") + game_screens::anchor_name(s_anchor);
    if (!showing() && s_mirror.get_up()) {
        const mirror::View& v = s_mirror.get_shown();
        text += std::string(", restyled ") + mirror::kind_name(v.kind) + " \"" + v.title + "\"";
        if (v.selected >= 0 && std::cmp_less(v.selected, v.rows.size()))
            text += " on \"" + core::at(v.rows, v.selected).label + "\"";
    }
    return text;
}

} // namespace frontend::new_frontend
