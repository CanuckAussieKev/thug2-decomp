// NOT ORIGINAL - the new front end (ours): see new_frontend.h. The glue to the game: when it's up and over which
// original screen, its input, what its menus ask for (hand-offs, the ini, levels), its pictures and fonts, its ImGui
// context, the automation channel's commands.

#include "frontend/new_frontend.h"
#include "platform/keyboard.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
#include "game/rider/skater_state.h"
#include "game/rider/skater_score.h"
#include "game/skate/point_tally.h"
#include "engine/util/unique_handle.h"
#include "engine/core/parse_number.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"

namespace frontend::new_frontend {

namespace {

constexpr std::size_t kIniValueSize = 512;      // an ini value read
constexpr float kBoldWeight = 0.022F;           // the bold face's stroke weight
constexpr std::size_t kReadChunkSize = 4096;    // a file read in chunks
constexpr double kPointsCeiling = 2.0e9;        // the most points an award is taken for
constexpr int kMaxListNodes = 4096;             // a heap list walk stops after this many nodes
constexpr int kMaxPictureSide = 8192;           // the loading screen pictures' limit, in texels
constexpr int kPictureHeaderBytes = 0x20;       // the picture file's header before its texels
constexpr int kTrueColourBits = 32;             // the bit depths the pictures may have
constexpr int kPaletteBits = 8;
constexpr int kBitsPerByte = 8;
constexpr int kOpaqueAlpha = 0xFF;              // a full-bleed picture's alpha
constexpr float kInvisibleThreshold = 0.001F;   // an element scaled or faded below this isn't there
constexpr int kMaxLoggedFrames = 20;            // the frames a log line is written for
constexpr float kVisibleAlpha = 0.02F;          // an element needs more alpha than this to count as shown
constexpr int kWalkBudget = 8192;               // the most elements a tree walk visits
constexpr int kMaxParentDepth = 64;             // the most containers walked up
constexpr float kScaleArrowLeft = -49.0F;       // the scaling menus' arrow markers
constexpr float kScaleArrowRight = 76.0F;
constexpr float kColourArrowLeft = -53.0F; // the colour menus' arrow markers
constexpr float kColourArrowRight = 80.0F;
constexpr int kLitRedThreshold = 40;            // a stat cap sprite counts as lit when its red reaches this
constexpr float kLitAlpha = 0.6F;
constexpr uint32_t kMaxStatNames = 16;
constexpr int kMaxMenuChildren = 80;
constexpr int kSpriteBudget = 256;
constexpr int kValueBudget = 256;
constexpr float kRowSpacing = 24.0F; // the default spacing between a menu's rows
constexpr float kRowSlack = 2.0F;    // rows closer than this belong to one line
constexpr float kHalf = 0.5F;
constexpr std::size_t kStatNameSize = 32;
constexpr int kMaxKeyboardChildren = 32;
constexpr std::size_t kLogLineSize = 512;
constexpr int kMaxDumpDepth = 8;
constexpr int kTextBudget = 400;
constexpr std::size_t kMaxTexts = 6;
constexpr int kScreenBudget = 600;
constexpr std::size_t kTextLineSize = 160;
constexpr int kKeyTextBudget = 256;             // a wide key: a container of a score of cap sprites before its words
constexpr int kMinSingles = 20;                 // single-letter children that make a container a keyboard
constexpr int kChildTextBudget = 32;
constexpr int kButtonDown = 4;                  // the pad buttons a key names (render::button_for_tag)
constexpr int kButtonRight = 5;
constexpr int kButtonLeft = 6;
constexpr int kButtonUp = 7;
constexpr int kButtonDownRight = 10;
constexpr int kButtonUpLeft = 11;
constexpr int kButtonDownLeft = 12;
constexpr int kButtonUpRight = 13;
constexpr int kPolicyFrames = 30;
constexpr std::size_t kDecisionKeySize = 96;
constexpr int kChoicesPerRow = 16;              // a choice's index is its row * 16 + choice
constexpr int kKeysPerRow = 64;                 // a keyboard key's index is its row * 64 + column
constexpr float kEdgeFraction = 0.3F;           // the part of an adjustable row at each end that steps left or right
constexpr std::size_t kMaxMirrorEvents = 64;
constexpr std::size_t kMaxQueuedEvents = 256;
constexpr float kEditOpenTimeoutSeconds = 60.0F;
constexpr int kEditSettleFrames = 90;
constexpr int kEditPauseMenuFrames = 120;
constexpr int kEditChooseFrames = 20;
constexpr float kMaxFrameSeconds = 0.25F;
constexpr float kGuardSeconds = 2.0F;
constexpr float kLevelStartTimeoutSeconds = 30.0F;
void continue_last_level(modules::FrontEnd* front_end); // (below)

namespace ui_backend = frontend::level_editor::ui_backend;
using game_screens::Anchor;

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu (the skate module's level in the front end)
constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;  // SkaterState
constexpr uint32_t kSkaterScore = 0xD262802F;  // SkaterScore

// --- the game's state ---

// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t current_load_state() { return modules::Skate::sole() ? modules::Skate::sole()->level() : 0; }
bool in_front_end() { return current_load_state() == kLoadMainMenu; }
// The skate module's career (+0x20; null before the module is up).
objects::RiderStoryProgress* get_career() { return modules::Skate::sole() ? modules::Skate::sole()->career().get() : nullptr; }
modules::FrontEnd* the_front_end() { return modules::front_end_instance().get(); }
bool get_paused() { return (the_front_end() != nullptr) && the_front_end()->is_halted(); }
bool loading_screen_up()
{
    return render::loading_screen_shown() != 0 || render::renderer().loading_bar_timer() != 0 ||
           static_cast<uint8_t>(render::engine_parts().loading_screen_up()) != 0;
}

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name);
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

// A script integer by name (-1: none).
int script_integer(const std::string& name)
{
    if (name.empty()) {
        return -1;
    }
    const script::SymbolTableEntry* symbol = script::resolve_symbol(name_hash(name.c_str()));
    return symbol && symbol->type() == script::QbValueKind::kInteger ? symbol->value().integer() : -1;
}

// --- the setting and the files ---

std::string ini_path() { return game_folder_path(build_info::kFileName); }

std::string ini_string(const char* key, const char* fallback)
{
    std::array<char, kIniValueSize> value = {};
    GetPrivateProfileStringA("frontend", key, fallback, value.data(), sizeof(value), ini_path().c_str());
    return value.data();
}

bool is_file_present(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U);
}

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
    const util::UniqueFile file = util::open_file(path.c_str(), "rb");
    if (!file) {
        return {};
    }
    std::string text;
    std::array<char, kReadChunkSize> buffer{};
    for (size_t bytes_read = 0; (bytes_read = std::fread(buffer.data(), 1, sizeof(buffer), file.get())) > 0;) {
        text.append(buffer.data(), bytes_read);
    }
    return text;
}

// The text written next to `path` and moved over it in one step (a crash part-way leaves the old file).
bool write_atomically(const std::string& path, std::string_view text)
{
    const std::string temporary = path + ".new";
    util::UniqueFile file = util::open_file(temporary.c_str(), "wb");
    if (!file) {
        return false;
    }
    bool ok = std::fwrite(text.data(), 1, text.size(), file.get()) == text.size();
    ok = std::fflush(file.get()) == 0 && ok;
    file.reset(); // (closed before it's moved)
    if (!ok ||
        (MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE)) {
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
// NOT ORIGINAL grouping - new_frontend.cpp's state (were globals). Made on first use, never destroyed
// (new_frontend_state()).
struct NewFrontendState {
    State state_ = State::kIdle;
    float state_time_ = 0.0F;
    Anchor anchor_ = Anchor::kNone; // the game's menu up when ours came up (the log, the status)
    // Our screen a menu's item asked for (NewFrontend_Open: the overlay's CHANGE LEVEL / OPTIONS / LEVEL EDITOR items, the
    // skate shop's Play Level) - shown once, over whatever is up, on our root (Main in the front end, Pause in a level);
    // back at the root, ours goes aside again (the restyled menu it came from under it, or `new_frontend_state().open_back_` spawned when that
    // menu went - the skate shop's).
    Screen open_screen_ = Screen::kMain; // (Main: none asked for)
    LevelsFor open_levels_ = LevelsFor::kFreeSkate;
    uint32_t open_back_ = 0; // (a script spawned when it is closed with Back; 0: none)
    uint32_t shown_back_ = 0; // (the one of the screen shown now)
    Screen open_root_ = Screen::kMain;
    bool root_hidden_ = false; // the original's screen elements hidden under our pause menu
    uint32_t launch_level_ = 0; // the level up when a launch started (in a level: done when it changes)
    Catalogue catalogue_{};
    bool catalogue_tried_ = false;
    bool catalogue_ok_ = false;
    SavedState saved_{};
    uint32_t last_load_ = 0;
    std::vector<SDL_Event> events_;
    std::vector<paint::Hit> hits_;
    bool guard_ = false; // (the game's input held back until the keys that chose are let go)
    float guard_time_ = 0.0F;
    int buttons_down_ = 0;
    std::string skater_name_;
};

NewFrontendState& new_frontend_state()
{
    static core::NoDestroy<NewFrontendState> state;
    return state.get();
}

Menu& menu_state()
{
    static Menu value;
    return value;
}

// Controller directions held (the d-pad, the left stick): repeated as the keyboard repeats.
struct Repeat {
    int direction = -1; // an Input (Up / Down / Left / Right), -1 none
    float wait = 0.0F;
};
// NOT ORIGINAL grouping - the menu stick's repeat and its last position (were globals). Made on first use, never
// destroyed (stick_repeat()).
struct StickRepeat {
    Repeat repeat_{};
    int stick_x_ = 0;
    int stick_y_ = 0;
};

StickRepeat& stick_repeat()
{
    static core::NoDestroy<StickRepeat> state;
    return state.get();
}

constexpr float kRepeatDelay = 0.35F, kRepeatEvery = 0.09F;

// NOT ORIGINAL grouping - the menus' last tick and whether this frame ticked (were globals). Made on first use, never
// destroyed (menu_tick()).
struct MenuTick {
    std::chrono::steady_clock::time_point last_tick_;
    bool ticked_ = false;
};

MenuTick& menu_tick()
{
    static core::NoDestroy<MenuTick> state;
    return state.get();
}

// The Level Editor chosen on the Create screen: the level loads, then Start pauses it and the pause menu's LEVEL
// EDITOR opens the editor (as -edit does - frontend/start_level.h).
enum class Edit : uint8_t { kDisengaged, kWaitLevel, kWaitPause };
// NOT ORIGINAL grouping - the options being edited: the session, its time and presses, the values at its start, a
// pending restart, the note (were globals). Made on first use, never destroyed (edit_session()).
struct EditSession {
    Edit edit_ = Edit::kDisengaged;
    float edit_time_ = 0.0F;
    int edit_frames_ = 0;
    int edit_presses_ = 0;
    // The options' state: what the ini said at start-up (a Restart option changed since is "pending").
    std::vector<std::string> start_values_;
    std::vector<bool> restart_pending_;
    std::string options_note_;
};

EditSession& edit_session()
{
    static core::NoDestroy<EditSession> state;
    return state.get();
}

// --- the session in a level (the pause menu's panel) ---

struct SessionState {
    uint32_t level = 0;
    float seconds = 0.0F;
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
    if ((level == 0U) || level == kLoadMainMenu || get_paused() || level_editor::live_now()) {
        return;
    }
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater) {
        return;
    }
    session_state().seconds += dt;
    // The combo's points pending (the score's pot times its multiplier - level_editor_playtest.h), bails.
    int pending = 0;
    bool bailing = false;
    if (objects::ObjectModule* state = skater->fetch_module(kSkaterState)) {
        bailing = core::down_cast<objects::RiderPoseState>(state)->is_on(objects::PoseFlag::kBailing) != 0;
    }
    if (objects::ObjectModule* score = skater->fetch_module(kSkaterScore)) {
        if (const modules::PointTally* tally = core::down_cast<objects::RiderPointKeeper>(score)->points_held()) {
            const double points = static_cast<double>(tally->pot()) * tally->multiplier();
            if (points > 0.0 && points < kPointsCeiling) {
                pending = static_cast<int>(points);
            }
        }
    }
    level_editor::play_test::ComboEnd end;
    if (session_state().combo.tick_frame(pending, bailing, &end) && end.landed) {
        session_state().best_combo_ = std::max(session_state().best_combo_, end.points);
    }
    // The gaps (the skate module's checklist: the level's).
    std::vector<level_editor::play_test::GapRead> read;
    modules::Skate* skate = modules::Skate::sole();
    if (const goals::GapTally* list =
            skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list())
                  : nullptr) {
        int guard = 0;
        for (const memory::ListNode* node = list->gaps().next(); node && node != &list->gaps() && ++guard < kMaxListNodes;
             node = node->next()) {
            if (node->data() == memory::ListNode::head_marker()) {
                break;
            }
            auto* gap = static_cast<goals::GapEntry*>(node->data());
            read.push_back(
                {.name = gap->label().c_str() ? gap->label().c_str() : "", .pending = gap->pending(), .count = gap->num(), .score = gap->points_held()});
        }
    }
    session_state().gaps_total_ = static_cast<int>(read.size());
    for (const level_editor::play_test::GapChange& change : session_state().gaps.tick_frame(read)) {
        if (change.kind == level_editor::play_test::GapChange::Kind::kLanded) {
            session_state().gaps_landed_.insert(change.name);
        }
    }
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
uint32_t& art_clock()
{
    static uint32_t art_clock{0};
    return art_clock;
}
constexpr size_t kArtCached = 10;

void free_art()
{
    for (auto& [name, cached] : art_state()) {
        ui_backend::release_texture(cached.art.texture);
    }
    art_state().clear();
}

// The image file (the game's .img.xbx: a 32-byte header, a palette for 8-bit images, the texels - swizzled when both
// sides are powers of two; an upscaled loading screen may be DXT1 blocks - engine/gfx/hd_ui.h) as a texture.
paint::Art load_image_file(const std::string& name)
{
    const std::string path = "images\\" + name + ".img.xbx";
    file::Hdl file = file::unseal(path.c_str(), "rb");
    if (!file) {
        return {};
    }
    const uint32_t size = file::get_file_size(file);
    std::vector<uint8_t> data(size);
    const bool read = size > 0x20 && file::get_take_in(data.data(), static_cast<int>(size), 1, file) > 0;
    file::shut(file);
    if (!read) {
        return {};
    }
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
    if (width == 0 || height == 0 || width > kMaxPictureSide || height > kMaxPictureSide) {
        return {};
    }
    std::span<const uint8_t> texels = std::span<const uint8_t>(data).subspan(kPictureHeaderBytes);
    size_t left = data.size() - kPictureHeaderBytes;
    paint::Art art;
    art.width = static_cast<float>(width);
    art.height = static_cast<float>(height);
    // (a loading screen's rows are stored bottom first: render::draw_loading_screen_picture puts v 1 at the screen's top)
    art.bottom_up_ = true;
    if (format == render::hd_ui::kFormatDxt1 && render::hd_ui::image_scale(marker) != 1 && width % 4 == 0 &&
        height % 4 == 0) {
        const int row_bytes = static_cast<int>(width / 4 * 8);
        const int rows = static_cast<int>(height / 4);
        if (left < static_cast<size_t>(row_bytes) * rows) {
            return {};
        }
        art.texture = ui_backend::create_texture(static_cast<int>(width), static_cast<int>(height),
                                                 ui_backend::PixelLayout::kDxt1, texels.data(), {.row_bytes_ = row_bytes, .rows = rows});
        return art;
    }
    // (the bits per texel: format 0 true colour, 2 16-bit, 0x13 8-bit palettized; else the format is the count)
    const uint32_t other_bits = format == 0x13 ? 8 : format;
    const uint32_t not_true_colour_bits = format == 2 ? 16 : other_bits;
    const uint32_t bits = format == 0 ? 32 : not_true_colour_bits;
    const bool palettized = format == 0x13 || (format != 0 && format != 2 && format < 0x10);
    if (bits != kTrueColourBits && !(palettized && bits == kPaletteBits)) {
        return {}; // (16-bit and 4-bit images: no loading screen is one)
    }
    // (the palette: the colours index the rest of the file, as the original's read did - an index past a short
    // palette reads the texels that follow)
    std::span<const uint8_t> palette;
    if (palettized) {
        const size_t palette_bytes = (palette_size != 0U) ? palette_size : 0x400;
        if (left < palette_bytes) {
            return {};
        }
        palette = texels;
        texels = texels.subspan(palette_bytes);
        left -= palette_bytes;
    }
    const size_t count = static_cast<size_t>(width) * height;
    if (left < count * (bits / kBitsPerByte)) {
        return {};
    }
    const bool powers = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
    std::vector<uint8_t> plain;
    std::span<const uint8_t> rows_data = texels;
    if (powers) {
        plain.resize(count * (bits / kBitsPerByte));
        os_layer::unswizzle(plain, texels.first(plain.size()), {.width = static_cast<int>(width), .height = static_cast<int>(height), .bytes_per_texel_ = static_cast<uint32_t>(bits / kTrueColourBits)});
        rows_data = plain;
    }
    std::vector<uint8_t> bgra(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const std::span<const uint8_t> px =
            palettized ? palette.subspan(size_t{core::at(rows_data, i)} * 4) : rows_data.subspan(i * 4);
        core::at(bgra, (i * 4) + 0) = core::at(px, 0);
        core::at(bgra, (i * 4) + 1) = core::at(px, 1);
        core::at(bgra, (i * 4) + 2) = core::at(px, 2);
        core::at(bgra, (i * 4) + 3) =
            kOpaqueAlpha; // (a full-bleed picture: opaque - the files' alpha is the PS2's half scale)
    }
    art.texture =
        ui_backend::create_texture(static_cast<int>(width), static_cast<int>(height), ui_backend::PixelLayout::kBgra32,
                                   bgra.data(), {.row_bytes_ = static_cast<int>(width * 4), .rows = static_cast<int>(height)});
    return art;
}

paint::Art art_for(const std::string& image)
{
    auto it = art_state().find(image);
    if (it == art_state().end()) {
        if (art_state().size() >= kArtCached) {
            auto oldest = art_state().begin();
            for (auto i = art_state().begin(); i != art_state().end(); ++i) {
                if (i->second.used < oldest->second.used) {
                    oldest = i;
                }
            }
            ui_backend::release_texture(oldest->second.art.texture);
            art_state().erase(oldest);
        }
        CachedArt cached;
        cached.art = load_image_file(image);
        cached.failed = (cached.art.texture == 0U);
        if (cached.failed) {
            get_log("new front end: no picture images\\%s.img.xbx", image.c_str());
        }
        it = art_state().emplace(image, cached).first;
    }
    it->second.used = ++art_clock();
    return it->second.art;
}

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

std::vector<SectionView> get_views(bool game_rules = false)
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

void get_sound(const char* script, const char* flag = nullptr)
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

// --- the options ---

void load_option_values()
{
    const std::string text = read_text(ini_path());
    const std::vector<options::Spec>& specs = options::get_specs();
    std::vector<int> choices(specs.size());
    const bool first = edit_session().start_values_.empty();
    if (first) {
        edit_session().start_values_.resize(specs.size());
        edit_session().restart_pending_.assign(specs.size(), false);
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        std::string value;
        const bool present = options::ini_get(text, {.section = core::at(specs, i).section, .key = core::at(specs, i).key}, &value);
        core::at(choices, i) = options::choice_of(core::at(specs, i), value, present);
        if (first) {
            core::at(edit_session().start_values_, i) = core::at(core::at(specs, i).choices, core::at(choices, i)).value;
        }
    }
    menu_state().set_option_values(std::move(choices));
}

void apply_option(const ActStep& action)
{
    const int option = action.option;
    const int choice = action.value;
    const std::vector<options::Spec>& specs = options::get_specs();
    if (option < 0 || std::cmp_greater_equal(option, specs.size())) {
        return;
    }
    const options::Spec& spec = core::at(specs, option);
    const char* value = core::at(spec.choices, choice).value;
    const std::string path = ini_path();
    const std::string text = read_text(path);
    const std::string changed = options::ini_set(text, {.section = spec.section, .key = spec.key}, value);
    if (!write_atomically(path, changed)) {
        get_log("new front end: couldn't write %s ([%s] %s=%s)", path.c_str(), spec.section, spec.key, value);
        edit_session().options_note_ = text_tools::kCouldntSave;
        return;
    }
    get_log("new front end: options - [%s] %s=%s written to %s", spec.section, spec.key, value, path.c_str());
    edit_session().options_note_.clear();
    if (spec.apply == options::Apply::kLive) {
        const bool on = std::strcmp(value, "0") != 0;
        if (std::strcmp(spec.key, "mouse_menus") == 0) {
            mouse_menus::is_switched_on() = on;
        } else if (std::strcmp(spec.key, "restart_tester") == 0) {
            restart_tester::is_switched_on() = on;
        } else if (std::strcmp(spec.key, "level_editor") == 0) {
            level_editor::is_switched_on() = on;
        } else if (std::strcmp(spec.key, "continue_music") == 0) {
            audio::continue_music::is_switched_on() = on;
        } else if (std::strcmp(spec.key, "respect_locks") == 0) {
            menu_state().set_views(get_views(menu_state().levels_purpose() == LevelsFor::kNet));
        }
    } else if (std::cmp_less(option, edit_session().restart_pending_.size())) {
        core::at(edit_session().restart_pending_, option) = core::at(edit_session().start_values_, option) != value;
    }
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

// --- the state machine ---

void change_phase(State state)
{
    if (new_frontend_state().state_ == state) {
        return;
    }
    get_log("new front end: %s -> %s%s%s", get_state_name(new_frontend_state().state_), get_state_name(state),
            state == State::kShown ? " over the " : "",
            state == State::kShown ? game_screens::anchor_name(new_frontend_state().anchor_) : "");
    const bool was_up = new_frontend_state().state_ == State::kShown || new_frontend_state().state_ == State::kLaunching;
    const bool up = state == State::kShown || state == State::kLaunching;
    if (was_up && !up) {
        new_frontend_state().guard_ = true;
        new_frontend_state().guard_time_ = 0.0F;
    }
    new_frontend_state().state_ = state;
    new_frontend_state().state_time_ = 0.0F;
    new_frontend_state().events_.clear();
    stick_repeat().repeat_ = {};
}

// The original's elements hidden (or shown again): the game's own hide_root_window / unhide_root_window (root_window
// scaled to 0 / 1) and root_window's alpha 0 / 1 at once - scaled to 0 alone, some elements still left marks: specks
// behind the restyled dialogs and a thin bar at the top centre of the pause screens (pause_menu_gradient's sprite
// squeezed to a line) - the user's report of 2026-10-25. An element's alpha is its parents' times its own, so nothing
// under root_window is drawn.
void hide_root(bool hide, bool force = false)
{
    if (hide == new_frontend_state().root_hidden_ && !force) {
        return;
    }
    const char* script = hide ? "hide_root_window" : "unhide_root_window";
    if (!is_script(name_hash(script))) {
        return;
    }
    script::launch_script(name_hash(script), nullptr, nullptr, false, script);
    if (UiWidget* root = game_screens::get_find(name_hash("root_window"))) {
        root->change_opacity(hide ? 0.0F : 1.0F, true);
    }
    new_frontend_state().root_hidden_ = hide;
}

// While ours hides the original's elements, whatever script scales root_window back (create_pause_menu's
// unhide_root_window - a script making the pause menu again, the BMX's toggle, a dialog...) is undone at once: the
// original never shows under ours.
void keep_root_hidden()
{
    if (!new_frontend_state().root_hidden_) {
        return;
    }
    const UiWidget* root = game_screens::get_find(name_hash("root_window"));
    if (!root || (root->aim_point().scale_x_ < kInvisibleThreshold && root->aim_point().alpha < kInvisibleThreshold)) {
        return;
    }
    static int s_logged = 0;
    if (s_logged < kMaxLoggedFrames) {
        ++s_logged;
        get_log("new front end: the original's elements were shown again under ours (a script scaled root_window) - "
                "hidden");
    }
    hide_root(true, true);
}

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

uint32_t type_of(const UiWidget* element) { return static_cast<const objects::Entity*>(element)->kind_tag(); }
uint32_t id_of_element(const UiWidget* element) { return static_cast<const objects::Entity*>(element)->ident(); }
UiWidget* first_child(const UiWidget* element) { return element->eldest().get_pull(); }
UiWidget* get_next(const UiWidget* element) { return element->following().get_pull(); }
bool element_hidden(const UiWidget* element)
{
    return (static_cast<const objects::Entity*>(element)->object_flags() & UiWidget::kHidden) != 0;
}
bool get_visible(const UiWidget* element)
{
    return !element_hidden(element) && element->aim_point().alpha > kVisibleAlpha && core::at(element->aim_point().rgba, 3) > 2;
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
    return manager ? manager->tree_root().get_pull() : nullptr;
}

// The menu that takes the pad events now: of the focused menus, one in a dialog box first, else the last in the tree
// (the newest screen). `inDialog`: it is in dialog_box_container. `coming`: a menu not taking them yet counts too, when
// it is shown - the screen being made (the pause menu animates in for 13 frames before it takes the pad).
UiWidget* active_menu(bool* in_dialog = nullptr, bool coming = false)
{
    const UiWidget* root = root_element();
    if (!root) {
        return nullptr;
    }
    static const uint32_t kDialogContainer = name_hash("dialog_box_container");
    UiWidget* found = nullptr;
    UiWidget* dialog = nullptr;
    int budget = kWalkBudget;
    // (depth first, without recursion: the element, its first child, its next sibling, back up through the parents)
    struct Walk {
        UiWidget* element;
        bool in_dialog_;
    };
    std::vector<Walk> stack;
    stack.push_back({.element = first_child(root), .in_dialog_ = false});
    while (!stack.empty() && --budget > 0) {
        Walk& top = stack.back();
        UiWidget* widget = top.element;
        if (!widget) {
            stack.pop_back();
            continue;
        }
        const bool here = top.in_dialog_ || id_of_element(widget) == kDialogContainer;
        top.element = get_next(widget);
        if (mouse_menus::is_menu(widget) &&
            (coming ? get_visible(widget) : mouse_menus::menu_gets_pad_events(widget))) {
            found = widget;
            if (here) {
                dialog = widget;
            }
        }
        if (UiWidget* child = first_child(widget)) {
            stack.push_back({.element = child, .in_dialog_ = here});
        }
    }
    if (in_dialog) {
        *in_dialog = dialog != nullptr;
    }
    return dialog ? dialog : found;
}

// The strings of the text elements in `element` (itself included), in tree order; `skip`: subtrees left out.
void collect_texts(const UiWidget* element, std::vector<std::string>* out, const std::vector<const UiWidget*>& skip,
                   int* budget, bool visible_only)
{
    if (!element || --*budget <= 0) {
        return;
    }
    if (std::ranges::find(skip, element) != skip.end()) {
        return;
    }
    if (visible_only && !get_visible(element)) {
        return;
    }
    if (type_of(element) == text_type()) {
        const char* text = core::down_cast<const LabelWidget>(element)->chars();
        if (text && (*text != '\0')) {
            out->emplace_back(text);
        }
    }
    for (const UiWidget* child = first_child(element); child && *budget > 0; child = get_next(child)) {
        collect_texts(child, out, skip, budget, visible_only);
    }
}

// An element's height on the 640 x 480 layout: its position and its parents' added up (justification and scale aside -
// enough to tell rows apart).
float absolute_y(const UiWidget* element)
{
    float y = 0.0F;
    for (int guard = 0; element && guard < kMaxParentDepth; ++guard, element = element->container().get_pull()) {
        y += element->aim_point().y;
    }
    return y;
}

// The visible texts in `element` (itself included) with their heights, in tree order; `skip`: subtrees left out.
void collect_texts_at(const UiWidget* element, std::vector<std::pair<std::string, float>>* out,
                      const std::vector<const UiWidget*>& skip, int* budget)
{
    if (!element || --*budget <= 0 || !get_visible(element)) {
        return;
    }
    if (std::ranges::find(skip, element) != skip.end()) {
        return;
    }
    if (type_of(element) == text_type()) {
        const char* text = core::down_cast<const LabelWidget>(element)->chars();
        if (text && (*text != '\0')) {
            out->emplace_back(text, absolute_y(element));
        }
    }
    for (const UiWidget* child = first_child(element); child && *budget > 0; child = get_next(child)) {
        collect_texts_at(child, out, skip, budget);
    }
}

// A visible sprite in `element` that isn't a highlight bar (those are drawn at alpha 0 until their item is focused).
bool has_visible_sprite(const UiWidget* element, int* budget)
{
    if (!element || --*budget <= 0 || element_hidden(element) || element->aim_point().alpha <= kVisibleAlpha) {
        return false;
    }
    if (type_of(element) == sprite_type() && core::at(element->aim_point().rgba, 3) > 2 &&
        std::fabs(element->aim_point().scale_x_) > kInvisibleThreshold) {
        return true;
    }
    for (const UiWidget* child = first_child(element); child; child = get_next(child)) {
        if (has_visible_sprite(child, budget)) {
            return true;
        }
    }
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
    static const uint32_t kCheckbox = name_hash("checkbox");
    static const uint32_t kCheckmark = name_hash("checkmark");
    static const uint32_t kLeft = name_hash("left_arrow");
    static const uint32_t kRight = name_hash("right_arrow");
    static const uint32_t kUp = name_hash("up_arrow");
    static const uint32_t kHueArrow = name_hash("hue_up_arrow");
    static const uint32_t kStar = name_hash("star");
    static const uint32_t kCircle = name_hash("circle");
    static const auto kColourArrows = std::to_array<uint32_t>({name_hash("hue_up_arrow"), name_hash("saturation_up_arrow"),
                                             name_hash("value_up_arrow"), name_hash("alpha_up_arrow")});
    for (const UiWidget* widget = element; widget && --*budget > 0; widget = get_next(widget)) {
        if (type_of(widget) == sprite_type()) {
            const uint32_t texture = core::down_cast<const ImageWidget>(widget)->texture();
            if (texture == kLeft || texture == kRight) {
                item->adjustable = true;
            }
            if (texture == kUp) {
                // (colormenu_nowrap_arrow_left / right -53 .. 80 for the colour menus' markers, scalingmenu_arrow_left /
                // right -49 .. 76 for the scaling ones)
                const uint32_t id = id_of_element(widget);
                float left = -kScaleArrowLeft;
                float right = kScaleArrowRight;
                for (const uint32_t colour : kColourArrows) {
                    if (id == colour) {
                        left = -kColourArrowLeft;
                        right = kColourArrowRight;
                    }
                }
                item->adjustable = true;
                item->slider_at_ = std::clamp((widget->aim_point().x - left) / (right - left), 0.0F, 1.0F);
                item->slider_hue_ = id == kHueArrow;
                item->slider_kind_ = 0; // (the colour arrows' sliders: 1-4)
                for (int arrow = 0; arrow < 4; ++arrow) {
                    if (id == core::at(kColourArrows, arrow)) {
                        item->slider_kind_ = arrow + 1;
                        break;
                    }
                }
            }
            if (item->stat_index_ >= 0 && (texture == kStar || texture == kCircle)) {
                // A stat's star (build_stat_slider: ten of them; stats_menu_update_row lights the first `value` in
                // stats_menu_star_highlight [85 85 0], the rest stats_menu_star_lowlight [10 10 10] - the story's earned
                // but unspent ones lit at alpha 0.3)
                item->stat_value_ = std::max(item->stat_value_, 0);
                item->stat_cap_ = std::max(item->stat_cap_, 0);
                if (core::at(widget->aim_point().rgba, 0) >= kLitRedThreshold) {
                    ++item->stat_cap_;
                    if (widget->aim_point().alpha > kLitAlpha) {
                        ++item->stat_value_;
                    }
                }
            }
            if (texture == kCheckbox && get_visible(widget)) {
                item->checkbox = 0;
                for (const UiWidget* mark = first_child(widget); mark; mark = get_next(mark)) {
                    if (type_of(mark) == sprite_type() && get_visible(mark) &&
                        core::down_cast<const ImageWidget>(mark)->texture() == kCheckmark) {
                        item->checkbox = 1;
                    }
                }
            }
        }
        if (const UiWidget* child = first_child(widget)) {
            read_value_sprites(child, item, budget);
        }
    }
}

// The skater select's stats for the template `name`: stat_names' short_string (else string) and the template's value.
std::vector<std::pair<std::string, int>> skater_stats(uint32_t name)
{
    std::vector<std::pair<std::string, int>> stats;
    objects::ProfileRoster* manager = objects::profile_roster();
    objects::RiderIdentity* profile = manager ? manager->get_template(name) : nullptr;
    script::Array* names = script::fetch_list(name_hash("stat_names"));
    if (!profile || !names || names->kind_tag() != script::QbValueKind::kStruct) {
        return stats;
    }
    for (uint32_t i = 0; i < names->extent() && i < kMaxStatNames; ++i) {
        auto* entry = names->struct_at(i);
        uint32_t stat = 0;
        const char* label = nullptr;
        if (!entry || !entry->read_hash_value(name_hash("name"), &stat, 0)) {
            continue;
        }
        if (!entry->fetch_text(name_hash("short_string"), &label, 0)) {
            entry->fetch_text(name_hash("string"), &label, 0);
        }
        stats.emplace_back(label ? label : "?", profile->fetch_tally(stat));
    }
    return stats;
}

mirror::MenuRead read_menu(UiWidget* menu, bool in_dialog)
{
    mirror::MenuRead read;
    if (!menu) {
        return read;
    }
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
    int budget = kWalkBudget;
    int index = 0;
    for (const UiWidget* child = first_child(menu); child && index < kMaxMenuChildren; child = get_next(child), ++index) {
        mirror::ItemRead item;
        item.id = id_of_element(child);
        item.focusable = mouse_menus::item_focusable(child);
        collect_texts(child, &item.texts, {}, &budget, false);
        if (!skaters && index != read.selected_index_) {
            int sprite_budget = kSpriteBudget;
            item.graphic_value_ = has_visible_sprite(child, &sprite_budget);
        }
        if (!skaters) {
            int value_budget = kValueBudget;
            if (const UiWidget* first = first_child(child)) {
                read_value_sprites(first, &item, &value_budget);
            }
        }
        if (skaters) {
            objects::ProfileRoster* manager = objects::profile_roster();
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
                const char* text = core::down_cast<LabelWidget>(child)->chars();
                read.dialog_title_ = text ? text : "";
            } else if (type_of(child) == kTextBlock) {
                std::vector<std::string> lines;
                collect_texts(child, &lines, {}, &budget, false);
                std::string joined;
                for (const std::string& line : lines) {
                    joined += (joined.empty() ? "" : " ") + line;
                }
                read.dialog_lines_.push_back(joined);
            }
        }
        return read;
    }
    if (skaters) {
        if (UiWidget* name = game_screens::get_find(name_hash("select_skater_name"))) {
            if (type_of(name) == text_type() && core::down_cast<LabelWidget>(name)->chars()) {
                read.skater_name_ = core::down_cast<LabelWidget>(name)->chars();
            }
        }
        if (read.selected_index_ >= 0 && std::cmp_less(read.selected_index_, read.items.size())) {
            read.stats = skater_stats(core::at(read.items, read.selected_index_).id);
        }
        return read;
    }
    // A list: its title (title_text, the themed menus'), the other words on its screen (under the same child of the
    // root: not the menu, its title or the helper text).
    if (UiWidget* title = game_screens::get_find(name_hash("title_text"))) {
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars() && get_visible(title)) {
            read.title = core::down_cast<LabelWidget>(title)->chars();
        }
    }
    const UiWidget* root = root_element();
    const UiWidget* screen = menu;
    for (int guard = 0; screen && guard < kMaxParentDepth; ++guard) {
        const UiWidget* parent = screen->container().get_pull();
        if (!parent || parent == root) {
            break;
        }
        screen = parent;
    }
    std::vector<const UiWidget*> skip = {menu};
    for (const char* id : {"helper_text_anchor", "submenu_title_anchor", "title_text"}) {
        if (const UiWidget* widget = game_screens::get_find(name_hash(id))) {
            skip.push_back(widget);
        }
    }
    if (screen && screen != menu) {
        // A column beside the menu (the grab tricks' names beside their key combos): a word at a row's height is that
        // row's value; the rest are the screen's other words.
        std::vector<std::pair<std::string, float>> beside;
        collect_texts_at(screen, &beside, skip, &budget);
        std::vector<float> row_y;
        for (const UiWidget* child = first_child(menu); child; child = get_next(child)) {
            float y = absolute_y(child);
            for (const UiWidget* text_child = first_child(child); text_child; text_child = get_next(text_child)) {
                if (type_of(text_child) == text_type()) {
                    y = absolute_y(text_child);
                    break;
                }
            }
            row_y.push_back(y);
        }
        // (the nearest row, within half the rows' spacing)
        float spacing = kRowSpacing;
        for (size_t i = 1; i < row_y.size(); ++i) {
            if (core::at(row_y, i) - core::at(row_y, i - 1) > kRowSlack) {
                spacing = std::min(spacing, core::at(row_y, i) - core::at(row_y, i - 1));
            }
        }
        for (const auto& [text, y] : beside) {
            size_t row = row_y.size();
            float best = spacing * kHalf;
            for (size_t i = 0; i < row_y.size() && i < read.items.size(); ++i) {
                if (std::fabs(core::at(row_y, i) - y) < best && core::at(read.items, i).focusable) {
                    best = std::fabs(core::at(row_y, i) - y);
                    row = i;
                }
            }
            if (row < row_y.size()) {
                core::at(read.items, row).texts.push_back(text);
            } else {
                read.extra_texts_.push_back(text);
            }
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
            std::array<char, kStatNameSize> name{};
            text::format_to(name, "stats_menu_names_%u", static_cast<unsigned>(i));
            core::at(ids, i) = name_hash(name.data());
        }
        return ids;
    }();
    for (size_t i = 0; i < kIds.size(); ++i) {
        if (core::at(kIds, i) == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool has_stat_rows(const UiWidget* menu)
{
    for (const UiWidget* child = menu ? first_child(menu) : nullptr; child; child = get_next(child)) {
        if (stat_row_index(id_of_element(child)) >= 0) {
            return true;
        }
    }
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
    const UiWidget* editing_row = editing ? editing->container().get_pull() : nullptr;
    static const uint32_t kColumn1 = name_hash("sub_vmenu");
    static const uint32_t kColumn2 = name_hash("sub_vmenu2");
    std::vector<const UiWidget*> menus;
    if (has_stat_rows(active) && id_of_element(active) != kColumn1 && id_of_element(active) != kColumn2) {
        menus.push_back(active);
    } else {
        for (const uint32_t column : {kColumn1, kColumn2}) {
            if (const UiWidget* menu = game_screens::get_find(column); menu && has_stat_rows(menu)) {
                menus.push_back(menu);
            }
        }
    }
    if (menus.empty()) {
        return read;
    }
    read.menu_id_ = id_of_element(menus.front());
    read.menu_key_ = menus.front();
    int budget = kWalkBudget;
    int selected_stat = -1;
    for (size_t column = 0; column < menus.size(); ++column) {
        const UiWidget* menu = core::at(menus, column);
        const bool takes_pad = menu == active && (editing == nullptr);
        const int selected = takes_pad ? mouse_menus::selected_index(menu) : -1;
        int index = 0;
        for (const UiWidget* child = first_child(menu); child && index < kMaxKeyboardChildren; child = get_next(child), ++index) {
            const int stat = stat_row_index(id_of_element(child));
            if (stat < 0) {
                continue;
            }
            mirror::ItemRead item;
            item.id = id_of_element(child);
            item.focusable = mouse_menus::item_focusable(child);
            item.stat_index_ = stat;
            item.stat_column_ = static_cast<int>(column);
            item.stat_child_ = takes_pad ? index : -1;
            collect_texts(child, &item.texts, {}, &budget, false);
            int value_budget = kValueBudget;
            if (const UiWidget* first = first_child(child)) {
                read_value_sprites(first, &item, &value_budget);
            }
            if ((takes_pad && index == selected) || child == editing_row) {
                selected_stat = stat;
            }
            read.items.push_back(std::move(item));
        }
    }
    std::ranges::stable_sort(read.items, {}, &mirror::ItemRead::stat_index_);
    for (size_t i = 0; i < read.items.size(); ++i) {
        if (core::at(read.items, i).stat_index_ == selected_stat) {
            read.stats_selected_ = static_cast<int>(i);
        }
    }
    // The other words: the screen's texts (the points left, the description, the story's goals) - the stats menus and the
    // helper text left out.
    if (UiWidget* title = game_screens::get_find(name_hash("title_text"))) {
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars() && get_visible(title)) {
            read.title = core::down_cast<LabelWidget>(title)->chars();
        }
    }
    const UiWidget* root = root_element();
    const UiWidget* screen = menus.front();
    for (int guard = 0; screen && guard < kMaxParentDepth; ++guard) {
        const UiWidget* parent = screen->container().get_pull();
        if (!parent || parent == root) {
            break;
        }
        screen = parent;
    }
    std::vector<const UiWidget*> skip(menus.begin(), menus.end());
    for (const char* id : {"helper_text_anchor", "title_text"}) {
        if (const UiWidget* widget = game_screens::get_find(name_hash(id))) {
            skip.push_back(widget);
        }
    }
    if (screen) {
        collect_texts(screen, &read.extra_texts_, skip, &budget, true);
    }
    return read;
}

// [frontend] debug_restyle=1: a menu left as it is (not restyled) logged once with its screen's element tree - what
// the reader would have to understand to restyle it (each element: type, id, text, texture, position, hidden).
const char* current_type_name(uint32_t type)
{
    static const auto kTypes = std::to_array<const char*>({
        "TextElement",    "SpriteElement",    "ContainerElement", "VMenu",         "HMenu", "VScrollingMenu",
        "HScrollingMenu", "TextBlockElement", "Element3d",        "WindowElement", "Menu"});
    for (const char* name : kTypes) {
        if (name_hash(name) == type) {
            return name;
        }
    }
    return nullptr;
}

void dump_tree(const UiWidget* element, int depth, int* budget)
{
    for (; element && --*budget > 0; element = get_next(element)) {
        std::array<char, kLogLineSize> line{};
        const char* type = current_type_name(type_of(element));
        const size_t at =
            text::format_to(line, "%*s%s %08X (%.0f, %.0f)%s", depth * 2, "", type ? type : "?", id_of_element(element),
                            element->aim_point().x, element->aim_point().y, get_visible(element) ? "" : " hidden");
        if (type_of(element) == text_type()) {
            const char* text = core::down_cast<const LabelWidget>(element)->chars();
            const std::span<char> rest = std::span<char>(line).subspan(at);
            text::format_to(rest, " \"%.80s\"", text ? text : "");
        } else if (type_of(element) == sprite_type()) {
            text::format_to(std::span<char>(line).subspan(at), " texture %08X",
                            core::down_cast<const ImageWidget>(element)->texture());
        }
        get_log("new front end: debug_restyle %s", line.data());
        if (depth < kMaxDumpDepth) {
            dump_tree(first_child(element), depth + 1, budget);
        }
    }
}

void dump_left_menu(UiWidget* menu, const std::string& why)
{
    static const bool kOn = GetPrivateProfileIntA("frontend", "debug_restyle", 0, ini_path().c_str()) != 0;
    // (each menu once by what it holds: menus share ids and are rebuilt in the same element)
    static std::set<std::string> logged;
    std::vector<std::string> texts;
    int text_budget = kTextBudget;
    collect_texts(menu, &texts, {}, &text_budget, false);
    std::string key = std::to_string(id_of_element(menu));
    for (size_t i = 0; i < texts.size() && i < kMaxTexts; ++i) {
        key += "|" + core::at(texts, i);
    }
    if (!kOn || (menu == nullptr) || !logged.insert(key).second) {
        return;
    }
    const UiWidget* root = root_element();
    const UiWidget* screen = menu;
    for (int guard = 0; screen && guard < kMaxParentDepth; ++guard) {
        const UiWidget* parent = screen->container().get_pull();
        if (!parent || parent == root) {
            break;
        }
        screen = parent;
    }
    get_log("new front end: debug_restyle - the menu %08X (%s), its screen %08X:", id_of_element(menu), why.c_str(),
            id_of_element(screen));
    int budget = kScreenBudget;
    const UiWidget* one = screen;
    std::array<char, kTextLineSize> line{};
    text::format_to(line, "%s %08X", current_type_name(type_of(one)) ? current_type_name(type_of(one)) : "?",
                    id_of_element(one));
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
    if (!menu || type_of(menu) != h_menu_type()) {
        return nullptr;
    }
    UiWidget* parent = menu->container().get_pull();
    if (!parent || type_of(parent) != v_menu_type()) {
        return nullptr;
    }
    int rows = 0;
    int singles = 0;
    for (const UiWidget* row = first_child(parent); row; row = get_next(row)) {
        if (type_of(row) != h_menu_type()) {
            continue;
        }
        ++rows;
        for (const UiWidget* key = first_child(row); key; key = get_next(key)) {
            std::vector<std::string> texts;
            int budget = kKeyTextBudget;
            collect_texts(key, &texts, {}, &budget, false);
            for (const std::string& text : texts) {
                singles += text.size() == 1 ? 1 : 0;
            }
        }
    }
    return rows >= 3 && singles >= kMinSingles ? parent : nullptr;
}

// The keyboard read: its rows of keys (a key's words: the last that isn't a button tag, e.g. "Backspace"), the key
// selected, the text typed (the first row that isn't keys - its text, the cursor's "_" left in). `active`: the menu
// whose keyboard it is (keyboard_of).
mirror::MenuRead read_keyboard(UiWidget* active)
{
    const UiWidget* keys = keyboard_of(active);
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
                int budget = kChildTextBudget;
                collect_texts(child, &texts, {}, &budget, false);
                if (!texts.empty()) {
                    read.entry = texts.front();
                }
                entry_read = true;
            }
            continue;
        }
        std::vector<std::string> key_row;
        for (const UiWidget* key = first_child(child); key; key = get_next(key)) {
            std::vector<std::string> texts;
            int budget = kKeyTextBudget; // (a wide key: a container of a score of cap sprites before its words)
            collect_texts(key, &texts, {}, &budget, false);
            std::string label;
            for (const std::string& text : texts) {
                if (!(text.size() >= 2 && core::at(text, 0) == '\\')) {
                    label = text;
                }
            }
            key_row.push_back(label);
        }
        if (child == active) {
            read.key_row_ = row;
            read.key_col_ = mouse_menus::selected_index(active);
        }
        read.keys.push_back(std::move(key_row));
        ++row;
    }
    if (UiWidget* title = game_screens::get_find(name_hash("title_text"))) {
        if (type_of(title) == text_type() && core::down_cast<LabelWidget>(title)->chars() && get_visible(title)) {
            read.title = core::down_cast<LabelWidget>(title)->chars();
        }
    }
    return read;
}

// A button tag as the game spells it (gfx/text.cpp's): the explicit buttons through the controller map when one is in
// use, the meta buttons through the meta map; the arrow glyphs Up / Down / Left / Right, else the key's name.
std::string pad_button_label_for(mirror::ButtonTag button_tag)
{
    const char tag = button_tag.tag;
    const char digit = button_tag.digit;
    int button = 0;
    if (tag == 'm' || tag == 'M') {
        button = text::hex_digit_value(render::button_glyph(digit));
    } else {
        button = static_cast<int>(render::button_for_tag(digit));
    }
    switch (button) {
    case kButtonDown:
        return text_tools::kKeyDown;
    case kButtonRight:
        return text_tools::kKeyRight;
    case kButtonLeft:
        return text_tools::kKeyLeft;
    case kButtonUp:
        return text_tools::kKeyUp;
    case kButtonDownRight:
        return text_tools::
            kKeyDownRight; // (the grab tricks' order: Down, Down-Left \bc, Down-Right \ba, ... Up-Left \bb,
    case kButtonUpLeft:
        return text_tools::kKeyUpLeft; // Up-Right \bd)
    case kButtonDownLeft:
        return text_tools::kKeyDownLeft;
    case kButtonUpRight:
        return text_tools::kKeyUpRight;
    default:
        break;
    }
    const char* name = sio::get_button_key_name(button);
    return name ? name : "";
}

// NOT ORIGINAL grouping - the mirror of the game's menu: its hits, events, scroll and what was logged (the original's
// globals). Made on first use, never destroyed (menu_mirror()).
struct MenuMirror {
    mirror::Tracker mirror_{};
    std::vector<paint::Hit> mirror_hits_;
    std::vector<SDL_Event> mirror_events_;
    float mirror_scroll_ = 0.0F;
    // Set while the original's screens are those of a hand-off left to them entirely: the create modes' editors (CAS,
    // CAT, CAG, the park editor, graphics) and online - nothing restyled until ours is up again.
    const char* left_original_ = nullptr;
    std::string mirror_logged_;
};

MenuMirror& menu_mirror()
{
    static core::NoDestroy<MenuMirror> state;
    return state.get();
}

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
    if (!new_frontend_state().catalogue_ok_ || new_frontend_state().catalogue_.sections().empty() ||
        (current_load_state() == 0U) || loading_screen_up()) {
        return false;
    }
    if (!game_screens::get_find(name_hash("level_select_anchor")) || !game_screens::get_find(name_hash("level_menu"))) {
        return false;
    }
    if (game_screens::get_find(name_hash("created_park_menu")) ||
        game_screens::get_find(name_hash("premade_park_menu"))) {
        return false;
    }
    if (script_integer("in_server_options") != 1 && !game_screens::net_session()) {
        return false;
    }
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
    policy.editor = menu_mirror().left_original_ != nullptr || mode == "parkeditor" || mode == "creategoals" ||
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
        g_policy_frames = kPolicyFrames;
        mirror::set_button_namer(pad_button_label_for);
    }
    // (online: restyled too unless [frontend] restyle_online=0 - tested by the user by hand, never by a tool)
    if ((current_load_state() == 0U) || loading_screen_up() || (game_screens::net_session() && !policy.online) ||
        level_editor::live_now()) {
        return {};
    }
    // (a network game's level select: ours, never restyled as a list)
    if (net_level_select_up()) {
        return {};
    }
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
        if (s_coming_frames <= kComingAtMost) {
            menu = coming;
        }
    } else {
        s_coming = nullptr;
    }
    if (!in_dialog && stats_screen_up(menu)) {
        // (the stats screen - its menus, or the story's stat being changed with none of them taking the pad)
        const mirror::View stats = mirror::get_build(read_stats(menu), policy);
        if (menu_mirror().mirror_logged_ != "stats") {
            menu_mirror().mirror_logged_ = "stats";
            get_log("new front end: restyled - the stats screen \"%s\", %zu stats", stats.title.c_str(), stats.rows.size());
        }
        return stats;
    }
    if (!menu) {
        return {};
    }
    // A create mode's editor chosen on the restyled CREATE-A-MODES (its item selected when that menu went): its menus
    // are left as they are - until the main menu or CREATE-A-MODES is back.
    static uint32_t s_last_menu = 0;
    static uint32_t s_last_item = 0;
    static const char* const kEditor = "a create mode's editor";
    const uint32_t menu_id = id_of_element(menu);
    const bool create_modes = game_screens::get_find(name_hash("menu_cas")) != nullptr;
    if (menu_mirror().left_original_ == kEditor && (menu_id == name_hash("main_vmenu") || create_modes)) {
        menu_mirror().left_original_ = nullptr;
    }
    if (!menu_mirror().left_original_ && menu_id != s_last_menu && s_last_menu == name_hash("main_menu") && !create_modes &&
        menu_id != name_hash("main_vmenu")) { // (Back to the main menu isn't an editor)
        for (const char* editor : {"menu_cas", "menu_cat", "menu_creategoals", "menu_parked", "menu_cagr"}) {
            if (s_last_item == name_hash(editor)) {
                get_log("new front end: %s - its menus left as they are", editor);
                menu_mirror().left_original_ = kEditor;
            }
        }
    }
    s_last_menu = menu_id;
    {
        const int selected = mouse_menus::selected_index(menu);
        const UiWidget* item = first_child(menu);
        for (int i = 0; item && i < selected; ++i) {
            item = get_next(item);
        }
        s_last_item = item && selected >= 0 ? id_of_element(item) : 0;
    }
    mirror::Policy now = policy;
    now.editor = now.editor || menu_mirror().left_original_ != nullptr;
    const UiWidget* keyboard = in_dialog ? nullptr : keyboard_of(menu);
    mirror::View view = mirror::get_build(keyboard ? read_keyboard(menu) : read_menu(menu, in_dialog), now);
    // (each menu's decision logged once)
    std::array<char, kDecisionKeySize> key{};
    text::format_to(key, "%08X %s %s", id_of_element(menu), mirror::kind_name(view.kind), view.why.c_str());
    if (menu_mirror().mirror_logged_ != key.data()) {
        menu_mirror().mirror_logged_ = key.data();
        if (view.kind != mirror::Kind::kNone) {
            get_log("new front end: restyled - the original's %s (%08X) \"%s\", %zu rows", mirror::kind_name(view.kind),
                    id_of_element(menu), view.title.c_str(), view.rows.size());
        } else {
            get_log("new front end: the original's menu %08X left as it is: %s", id_of_element(menu),
                    view.why.empty() ? "not restyled" : view.why.c_str());
        }
    }
    if (view.kind == mirror::Kind::kNone) {
        dump_left_menu(menu, view.why);
    }
    return view;
}

// The mouse over the restyled screen: the original menu's selection stepped to the row (its own pad events), a click
// chooses it, the right button / Back is pad_back, the wheel steps.
struct StepContext {
    modules::FrontEnd* front_end_;
    bool horizontal;
};
int read_selection(void* /*unused*/)
{
    const UiWidget* menu = active_menu();
    return menu ? mouse_menus::selected_index(menu) : -1;
}
void send_step(void* context, int direction)
{
    auto* step = static_cast<StepContext*>(context);
    const uint32_t sideways_event = direction < 0 ? modules::FrontEnd::kPadLeft : modules::FrontEnd::kPadRight;
    const uint32_t vertical_event = direction < 0 ? modules::FrontEnd::kPadUp : modules::FrontEnd::kPadDown;
    const uint32_t event = step->horizontal ? sideways_event : vertical_event;
    mouse_menus::send_menu_pad_event(step->front_end_, event);
}
bool step_to(modules::FrontEnd* front_end, int row)
{
    const mirror::View& v = menu_mirror().mirror_.get_shown();
    const UiWidget* menu = active_menu();
    if (!menu || row < 0 || std::cmp_greater_equal(row, v.rows.size())) {
        return false;
    }
    const int target = core::at(v.rows, row).index;
    if (target < 0) {
        return false; // (a row of a menu not taking the pad: the story's other stats column)
    }
    StepContext context{.front_end_ = front_end, .horizontal = v.horizontal};
    mouse_menus::step_selection(target, mouse_menus::child_count(menu), mouse_menus::menu_wraps(menu), read_selection,
                                send_step, &context);
    return read_selection(nullptr) == target;
}

// A key of the on-screen keyboard: its row and its column.
struct KeyPlace {
    int row;
    int col;
};

// The keyboard's key `key` selected with the game's own pad events (each step read back), then chosen.
void choose_key(modules::FrontEnd* front_end, KeyPlace key)
{
    const int row = key.row;
    const int col = key.col;
    for (int guard = 0; guard < kMaxParentDepth; ++guard) {
        UiWidget* active = active_menu();
        const UiWidget* keys = keyboard_of(active);
        if (!keys) {
            return;
        }
        const mirror::MenuRead now = read_keyboard(active);
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
    const mirror::View& v = menu_mirror().mirror_.get_shown();
    if (row < 0 || std::cmp_greater_equal(row, v.rows.size())) {
        return false;
    }
    if (row == v.selected) {
        return true;
    }
    if (v.stats_editing_) {
        return false;
    }
    const mirror::Row& row_ref = core::at(v.rows, row);
    if (row_ref.index >= 0) {
        return step_to(front_end, row);
    }
    if (v.selected >= 0) {
        mouse_menus::send_menu_pad_event(front_end, row_ref.stat_column_ < core::at(v.rows, v.selected).stat_column_
                                                        ? modules::FrontEnd::kPadLeft
                                                        : modules::FrontEnd::kPadRight);
    }
    return false;
}

// A click on the stats screen: a row's name selects it (the story's selected row: chosen - a stat to change, or done);
// its blocks select it and step its value - under the value Left, past it Right (the story's row is chosen first: its
// Left / Right change the stat only while it is being changed).
void stats_click(modules::FrontEnd* front_end, const paint::Hit& hit)
{
    const mirror::View& v = menu_mirror().mirror_.get_shown();
    const bool story = v.stat_columns_ > 1;
    if (hit.kind == paint::Hit::Kind::kItem) {
        const bool was_selected = hit.index == v.selected;
        if (step_to_stat(front_end, hit.index) && was_selected && story) {
            mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
        }
        return;
    }
    if (hit.kind != paint::Hit::Kind::kChoice || !step_to_stat(front_end, hit.index / kChoicesPerRow)) {
        return;
    }
    if (story && !v.stats_editing_) {
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadChoose);
    }
    mouse_menus::send_menu_pad_event(front_end, hit.index % kChoicesPerRow == 0 ? modules::FrontEnd::kPadLeft
                                                                    : modules::FrontEnd::kPadRight);
}

void mirror_mouse(modules::FrontEnd* front_end)
{
    std::vector<SDL_Event> events;
    events.swap(menu_mirror().mirror_events_);
    if (!menu_mirror().mirror_.get_up()) {
        return;
    }
    int w = 0;
    int height = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowSize(window, &w, &height);
    }
    TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(height));
    auto hit_at = [&frame](float wx, float wy) -> const paint::Hit* {
        const float x = frame.design_x(wx);
        const float y = frame.design_y(wy);
        for (const paint::Hit& hit : menu_mirror().mirror_hits_) {
            if (get_holds(hit, x, y)) {
                return &hit;
            }
        }
        return nullptr;
    };
    const bool skaters = menu_mirror().mirror_.get_shown().kind == mirror::Kind::kSkaters;
    const bool stats = menu_mirror().mirror_.get_shown().kind == mirror::Kind::kStats;
    for (const SDL_Event& widget : events) {
        switch (widget.type) {
        case SDL_MOUSEMOTION:
            if (stats) {
                if (const paint::Hit* hit =
                        hit_at(static_cast<float>(widget.motion.x), static_cast<float>(widget.motion.y))) {
                    const int choice_row = hit->kind == paint::Hit::Kind::kChoice ? hit->index / 16 : -1;
                    const int row = hit->kind == paint::Hit::Kind::kItem ? hit->index : choice_row;
                    if (row >= 0 && row != menu_mirror().mirror_.get_shown().selected) {
                        step_to_stat(front_end, row);
                    }
                }
                break;
            }
            // (the skater select: each step loads a skater - only a click selects there)
            if (!skaters) {
                if (const paint::Hit* hit =
                        hit_at(static_cast<float>(widget.motion.x), static_cast<float>(widget.motion.y))) {
                    if (hit->kind == paint::Hit::Kind::kItem &&
                        hit->index != menu_mirror().mirror_.get_shown().selected) {
                        step_to(front_end, hit->index);
                    }
                }
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (widget.button.button == SDL_BUTTON_RIGHT) {
                mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadBack);
            } else if (widget.button.button == SDL_BUTTON_LEFT) {
                const paint::Hit* hit =
                    hit_at(static_cast<float>(widget.button.x), static_cast<float>(widget.button.y));
                if (!hit) {
                    break;
                }
                if (hit->kind == paint::Hit::Kind::kBack) {
                    mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadBack);
                } else if (stats) {
                    stats_click(front_end, *hit);
                } else if (hit->kind == paint::Hit::Kind::kStageMap &&
                           menu_mirror().mirror_.get_shown().kind == mirror::Kind::kKeyboard) {
                    choose_key(front_end, {.row = hit->index / kKeysPerRow, .col = hit->index % kKeysPerRow});
                } else if (hit->kind == paint::Hit::Kind::kContinue) {
                    continue_last_level(front_end);
                } else if (hit->kind == paint::Hit::Kind::kItem) {
                    const bool was_selected = hit->index == menu_mirror().mirror_.get_shown().selected;
                    if (step_to(front_end, hit->index) && (!skaters || was_selected)) {
                        // (a row whose value Left / Right change - a slider, an option: its sides do that, as the
                        // game's own mouse control sends them - mouse_menus.cpp option_side; the middle chooses)
                        const mirror::View& shown = menu_mirror().mirror_.get_shown();
                        const float click_x = frame.design_x(static_cast<float>(widget.button.x));
                        const float width = hit->x1 - hit->x0;
                        int side = 0;
                        if (shown.kind == mirror::Kind::kLinkChain && hit->index >= 0 &&
                            std::cmp_less(hit->index, shown.rows.size()) &&
                            core::at(shown.rows, hit->index).adjustable) {
                            if (click_x < hit->x0 + (width * kEdgeFraction)) {
                                side = -1;
                            } else if (click_x >= hit->x1 - (width * kEdgeFraction)) {
                                side = 1;
                            }
                        }
                        uint32_t event = modules::FrontEnd::kPadChoose; // (an edge of a setting: its arrows)
                        if (side < 0) {
                            event = modules::FrontEnd::kPadLeft;
                        } else if (side > 0) {
                            event = modules::FrontEnd::kPadRight;
                        }
                        mouse_menus::send_menu_pad_event(front_end, event);
                    }
                }
                return; // (the screen may be another now)
            }
            break;
        case SDL_MOUSEWHEEL:
            if (widget.wheel.y != 0) {
                StepContext context{.front_end_ = front_end, .horizontal = menu_mirror().mirror_.get_shown().horizontal};
                send_step(&context, widget.wheel.y > 0 ? -1 : 1);
            }
            break;
        // (the main menu's Continue - ours: the game's menu has no such item; every other key the game's menu takes)
        case SDL_KEYDOWN:
            if (menu_mirror().mirror_.get_shown().main_menu_ && (widget.key.repeat == 0U) &&
                (widget.key.keysym.scancode == SDL_SCANCODE_X || widget.key.keysym.scancode == SDL_SCANCODE_R)) {
                continue_last_level(front_end);
                return;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (menu_mirror().mirror_.get_shown().main_menu_ && widget.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                continue_last_level(front_end);
                return;
            }
            break;
        default:
            break;
        }
    }
}

// NOT ORIGINAL grouping - the cover over the game's screen, the dialog under it, and the front end's settling and
// waiting (were globals). Made on first use, never destroyed (screen_cover()).
struct ScreenCover {
    // The cover over a hand-off's end (Cover in new_frontend_model.h): what it draws meanwhile - ours (menu_state() as it was)
    // or the restyled screen last up (a copy: the tracker forgets it as it goes).
    Cover cover_{};
    mirror::Tracker cover_tracker_{};
    float cover_scroll_ = 0.0F;
    // A dialog box in the front end (QUIT?, the warnings): the restyled screen it came up over, drawn under it (dimmed by the
    // dialog's own ground) - the front end's 3D scene (the skate shop, the skater standing) is never seen through a dialog
    // (the user's report, 2026-11-02: Quit showed the skate shop).
    mirror::Tracker dialog_under_{};
    bool dialog_under_on_ = false;
    float dialog_under_scroll_ = 0.0F;
    bool mirror_would_restyle_ = false; // (this frame's decision: the menu up is one to restyle)
    // The original's elements held hidden though nothing is restyled yet: frames left while a menu that would be (or is
    // still being built - View::unsettled) went; frames a menu has stayed unsettled (shown as the game made it after
    // kFramesUnsettled).
    int hold_frames_ = 0;
    int unsettled_frames_ = 0;
    // The front end come up (loaded, the loading screen gone) and none of its menus restyled or left original yet: ours
    // draws its ground and the main menu's art meanwhile (Paint::Waiting) - the front end's 3D scene never shows bare.
    bool front_end_was_ = false;
    bool awaiting_front_end_ = false;
    float await_time_ = 0.0F;
};

ScreenCover& screen_cover()
{
    static core::NoDestroy<ScreenCover> state;
    return state.get();
}

constexpr float kAwaitAtMost = 8.0F; // (seconds: then the front end as it is)

void start_cover(Cover::Over over, const char* why)
{
    if (screen_cover().cover_.get_on() && screen_cover().cover_.get_drawn() == over) {
        return;
    }
    get_log("new front end: covering - %s (%s drawn until the next screen is ready)", why,
            over == Cover::Over::kOurs ? "ours" : "the restyled screen");
    screen_cover().cover_.kickoff(over);
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
    if (!up && new_frontend_state().open_screen_ == Screen::kLevels && new_frontend_state().open_levels_ == LevelsFor::kNet) {
        new_frontend_state().open_screen_ = Screen::kMain; // (it went before ours could come up)
        new_frontend_state().open_back_ = 0;
    }
    if (!came_up || new_frontend_state().state_ == State::kShown || new_frontend_state().state_ == State::kLaunching ||
        new_frontend_state().open_screen_ != Screen::kMain) {
        return;
    }
    const bool server_options = script_integer("in_server_options") == 1;
    new_frontend_state().open_screen_ = Screen::kLevels;
    new_frontend_state().open_levels_ = LevelsFor::kNet;
    new_frontend_state().open_back_ = name_hash(server_options ? "network_level_select_pad_back" : "level_select_menu_exit");
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
    const std::optional<LevelPlace> place = new_frontend_state().catalogue_.get_find(load);
    const StageMap* level =
        place ? &core::at(core::at(new_frontend_state().catalogue_.sections(), place->section).levels, place->level)
              : nullptr;
    change_phase(State::kAway);
    new_frontend_state().shown_back_ = 0;
    if (!level || !net_level_select_up()) {
        get_log("new front end: %s - the game's level select for it is gone; nothing chosen", load.c_str());
        return;
    }
    if (_stricmp(level->level_num_name_.c_str(), "LevelNum_Sk5ed") == 0) {
        get_log("new front end: the network game's level - the created park: the game's own menu for it");
        start_cover(Cover::Over::kOurs, "the created park's menu");
        script::spawn_script(name_hash("level_select_created_park_menu"), nullptr);
        return;
    }
    const bool server_options = script_integer("in_server_options") == 1;
    const char* script = server_options ? "level_select_menu_exit" : "level_select_change_level";
    get_log("new front end: the network game's level - %s (%s)", load.c_str(), script);
    gsl::owner<script::Struct*> params = script::new_struct();
    params->put_name_key(checksums::Name{name_hash("level")}, name_hash(load.c_str()));
    params->append_word(name_hash("text"), level->name.c_str());
    script::spawn_script(name_hash(script), params);
    params->get_clear();
    script::delete_struct(params);
    start_cover(Cover::Over::kOurs, "a level chosen for the network game");
}

// Once a frame while ours isn't drawn (before_screen_layout): the restyled screen up or down, the original's elements
// hidden under it.
void update_mirror(float dt, bool allowed)
{
    const mirror::View now = allowed ? mirror_now() : mirror::View{};
    screen_cover().mirror_would_restyle_ = now.kind != mirror::Kind::kNone;
    // (a menu that will be restyled, or one still being built: its elements hidden from the frame it is first seen -
    // the tracker brings ours up a frame or so later; meanwhile nothing of the menu is drawn, never the original)
    screen_cover().unsettled_frames_ = now.kind == mirror::Kind::kNone && now.unsettled ? screen_cover().unsettled_frames_ + 1 : 0;
    if (screen_cover().mirror_would_restyle_ ||
        (screen_cover().unsettled_frames_ > 0 && screen_cover().unsettled_frames_ <= mirror::kFramesUnsettled)) {
        screen_cover().hold_frames_ = mirror::kFramesToHide;
    } else if (screen_cover().hold_frames_ > 0) {
        --screen_cover().hold_frames_;
    }
    const bool was_up = menu_mirror().mirror_.get_up();
    mirror::Tracker before;
    if (was_up) {
        before = menu_mirror().mirror_;
    }
    const bool up = menu_mirror().mirror_.tick_frame(now, dt);
    if (up && !was_up) {
        get_log("new front end: restyling the original's %s", mirror::kind_name(menu_mirror().mirror_.get_shown().kind));
        menu_mirror().mirror_scroll_ = 0.0F;
    }
    if (!up && was_up) {
        get_log("new front end: the restyled screen gone");
        menu_mirror().mirror_hits_.clear();
        // (what comes next - ours, another restyled screen, an original one - may take a few frames: the restyled
        // screen stays drawn meanwhile, never the original under it)
        if (allowed && (current_load_state() != 0U) && !loading_screen_up()) {
            screen_cover().cover_tracker_ = before;
            screen_cover().cover_scroll_ = menu_mirror().mirror_scroll_;
            start_cover(Cover::Over::kRestyled, "the restyled screen went");
        }
    }
    if (up || screen_cover().hold_frames_ > 0) {
        hide_root(true);
    }
    // (the screen under a dialog: the restyled one it replaced; kept while the dialog - or the cover drawing it - is up)
    const bool dialog_now = up && menu_mirror().mirror_.get_shown().kind == mirror::Kind::kDialog;
    if (dialog_now && was_up && before.get_shown().kind != mirror::Kind::kDialog) {
        screen_cover().dialog_under_ = before;
        screen_cover().dialog_under_scroll_ = menu_mirror().mirror_scroll_;
        screen_cover().dialog_under_on_ = true;
    } else if (dialog_now && !was_up && screen_cover().cover_.get_drawn() == Cover::Over::kRestyled &&
               screen_cover().cover_tracker_.get_shown().kind != mirror::Kind::kDialog) {
        screen_cover().dialog_under_ = screen_cover().cover_tracker_;
        screen_cover().dialog_under_scroll_ = screen_cover().cover_scroll_;
        screen_cover().dialog_under_on_ = true;
    } else if ((up && !dialog_now) || (!up && screen_cover().cover_.get_drawn() != Cover::Over::kRestyled)) {
        screen_cover().dialog_under_on_ = false;
    }
}

// Once a frame while a cover is on (and ours isn't up): off once the screen the hand-off went to is ready. When it ends
// with nothing restyled, the original's elements come back the same frame (before_screen_layout).
void update_cover(float dt)
{
    if (!screen_cover().cover_.get_on()) {
        return;
    }
    bool in_dialog = false;
    const UiWidget* active = active_menu(&in_dialog);
    Cover::Now now;
    now.target_ready_ = menu_mirror().mirror_.get_up();
    now.gameplay = (current_load_state() == 0U) || loading_screen_up() || level_editor::live_now() ||
                   (!in_front_end() && !get_paused() && (active == nullptr));
    now.screen = active;
    now.left_original_ = (active != nullptr) && !screen_cover().mirror_would_restyle_;
    const char* why = nullptr;
    if (!screen_cover().cover_.tick_frame(now, dt, &why)) {
        get_log("new front end: cover off - %s (%.2f s)", why ? why : "?", screen_cover().cover_.get_instant());
    }
}

void free_device_objects();

// Ours up: the screen a menu's item asked for (NewFrontend_Open), on its root - Main in the front end, Pause in a level
// (never shown: back at it, ours goes aside). `anchor`: the original screen up under it (or None).
void get_show(Anchor anchor)
{
    load_catalogue();
    if (new_frontend_state().open_screen_ == Screen::kMain) {
        change_phase(State::kAway);
        return;
    }
    const Screen root = in_front_end() ? Screen::kMain : Screen::kFreeze;
    const std::vector<Screen> stack = {root, new_frontend_state().open_screen_};
    const LevelsFor levels = new_frontend_state().open_levels_;
    new_frontend_state().open_root_ = root;
    new_frontend_state().shown_back_ = new_frontend_state().open_back_;
    new_frontend_state().open_screen_ = Screen::kMain;
    new_frontend_state().open_back_ = 0;
    hide_root(true);
    new_frontend_state().anchor_ = anchor;
    menu_mirror().left_original_ = nullptr;
    if (screen_cover().cover_.get_on()) {
        get_log("new front end: cover off - ours up");
    }
    screen_cover().cover_.halt();
    if (menu_mirror().mirror_.get_up()) {
        get_log("new front end: the restyled screen gone (ours up)");
    }
    menu_mirror().mirror_.get_reset();
    menu_mirror().mirror_hits_.clear();
    menu_state().get_reset(&new_frontend_state().catalogue_, get_views(levels == LevelsFor::kNet), new_frontend_state().saved_.last_level_, stack, levels);
    load_option_values();
    new_frontend_state().skater_name_ = root == Screen::kFreeze ? std::string() : get_skater_name();
    new_frontend_state().hits_.clear();
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
            new_frontend_state().shown_back_ = 0;
            script::spawn_script(name_hash(script), nullptr);
            // (ours stays drawn until the game's options screen is up: no frame of the root menu behind - the user's
            // report, 2026-11-02)
            start_cover(Cover::Over::kOurs, "the game options");
            return;
        }
    }
    case ActionKind::kOption:
        apply_option(action);
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
            params->put_name_key(checksums::Name{name_hash("level")}, name_hash(action.load.c_str()));
            script::spawn_script(name_hash("level_select_change_level"), params);
            params->get_clear();
            script::delete_struct(params);
            new_frontend_state().launch_level_ = current_load_state();
            change_phase(State::kLaunching);
            return;
        }
        if (begin_level::read_in(action.load.c_str(), &why)) {
            new_frontend_state().saved_.last_level_ = action.load;
            menu_state().set_last_level(action.load);
            save_state();
            new_frontend_state().launch_level_ = current_load_state();
            if (action.levels == LevelsFor::kEdit) {
                edit_session().edit_ = Edit::kWaitLevel;
                edit_session().edit_time_ = 0.0F;
                edit_session().edit_frames_ = edit_session().edit_presses_ = 0;
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

void handle_queued(modules::FrontEnd* front_end, const SDL_Event& widget)
{
    int w = 0;
    int height = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowSize(window, &w, &height);
    }
    TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(height));
    auto hit_at = [&frame](float wx, float wy) -> const paint::Hit* {
        const float x = frame.design_x(wx);
        const float y = frame.design_y(wy);
        for (const paint::Hit& hit : new_frontend_state().hits_) {
            if (get_holds(hit, x, y)) {
                return &hit;
            }
        }
        return nullptr;
    };
    switch (widget.type) {
    case SDL_KEYDOWN: {
        const bool repeat = widget.key.repeat != 0;
        const bool shift = (widget.key.keysym.mod & KMOD_SHIFT) != 0;
        switch (widget.key.keysym.scancode) {
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
            if (!repeat) {
                run_action(front_end, menu_state().get_press(Intake::kConfirm));
            }
            break;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE:
            if (!repeat) {
                run_action(front_end, menu_state().get_press(Intake::kBack));
            }
            break;
        case SDL_SCANCODE_Q:
            // (on the level select Q / E change the picture; the tabs keep Page Up / Down, 1 / 2, Tab and the shoulders)
            if (menu_state().get_shown() == Screen::kLevels) {
                cycle_level_art(-1);
            } else {
                run_action(front_end, menu_state().get_press(Intake::kPrevTab));
            }
            break;
        case SDL_SCANCODE_PAGEUP:
        case SDL_SCANCODE_1:
            run_action(front_end, menu_state().get_press(Intake::kPrevTab));
            break;
        case SDL_SCANCODE_E:
            if (menu_state().get_shown() == Screen::kLevels) {
                cycle_level_art(1);
            } else {
                run_action(front_end, menu_state().get_press(Intake::kNextTab));
            }
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
        switch (widget.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            press_direction(front_end, Intake::kUp);
            stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kUp), .wait = kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            press_direction(front_end, Intake::kDown);
            stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kDown), .wait = kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            press_direction(front_end, Intake::kLeft);
            stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kLeft), .wait = kRepeatDelay};
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            press_direction(front_end, Intake::kRight);
            stick_repeat().repeat_ = {.direction = static_cast<int>(Intake::kRight), .wait = kRepeatDelay};
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
        if (widget.cbutton.button >= SDL_CONTROLLER_BUTTON_DPAD_UP &&
            widget.cbutton.button <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            stick_repeat().repeat_ = {};
        }
        return;
    case SDL_CONTROLLERAXISMOTION: {
        // The left stick as a d-pad: past half way (back inside a third: let go).
        int* axis = nullptr;
        if (widget.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            axis = &stick_repeat().stick_x_;
        } else if (widget.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            axis = &stick_repeat().stick_y_;
        }
        if (!axis) {
            return;
        }
        const int value = widget.caxis.value;
        const int settled = std::abs(value) < 11000 ? 0 : *axis; // (between the two: as it was)
        const int pushed_back = value < -16000 ? -1 : settled;
        const int now = value > 16000 ? 1 : pushed_back;
        if (now == *axis) {
            return;
        }
        *axis = now;
        if (now == 0) {
            stick_repeat().repeat_ = {};
            return;
        }
        const Intake sideways = now > 0 ? Intake::kRight : Intake::kLeft;
        const Intake vertical = now > 0 ? Intake::kDown : Intake::kUp;
        const Intake input = axis == &stick_repeat().stick_x_ ? sideways : vertical;
        press_direction(front_end, input);
        stick_repeat().repeat_ = {.direction = static_cast<int>(input), .wait = kRepeatDelay};
        return;
    }
    case SDL_MOUSEMOTION:
        if (const paint::Hit* hit = hit_at(static_cast<float>(widget.motion.x), static_cast<float>(widget.motion.y))) {
            if (hit->kind == paint::Hit::Kind::kItem) {
                run_action(front_end, menu_state().hover_item(hit->index));
            } else if (hit->kind == paint::Hit::Kind::kStageMap) {
                run_action(front_end, menu_state().hover_level(hit->index));
            }
        }
        return;
    case SDL_MOUSEBUTTONDOWN:
        if (widget.button.button == SDL_BUTTON_RIGHT) {
            run_action(front_end, menu_state().get_press(Intake::kBack));
            return;
        }
        if (widget.button.button != SDL_BUTTON_LEFT) {
            return;
        }
        if (const paint::Hit* hit = hit_at(static_cast<float>(widget.button.x), static_cast<float>(widget.button.y))) {
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
                run_action(front_end, menu_state().click_choice({.row = clicked.index / kChoicesPerRow, .choice = clicked.index % kChoicesPerRow}));
                break;
            }
        }
        return;
    case SDL_MOUSEWHEEL:
        if (widget.wheel.y != 0) {
            run_action(front_end, menu_state().get_wheel(widget.wheel.y));
        }
        return;
    default:
        return;
    }
}

void handle_event(SDL_Event* event)
{
    if (!is_switched_on()) {
        return;
    }
    switch (event->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        ++new_frontend_state().buttons_down_;
        break;
    case SDL_CONTROLLERBUTTONUP:
        new_frontend_state().buttons_down_ = std::max(0, new_frontend_state().buttons_down_ - 1);
        break;
    default:
        break;
    }
    if (new_frontend_state().state_ != State::kShown) {
        // (a restyled original screen: the mouse is ours - its keys and buttons go on to the original menu)
        if (menu_mirror().mirror_.get_up() && menu_mirror().mirror_events_.size() < kMaxMirrorEvents &&
            (event->type == SDL_MOUSEMOTION || event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL ||
             event->type == SDL_KEYDOWN || event->type == SDL_CONTROLLERBUTTONDOWN)) {
            menu_mirror().mirror_events_.push_back(*event);
        }
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
        if (new_frontend_state().events_.size() < kMaxQueuedEvents) {
            new_frontend_state().events_.push_back(*event);
        }
        break;
    default:
        break;
    }
}

// --- the Level Editor from the Create screen ---

void edit_update(modules::FrontEnd* front_end, float dt)
{
    if (edit_session().edit_ == Edit::kDisengaged) {
        return;
    }
    edit_session().edit_time_ += dt;
    if (edit_session().edit_time_ > kEditOpenTimeoutSeconds) {
        get_log("new front end: the level editor didn't open in 60 s - free skate as it is");
        edit_session().edit_ = Edit::kDisengaged;
        return;
    }
    const bool in_level = (current_load_state() != 0U) && !in_front_end() &&
                          (objects::find_object(kSkater) != nullptr) && !loading_screen_up();
    if (!in_level) {
        edit_session().edit_frames_ = 0;
        return;
    }
    if (edit_session().edit_ == Edit::kWaitLevel) {
        edit_session().edit_frames_ = get_paused() ? 0 : edit_session().edit_frames_ + 1;
        if (edit_session().edit_frames_ < kEditSettleFrames) {
            return;
        }
        // Start, as the player presses it: handle_start_pressed pauses and makes the pause menu.
        mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++edit_session().edit_presses_;
        edit_session().edit_ = Edit::kWaitPause;
        edit_session().edit_frames_ = 0;
        return;
    }
    // (kWaitPause) the pause menu up with its LEVEL EDITOR: chosen.
    if (!get_paused()) {
        if (++edit_session().edit_frames_ >= kEditPauseMenuFrames && edit_session().edit_presses_ < 3) {
            mouse_menus::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
            ++edit_session().edit_presses_;
            edit_session().edit_frames_ = 0;
        }
        return;
    }
    if (++edit_session().edit_frames_ < kEditChooseFrames) {
        return;
    }
    edit_session().edit_ = Edit::kDisengaged;
    if (UiWidget* item = game_screens::get_find(name_hash("menu_level_editor"))) {
        get_log("new front end: the pause menu's LEVEL EDITOR chosen");
        game_screens::launch_pad(front_end, modules::FrontEnd::kPadChoose, item);
    } else {
        get_log("new front end: no LEVEL EDITOR in the pause menu (refused here?) - the pause menu as it is");
    }
}

// --- the ImGui context ---

// NOT ORIGINAL grouping - the drawing context and the fonts, and whether making them failed (were globals). Made on
// first use, never destroyed (draw_context()).
struct DrawContext {
    ImGuiContext* context_ = nullptr;
    bool context_failed_ = false;
    paint::FontFiles fonts_{};
    bool fonts_resolved_ = false;
};

DrawContext& draw_context()
{
    static core::NoDestroy<DrawContext> state;
    return state.get();
}

bool begin_context(ImGuiContext** previous)
{
    *previous = ImGui::GetCurrentContext();
    if (!draw_context().context_) {
        if (draw_context().context_failed_) {
            return false;
        }
        draw_context().context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(draw_context().context_);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        // (the cursor is the game's - mouse_menus shows it in the front end; ImGui leaves it alone)
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        if (!ui_backend::prepare_all()) {
            ImGui::DestroyContext(draw_context().context_);
            draw_context().context_ = nullptr;
            draw_context().context_failed_ = true;
            ImGui::SetCurrentContext(*previous);
            get_log("new front end: ImGui couldn't start on the game's window / device - not drawn");
            return false;
        }
    }
    ImGui::SetCurrentContext(draw_context().context_);
    return true;
}

void end_context(ImGuiContext* previous) { ImGui::SetCurrentContext(previous); }

void free_device_objects()
{
    free_art();
    if (!draw_context().context_) {
        return;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(draw_context().context_);
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
Awaiting& stored_pending()
{
    static core::NoDestroy<Awaiting> pending;
    return pending.get();
}

bool shown_and_idle(Screen screen)
{
    return new_frontend_state().state_ == State::kShown && !menu_state().occupied() && menu_state().get_shown() == screen;
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
    if (!is_switched_on()) {
        return;
    }
    os_layer::register_event_handler(handle_event);
    get_log("new front end: on ([features] new_frontend=1)");
}

bool showing() { return new_frontend_state().state_ == State::kShown || new_frontend_state().state_ == State::kLaunching; }

// (while a cover is on, the screen under it is hidden: no presses reach it blind - a few frames, 3 s at most)
bool blocks_game_input() { return is_switched_on() && (showing() || new_frontend_state().guard_ || screen_cover().cover_.get_on()); }

void tick_frame(modules::FrontEnd* front_end)
{
    if (!is_switched_on()) {
        return;
    }
    auto now = std::chrono::steady_clock::now();
    float dt = menu_tick().ticked_ ? std::chrono::duration<float>(now - menu_tick().last_tick_).count() : 0.0F;
    menu_tick().last_tick_ = now;
    menu_tick().ticked_ = true;
    dt = std::clamp(dt, 0.0F, kMaxFrameSeconds);
    new_frontend_state().state_time_ += dt;
    load_catalogue();
    note_level_loaded();
    update_session(dt);
    edit_update(front_end, dt);

    if (new_frontend_state().guard_) {
        new_frontend_state().guard_time_ += dt;
        const bool held = get_key_down(SDL_SCANCODE_RETURN) || get_key_down(SDL_SCANCODE_KP_ENTER) ||
                          get_key_down(SDL_SCANCODE_SPACE) || get_key_down(SDL_SCANCODE_ESCAPE) ||
                          get_key_down(SDL_SCANCODE_BACKSPACE) || new_frontend_state().buttons_down_ > 0 ||
                          ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0U);
        if (!held || new_frontend_state().guard_time_ > kGuardSeconds) {
            new_frontend_state().guard_ = false;
        }
    }

    // A tool's own UI has the screen (the level editor - its Dear ImGui screens, or its old HUD of screen elements):
    // ours steps aside completely - not drawn, nothing restyled, nothing covered, the original's elements as the tool
    // left them - and comes back only once the pause menu really is back (the user's report of 2026-10-25: our pause
    // menu stayed drawn over the level editor - its hand-off never finished, as this isn't called while the editor
    // runs... mouse_menus::tick_frame calls it then too, for this).
    if (level_editor::live_now()) {
        if (new_frontend_state().state_ != State::kIdle && new_frontend_state().state_ != State::kAway) {
            get_log("new front end: the level editor is up - ours aside");
            change_phase(State::kAway);
        }
        if (screen_cover().cover_.get_on()) {
            get_log("new front end: cover off - the level editor is up");
        }
        screen_cover().cover_.halt();
        if (menu_mirror().mirror_.get_up()) {
            menu_mirror().mirror_.get_reset();
            menu_mirror().mirror_hits_.clear();
        }
        new_frontend_state().events_.clear();
        menu_mirror().mirror_events_.clear();
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
    const bool pause_active = (active == nullptr) || id_of_element(active) == name_hash("pause_vmenu");
    (void)pause_active;
    // (every menu is the game's, restyled - ours only for a screen a menu's item asked for, while that is up)
    const bool usable =
        (current_load_state() != 0U) && !loading_screen_up() && !in_dialog &&
        edit_session().edit_ == Edit::kDisengaged &&
        (new_frontend_state().open_screen_ != Screen::kMain || new_frontend_state().state_ == State::kShown);

    switch (new_frontend_state().state_) {
    case State::kIdle:
    case State::kAway:
        if ((current_load_state() == 0U) || (!in_front_end() && anchor == Anchor::kNone &&
                                             new_frontend_state().state_ == State::kAway && !get_paused())) {
            if (new_frontend_state().state_ != State::kIdle) {
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
        if (menu_mirror().mirror_.get_up()) {
            mirror_mouse(front_end);
        } else {
            menu_mirror().mirror_events_.clear();
        }
        keep_root_hidden();
        return;
    case State::kLaunching:
        menu_state().tick(dt);
        if ((new_frontend_state().launch_level_ == kLoadMainMenu && !in_front_end()) ||
            (new_frontend_state().launch_level_ != kLoadMainMenu && current_load_state() != new_frontend_state().launch_level_) ||
            (new_frontend_state().launch_level_ != kLoadMainMenu && loading_screen_up())) {
            change_phase(State::kIdle);
            free_device_objects();
        } else if (new_frontend_state().state_time_ > kLevelStartTimeoutSeconds) {
            get_log("new front end: the level didn't start in 30 seconds - back to the menus");
            menu_state().cancel_launch();
            change_phase(State::kIdle);
        }
        return;
    case State::kShown:
        break;
    }
    keep_root_hidden();
    if (menu_state().get_shown() == new_frontend_state().open_root_ && !menu_state().occupied()) {
        // (back from the screen a menu's item asked for: that menu - the game's, restyled - again, or the script it
        // gave for Back when its menu went - the skate shop's)
        get_log("new front end: back from our screen%s",
                (new_frontend_state().shown_back_ != 0U) ? " - its back script" : "");
        change_phase(State::kAway);
        if (menu_state().levels_purpose() == LevelsFor::kNet) { // (the game's select under ours until its script goes)
            start_cover(Cover::Over::kOurs, "back from a network game's level select");
        }
        if (new_frontend_state().shown_back_ != 0U) {
            script::spawn_script(new_frontend_state().shown_back_, nullptr);
        }
        new_frontend_state().shown_back_ = 0;
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
            if ((current_load_state() != 0U) && !loading_screen_up()) {
                start_cover(Cover::Over::kOurs, "the original's screen changed under ours");
            }
        }
        return;
    }
    std::vector<SDL_Event> events;
    events.swap(new_frontend_state().events_);
    for (const SDL_Event& widget : events) {
        handle_queued(front_end, widget);
        if (new_frontend_state().state_ != State::kShown) {
            return;
        }
    }
    if (stick_repeat().repeat_.direction >= 0) {
        stick_repeat().repeat_.wait -= dt;
        if (stick_repeat().repeat_.wait <= 0.0F) {
            stick_repeat().repeat_.wait += kRepeatEvery;
            press_direction(front_end, static_cast<Intake>(stick_repeat().repeat_.direction));
        }
    }
    // The automation channel's command waiting for its screen.
    if (stored_pending().active && shown_and_idle(stored_pending().screen)) {
        stored_pending().active = false;
        if (menu_state().select_id(stored_pending().item)) {
            get_log("new front end: automation - %s/%s %s", get_screen_name(stored_pending().screen), stored_pending().item.c_str(),
                    stored_pending().confirm ? "chosen" : "selected");
            stored_pending().result = "ok";
            if (stored_pending().confirm) {
                run_action(front_end, menu_state().get_press(Intake::kConfirm));
            }
        } else {
            stored_pending().result = "error: no item " + stored_pending().item + " on " + get_screen_name(stored_pending().screen);
            get_log("new front end: automation - %s", stored_pending().result.c_str());
        }
    }
    if (new_frontend_state().state_ == State::kShown) {
        run_action(front_end, menu_state().tick(dt));
    }
}

bool owns_mouse() { return is_switched_on() && !showing() && menu_mirror().mirror_.get_up(); }

void before_screen_layout()
{
    // (the level editor: Update steps ours aside and gives the original's elements back)
    if (!is_switched_on() || level_editor::live_now()) {
        return;
    }
    static std::chrono::steady_clock::time_point s_last;
    static bool s_any = false;
    auto clock = std::chrono::steady_clock::now();
    const float dt = s_any ? std::clamp(std::chrono::duration<float>(clock - s_last).count(), 0.0F, 0.25F) : 0.0F;
    s_last = clock;
    s_any = true;
    const bool front_end = in_front_end() && !loading_screen_up();
    if (front_end && !screen_cover().front_end_was_) {
        screen_cover().awaiting_front_end_ = true;
        screen_cover().await_time_ = 0.0F;
    }
    screen_cover().front_end_was_ = front_end;
    if (new_frontend_state().state_ == State::kLaunching) {
        return; // (a level starting: the original's elements as the launch left them)
    }
    watch_net_level_select();
    if (new_frontend_state().state_ == State::kIdle || new_frontend_state().state_ == State::kAway) {
        update_mirror(dt, true);
        update_cover(dt);
    }
    if (screen_cover().awaiting_front_end_) {
        screen_cover().await_time_ += dt;
        const bool left_original =
            (active_menu() != nullptr) && !screen_cover().mirror_would_restyle_ && screen_cover().hold_frames_ == 0;
        if (!front_end || menu_mirror().mirror_.get_up() || showing() || left_original || screen_cover().await_time_ > kAwaitAtMost) {
            const char* first_screen = "none";
            if (menu_mirror().mirror_.get_up() || showing()) {
                first_screen = "ours";
            } else if (left_original) {
                first_screen = "an original screen";
            }
            get_log("new front end: the front end's first screen is up after %.2f s (%s)", screen_cover().await_time_,
                    first_screen);
            screen_cover().awaiting_front_end_ = false;
        }
    }
    // Decided here, before the screen elements are laid out for this frame's drawing: whatever the scripts made since
    // the last frame (the pause menu, a dialog, the main menu) is hidden before it is ever drawn.
    const bool net_levels_asked = new_frontend_state().open_screen_ == Screen::kLevels && new_frontend_state().open_levels_ == LevelsFor::kNet;
    if (new_frontend_state().state_ == State::kShown || menu_mirror().mirror_.get_up() || screen_cover().cover_.get_on() || screen_cover().hold_frames_ > 0 ||
        screen_cover().awaiting_front_end_ || net_levels_asked) {
        hide_root(true);
        keep_root_hidden(); // (a script that scaled root_window back since - create_pause_menu's unhide_root_window)
    } else if (new_frontend_state().root_hidden_) {
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
    if (new_frontend_state().saved_.last_level_.empty() || !new_frontend_state().catalogue_ok_) {
        return {};
    }
    const std::optional<LevelPlace> place = new_frontend_state().catalogue_.get_find(new_frontend_state().saved_.last_level_);
    if (!place) {
        return {};
    }
    return core::at(core::at(new_frontend_state().catalogue_.sections(), place->section).levels, place->level).name;
}

// Continue (the restyled main menu's X / R, or its hint clicked): the last level played, in free skate.
void continue_last_level(modules::FrontEnd* front_end)
{
    if (continue_level_name().empty()) {
        return;
    }
    get_sound("generic_menu_pad_choose_sound");
    ActStep skate{.kind = ActionKind::kSkate};
    skate.levels = LevelsFor::kFreeSkate;
    skate.load = new_frontend_state().saved_.last_level_;
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
    extras.catalogue = new_frontend_state().catalogue_ok_ ? &new_frontend_state().catalogue_ : nullptr;
    extras.continue_name_ = continue_level_name();
    return extras;
}

// --- the script commands (the overlay's menu items) ---

// NewFrontend_Item item=on | restart | levels | options | editor: whether the overlay's menus make that item of ours -
// on: the new front end's main menu (its items, our Career menu); RESTART (free skate's pause menu); CHANGE LEVEL /
// the skate shop's Play Level opening our level select (free skate, with the levels' data); OPTIONS (ours); LEVEL
// EDITOR (Create-a-modes: [features] level_editor and its scripts). False while the new front end is off, and in
// network games: the menus as they were.
bool cfunc_item(script::Struct* params, script::Script* /*script*/)
{
    if (!is_switched_on() || game_screens::net_session()) {
        return false;
    }
    uint32_t item = 0;
    if (params) {
        params->read_hash_value(name_hash("item"), &item, 0);
    }
    const std::string mode = game_screens::active_ruleset_label();
    const bool free_skate = mode == "freeskate" || mode == "singlesession";
    const bool levels = new_frontend_state().catalogue_ok_ && !new_frontend_state().catalogue_.sections().empty();
    if (item == name_hash("on")) {
        return true;
    }
    if (item == name_hash("restart")) {
        return !in_front_end() && free_skate;
    }
    if (item == name_hash("levels")) {
        return free_skate && levels;
    }
    if (item == name_hash("options")) {
        return true;
    }
    if (item == name_hash("editor")) {
        return in_front_end() && levels && level_editor::is_switched_on() && is_script(name_hash("level_editor_enter"));
    }
    return false;
}

// NewFrontend_Open screen=levels | editlevels | options [back=<script>]: our screen (the menu's item chose it: its own
// sound played) - shown the next frame over whatever is up; closed with Back to the restyled menu it came from, or
// `back` spawned (its menu went: the skate shop's).
bool cfunc_show_new_frontend(script::Struct* params, script::Script* /*script*/)
{
    uint32_t screen = 0;
    uint32_t back = 0;
    if (!is_switched_on() || !params || !params->read_hash_value(name_hash("screen"), &screen, 0)) {
        return false;
    }
    params->read_hash_value(name_hash("back"), &back, 0);
    const bool levels = new_frontend_state().catalogue_ok_ && !new_frontend_state().catalogue_.sections().empty();
    if (screen == name_hash("levels") && levels) {
        new_frontend_state().open_screen_ = Screen::kLevels;
        new_frontend_state().open_levels_ = in_front_end() ? LevelsFor::kFreeSkate : LevelsFor::kFreeze;
    } else if (screen == name_hash("editlevels") && levels) {
        new_frontend_state().open_screen_ = Screen::kLevels;
        new_frontend_state().open_levels_ = LevelsFor::kEdit;
    } else if (screen == name_hash("options")) {
        new_frontend_state().open_screen_ = Screen::kOptions;
    } else {
        return false;
    }
    new_frontend_state().open_back_ = back;
    get_log("new front end: %s asked for%s", get_screen_name(new_frontend_state().open_screen_),
            (back != 0U) ? " (with a back script)" : "");
    return true;
}
} // namespace

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "NewFrontend_Item", .function = &cfunc_item},
        {.name = "NewFrontend_Open", .function = &cfunc_show_new_frontend},
    });
    script::install_script_functions(kFunctions);
}

void blit()
{
    // (never over a tool's own UI: the level editor - Update steps ours aside)
    if (!is_switched_on() || level_editor::live_now() ||
        (!showing() && !menu_mirror().mirror_.get_up() && !screen_cover().cover_.get_on() &&
         !screen_cover().awaiting_front_end_)) {
        return;
    }
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous)) {
        return;
    }
    int w = 0;
    int height = 0;
    ui_backend::window_size(&w, &height);
    if (w > 0 && height > 0) {
        const TickCycle frame = fit_frame(static_cast<float>(w), static_cast<float>(height));
        const float scale = std::round(frame.scale() * 100.0F) / 100.0F;
        if (!draw_context().fonts_resolved_) {
            draw_context().fonts_ = resolve_fonts();
            draw_context().fonts_resolved_ = true;
        }
        if (scale != paint::built_scale()) {
            paint::build_fonts(draw_context().fonts_, scale);
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
        if (!showing() && !menu_mirror().mirror_.get_up() && !screen_cover().cover_.get_on()) {
            // (the front end come up, its first menu not yet: its ground and the main menu's art)
            paint::get_waiting(frame, static_cast<float>(w), static_cast<float>(height), restyle_extras(), "career");
            ImGui::Render();
            ui_backend::render();
            end_context(previous);
            return;
        }
        if (!showing() && (menu_mirror().mirror_.get_up() || screen_cover().cover_.get_drawn() == Cover::Over::kRestyled)) {
            // (an original screen restyled - phase 3; or, under a cover, the one last up)
            const paint::Extras pause = restyle_extras();
            const mirror::Tracker& drawn = menu_mirror().mirror_.get_up() ? menu_mirror().mirror_ : screen_cover().cover_tracker_;
            if (drawn.get_shown().kind == mirror::Kind::kDialog && !drawn.get_shown().over_game_) {
                // (a dialog in the front end: over the screen it came from, else over the front end's ground and art -
                // never over the 3D scene)
                if (screen_cover().dialog_under_on_) {
                    paint::restyled(screen_cover().dialog_under_, frame, static_cast<float>(w),
                                    static_cast<float>(height), nullptr, &screen_cover().dialog_under_scroll_, &pause);
                } else {
                    paint::get_waiting(frame, static_cast<float>(w), static_cast<float>(height), pause, "career");
                }
            }
            if (menu_mirror().mirror_.get_up()) {
                paint::restyled(menu_mirror().mirror_, frame, static_cast<float>(w), static_cast<float>(height),
                                &menu_mirror().mirror_hits_, &menu_mirror().mirror_scroll_, &pause);
            } else {
                std::vector<paint::Hit> unused;
                paint::restyled(screen_cover().cover_tracker_, frame, static_cast<float>(w), static_cast<float>(height),
                                &unused, &screen_cover().cover_scroll_, &pause);
            }
            ImGui::Render();
            ui_backend::render();
            end_context(previous);
            return;
        }
        paint::Extras extras;
        extras.skater_name_ = new_frontend_state().skater_name_;
        extras.art = art_for;
        extras.records = records_for;
        extras.levels_missing_ = !new_frontend_state().catalogue_ok_;
        extras.restart_pending_ = edit_session().restart_pending_;
        extras.options_note_ = edit_session().options_note_;
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
        paint::get_screen(menu_state(), frame, static_cast<float>(w), static_cast<float>(height), extras,
                          &new_frontend_state().hits_, &rows);
        if (rows > 0) {
            menu_state().set_visible_rows(rows);
        }
        ImGui::Render();
        ui_backend::render();
    }
    end_context(previous);
}

void device_lost()
{
    if (!draw_context().context_) {
        return;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(draw_context().context_);
    ui_backend::invalidate_context_objects();
    ImGui::SetCurrentContext(previous);
}

std::string automation_command(const std::string& line, bool* done)
{
    *done = true;
    if (!is_switched_on()) {
        return "error: the new front end is off ([features] new_frontend=0)";
    }
    // fe select <screen>/<item> [seconds] | fe focus <screen>/<item> [seconds] | fe back | fe press <input>
    std::vector<std::string> words;
    for (size_t at = 0; at < line.size();) {
        const size_t start = line.find_first_not_of(" \t", at);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = line.find_first_of(" \t", start);
        words.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        at = end == std::string::npos ? line.size() : end;
    }
    if (words.empty()) {
        return "error: fe select <screen>/<item> | fe focus <screen>/<item> | fe back | fe press <input>";
    }
    std::string verb = core::at(words, 0);
    for (char& ch : verb) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (verb == "select" || verb == "focus") {
        if (words.size() < 2 || core::at(words, 1).find('/') == std::string::npos) {
            return "error: fe " + verb + " <screen>/<item> [seconds]";
        }
        const std::string screen_name = core::at(words, 1).substr(0, core::at(words, 1).find('/'));
        const std::string item = core::at(words, 1).substr(core::at(words, 1).find('/') + 1);
        Screen screen{};
        if (!screen_from_name(screen_name, &screen)) {
            return "error: no screen " + screen_name;
        }
        if (get_refused(screen, item)) {
            return "refused: online (" + core::at(words, 1) + ")";
        }
        stored_pending() = {};
        stored_pending().active = true;
        stored_pending().confirm = verb == "select";
        stored_pending().screen = screen;
        stored_pending().item = item;
        *done = false;
        return "";
    }
    if (verb == "back") {
        if (new_frontend_state().state_ != State::kShown || menu_state().occupied()) {
            return "error: the new front end isn't up";
        }
        run_action(the_front_end(), menu_state().get_press(Intake::kBack));
        return "ok";
    }
    if (verb == "press") {
        static const auto kInputs = std::to_array<std::pair<const char*, Intake>>({
            {"up", Intake::kUp},           {"down", Intake::kDown},       {"left", Intake::kLeft},
            {"right", Intake::kRight},     {"confirm", Intake::kConfirm}, {"back", Intake::kBack},
            {"prevtab", Intake::kPrevTab}, {"nexttab", Intake::kNextTab}});
        if (words.size() < 2) {
            return "error: fe press <up|down|left|right|confirm|back|resume|prevtab|nexttab>";
        }
        for (const auto& [name, input] : kInputs) {
            if (_stricmp(core::at(words, 1).c_str(), name) == 0) {
                if (new_frontend_state().state_ != State::kShown || menu_state().occupied()) {
                    return "error: the new front end isn't up";
                }
                if (input == Intake::kConfirm && get_refused(menu_state().get_shown(), menu_state().chosen_item_id())) {
                    return "refused: online";
                }
                run_action(the_front_end(), menu_state().get_press(input));
                return "ok";
            }
        }
        return "error: no input " + core::at(words, 1);
    }
    return "error: fe " + verb + "? (select, focus, back, press)";
}

std::string automation_result()
{
    if (stored_pending().active) {
        return {};
    }
    return stored_pending().result.empty() ? std::string("ok") : stored_pending().result;
}

void automation_cancel() { stored_pending().active = false; }

bool automation_screen_up(const std::string& name)
{
    // (the restyled original screens: dialog, skaters, list)
    if (is_switched_on() && !showing() && menu_mirror().mirror_.get_up() &&
        _stricmp(name.c_str(), mirror::kind_name(menu_mirror().mirror_.get_shown().kind)) == 0) {
        return true;
    }
    Screen screen{};
    return is_switched_on() && screen_from_name(name, &screen) && shown_and_idle(screen);
}

std::string automation_status()
{
    if (!is_switched_on()) {
        return {};
    }
    std::string text = std::string(get_state_name(new_frontend_state().state_));
    if (new_frontend_state().state_ == State::kShown || new_frontend_state().state_ == State::kLaunching) {
        text += std::string(" ") + get_screen_name(menu_state().get_shown());
        const std::string item = menu_state().chosen_item_id();
        if (!item.empty()) {
            text += "/" + item;
        }
    }
    if (new_frontend_state().anchor_ != Anchor::kNone && new_frontend_state().state_ == State::kShown) {
        text += std::string(" over the ") + game_screens::anchor_name(new_frontend_state().anchor_);
    }
    if (!showing() && menu_mirror().mirror_.get_up()) {
        const mirror::View& v = menu_mirror().mirror_.get_shown();
        text += std::string(", restyled ") + mirror::kind_name(v.kind) + " \"" + v.title + "\"";
        if (v.selected >= 0 && std::cmp_less(v.selected, v.rows.size())) {
            text += " on \"" + core::at(v.rows, v.selected).label + "\"";
        }
    }
    return text;
}

} // namespace frontend::new_frontend
