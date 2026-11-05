// NOT ORIGINAL - the new front end (ours): see new_frontend.h. The glue to the game: when it's up and over which
// original screen, what its menus ask for (hand-offs, levels), the mirror's cover and updates, its ImGui context and
// the per-frame cycle. The pieces (data / widgets / input / automation) are new_frontend_<name>.cpp, sharing new_frontend_internal.h.

#include "frontend/new_frontend.h"
#include "engine/pad_input/input_source.h"
#include <windows.h>
#include <algorithm>
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
#include <vector>
#include <SDL2/SDL.h>
#include <utility>
#include <array>
#include "base/core/checksums.h"
#include "engine/file/file_load.h"
#include "game/front_end/front_end.h"
#include "editor/level_editor.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_ui_backend.h"
#include "frontend/mouse_menus.h"
#include "engine/ui/menu_reading.h"
#include "game/front_end/menu_pad_event.h"
#include "frontend/new_frontend_draw.h"
#include "frontend/new_frontend_game.h"
#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "devtools/restart_tester.h"
#include "engine/audio/continue_music.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_manager.h"
#include "devtools/start_level.h"
#include "engine/gfx/font.h"
#include "engine/gfx/hd_ui.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "game/skate/career.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/thaw_bmx.h"
#include "platform/platform_system.h"
#include "engine/pad_input/input_event.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/gfx/loading_bar_cfuncs.h"
#include "game/rider/skater_state.h"
#include "game/rider/skater_score.h"
#include "game/skate/point_tally.h"
#include "base/core/parse_number.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "frontend/new_frontend_internal.h"

namespace frontend::new_frontend {
// What the source file's anonymous namespace held that its pieces share (new_frontend_internal.h): visible as it was.
// (In each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::new_frontend

namespace frontend::new_frontend {
namespace {

constexpr double kPointsCeiling = 2.0e9;        // the most points an award is taken for
constexpr int kMaxListNodes = 4096;             // a heap list walk stops after this many nodes
constexpr int kMaxPictureSide = 8192;           // the loading screen pictures' limit, in texels
constexpr int kPictureHeaderBytes = 0x20;       // the picture file's header before its texels
constexpr int kTrueColourBits = 32;             // the bit depths the pictures may have
constexpr int kPaletteBits = 8;
constexpr int kBitsPerByte = 8;
constexpr int kOpaqueAlpha = 0xFF;              // a full-bleed picture's alpha

constexpr int kMaxLoggedFrames = 20;            // the frames a log line is written for

constexpr int kPolicyFrames = 30;
constexpr std::size_t kDecisionKeySize = 96;

constexpr float kEditOpenTimeoutSeconds = 60.0F;
constexpr int kEditSettleFrames = 90;
constexpr int kEditPauseMenuFrames = 120;
constexpr int kEditChooseFrames = 20;
constexpr float kMaxFrameSeconds = 0.25F;
constexpr float kGuardSeconds = 2.0F;
constexpr float kLevelStartTimeoutSeconds = 30.0F;

namespace ui_backend = frontend::level_editor::ui_backend;

} // namespace
namespace internal {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

} // namespace internal
namespace {

constexpr uint32_t kSkater = 0x5B8AB877;       // Skater (the local skater's alias)
constexpr uint32_t kSkaterState = 0x43686585;  // SkaterState
constexpr uint32_t kSkaterScore = 0xD262802F;  // SkaterScore

} // namespace
namespace internal {

// --- the game's state ---

// The level loaded: its load script's checksum (the skate module's +0x378; load_mainmenu in the front end).
uint32_t current_load_state() { return modules::Skate::sole() ? modules::Skate::sole()->level() : 0; }
bool in_front_end() { return current_load_state() == kLoadMainMenu; }
// The skate module's career (+0x20; null before the module is up).
objects::RiderStoryProgress* get_career() { return modules::Skate::sole() ? modules::Skate::sole()->career().get() : nullptr; }
modules::FrontEnd* the_front_end() { return modules::front_end_instance().get(); }

} // namespace internal
namespace {

bool get_paused() { return (the_front_end() != nullptr) && the_front_end()->is_halted(); }
bool loading_screen_up()
{
    return render::loading_screen_shown() != 0 || render::renderer().loading_bar_timer() != 0 ||
           static_cast<uint8_t>(render::engine_parts().loading_screen_up()) != 0;
}

} // namespace
namespace internal {

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

StickRepeat& stick_repeat()
{
    static core::NoDestroy<StickRepeat> state;
    return state.get();
}

} // namespace internal
namespace {

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
// EDITOR opens the editor (as -edit does - devtools/start_level.h).
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

} // namespace
namespace internal {

MenuMirror& menu_mirror()
{
    static core::NoDestroy<MenuMirror> state;
    return state.get();
}

} // namespace internal
namespace {

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
        const int selected = menu_reading::selected_index(menu);
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

} // namespace
namespace internal {

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

} // namespace internal
namespace {

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
        modules::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
        ++edit_session().edit_presses_;
        edit_session().edit_ = Edit::kWaitPause;
        edit_session().edit_frames_ = 0;
        return;
    }
    // (kWaitPause) the pause menu up with its LEVEL EDITOR: chosen.
    if (!get_paused()) {
        if (++edit_session().edit_frames_ >= kEditPauseMenuFrames && edit_session().edit_presses_ < 3) {
            modules::send_menu_pad_event(front_end, modules::FrontEnd::kPadStart);
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

} // namespace
namespace internal {

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

} // namespace internal

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
    input::add_event_handler(handle_event);
    get_log("new front end: on ([features] new_frontend=1)");
}

bool showing() { return new_frontend_state().state_ == State::kShown || new_frontend_state().state_ == State::kLaunching; }

// (while a cover is on, the screen under it is hidden: no presses reach it blind - a few frames, 3 s at most)
bool blocks_game_input() { return is_switched_on() && (showing() || new_frontend_state().guard_ || screen_cover().cover_.get_on()); }

namespace {

// A tool's own UI has the screen: ours aside - not drawn, nothing restyled, nothing covered.
void step_aside_for_tool()
{
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
}

// Ours idle or aside: back to idle in a level with nothing to show; shown when it can be; else the restyled
// screen's mouse.
void tick_aside(modules::FrontEnd* front_end, Anchor anchor, bool usable)
{
    if ((current_load_state() == 0U) ||
        (!in_front_end() && anchor == Anchor::kNone && new_frontend_state().state_ == State::kAway && !get_paused())) {
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
}

// A level starting from ours: idle once it has started, or back to the menus after 30 seconds.
void tick_launching(float dt)
{
    menu_state().tick(dt);
    if ((new_frontend_state().launch_level_ == kLoadMainMenu && !in_front_end()) ||
        (new_frontend_state().launch_level_ != kLoadMainMenu &&
         current_load_state() != new_frontend_state().launch_level_) ||
        (new_frontend_state().launch_level_ != kLoadMainMenu && loading_screen_up())) {
        change_phase(State::kIdle);
        free_device_objects();
    } else if (new_frontend_state().state_time_ > kLevelStartTimeoutSeconds) {
        get_log("new front end: the level didn't start in 30 seconds - back to the menus");
        menu_state().cancel_launch();
        change_phase(State::kIdle);
    }
}

// The original's screen changed on its own: ours aside - back when an anchor is up.
void step_aside_from_screen(Anchor anchor, bool usable)
{
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
}

} // namespace

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
        const input::KeyState keys = input::keys();
        const bool held = get_key_down(keys, SDL_SCANCODE_RETURN) || get_key_down(keys, SDL_SCANCODE_KP_ENTER) ||
                          get_key_down(keys, SDL_SCANCODE_SPACE) || get_key_down(keys, SDL_SCANCODE_ESCAPE) ||
                          get_key_down(keys, SDL_SCANCODE_BACKSPACE) || new_frontend_state().buttons_down_ > 0 ||
                          left_button_down();
        if (!held || new_frontend_state().guard_time_ > kGuardSeconds) {
            new_frontend_state().guard_ = false;
        }
    }

    // A tool's own UI has the screen (the level editor - its Dear ImGui screens, or its old HUD of screen elements):
    // ours steps aside completely - not drawn, nothing restyled, nothing covered, the original's elements as the tool
    // left them - and comes back only once the pause menu really is back (the user's report of 2026-10-25: our pause
    // menu stayed drawn over the level editor - its hand-off never finished, as this isn't called while the editor
    // runs... its extension's menu_frame calls it then too, for this).
    if (level_editor::live_now()) {
        step_aside_for_tool();
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
        tick_aside(front_end, anchor, usable);
        return;
    case State::kLaunching:
        tick_launching(dt);
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
        step_aside_from_screen(anchor, usable);
        return;
    }
    std::vector<input::Event> events;
    events.swap(new_frontend_state().events_);
    for (const input::Event& widget : events) {
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

} // namespace
namespace internal {

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

} // namespace internal
namespace {

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
bool cfunc_item(script::Struct* params, script::Context /*context*/)
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
bool cfunc_show_new_frontend(script::Struct* params, script::Context /*context*/)
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

} // namespace frontend::new_frontend
