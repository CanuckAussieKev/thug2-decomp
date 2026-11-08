// NOT ORIGINAL - the level editor's own application: the editor mode's Start page, its levels, leaving and quitting
// (level_editor_shell.h).

#include "editor/level_editor_shell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "editor/level_editor.h"
#include "editor/level_editor_gui_text.h"
#include "editor/level_editor_content.h"
#include "editor/level_editor_import.h"
#include "editor/level_editor_internal.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_open.h"
#include "editor/level_editor_play.h"
#include "editor/level_editor_playtest.h"
#include "editor/level_editor_prefs.h"
#include "editor/level_editor_split.h"
#include "editor/level_editor_ui.h"
#include "editor/level_editor_ui_app.h"
#include "editor/level_editor_ui_theme.h"
#include "editor/level_editor_ux3.h"
#include "engine/audio/music_player.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/loading_dialog.h"
#include "engine/gfx/screen.h"
#include "engine/script/param_struct.h"
#include "engine/sys/editor_mode.h"
#include "engine/ui/ui_backend.h"
#include "game/front_end/front_end.h"
#include "game/front_end/start_level.h"
#include "imgui.h"
#include "platform/mute_audio.h"
#include "platform/run_log.h"

namespace frontend::level_editor::shell {

namespace {

constexpr int kFrontEndSettleFrames = 60;   // (the main menu up this long before a level may load - as -level waits)
constexpr int kLeaveSettleFrames = 10;      // (the pause menu up after the editor closed, before the level changes)
constexpr int kLeaveMostFrames = 600;       // (the editor didn't close: given up)
constexpr int kOpenMostFrames = 60 * 120;   // (the level didn't come up with the editor in two minutes: given up)
constexpr int kGiveUpFrames = 30;           // (the editor's steps over and the editor not running this long: refused)
constexpr int kNeverLoadedFrames = 60 * 5;  // (-edit's level not asked for this long after the menu settled: unknown)
constexpr int kPicturesPerFrame = 2;        // (level pictures read a frame, as the cards come into view)
constexpr std::size_t kMostPictures = 48;   // (kept; the least lately drawn freed past this)
constexpr int kPictureShrink = 2;           // (the level select pictures - 640x360 - at half size)
constexpr float kCardWidth = 208.0F;        // a level's card, at scale 1
constexpr float kPictureAspect = 9.0F / 16.0F;
constexpr float kHalf = 0.5F; // (centred)
constexpr float kRecentWidth = 300.0F;      // the Start page's recent levels column
constexpr float kPagePadding = 24.0F;
constexpr float kCardRounding = 4.0F;
constexpr float kCardGap = 12.0F;
constexpr float kDialogPart = 0.75F;        // Open Level's window: this much of the window
constexpr float kPromptWidth = 420.0F;
constexpr float kButtonWidth = 110.0F;
constexpr float kLoadingWidth = 440.0F;     // the loading dialog's box, at scale 1
constexpr std::size_t kFilterSize = 64;
constexpr ImU32 kCardColour = IM_COL32(38, 41, 47, 255);
constexpr ImU32 kCardHoverColour = IM_COL32(54, 58, 66, 255);
constexpr ImU32 kCardEdgeColour = IM_COL32(255, 255, 255, 24);
constexpr ImU32 kPlaceholderText = IM_COL32(150, 156, 166, 255);
constexpr ImVec4 kPageColour{0.075F, 0.08F, 0.09F, 1.0F};

// What the shell is doing.
enum class Phase : uint8_t {
    kStarting, // the game starting underneath (the front end's scripts, its main menu settling)
    kStart,    // the Start page
    kLeaving,  // the editor closing for another level
    kOpening,  // a level loading, then the editor opening there
    kEditing,  // the editor running (or playing from it)
};

// What a question about unsaved changes goes on to.
enum class After : uint8_t { kNone, kQuit, kOpen };

// A level asked for.
struct Wanted {
    uint32_t load = 0;
    bool edited = false;
    std::string title;
};

// A level's picture, made when its card is first drawn.
struct Thumb {
    ui_backend::Picture picture;
    uint64_t used = 0;
};

// The automation channel's command waiting.
enum class Waiting : uint8_t { kNone, kStart, kEditor, kPlaying, kImport };

// (the larger members first: no padding between them)
struct ShellState {
    uint64_t frame = 0; // (the pages drawn: the pictures' last use)
    std::vector<layout::CatalogLevel> catalog;
    std::string message; // (the Start page's: why the last level didn't open)
    std::string filter;
    std::string result; // (the automation channel's last answer)
    std::string loading_title; // (what the loading dialog was last told)
    Wanted wanted;      // (the level to open)
    std::unordered_map<std::string, Thumb> pictures;
    int settle_frames = 0;
    int loads_before = 0;
    int phase_frames = 0;
    int failed_frames = 0;
    int loads_this_frame = 0;
    Phase phase = Phase::kStarting;
    bool have_wanted = false;
    bool own_open = false;   // (the shell asked for the load - not -edit's start)
    bool open_begun = false; // (the editor's opening steps started: level_editor_open.h)
    bool leave_asked = false;
    After after = After::kNone; // (the question, the quit)
    bool prompt = false;
    bool quit_after_stop = false;
    bool catalog_made = false;
    bool browser_open = false;
    bool browser_focus = false;
    bool music_held = false; // (the front end's music paused by the shell)
    bool music_logged = false;
    Waiting waiting = Waiting::kNone; // (the automation channel's command)
};

ShellState& shell_state()
{
    static core::NoDestroy<ShellState> state;
    return state.get();
}

std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(),
                           [](char character) { return static_cast<char>(std::tolower(static_cast<unsigned char>(character))); });
    return text;
}

bool is_script(const char* name)
{
    const script::SymbolTableEntry* symbol = script::resolve_symbol(checksums::hash_from_text(name));
    return (symbol != nullptr) && symbol->type() == script::QbValueKind::kScriptBody;
}

// Changes the editor would lose: made since the level loaded and not saved.
bool unsaved()
{
    const internal::UnsavedChanges& changes = internal::unsaved_changes();
    return changes.unsaved_ && changes.unsaved_load_ == layout::level_loads();
}

const std::vector<layout::CatalogLevel>& catalog()
{
    ShellState& state = shell_state();
    if (!state.catalog_made) {
        state.catalog = layout::level_catalog();
        state.catalog_made = !state.catalog.empty();
    }
    return state.catalog;
}

const layout::CatalogLevel* find_level(uint32_t load)
{
    for (const layout::CatalogLevel& level : catalog()) {
        if (level.load == load) {
            return &level;
        }
    }
    return nullptr;
}

std::string title_of(uint32_t load, bool edited)
{
    const layout::CatalogLevel* level = find_level(load);
    if (level == nullptr) {
        return text::format("%08X", load);
    }
    return edited && !level->edited_title.empty() ? level->edited_title : level->title;
}

// The front end is far enough up for a level to load (as -level waits: the main menu's scripts, a second settled),
// or a level is up.
bool game_ready(modules::FrontEnd* front_end)
{
    if (render::current_level() != 0) {
        return true;
    }
    // (the data overlay's main menu has main_menu_skate_now, the game's own main_menu_exit)
    const bool menu_up = !((front_end != nullptr) && front_end->is_halted()) &&
                         (is_script("main_menu_skate_now") || is_script("main_menu_exit"));
    shell_state().settle_frames = menu_up ? shell_state().settle_frames + 1 : 0;
    return shell_state().settle_frames >= kFrontEndSettleFrames;
}

void quit_now()
{
    get_log("editor mode: quitting");
    if (ImGui::GetCurrentContext() != nullptr) {
        ui::app::save_layout(true);
    }
    render::engine_parts().set_quit_requested(true);
}

void show_start(const std::string& message)
{
    ShellState& state = shell_state();
    state.phase = Phase::kStart;
    state.message = message;
    state.have_wanted = false;
    state.catalog_made = false; // (an edited copy may have been saved or reset meanwhile)
    import::reload_if_pending(); // (an import finished while a level was up)
    ui::app::set_window_title({});
    if (!message.empty()) {
        get_log("editor mode: %s", message.c_str());
    }
}

// The level's load asked for (the level select's SKATE NOW underneath, from the main menu or the pause menu).
void start_load()
{
    ShellState& state = shell_state();
    std::string why;
    import::reload_if_pending(); // (an import finished while the last level was up: its levels known before this loads)
    state.loads_before = layout::level_loads();
    if (!begin_level::read_in_load(state.wanted.load, state.wanted.edited, &why)) {
        show_start("Couldn't open " + state.wanted.title + " - " + why);
        return;
    }
    get_log("editor mode: opening %s", state.wanted.title.c_str());
    state.phase = Phase::kOpening;
    state.phase_frames = 0;
    state.own_open = true;
    state.open_begun = false;
}

// A level to open: now, or once the editor has closed (another level from inside it).
void open_level(const Wanted& wanted)
{
    ShellState& state = shell_state();
    state.wanted = wanted;
    state.have_wanted = true;
    state.browser_open = false;
    state.message.clear();
    if (level_editor::live_now()) {
        state.phase = Phase::kLeaving;
        state.phase_frames = 0;
        state.leave_asked = true;
        get_log("editor mode: closing the editor to open %s", wanted.title.c_str());
        return;
    }
    start_load();
}

// What the question goes on to (the answer given, or no question needed).
void carry_on(After after)
{
    ShellState& state = shell_state();
    state.after = After::kNone;
    if (after == After::kQuit) {
        quit_now();
    } else if (after == After::kOpen) {
        open_level(state.wanted);
    }
}

// Unsaved changes asked about first; while playing, back to the editor first.
void ask(After after)
{
    ShellState& state = shell_state();
    if (!unsaved()) {
        carry_on(after);
        return;
    }
    if (!level_editor::live_now() && perform::playing(layout::level_loads())) {
        state.quit_after_stop = after == After::kQuit;
        perform::ask_return();
        return;
    }
    state.after = after;
    state.prompt = true;
}

void ask_open(uint32_t load, bool edited)
{
    shell_state().wanted = {.load = load, .edited = edited, .title = title_of(load, edited)};
    ask(After::kOpen);
}

// --- the levels' cards --------------------------------------------------------------------------------------------

const ui_backend::Picture* picture_for(const std::string& name)
{
    ShellState& state = shell_state();
    if (name.empty()) {
        return nullptr;
    }
    auto it = state.pictures.find(name);
    if (it == state.pictures.end()) {
        if (state.loads_this_frame >= kPicturesPerFrame) {
            return nullptr;
        }
        ++state.loads_this_frame;
        if (state.pictures.size() >= kMostPictures) {
            auto oldest = std::ranges::min_element(state.pictures, {}, [](const auto& entry) { return entry.second.used; });
            ui_backend::release_texture(oldest->second.picture.texture);
            state.pictures.erase(oldest);
        }
        it = state.pictures.emplace(name, Thumb{.picture = ui_backend::load_picture(name, kPictureShrink)}).first;
    }
    it->second.used = state.frame;
    return it->second.picture.texture != 0U ? &it->second.picture : nullptr;
}

bool matches(const std::string& title, const std::string& filter)
{
    return filter.empty() || lowered(title).find(lowered(filter)) != std::string::npos;
}

// A level's card: its picture (or its name's first letters) and its title. True when clicked.
bool card(const layout::CatalogLevel& level, bool edited, float width, bool enabled)
{
    const float picture_height = width * kPictureAspect;
    const float height = picture_height + (ImGui::GetTextLineHeightWithSpacing() * 1.6F);
    ImGui::PushID(static_cast<int>(level.load ^ (edited ? 1U : 0U)));
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("card", ImVec2(width, height)) && enabled;
    const bool hovered = ImGui::IsItemHovered() && enabled;
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + width, at.y + height);
    list->AddRectFilled(at, end, hovered ? kCardHoverColour : kCardColour, kCardRounding);
    const ImVec2 picture_end(at.x + width, at.y + picture_height);
    if (ImGui::IsRectVisible(at, end)) {
        if (const ui_backend::Picture* picture = picture_for(level.picture)) {
            const ImVec2 top_uv(0.0F, picture->bottom_up ? 1.0F : 0.0F);
            const ImVec2 bottom_uv(1.0F, picture->bottom_up ? 0.0F : 1.0F);
            list->AddImageRounded(static_cast<ImTextureID>(picture->texture), at, picture_end, top_uv, bottom_uv,
                                  IM_COL32_WHITE, kCardRounding, ImDrawFlags_RoundCornersTop);
        } else {
            const std::string letters = level.title.substr(0, std::min<std::size_t>(level.title.size(), 2));
            const ImVec2 size = ImGui::CalcTextSize(letters.c_str());
            list->AddText(ImVec2(at.x + ((width - size.x) * kHalf), at.y + ((picture_height - size.y) * kHalf)),
                          kPlaceholderText, letters.c_str());
        }
    }
    const std::string& title = edited && !level.edited_title.empty() ? level.edited_title : level.title;
    const ImGuiStyle& style = ImGui::GetStyle();
    list->PushClipRect(at, end, true);
    list->AddText(ImVec2(at.x + style.FramePadding.x, picture_end.y + style.FramePadding.y),
                  ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), title.c_str());
    list->PopClipRect();
    list->AddRect(at, end, kCardEdgeColour, kCardRounding);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::PopID();
    return clicked;
}

// The cards of the levels that pass the filter, section by section (the edited copies first), wrapped to the width.
void level_grid(float scale, bool enabled)
{
    const ShellState& state = shell_state();
    const float width = kCardWidth * scale;
    const float gap = kCardGap * scale;
    const float room = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>((room + gap) / (width + gap)));
    struct Section {
        const char* name;
        int index; // (-1: the edited copies)
    };
    std::vector<Section> sections = {{.name = "Edited Levels", .index = -1}};
    for (int i = 0; *layout::catalog_section(i) != '\0'; ++i) {
        sections.push_back({.name = layout::catalog_section(i), .index = i});
    }
    const std::vector<layout::CatalogLevel>& levels = catalog();
    // (the level clicked opened after the cards: opening it may make the list again)
    const layout::CatalogLevel* clicked = nullptr;
    bool clicked_edited = false;
    for (const Section& section : sections) {
        int column = 0;
        bool header = false;
        for (const layout::CatalogLevel& level : levels) {
            const bool edited = section.index < 0;
            if ((edited && !level.edited) || (!edited && level.section != section.index) ||
                !matches(edited ? level.edited_title : level.title, state.filter)) {
                continue;
            }
            if (!header) {
                header = true;
                ImGui::Spacing();
                ImGui::SeparatorText(section.name);
            }
            if (column > 0) {
                ImGui::SameLine(0.0F, gap);
            }
            if (card(level, edited, width, enabled)) {
                clicked = &level;
                clicked_edited = edited;
            }
            column = (column + 1) % columns;
        }
    }
    if (clicked != nullptr) {
        ask_open(clicked->load, clicked_edited);
    }
    if (levels.empty()) {
        gui::text_disabled("%s", "No levels yet");
    }
}

void filter_field(float width)
{
    ShellState& state = shell_state();
    std::string buffer = state.filter;
    buffer.resize(kFilterSize);
    ImGui::SetNextItemWidth(width);
    if (state.browser_focus) {
        ImGui::SetKeyboardFocusHere();
        state.browser_focus = false;
    }
    if (ImGui::InputTextWithHint("##filter", "Filter levels", buffer.data(), buffer.size())) {
        state.filter = buffer.substr(0, buffer.find('\0'));
    }
}

void recent_list(bool enabled)
{
    // (a copy: a pick below changes the list)
    const std::vector<ui::app::RecentLevel> recent(ui::app::recent_levels().begin(), ui::app::recent_levels().end());
    if (recent.empty()) {
        gui::text_disabled("%s", "No recent levels");
        return;
    }
    for (const ui::app::RecentLevel& level : recent) {
        if (find_level(level.load) == nullptr) {
            continue;
        }
        const std::string label = title_of(level.load, level.edited) + "##recent" + std::to_string(level.load) +
                                  (level.edited ? "e" : "");
        if (ImGui::Selectable(label.c_str(), false, enabled ? 0 : ImGuiSelectableFlags_Disabled)) {
            ask_open(level.load, level.edited);
        }
    }
}

// --- the pages ----------------------------------------------------------------------------------------------------

void menu_bar(bool ready)
{
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import Level...", nullptr, false, ready)) {
            import::open_window({});
        }
        if (ImGui::BeginMenu("Recent", ready)) {
            recent_list(ready);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            ask(After::kQuit);
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void start_page(const ux3::Fonts& fonts)
{
    const ShellState& state = shell_state();
    const bool ready = state.phase == Phase::kStart;
    menu_bar(ready);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPageColour);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPagePadding * fonts.scale, kPagePadding * fonts.scale));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##start_page", nullptr, flags)) {
        ImGui::PushFont(fonts.heading);
        ImGui::TextUnformatted("THUG2 Editor");
        ImGui::PopFont();
        if (state.phase == Phase::kStarting) {
            gui::text_disabled("%s", "Starting...");
        } else if (!state.message.empty()) {
            gui::text_colored(ui_theme::kWarningColour, "%s", state.message.c_str());
        } else {
            gui::text_disabled("%s", "Open a level to edit");
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(!ready);
        if (ImGui::Button("Import Level...")) {
            import::open_window({});
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        const float recent_width = kRecentWidth * fonts.scale;
        if (ImGui::BeginChild("##recent", ImVec2(recent_width, 0.0F))) {
            ImGui::SeparatorText("Recent");
            recent_list(ready);
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0F, kPagePadding * fonts.scale);
        if (ImGui::BeginChild("##levels", ImVec2(0.0F, 0.0F))) {
            filter_field(recent_width);
            level_grid(fonts.scale, ready);
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// The level being opened, by its name in the level list (-edit's: as it was given when the list hasn't it).
std::string opening_title()
{
    const ShellState& state = shell_state();
    if (state.have_wanted) {
        return state.wanted.title;
    }
    const std::string name = begin_level::get_requested();
    if (name.empty()) {
        return layout::level_title();
    }
    const std::string load = lowered(name.substr(0, begin_level::kLoadPrefixLength)) == "load_" ? name : "Load_" + name;
    const layout::CatalogLevel* level = find_level(checksums::hash_from_text(load.c_str()));
    return level != nullptr ? level->title : name;
}

// The editor's look for the loading dialog the game draws while a level loads (engine/gfx/loading_dialog.h).
gfx::loading_dialog::Look editor_look(const ux3::Fonts& fonts)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    gfx::loading_dialog::Look look;
    look.page = ImGui::ColorConvertFloat4ToU32(kPageColour);
    look.window = ImGui::GetColorU32(ImGuiCol_WindowBg);
    look.border = ImGui::GetColorU32(ImGuiCol_Border);
    look.title_bar = ImGui::GetColorU32(ImGuiCol_TitleBgActive);
    look.text = ImGui::GetColorU32(ImGuiCol_Text);
    look.text_dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    look.frame = ImGui::GetColorU32(ImGuiCol_FrameBg);
    look.accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    look.warning = ImGui::ColorConvertFloat4ToU32(ui_theme::kWarningColour);
    look.ui_pixels = fonts.ui != nullptr ? fonts.ui->FontSize : ImGui::GetFontSize();
    look.heading_pixels = fonts.heading != nullptr ? fonts.heading->FontSize : ImGui::GetFontSize();
    look.width = kLoadingWidth * fonts.scale;
    look.padding_x = style.WindowPadding.x;
    look.padding_y = style.WindowPadding.y;
    look.frame_padding_x = style.FramePadding.x;
    look.frame_padding_y = style.FramePadding.y;
    look.spacing_y = style.ItemSpacing.y;
    look.rounding = style.FrameRounding;
    return look;
}

// The page over the editor closing, a level loading and the editor opening: the loading dialog, as the game draws it
// during the load itself (the same box in the same place).
void opening_page(const ux3::Fonts& fonts)
{
    const ShellState& state = shell_state();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPageColour);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##opening_page", nullptr, flags)) {
        gfx::loading_dialog::BoxText text;
        text.caption = "Open Level";
        text.heading = "Loading " + opening_title() + "...";
        if (state.phase == Phase::kLeaving) {
            text.status = "Closing the editor";
            text.fraction = -static_cast<float>(ImGui::GetTime());
        } else if (state.open_begun) {
            text.status = "Opening the editor";
            text.fraction = 1.0F;
        } else {
            text.status = "Loading";
            text.fraction = -static_cast<float>(ImGui::GetTime());
        }
        gfx::loading_dialog::draw_box(ImGui::GetWindowDrawList(), editor_look(fonts),
                                      {.ui = fonts.ui, .heading = fonts.heading}, text,
                                      {.origin_x = viewport->Pos.x,
                                       .origin_y = viewport->Pos.y,
                                       .width = viewport->Size.x,
                                       .height = viewport->Size.y});
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// The front end's music kept quiet under the Start page (and under the boot and a load from it, until a level is up):
// paused while so, unpaused once (the level's own music then plays as ever).
void quiet_front_end_music()
{
    ShellState& state = shell_state();
    const bool front_end = state.phase == Phase::kStarting || state.phase == Phase::kStart ||
                           (state.phase == Phase::kOpening && render::current_level() == 0);
    if (front_end) {
        if (!audio::music_is_paused()) {
            audio::pause_music(1);
            if (audio::music_is_paused() && !state.music_logged) {
                state.music_logged = true;
                get_log("editor mode: the front end's music paused (again whenever its scripts unpause it)");
            }
        }
        state.music_held = true;
    } else if (state.music_held) {
        state.music_held = false;
        state.music_logged = false;
        audio::pause_music(0);
        get_log("editor mode: the music unpaused (a level is up)");
    }
}

// The editor is silent: no music and no sound effects except while playing with Preferences' Sound in Play on - the
// process's audio session muted (platform/mute_audio.h; the game's own volume settings left alone). Windows keeps a
// session's mute for the exe's next run: the exe clears it at start-up (app/main.cpp), so the game after the editor
// has its sound.
void quiet_audio()
{
    static bool applied = false;
    const bool playing = shell_state().phase == Phase::kEditing && !level_editor::live_now() &&
                         perform::playing(layout::level_loads());
    const bool muted = !(playing && prefs::get_pull().play_sound_);
    if (muted != applied && sound::set_audio_session_muted(muted)) {
        applied = muted;
        get_log("editor mode: sound %s", muted ? "off" : "on (playing)");
    }
}

// The level the loading dialog names (engine/gfx/loading_dialog.h), when it changes.
void name_loading_level()
{
    ShellState& state = shell_state();
    const std::string title = state.phase == Phase::kEditing ? layout::level_title() : opening_title();
    if (title != state.loading_title) {
        state.loading_title = title;
        gfx::loading_dialog::set_title(title);
    }
}

void draw_page(const ux3::Fonts& fonts)
{
    ShellState& state = shell_state();
    ++state.frame;
    state.loads_this_frame = 0;
    gfx::loading_dialog::set_look(editor_look(fonts)); // (the box the game draws through a load looks as this one)
    if (state.phase == Phase::kStart || state.phase == Phase::kStarting) {
        start_page(fonts);
    } else {
        opening_page(fonts);
    }
    dialogs(fonts);
}

void open_level_window(const ux3::Fonts& fonts)
{
    ShellState& state = shell_state();
    if (!state.browser_open) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x * kHalf),
                                   viewport->WorkPos.y + (viewport->WorkSize.y * kHalf)),
                            ImGuiCond_Appearing, ImVec2(kHalf, kHalf));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * kDialogPart, viewport->WorkSize.y * kDialogPart),
                             ImGuiCond_Appearing);
    if (ImGui::Begin("Open Level", &state.browser_open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        filter_field(kRecentWidth * fonts.scale);
        if (ImGui::BeginChild("##open_levels", ImVec2(0.0F, 0.0F))) {
            level_grid(fonts.scale, true);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void unsaved_prompt(const ux3::Fonts& fonts)
{
    ShellState& state = shell_state();
    constexpr const char* kName = "Unsaved Changes";
    if (state.prompt) {
        state.prompt = false;
        ImGui::OpenPopup(kName);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x * kHalf),
                                   viewport->WorkPos.y + (viewport->WorkSize.y * kHalf)),
                            ImGuiCond_Appearing, ImVec2(kHalf, kHalf));
    ImGui::SetNextWindowSize(ImVec2(kPromptWidth * fonts.scale, 0.0F));
    if (!ImGui::BeginPopupModal(kName, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        return;
    }
    const std::string level = layout::level_title();
    gui::text_wrapped("Save the changes to %s?", level.empty() ? "this level" : level.c_str());
    gui::text_disabled("%s", state.after == After::kQuit ? "The editor closes." : "Another level opens.");
    ImGui::Spacing();
    const ImVec2 button(kButtonWidth * fonts.scale, 0.0F);
    After answered = After::kNone;
    bool cancelled = false;
    if (ImGui::Button("Save", button)) {
        internal::save_layout();
        answered = state.after;
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", button)) {
        answered = state.after;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", button) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancelled = true;
    }
    if (answered != After::kNone || cancelled) {
        ImGui::CloseCurrentPopup();
        state.after = After::kNone;
    }
    ImGui::EndPopup();
    if (answered != After::kNone) {
        carry_on(answered);
    }
}

// --- the automation channel ---------------------------------------------------------------------------------------

std::vector<std::string> words_of(const std::string& line)
{
    std::vector<std::string> words;
    std::size_t at = 0;
    while ((at = line.find_first_not_of(' ', at)) != std::string::npos) {
        const std::size_t end = line.find(' ', at);
        words.push_back(line.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end;
    }
    return words;
}

void finish(const std::string& result)
{
    shell_state().waiting = Waiting::kNone;
    shell_state().result = result;
}

// The command waiting, checked once a frame.
void check_waiting()
{
    const ShellState& state = shell_state();
    switch (state.waiting) {
    case Waiting::kStart:
        if (state.phase == Phase::kStart) {
            finish("ok");
        }
        return;
    case Waiting::kEditor:
        if (level_editor::live_now()) {
            finish("ok");
        } else if (state.phase == Phase::kStart && !state.message.empty()) {
            finish("error: " + state.message);
        }
        return;
    case Waiting::kPlaying:
        if (perform::playing(layout::level_loads())) {
            finish("ok");
        }
        return;
    case Waiting::kImport: {
        std::string result;
        if (import::automation_done(&result)) {
            finish(result);
        }
        return;
    }
    case Waiting::kNone:
        return;
    }
}

} // namespace

bool on() { return sys::editor_mode::on(); }

void tick_frame(modules::FrontEnd* front_end)
{
    if (!on()) {
        return;
    }
    ShellState& state = shell_state();
    ++state.phase_frames;
    switch (state.phase) {
    case Phase::kStarting:
        if (*begin_level::get_requested() != '\0') {
            // (-edit <level>: start level loads it and the editor opens there - level_editor_open.h)
            state.phase = Phase::kOpening;
            state.phase_frames = 0;
            state.own_open = false;
        } else if (game_ready(front_end)) {
            show_start({});
            get_log("editor mode: the Start page is up");
        }
        break;
    case Phase::kStart:
        break;
    case Phase::kLeaving: {
        const bool paused = (front_end != nullptr) && front_end->is_halted();
        if (level_editor::live_now() || !paused) {
            if (state.phase_frames > kLeaveMostFrames) {
                show_start("The editor didn't close - " + state.wanted.title + " not opened");
            }
            break;
        }
        if (state.phase_frames > kLeaveSettleFrames) {
            start_load();
        }
        break;
    }
    case Phase::kOpening:
        if (level_editor::live_now()) {
            state.phase = Phase::kEditing;
            state.have_wanted = false;
            ui::app::note_recent({.load = layout::loaded_level(), .edited = layout::editing_copy()});
            break;
        }
        if (state.own_open && !state.open_begun && layout::level_loads() != state.loads_before) {
            state.open_begun = true;
            open_in_level::begin(state.wanted.title);
        }
        if (state.own_open && state.open_begun) {
            open_in_level::tick_frame(front_end);
        }
        {
            // (-edit's start: the editor's steps begun by start level - level_editor_open.h)
            if (!state.own_open && !state.open_begun && open_in_level::pending()) {
                state.open_begun = true;
            }
            // Given up: the editor's steps over in the level without the editor (a refusal - logged); -edit's level
            // never loading (an unknown level: start level has had the main menu up long enough to have asked).
            const std::string title = state.have_wanted ? state.wanted.title : std::string(begin_level::get_requested());
            const bool up = render::current_level() != 0;
            const bool steps_over = state.open_begun && !open_in_level::pending() && up;
            state.failed_frames = steps_over ? state.failed_frames + 1 : 0;
            const bool never_asked = !state.own_open && !state.open_begun && !up && game_ready(front_end) &&
                                     state.settle_frames > kFrontEndSettleFrames + kNeverLoadedFrames;
            if (state.failed_frames > kGiveUpFrames) {
                state.failed_frames = 0;
                show_start("The level editor didn't open in " + title + " (the log says why)");
            } else if (never_asked) {
                show_start("Couldn't open " + title + " (the log says why)");
            } else if (state.phase_frames > kOpenMostFrames) {
                show_start("The level didn't open in time");
            }
        }
        break;
    case Phase::kEditing:
        if (!level_editor::live_now() && !perform::playing(layout::level_loads())) {
            // (play from here's few frames between the editor and play aside: left some other way - the Start page)
            if (state.phase_frames > kFrontEndSettleFrames) {
                show_start({});
            }
        } else {
            state.phase_frames = 0;
        }
        if (state.quit_after_stop && level_editor::live_now()) {
            state.quit_after_stop = false;
            ask(After::kQuit);
        }
        break;
    }
    quiet_front_end_music();
    quiet_audio();
    name_loading_level();
    check_waiting();
}

bool draw_frame()
{
    if (!on()) {
        return false;
    }
    ui::shell_frame(&draw_page);
    return true;
}

void dialogs(const ux3::Fonts& fonts)
{
    if (!on()) {
        return;
    }
    open_level_window(fonts);
    import::window(fonts);
    unsaved_prompt(fonts);
}

void file_menu_top()
{
    if (!on()) {
        return;
    }
    if (ImGui::MenuItem("Open Level...")) {
        shell_state().browser_open = true;
        shell_state().browser_focus = true;
        shell_state().catalog_made = false;
    }
    if (ImGui::MenuItem("Import Level...")) {
        import::open_window({});
    }
    if (ImGui::BeginMenu("Recent")) {
        recent_list(true);
        ImGui::EndMenu();
    }
    ImGui::Separator();
}

void file_menu_bottom()
{
    if (!on()) {
        return;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Exit")) {
        ask(After::kQuit);
    }
}

void play_file_menu()
{
    if (ImGui::MenuItem("Exit")) {
        ask(After::kQuit);
    }
}

void play_menu_top()
{
    if (!on()) {
        return;
    }
    if (ImGui::MenuItem("Play")) {
        play_test::play_from_start();
    }
}

bool blocks_game_input()
{
    if (!on()) {
        return false;
    }
    // (playing: the Scene panel's focus decides - level_editor_ui_app.h)
    return level_editor::live_now() || !perform::playing(layout::level_loads());
}

bool holds_quit()
{
    if (!on()) {
        return false;
    }
    ask(After::kQuit);
    return true;
}

bool take_leave_request()
{
    const bool asked = shell_state().leave_asked;
    shell_state().leave_asked = false;
    return asked;
}

void levels_changed() { shell_state().catalog_made = false; }

void open_imported(const std::string& load)
{
    if (!on() || shell_state().phase == Phase::kStarting || shell_state().phase == Phase::kOpening ||
        shell_state().phase == Phase::kLeaving) {
        return;
    }
    ask_open(checksums::hash_from_text(load.c_str()), false);
}

bool screen_command(const std::string& line, bool* done, std::string* result)
{
    std::vector<std::string> words = words_of(line);
    if (words.empty() || words.front() != "editor") {
        return false;
    }
    // (a last number: the automation channel's timeout)
    bool timeout_given = false;
    if (words.size() > 2 && std::ranges::all_of(words.back(), [](char character) { return std::isdigit(static_cast<unsigned char>(character)) != 0; })) {
        words.pop_back();
        timeout_given = true;
    }
    ShellState& state = shell_state();
    *done = true;
    if (!on()) {
        *result = "refused: not the editor mode";
        return true;
    }
    const std::string verb = words.size() > 1 ? words.at(1) : std::string();
    state.result.clear();
    if (verb == "start") {
        state.waiting = Waiting::kStart;
    } else if (verb == "open" && words.size() > 2) {
        std::string name = words.at(2);
        if (lowered(name.substr(0, begin_level::kLoadPrefixLength)) != "load_") {
            name = "Load_" + name;
        }
        const bool edited = words.size() > 3 && words.at(3) == "edited";
        if (state.phase == Phase::kStarting || state.phase == Phase::kOpening || state.phase == Phase::kLeaving) {
            *result = "refused: busy";
            return true;
        }
        const uint32_t load = checksums::hash_from_text(name.c_str());
        open_level({.load = load, .edited = edited, .title = find_level(load) != nullptr ? title_of(load, edited) : name});
        state.waiting = Waiting::kEditor;
    } else if (verb == "import" && words.size() > 2) {
        // (the folder: the rest of the line - it may hold spaces)
        const std::size_t at = line.find("import");
        std::string folder = line.substr(line.find_first_not_of(' ', at + std::string_view("import").size()));
        while (!folder.empty() && folder.back() == ' ') {
            folder.pop_back();
        }
        if (timeout_given) {
            folder.erase(folder.rfind(' '));
        }
        import::automation_list(folder);
        state.waiting = Waiting::kImport;
    } else if (verb == "importrun" && words.size() > 2) {
        import::automation_import(std::vector<std::string>(words.begin() + 2, words.end()));
        state.waiting = Waiting::kImport;
    } else if (verb == "prop" && words.size() > 3 && level_editor::live_now()) {
        // (the Content Browser's Place at Cursor for another level's piece: cut out first if need be - its log line
        // "placed prop pack" says when it is in)
        *result = content::automation_place(words.at(2), words.at(3));
        return true;
    } else if ((verb == "split" || verb == "pick") && level_editor::live_now()) {
        *result = split_view::automation(words); // (the split view - level_editor_split.h)
        return true;
    } else if (verb == "save" && level_editor::live_now()) {
        std::string message;
        *result = layout::store_away(&message) ? "ok " + message : "error: " + message;
        return true;
    } else if ((verb == "play" || verb == "playhere") && level_editor::live_now()) {
        if (verb == "play") {
            play_test::play_from_start();
        } else {
            perform::get_request();
        }
        state.waiting = Waiting::kPlaying;
    } else if (verb == "stop" && perform::playing(layout::level_loads())) {
        perform::ask_return();
        state.waiting = Waiting::kEditor;
    } else if (verb == "exit") {
        if (unsaved()) {
            *result = "refused: unsaved changes";
            return true;
        }
        *result = "ok";
        quit_now();
        return true;
    } else {
        *result = "error: fe editor start | open <level> [edited] | play | playhere | stop | exit | import <folder> | "
                  "importrun <level>... | prop <level> <piece> | save | split <layout> | pick <view> <x> <y>";
        return true;
    }
    check_waiting();
    *done = state.waiting == Waiting::kNone;
    if (*done) {
        result->swap(state.result);
    }
    return true;
}

bool screen_result(std::string* result)
{
    ShellState& state = shell_state();
    if (state.waiting == Waiting::kNone && state.result.empty()) {
        return false;
    }
    result->clear();
    if (state.waiting == Waiting::kNone) {
        result->swap(state.result); // (read once)
    }
    return true;
}

void screen_cancel() { shell_state().waiting = Waiting::kNone; }

bool screen_status(std::string* status)
{
    if (!on()) {
        return false;
    }
    static constexpr auto kPhases = std::to_array<std::string_view>({"starting", "start page", "leaving", "opening", "editing"});
    *status = "editor mode: " + std::string(core::at(kPhases, static_cast<std::size_t>(shell_state().phase))) +
              "; import: " + import::phase_name();
    return true;
}

} // namespace frontend::level_editor::shell
