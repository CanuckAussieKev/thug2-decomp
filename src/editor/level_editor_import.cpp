// NOT ORIGINAL - the editor mode's Import Level window (level_editor_import.h).

#include "editor/level_editor_import.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_shell.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "engine/file/disk.h"
#include "engine/gfx/screen.h"
#include "engine/script/load.h"
#include "imgui.h"
#include "platform/child_process.h"
#include "platform/folder_dialog.h"
#include "platform/run_log.h"

namespace frontend::level_editor::import {

namespace {

constexpr std::size_t kMostLines = 4000;          // the output kept in the window
constexpr std::size_t kMostStepOutput = std::size_t{64} * 1024; // a step's own output kept (for its error line)
constexpr std::size_t kSettingSize = 1024;
constexpr float kWindowPart = 0.7F;
constexpr float kHalf = 0.5F;
constexpr float kOutputLines = 9.0F;
constexpr float kButtonWidth = 120.0F;
constexpr float kWideButtonWidth = 200.0F;
constexpr float kPlaceWeight = 1.6F;
constexpr float kLeastTableLines = 4.0F;
constexpr float kStatusWidth = 130.0F; // the table's fixed columns, at scale 1
constexpr float kRetailWidth = 80.0F;
constexpr float kOpenWidth = 56.0F;
constexpr float kRetailRowGaps = 2.0F; // (the gaps in the Retail install row, around its Browse button)
constexpr int kTableColumns = 6;
// A listing's lines' fields: "level <id> <title> <place> <flags> <load> <retail>", "game <key> <title> <folder>".
constexpr std::size_t kId = 1;
constexpr std::size_t kTitle = 2;
constexpr std::size_t kPlace = 3;
constexpr std::size_t kFlags = 4;
constexpr std::size_t kLoad = 5;
constexpr std::size_t kRetail = 6;
constexpr std::size_t kLevelFields = 7;
constexpr std::size_t kGameFields = 4;
constexpr const char* kIniSection = "editor";
constexpr const char* kTool = R"(editor_import\import_levels.ts)";
// The overlay's level list scripts (make_menu_level.ts writes both), read again after an import.
constexpr auto kListScripts =
    std::to_array<const char*>({R"(scripts\game\levels.qb)", R"(scripts\game\menu\gamemenu_levelselect.qb)"});
// The games make_menu_level reads loading pictures from (import_levels.ts LOADING_ART_GAMES): their folders, when
// remembered, go with the overlay step.
constexpr auto kLoadingArtGames = std::to_array<const char*>({"thps2", "thps3", "thps4", "thug1", "thaw", "thp8"});

enum class Phase : uint8_t { kIdle, kListing, kConverting, kBuilding, kExporting, kFailed, kDone, kChecking };
enum class Waiting : uint8_t { kNone, kList, kImport };

struct Level {
    std::string id;
    std::string title;
    std::string place;
    std::string load;   // (imported: its load script)
    std::string retail; // (the checker's verdict as shown: "OK", "Too big: <limit>", or empty: not checked)
    bool by_default = false;
    bool imported = false;
    bool missing = false;
    bool checked = false;
};

// (the larger members first)
struct ImportState {
    std::vector<Level> levels;
    std::vector<std::string> lines; // (the output shown)
    std::vector<std::string> converting; // (the levels of the import under way)
    std::string folder;       // the folder field
    std::string listed;       // the folder holding the game's files (the listing's)
    std::string game;         // its key (thps3) and title
    std::string game_title;
    std::string partial;      // (a line not ended yet)
    std::string step_output;  // (the running step's, for its error line)
    std::string message;      // the last outcome
    std::string done_message; // (said once the list after an import is in)
    std::string dialog_why;
    std::string retail_folder; // the retail THUG2 folder (thug2-decomp.ini [editor] retail_folder)
    std::string export_verb;   // (the retail step under way: install or uninstall)
    int child = 0;
    Phase phase = Phase::kIdle;
    Waiting waiting = Waiting::kNone;
    bool open = false;
    bool appearing = false;
    bool relisting = false; // (the list again after an import: the output kept)
    bool cancelled = false;
    bool reload_pending = false;
    bool scroll = false;
    bool failed = false; // (the last outcome was a failure)
    bool settings_read = false;
    bool raised_limits = false; // "Limits DLL installed" ([editor] retail_raised_limits)
    bool check_wanted = false;  // the budget checker is to run (once nothing else does)
    bool dialog_retail = false; // the folder dialog open is the retail folder's
};

ImportState& import_state()
{
    static core::NoDestroy<ImportState> state;
    return state.get();
}

std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
    return text;
}

// --- settings (thug2-decomp.ini [editor], UTF-8 paths) ---------------------------------------------------------------

std::wstring widened(std::string_view text)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string narrowed(std::wstring_view text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<std::size_t>(std::max(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), length, nullptr, nullptr);
    return narrow;
}

std::wstring ini_file() { return widened(game_folder_path("thug2-decomp.ini")); }

std::string setting(const std::string& key)
{
    std::array<wchar_t, kSettingSize> value{};
    const DWORD length = GetPrivateProfileStringW(widened(kIniSection).c_str(), widened(key).c_str(), L"", value.data(),
                                                  static_cast<DWORD>(value.size()), ini_file().c_str());
    return narrowed(std::wstring_view(value.data(), length));
}

void set_setting(const std::string& key, const std::string& value)
{
    WritePrivateProfileStringW(widened(kIniSection).c_str(), widened(key).c_str(), widened(value).c_str(), ini_file().c_str());
}

std::string without_last_slash(std::string folder)
{
    while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/')) {
        folder.pop_back();
    }
    return folder;
}

std::string in_tools(const char* name) { return tools_folder() + "\\" + name; }

bool tools_installed()
{
    return fs::disk().kind(in_tools("node.exe").c_str()) == fs::PathKind::kFile &&
           fs::disk().kind(in_tools("pipeline\\editor_import\\import_levels.ts").c_str()) == fs::PathKind::kFile;
}

bool export_installed()
{
    std::string tool = kRetailExportTool;
    std::ranges::replace(tool, '/', '\\');
    return fs::disk().kind((in_tools("pipeline\\") + tool).c_str()) == fs::PathKind::kFile;
}

// --- the steps ---------------------------------------------------------------------------------------------------

bool busy_phase(Phase phase)
{
    return phase == Phase::kListing || phase == Phase::kConverting || phase == Phase::kBuilding ||
           phase == Phase::kExporting || phase == Phase::kChecking;
}

void add_line(std::string line)
{
    ImportState& state = import_state();
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    state.lines.push_back(std::move(line));
    if (state.lines.size() > kMostLines) {
        state.lines.erase(state.lines.begin(), state.lines.begin() + static_cast<std::ptrdiff_t>(state.lines.size() - kMostLines));
    }
    state.scroll = true;
}

void add_output(const std::string& text)
{
    ImportState& state = import_state();
    if (text.empty()) {
        return;
    }
    state.step_output += text;
    if (state.step_output.size() > kMostStepOutput) {
        state.step_output.erase(0, state.step_output.size() - kMostStepOutput);
    }
    if (state.phase == Phase::kListing || state.phase == Phase::kChecking) {
        return; // (the list and the verdicts are the window's table, not its output)
    }
    state.partial += text;
    std::size_t end = 0;
    while ((end = state.partial.find('\n')) != std::string::npos) {
        add_line(state.partial.substr(0, end));
        state.partial.erase(0, end + 1);
    }
}

// The lines of a step's output.
std::vector<std::string> lines_of(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        at = end + 1;
    }
    return lines;
}

// Why a step failed: its last line naming an error, else its last line.
std::string error_line(const std::string& output)
{
    const std::vector<std::string> lines = lines_of(output);
    std::string last;
    for (const std::string& line : lines) {
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        last = line;
    }
    for (const std::string& line : std::views::reverse(lines)) {
        if (lowered(line).find("error") != std::string::npos) {
            return line;
        }
    }
    return last.empty() ? std::string("no output") : last;
}

bool start_step(Phase phase, std::vector<std::string> arguments)
{
    ImportState& state = import_state();
    if (!tools_installed()) {
        state.phase = Phase::kFailed;
        state.failed = true;
        state.message = "The import tools aren't installed (" + tools_folder() + ")";
        return false;
    }
    sys::child_process::Command command;
    command.program = in_tools("node.exe");
    arguments.insert(arguments.begin(), in_tools("pipeline\\") + kTool);
    command.arguments = std::move(arguments);
    command.folder = tools_folder();
    command.environment = {{"NEVERSCRIPT", in_tools("ns.exe")}, {"NO_COLOR", "1"}, {"NODE_NO_WARNINGS", "1"}};
    std::string why;
    state.step_output.clear();
    state.partial.clear();
    state.cancelled = false;
    state.child = sys::child_process::start(command, &why);
    if (state.child == 0) {
        state.phase = Phase::kFailed;
        state.failed = true;
        state.message = "Couldn't start the import tools - " + why;
        get_log("level import: %s", state.message.c_str());
        return false;
    }
    state.phase = phase;
    return true;
}

void start_listing(bool again)
{
    ImportState& state = import_state();
    state.relisting = again;
    if (!again) {
        state.lines.clear();
        state.message.clear();
        state.failed = false;
        state.levels.clear();
        state.game.clear();
        state.game_title.clear();
    }
    state.folder = without_last_slash(state.folder);
    if (state.folder.empty()) {
        return;
    }
    get_log("level import: listing %s", state.folder.c_str());
    start_step(Phase::kListing, {"list", state.folder, "--thug2", output_folder()});
}

void start_import()
{
    ImportState& state = import_state();
    state.converting.clear();
    for (const Level& level : state.levels) {
        if (level.checked) {
            state.converting.push_back(level.id);
        }
    }
    if (state.converting.empty() || state.game.empty()) {
        return;
    }
    state.message.clear();
    state.failed = false;
    std::vector<std::string> arguments = {"convert", state.listed};
    arguments.insert(arguments.end(), state.converting.begin(), state.converting.end());
    arguments.insert(arguments.end(), {"--game", state.game, "--thug2", output_folder()});
    add_line("> Converting " + std::to_string(state.converting.size()) + " level(s) from " + state.game_title);
    get_log("level import: converting %zu level(s) of %s from %s into %s", state.converting.size(), state.game.c_str(),
            state.listed.c_str(), output_folder().c_str());
    start_step(Phase::kConverting, std::move(arguments));
}

void start_overlay()
{
    std::vector<std::string> arguments = {"overlay", "--thug2", output_folder()};
    for (const char* game : kLoadingArtGames) {
        const std::string folder = setting(std::string("import_") + game);
        if (!folder.empty()) {
            arguments.insert(arguments.end(), {std::string("--") + game, folder});
        }
    }
    add_line("> Building the level list");
    start_step(Phase::kBuilding, std::move(arguments));
}

// A level's key for the retail tool's --levels: its load script without the "Load_".
std::string retail_key(const Level& level)
{
    constexpr std::string_view kPrefix = "Load_";
    std::string key = level.load;
    if (key.starts_with(kPrefix)) {
        key.erase(0, kPrefix.size());
    }
    return key;
}

// The folder given as --retail: the retail install chosen, else the output folder (a game folder too - enough for the
// checker, which only reads the converted levels).
std::string retail_target()
{
    const ImportState& state = import_state();
    return state.retail_folder.empty() ? output_folder() : state.retail_folder;
}

// Starts the pipeline's retail tool (kRetailExportTool: install | uninstall | check) as a step of its own.
bool start_retail(Phase phase, const char* verb, const std::vector<std::string>& levels)
{
    ImportState& state = import_state();
    sys::child_process::Command command;
    command.program = in_tools("node.exe");
    command.arguments = {in_tools("pipeline\\") + kRetailExportTool, verb, "--retail", retail_target(), "--from", output_folder()};
    if (!levels.empty()) {
        std::string joined;
        for (const std::string& key : levels) {
            joined += (joined.empty() ? "" : ",") + key;
        }
        command.arguments.insert(command.arguments.end(), {"--levels", joined});
    }
    if (state.raised_limits) {
        command.arguments.emplace_back("--raised-limits");
    }
    if (phase == Phase::kExporting && state.export_verb == "install") {
        for (const char* game : kLoadingArtGames) {
            const std::string folder = setting(std::string("import_") + game);
            if (!folder.empty()) {
                command.arguments.insert(command.arguments.end(), {std::string("--") + game, folder});
            }
        }
    }
    command.folder = tools_folder();
    command.environment = {{"NEVERSCRIPT", in_tools("ns.exe")}, {"NO_COLOR", "1"}, {"NODE_NO_WARNINGS", "1"}};
    std::string why;
    state.step_output.clear();
    state.partial.clear();
    state.cancelled = false;
    state.child = sys::child_process::start(command, &why);
    if (state.child == 0) {
        state.phase = Phase::kFailed;
        state.failed = true;
        state.message = "Couldn't start the retail tool - " + why;
        get_log("level import: %s", state.message.c_str());
        return false;
    }
    state.phase = phase;
    return true;
}

void start_export(bool install)
{
    ImportState& state = import_state();
    std::vector<std::string> keys;
    if (install) {
        for (const Level& level : state.levels) {
            if (level.checked && level.imported) {
                keys.push_back(retail_key(level));
            }
        }
        if (keys.empty()) {
            return;
        }
    }
    if (!export_installed() || state.retail_folder.empty()) {
        return;
    }
    state.message.clear();
    state.failed = false;
    state.export_verb = install ? "install" : "uninstall";
    add_line(install ? "> Installing " + std::to_string(keys.size()) + " level(s) into " + state.retail_folder
                     : "> Taking the exported levels out of " + state.retail_folder);
    get_log("level import: retail %s into %s", state.export_verb.c_str(), state.retail_folder.c_str());
    start_retail(Phase::kExporting, state.export_verb.c_str(), keys);
}

// The budget checker over the imported levels (the Retail column): one line each, "<level>: retail OK (...)" or
// "<level>: too big for retail: <limit> 104% (...), ...".
void start_check()
{
    ImportState& state = import_state();
    state.check_wanted = false;
    std::vector<std::string> keys;
    for (Level& level : state.levels) {
        if (level.imported && !level.load.empty()) {
            keys.push_back(retail_key(level));
        } else {
            level.retail.clear();
        }
    }
    if (keys.empty() || !export_installed() || !tools_installed()) {
        return;
    }
    if (!start_retail(Phase::kChecking, "check", keys)) {
        state.phase = Phase::kIdle; // (the column stays empty; no message for it)
        state.failed = false;
        state.message.clear();
    }
}

void read_check(const std::string& output)
{
    ImportState& state = import_state();
    constexpr std::string_view kOk = "retail OK";
    constexpr std::string_view kBig = "too big for retail: ";
    for (const std::string& line : lines_of(output)) {
        const std::size_t colon = line.find(": ");
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = lowered(line.substr(0, colon));
        const std::string_view verdict = std::string_view(line).substr(colon + 2);
        std::string shown;
        if (verdict.starts_with(kOk)) {
            shown = "OK";
        } else if (verdict.starts_with(kBig)) {
            const std::string_view limits = verdict.substr(kBig.size());
            shown = "Too big: " + std::string(limits.substr(0, limits.find(' ')));
        } else {
            continue;
        }
        for (Level& level : state.levels) {
            if (level.imported && (lowered(retail_key(level)) == key || lowered(level.id) == key)) {
                level.retail = shown;
            }
        }
    }
}

// The level list scripts read again (the game's own loader: the overlay's files are read first) - not while a level
// is up (its structures in use): then before the next one opens.
void reload_lists()
{
    ImportState& state = import_state();
    if (render::current_level() != 0) {
        state.reload_pending = true;
        get_log("level import: the level list is read again once this level is left");
        return;
    }
    state.reload_pending = false;
    const std::string overlay = output_folder() + "\\thug2-decomp-data\\";
    for (const char* script : kListScripts) {
        if (fs::disk().kind((overlay + script).c_str()) != fs::PathKind::kFile) {
            get_log("level import: %s%s isn't there - the level list not read again", overlay.c_str(), script);
            return;
        }
    }
    for (const char* script : kListScripts) {
        script::load_qb(script, 0, 0);
    }
    get_log("level import: the level list read again");
    shell::levels_changed();
}

// The listing's lines: "game <key> <title> <folder>", "level <id> <title> <place> <flags> <load> <retail>".
void read_listing(const std::string& output)
{
    ImportState& state = import_state();
    std::vector<Level> levels;
    for (const std::string& line : lines_of(output)) {
        std::vector<std::string> fields;
        std::size_t at = 0;
        while (at <= line.size()) {
            const std::size_t end = std::min(line.find('\t', at), line.size());
            std::string field = line.substr(at, end - at);
            fields.push_back(field == "-" ? std::string() : std::move(field));
            at = end + 1;
        }
        if (fields.size() == kGameFields && fields.front() == "game") {
            state.game = core::at(fields, kId);
            state.game_title = core::at(fields, kTitle);
            state.listed = core::at(fields, kPlace);
        } else if (fields.size() == kLevelFields && fields.front() == "level") {
            Level level;
            level.id = core::at(fields, kId);
            level.title = core::at(fields, kTitle);
            level.place = core::at(fields, kPlace);
            const std::string flags = "," + core::at(fields, kFlags) + ",";
            level.by_default = flags.find(",default,") != std::string::npos;
            level.imported = flags.find(",imported,") != std::string::npos;
            level.missing = flags.find(",missing,") != std::string::npos;
            level.load = core::at(fields, kLoad);
            level.retail = core::at(fields, kRetail);
            // (checked as before when listed again, and the verdict kept until the checker says it again; otherwise
            // nothing is)
            for (const Level& known : state.levels) {
                if (known.id == level.id) {
                    level.checked = known.checked && !level.imported;
                    if (level.retail.empty() && level.imported) {
                        level.retail = known.retail;
                    }
                }
            }
            levels.push_back(std::move(level));
        }
    }
    state.levels = std::move(levels);
}

void step_finished(int exit_code)
{
    ImportState& state = import_state();
    sys::child_process::release(state.child);
    state.child = 0;
    if (!state.partial.empty()) {
        add_line(std::exchange(state.partial, {}));
    }
    const Phase phase = state.phase;
    if (phase == Phase::kChecking) {
        if (exit_code == 0) {
            read_check(state.step_output);
        } else {
            get_log("level import: the retail check failed (exit code %d): %s", exit_code, error_line(state.step_output).c_str());
        }
        state.phase = Phase::kIdle;
        return;
    }
    if (exit_code != 0) {
        state.phase = Phase::kFailed;
        state.failed = true;
        state.message = state.cancelled ? std::string("Cancelled") : error_line(state.step_output);
        if (phase == Phase::kListing && !state.relisting) {
            state.levels.clear();
        }
        get_log("level import: %s (exit code %d)", state.message.c_str(), exit_code);
        return;
    }
    switch (phase) {
    case Phase::kListing:
        read_listing(state.step_output);
        if (!state.game.empty()) {
            set_setting("import_" + state.game, state.folder);
            set_setting("import_last", state.folder);
        }
        state.phase = state.relisting ? Phase::kDone : Phase::kIdle;
        state.message = state.relisting ? state.done_message : std::string();
        state.relisting = false;
        state.check_wanted = true;
        break;
    case Phase::kConverting:
        start_overlay();
        break;
    case Phase::kBuilding:
        state.done_message = "Imported " + std::to_string(state.converting.size()) + " level(s)";
        get_log("level import: %s", state.done_message.c_str());
        reload_lists();
        start_listing(true);
        break;
    case Phase::kExporting:
        state.phase = Phase::kDone;
        state.message = state.export_verb == "install" ? "Installed into the retail install"
                                                       : "Took the exported levels out of the retail install";
        break;
    case Phase::kChecking:
    case Phase::kIdle:
    case Phase::kFailed:
    case Phase::kDone:
        break;
    }
}

void tick()
{
    ImportState& state = import_state();
    sys::folder_dialog::Answer answer;
    if (sys::folder_dialog::take_answer(&answer)) {
        state.dialog_why = answer.why;
        const bool for_retail = std::exchange(state.dialog_retail, false);
        if (!answer.folder.empty() && !busy_phase(state.phase)) {
            if (for_retail) {
                state.retail_folder = answer.folder;
                set_setting("retail_folder", state.retail_folder);
                state.check_wanted = true;
            } else {
                state.folder = answer.folder;
                start_listing(false);
            }
        }
    }
    if (state.child == 0) {
        if (state.check_wanted && !busy_phase(state.phase) && !state.levels.empty()) {
            start_check();
        }
        return;
    }
    add_output(sys::child_process::take_output(state.child));
    int exit_code = 0;
    if (sys::child_process::finished(state.child, &exit_code)) {
        add_output(sys::child_process::take_output(state.child));
        step_finished(exit_code);
    }
}

// --- the window -------------------------------------------------------------------------------------------------------

std::string step_text()
{
    const ImportState& state = import_state();
    const std::size_t count = state.converting.size();
    switch (state.phase) {
    case Phase::kListing:
        return state.relisting ? "Updating the level list" : "Reading " + state.folder;
    case Phase::kConverting:
        return "Converting " + std::to_string(count) + (count == 1 ? " level" : " levels") + " (step 1 of 2)";
    case Phase::kBuilding:
        return "Building the level list (step 2 of 2)";
    case Phase::kExporting:
        return state.export_verb == "install" ? "Installing into the retail install" : "Taking the levels out of the retail install";
    case Phase::kChecking:
        return "Checking the levels against the retail limits";
    case Phase::kIdle:
    case Phase::kFailed:
    case Phase::kDone:
        break;
    }
    return {};
}

void folder_row(float scale, bool busy_now)
{
    ImportState& state = import_state();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Game folder");
    ImGui::SameLine();
    std::string buffer = state.folder;
    buffer.resize(std::max<std::size_t>(buffer.size() + 1, kSettingSize));
    const float browse_width = kButtonWidth * scale;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_width - ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginDisabled(busy_now);
    if (ImGui::InputTextWithHint("##import_folder", "The other game's install folder", buffer.data(), buffer.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        state.folder = buffer.substr(0, buffer.find('\0'));
        start_listing(false);
    } else if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.folder = buffer.substr(0, buffer.find('\0'));
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(browse_width, 0.0F)) && !sys::folder_dialog::is_open()) {
        sys::folder_dialog::open(state.folder);
    }
    ImGui::EndDisabled();
    if (!state.game_title.empty()) {
        gui::text_disabled("%s - %zu levels", state.game_title.c_str(), state.levels.size());
    } else if (state.phase == Phase::kIdle && state.folder.empty()) {
        gui::text_disabled("%s", "THPS2, THPS3, THPS4, THUG, THAW, Project 8 or Proving Ground");
    } else {
        ImGui::NewLine();
    }
    if (!state.dialog_why.empty()) {
        gui::text_colored(ui_theme::kWarningColour, "%s", state.dialog_why.c_str());
    }
}

void retail_row(float scale, bool busy_now)
{
    ImportState& state = import_state();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Retail install");
    ImGui::SameLine();
    std::string buffer = state.retail_folder;
    buffer.resize(std::max<std::size_t>(buffer.size() + 1, kSettingSize));
    const float browse_width = kButtonWidth * scale;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float tick_width = ImGui::GetFrameHeight() + ImGui::CalcTextSize("Limits DLL installed").x + (style.ItemInnerSpacing.x * 2.0F);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_width - tick_width - (style.ItemSpacing.x * kRetailRowGaps));
    ImGui::BeginDisabled(busy_now);
    const bool entered = ImGui::InputTextWithHint("##retail_folder", "A THUG2 folder to install the levels into", buffer.data(),
                                                  buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
        const std::string chosen = without_last_slash(buffer.substr(0, buffer.find('\0')));
        if (chosen != state.retail_folder) {
            state.retail_folder = chosen;
            set_setting("retail_folder", chosen);
            state.check_wanted = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##retail", ImVec2(browse_width, 0.0F)) && !sys::folder_dialog::is_open()) {
        state.dialog_retail = true;
        sys::folder_dialog::open(state.retail_folder);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Limits DLL installed", &state.raised_limits)) {
        set_setting("retail_raised_limits", state.raised_limits ? "1" : "0");
        state.check_wanted = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", "The install has the raised-limits dinput8.dll beside THUG2.exe: levels are checked against its larger limits");
    }
}

const char* retail_text(const Level& level) { return level.retail.c_str(); }

void level_table(float height, bool busy_now, float scale)
{
    ImportState& state = import_state();
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                                  ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("##import_levels", kTableColumns, flags, ImVec2(0.0F, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##check", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("Place", ImGuiTableColumnFlags_WidthStretch, kPlaceWeight);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, kStatusWidth * scale);
    ImGui::TableSetupColumn("Retail", ImGuiTableColumnFlags_WidthFixed, kRetailWidth * scale);
    ImGui::TableSetupColumn("##open", ImGuiTableColumnFlags_WidthFixed, kOpenWidth * scale);
    ImGui::TableHeadersRow();
    ImGui::BeginDisabled(busy_now);
    for (std::size_t i = 0; i < state.levels.size(); ++i) {
        Level& level = core::at(state.levels, i);
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(level.missing);
        ImGui::Checkbox("##checked", &level.checked);
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(level.title.c_str());
        if (ImGui::IsItemHovered()) {
            gui::tooltip("%s", level.id.c_str());
        }
        ImGui::TableNextColumn();
        gui::text_disabled("%s", level.place.c_str());
        ImGui::TableNextColumn();
        if (level.missing) {
            gui::text_colored(ui_theme::kWarningColour, "%s", "Not in this install");
        } else if (level.imported) {
            ImGui::TextUnformatted("Imported");
        } else {
            gui::text_disabled("%s", "Not imported");
        }
        ImGui::TableNextColumn();
        if (level.retail.starts_with("Too big")) {
            gui::text_colored(ui_theme::kWarningColour, "%s", retail_text(level));
        } else {
            ImGui::TextUnformatted(retail_text(level));
        }
        ImGui::TableNextColumn();
        if (level.imported && !level.load.empty() && ImGui::SmallButton("Open")) {
            shell::open_imported(level.load);
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
}

void output_panel(const ux3::Fonts& fonts, float height)
{
    ImportState& state = import_state();
    if (!ImGui::BeginChild("##import_output", ImVec2(0.0F, height), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    if (fonts.mono != nullptr) {
        ImGui::PushFont(fonts.mono);
    }
    const bool at_end = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0F;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(state.lines.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            ImGui::TextUnformatted(core::at(state.lines, static_cast<std::size_t>(i)).c_str());
        }
    }
    if (state.scroll && at_end) {
        ImGui::SetScrollHereY(1.0F);
    }
    state.scroll = false;
    if (fonts.mono != nullptr) {
        ImGui::PopFont();
    }
    ImGui::EndChild();
}

void button_row(float scale, bool busy_now)
{
    ImportState& state = import_state();
    const ImVec2 button(kButtonWidth * scale, 0.0F);
    const auto checked = std::ranges::count_if(state.levels, [](const Level& level) { return level.checked; });
    const bool any_imported_checked = std::ranges::any_of(state.levels, [](const Level& level) { return level.checked && level.imported; });
    ImGui::BeginDisabled(busy_now || checked == 0);
    if (ImGui::Button("Import", button)) {
        start_import();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy_now);
    if (ImGui::Button("Cancel", button) && state.child != 0) {
        state.cancelled = true;
        sys::child_process::stop(state.child);
        get_log("level import: cancelled");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(busy_now || state.levels.empty());
    if (ImGui::Button("Select All")) {
        for (Level& level : state.levels) {
            level.checked = !level.missing;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Select None")) {
        for (Level& level : state.levels) {
            level.checked = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    gui::text_disabled("%d selected", static_cast<int>(checked));

    // (Export, Uninstall and Close on the right)
    const float right = ((kWideButtonWidth + (kButtonWidth * 2.0F)) * scale) + (ImGui::GetStyle().ItemSpacing.x * 2.0F);
    ImGui::SameLine();
    const float room = ImGui::GetContentRegionAvail().x;
    if (room > right) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - right);
    }
    const bool can_export = export_installed();
    const bool has_folder = !state.retail_folder.empty();
    ImGui::BeginDisabled(busy_now || !can_export || !has_folder || !any_imported_checked);
    if (ImGui::Button("Export to Retail Install...", ImVec2(kWideButtonWidth * scale, 0.0F))) {
        start_export(true);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (!can_export) {
            gui::tooltip("%s", "The retail export isn't in this version's tools");
        } else if (!has_folder) {
            gui::tooltip("%s", "Choose the retail THUG2 folder first");
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(busy_now || !can_export || !has_folder);
    if (ImGui::Button("Uninstall", button)) {
        start_export(false);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        gui::tooltip("%s", "Takes everything the export installed out of the retail install again");
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", button)) {
        state.open = false;
    }
}

} // namespace

std::string tools_folder()
{
    const std::string folder = setting("tools_folder");
    return without_last_slash(folder.empty() ? game_folder_path("editor-tools") : folder);
}

std::string output_folder()
{
    const std::string folder = setting("import_output");
    return without_last_slash(folder.empty() ? game_folder_path("") : folder);
}

void open_window(const std::string& folder)
{
    ImportState& state = import_state();
    if (!state.settings_read) {
        state.settings_read = true;
        state.retail_folder = setting("retail_folder");
        state.raised_limits = setting("retail_raised_limits") == "1";
    }
    if (!state.open) {
        state.appearing = true;
    }
    state.open = true;
    if (busy_phase(state.phase)) {
        return;
    }
    std::string start = folder;
    if (start.empty()) {
        start = state.folder.empty() ? setting("import_last") : state.folder;
    }
    if (start != state.folder || state.levels.empty()) {
        state.folder = start;
        start_listing(false);
    }
}

bool busy() { return busy_phase(import_state().phase); }

void window(const ux3::Fonts& fonts)
{
    ImportState& state = import_state();
    tick();
    if (!state.open) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x * kHalf),
                                   viewport->WorkPos.y + (viewport->WorkSize.y * kHalf)),
                            ImGuiCond_Appearing, ImVec2(kHalf, kHalf));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * kWindowPart, viewport->WorkSize.y * kWindowPart),
                             ImGuiCond_Appearing);
    if (state.appearing) {
        ImGui::SetNextWindowFocus();
        state.appearing = false;
    }
    if (!ImGui::Begin("Import Level", &state.open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const bool busy_now = busy_phase(state.phase);
    const float scale = fonts.scale;
    if (!tools_installed()) {
        gui::text_colored(ui_theme::kWarningColour, "The import tools aren't installed: %s", tools_folder().c_str());
    }
    folder_row(scale, busy_now);
    retail_row(scale, busy_now);
    ImGui::Spacing();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float output_height = (line * kOutputLines) + (style.WindowPadding.y * 2.0F);
    const float below = output_height + (ImGui::GetFrameHeightWithSpacing() * 4.0F) + (line * 2.0F);
    level_table(std::max(line * kLeastTableLines, ImGui::GetContentRegionAvail().y - below), busy_now, scale);

    ImGui::Spacing();
    if (busy_now) {
        const std::string text = step_text();
        ImGui::ProgressBar(-static_cast<float>(ImGui::GetTime()), ImVec2(-1.0F, 0.0F), text.c_str());
    } else if (!state.message.empty()) {
        if (state.failed) {
            gui::text_colored(ui_theme::kWarningColour, "%s", state.message.c_str());
        } else {
            gui::text("%s", state.message.c_str());
        }
    } else {
        gui::text_disabled("Levels are imported into %s", output_folder().c_str());
    }
    output_panel(fonts, output_height);
    ImGui::Spacing();
    button_row(scale, busy_now);
    ImGui::End();
}

bool reload_if_pending()
{
    if (!import_state().reload_pending || render::current_level() != 0) {
        return false;
    }
    reload_lists();
    return true;
}

void automation_list(const std::string& folder)
{
    ImportState& state = import_state();
    state.waiting = Waiting::kList;
    state.open = true;
    state.folder = folder;
    if (!busy_phase(state.phase)) {
        start_listing(false);
    }
}

void automation_import(const std::vector<std::string>& levels)
{
    ImportState& state = import_state();
    state.waiting = Waiting::kImport;
    for (Level& level : state.levels) {
        level.checked = std::ranges::any_of(levels, [&level](const std::string& id) { return lowered(id) == lowered(level.id); });
    }
    if (busy_phase(state.phase)) {
        return;
    }
    start_import();
    if (!busy_phase(state.phase) && !state.failed) {
        state.phase = Phase::kFailed;
        state.failed = true;
        state.message = "None of those levels is in the list";
    }
}

bool automation_done(std::string* result)
{
    ImportState& state = import_state();
    tick(); // (the window may be shut: the steps go on)
    if (state.waiting == Waiting::kNone || busy_phase(state.phase)) {
        return false;
    }
    if (state.failed) {
        *result = "error: " + state.message;
    } else if (state.waiting == Waiting::kList) {
        *result = "ok " + state.game + " " + std::to_string(state.levels.size()) + " levels";
    } else {
        *result = "ok " + state.message;
    }
    state.waiting = Waiting::kNone;
    return true;
}

const char* phase_name()
{
    static constexpr auto kNames = std::to_array<const char*>({"idle", "listing", "converting", "building", "exporting", "failed", "done", "checking"});
    return core::at(kNames, static_cast<std::size_t>(import_state().phase));
}

} // namespace frontend::level_editor::import
