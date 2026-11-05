// NOT ORIGINAL - what the new front end keeps and loads (ours): the ini, the fonts, the level
// catalogue, the saved state and the records. See new_frontend.h.

#include <windows.h>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <array>
#include "base/core/checksums.h"
#include "engine/file/data_overlay.h"
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_model.h"
#include "engine/script/cscript.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "base/core/parse_number.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "frontend/new_frontend_internal.h"

namespace frontend::new_frontend {
// What the source file's anonymous namespace held that its pieces share (new_frontend_internal.h): visible as it was.
// (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend

namespace frontend::new_frontend {
namespace {

constexpr std::size_t kIniValueSize = 512;      // an ini value read
constexpr float kBoldWeight = 0.022F;           // the bold face's stroke weight
constexpr std::size_t kReadChunkSize = 4096;    // a file read in chunks

} // namespace
namespace internal {

// --- the setting and the files ---

std::string ini_path() { return game_folder_path(build_info::kFileName); }

std::string ini_string(const char* key, const char* fallback)
{
    std::array<char, kIniValueSize> value = {};
    GetPrivateProfileStringA("frontend", key, fallback, value.data(), sizeof(value), ini_path().c_str());
    return value.data();
}

} // namespace internal
namespace {

bool is_file_present(const std::string& path) { return fs::disk().kind(path.c_str()) == fs::PathKind::kFile; }

std::string windows_fonts()
{
    std::array<char, MAX_PATH> windows = {};
    const UINT length = GetWindowsDirectoryA(windows.data(), MAX_PATH);
    return std::string(windows.data(), length) + "\\Fonts\\";
}

// A font named in the ini (or the default list): a full path as it is, else the overlay's frontend\fonts, else
// Windows' fonts folder. "" if none of the names is there.
std::string find_font(const std::vector<std::string>& names)
{
    const std::string ours = data_overlay::get_folder() + R"(\frontend\fonts\)";
    const std::string windows = windows_fonts();
    for (const std::string& name : names) {
        if (name.empty()) {
            continue;
        }
        if (name.find(':') != std::string::npos || name.starts_with("\\\\")) {
            if (is_file_present(name)) {
                return name;
            }
            continue;
        }
        if (is_file_present(ours + name)) {
            return ours + name;
        }
        if (is_file_present(windows + name)) {
            return windows + name;
        }
    }
    return {};
}

} // namespace
namespace internal {

paint::FontFiles resolve_fonts()
{
    paint::FontFiles files;
    files.display = find_font({ini_string("display_font", ""), "Anton-Regular.ttf", "bahnschrift.ttf", "impact.ttf"});
    files.mono = find_font({ini_string("mono_font", ""), "IBMPlexMono-Regular.ttf", "consola.ttf"});
    files.body = find_font({ini_string("body_font", ""), "IBMPlexSans-Regular.ttf", "segoeui.ttf"});
    // Bahnschrift (Windows' DIN): the default instance of its variable font is regular width and weight - drawn
    // narrower and heavier to come nearer Anton; Anton and Impact are already condensed and heavy.
    std::string lower = files.display;
    for (char& ch : lower) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    const bool bahnschrift = lower.find("bahnschrift") != std::string::npos;
    files.condense =
        static_cast<float>(text::to_int(ini_string("display_condense", bahnschrift ? "84" : "100").c_str())) / 100.0F;
    files.bold = (text::to_int(ini_string("display_bold", bahnschrift ? "1" : "0").c_str()) != 0) ? kBoldWeight : 0.0F;
    get_log("new front end: fonts - display %s (condensed %.0f%%%s), mono %s, body %s",
            files.display.empty() ? "(none)" : files.display.c_str(), files.condense * 100.0F,
            files.bold > 0 ? ", heavier" : "", files.mono.empty() ? "(none)" : files.mono.c_str(),
            files.body.empty() ? "(none)" : files.body.c_str());
    return files;
}

std::string read_text(const std::string& path)
{
    const fs::OwnedStream file = fs::open_stream(path.c_str(), "rb", fs::StreamSharing::kSecure);
    if (!file) {
        return {};
    }
    std::string text;
    std::array<char, kReadChunkSize> buffer{};
    for (size_t bytes_read = 0; (bytes_read = fs::disk().stream_read(file.get(), buffer.data(), 1, sizeof(buffer))) > 0;) {
        text.append(buffer.data(), bytes_read);
    }
    return text;
}

// The text written next to `path` and moved over it in one step (a crash part-way leaves the old file).
bool write_atomically(const std::string& path, std::string_view text)
{
    const std::string temporary = path + ".new";
    fs::OwnedStream file = fs::open_stream(temporary.c_str(), "wb", fs::StreamSharing::kSecure);
    if (!file) {
        return false;
    }
    bool ok = fs::disk().stream_write(file.get(), text.data(), 1, text.size()) == text.size();
    ok = fs::disk().stream_flush(file.get()) == 0 && ok;
    file.reset(); // (closed before it's moved)
    if (!ok || !fs::disk().replace(temporary.c_str(), path.c_str(), true)) {
        fs::disk().remove(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace internal
namespace {

std::string state_path() { return game_folder_path("thug2-decomp-frontend.txt"); }
// The level select's chosen picture per level (Q / E there): `<load script> <number>` lines.
std::string art_choices_path() { return game_folder_path("thug2-decomp-art.txt"); }

} // namespace
namespace internal {

paint::Highscores records_for(const StageMap& level)
{
    paint::Highscores records;
    const int number = level.level_index_ >= 0 ? level.level_index_ : script_integer(level.level_num_name_);
    objects::RiderStoryProgress* career = get_career();
    // (the career's records are THUG2's own levels': 21 of them)
    if (!career || number < 0 || number >= objects::RiderStoryProgress::kLevels || !level.game_tag_.empty() ||
        level.level_num_name_.empty()) {
        return records;
    }
    const int goals = career->count_goals(number);
    if (goals > 0) {
        records.goals = goals;
    }
    if (goals::GapTally* gaps = career->fetch_gap_list(number)) {
        const int total = gaps->total_of();
        if (total > 0) {
            int got = 0;
            for (int i = 0; i < total; ++i) {
                got += gaps->count_gaps_found(i) > 0 ? 1 : 0;
            }
            records.gaps_total_ = total;
            records.gaps_got_ = got;
        }
    }
    return records;
}

} // namespace internal
namespace {

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
    if ((!game_rules && !respect_locks()) || script_integer("all_levels_unlocked") == 1) {
        return true;
    }
    const int flag = script_integer(level.seen_flag_.empty() ? level.flag : level.seen_flag_);
    objects::RiderStoryProgress* career = get_career();
    return flag < 0 || (career == nullptr) || career->get_global_flag(flag);
}

} // namespace
namespace internal {

std::vector<SectionView> get_views(bool game_rules)
{
    std::vector<SectionView> views;
    for (const Section& section : new_frontend_state().catalogue_.sections()) {
        SectionView view;
        for (size_t i = 0; i < section.levels.size(); ++i) {
            const StageMap& level = core::at(section.levels, i);
            const bool unlocked = get_unlocked(level, game_rules);
            // (the game's select drops hide_when_locked outside a career when it uses the seen flags: listed, locked)
            if (!unlocked && level.hide_when_locked_ && level.seen_flag_.empty()) {
                continue;
            }
            view.levels.push_back(static_cast<int>(i));
            view.locked.push_back(!unlocked);
        }
        views.push_back(std::move(view));
    }
    return views;
}

void load_catalogue()
{
    if (new_frontend_state().catalogue_tried_) {
        return;
    }
    new_frontend_state().catalogue_tried_ = true;
    const std::string path = data_overlay::get_folder() + "\\frontend\\levels.json";
    const std::string text = read_text(path);
    std::string error;
    if (text.empty()) {
        get_log("new front end: no %s (make_menu_level.ts makes it) - Free Skate goes to the original level select",
                path.c_str());
    } else if (!parse_catalogue(text, &new_frontend_state().catalogue_, &error)) {
        get_log("new front end: %s - %s; Free Skate goes to the original level select", path.c_str(), error.c_str());
        new_frontend_state().catalogue_ = Catalogue{};
    } else {
        new_frontend_state().catalogue_ok_ = true;
        size_t levels = 0;
        for (const Section& section : new_frontend_state().catalogue_.sections()) {
            levels += section.levels.size();
        }
        get_log("new front end: %zu sections, %zu levels from %s", new_frontend_state().catalogue_.sections().size(), levels, path.c_str());
    }
    new_frontend_state().saved_ = parse_state(read_text(state_path()));
    new_frontend_state().catalogue_.apply_art_choices(parse_art_choices(read_text(art_choices_path())));
}

// Q / E on the level select: the highlighted level's next / previous picture, kept for the next start.
bool cycle_level_art(int step)
{
    const StageMap* level = menu_state().selected_level();
    if (!level || !new_frontend_state().catalogue_.cycle_art(level->load, step)) {
        return false;
    }
    write_atomically(art_choices_path(), format_art_choices(new_frontend_state().catalogue_.art_choices()));
    return true;
}

void save_state() { write_atomically(state_path(), format_state(new_frontend_state().saved_)); }

// The last level played: whichever menu started it, once it's the level loaded.
void note_level_loaded()
{
    const uint32_t load = current_load_state();
    if (load == new_frontend_state().last_load_) {
        return;
    }
    new_frontend_state().last_load_ = load;
    if ((load == 0U) || load == kLoadMainMenu || !new_frontend_state().catalogue_ok_) {
        return;
    }
    for (const Section& section : new_frontend_state().catalogue_.sections()) {
        for (const StageMap& level : section.levels) {
            if (name_hash(level.load.c_str()) == load) {
                if (new_frontend_state().saved_.last_level_ != level.load) {
                    new_frontend_state().saved_.last_level_ = level.load;
                    save_state();
                }
                return;
            }
        }
    }
}

std::string get_skater_name()
{
    const uint32_t script = name_hash("select_skater_get_current_skater_name");
    if (!is_script(script)) {
        return {};
    }
    script::Struct results;
    script::run_script_once(script, nullptr, nullptr, &results);
    const char* name = nullptr;
    std::string out;
    // (the profile's info - GetSkaterProfileInfo's display_name - is among the script's parameters after it)
    if ((results.fetch_text("display_name", &name, 0) || results.fetch_text("current_skater", &name, 0)) && name) {
        out = name;
    }
    results.get_clear();
    return out;
}

void get_sound(const char* script, const char* flag)
{
    const uint32_t name = name_hash(script);
    if (!is_script(name)) {
        return;
    }
    script::Struct params;
    if (flag) {
        params.put_name_key(checksums::Name{0U}, name_hash(flag));
    }
    script::launch_script(name, &params, nullptr, false, script);
    params.get_clear();
}

// The level being played, as the data names it (null if it isn't there).
const StageMap* level_played(const Section** section)
{
    const uint32_t load = current_load_state();
    for (const Section& catalogue_section : new_frontend_state().catalogue_.sections()) {
        for (const StageMap& level : catalogue_section.levels) {
            if (name_hash(level.load.c_str()) == load) {
                if (section) {
                    *section = &catalogue_section;
                }
                return &level;
            }
        }
    }
    return nullptr;
}

} // namespace internal
} // namespace frontend::new_frontend
