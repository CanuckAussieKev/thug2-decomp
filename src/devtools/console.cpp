// NOT ORIGINAL - the developer console (console.h).

#include "devtools/console.h"
#include "devtools/rt_check.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "devtools/compare_split.h"
#include "devtools/demo.h"
#include "devtools/demo_viewer.h"
#include "engine/ext/extension.h"
#include "engine/gfx/texture_sets.h"
#include "engine/objects/entity.h"
#include "engine/pad_input/input_event.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/tokens.h"
#include "engine/ui/ui_backend.h"
#include "engine/ui/ui_fonts.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/screenshot_files.h"
#include "platform/sdl_keys.h"
#include "platform/sdl_window.h"

namespace devtools::console {
namespace {

constexpr int kGraveScancode = 53; // SDL_SCANCODE_GRAVE: the key left of 1
constexpr size_t kMaxLines = 4000;
constexpr float kFontPixels = 15.0F; // (at 720 lines)
constexpr float kBaseHeight = 720.0F;

struct State {
    bool open = false;
    bool focus_input = false; // (the input line takes the keyboard at the next frame)
    bool scroll_to_end = false;
    bool context_failed = false;
    int key = kGraveScancode;
    ImGuiContext* context = nullptr;
    std::deque<std::string> lines;
    std::vector<std::string> history;
    int history_at = -1;
    std::array<char, 512> input{};
};

State& state()
{
    static State s;
    return s;
}

void print(const std::string& text)
{
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        state().lines.push_back(line);
    }
    while (state().lines.size() > kMaxLines) {
        state().lines.pop_front();
    }
    state().scroll_to_end = true;
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// --- scripts ---------------------------------------------------------------------------------------------------------

// A level's interpreter by the code it is at: a TypeScript level's is its script's one kTsBody token (the interpreter
// leaves it there while the level runs in QuickJS, and a script it calls returns there).
const char* interpreter_at(const uint8_t* pc)
{
    if (!pc) {
        return "--";
    }
    // (a level not yet begun is at its code's start - a TypeScript script's is a line's end, then its kTsBody)
    return script::as_token(*script::skip_newline_tokens(script::tokens_at(pc))) == script::QbToken::kTsBody ? "TS"
                                                                                                             : "QB";
}

const char* waiting_for(script::BlockReason reason)
{
    switch (reason) {
    case script::BlockReason::kNothing:
        return "";
    case script::BlockReason::kCount:
        return "waiting frames";
    case script::BlockReason::kTimer:
    case script::BlockReason::kRealTimer:
        return "waiting time";
    case script::BlockReason::kBlock:
        return "blocked";
    default:
        return "waiting on its object";
    }
}

// (the names the .qb files carry, kept while they load - script::keep_qb_names, turned on at start-up -, else the
// game's own table's)
std::string name_of(uint32_t checksum)
{
    const char* kept = script::kept_qb_name(checksum);
    return kept ? kept : script::find_checksum_name_or_unknown(checksum);
}

std::string list_scripts(const std::string& filter)
{
    const std::string wanted = lower(filter);
    std::ostringstream out;
    int shown = 0;
    int total = 0;
    int ts_levels = 0;
    int qb_levels = 0;
    for (script::Script* s = script::newest_script(); s; s = s->following()) {
        ++total;
        // The call stack, outermost first: the callers' frames, then the level running now.
        struct Level {
            uint32_t name;
            const char* interpreter;
        };
        std::vector<Level> levels;
        // (call_stack is the frames' room - 2 in the record, 16 on the heap -: the first call_depth are in use)
        const std::span<const script::CallFrame> frames = std::as_const(*s).call_stack();
        const size_t depth = std::min(frames.size(), static_cast<size_t>(std::max(0, s->call_depth())));
        for (const script::CallFrame& frame : frames.first(depth)) {
            levels.push_back({frame.script_id_, interpreter_at(frame.caller_frame_)});
        }
        levels.push_back({s->script_title(), interpreter_at(s->pc())});
        if (!wanted.empty() && std::ranges::none_of(levels, [&](const Level& level) {
                return lower(name_of(level.name)).find(wanted) != std::string::npos;
            })) {
            continue;
        }
        ++shown;
        for (const Level& level : levels) {
            (std::strcmp(level.interpreter, "TS") == 0 ? ts_levels : qb_levels) +=
                std::strcmp(level.interpreter, "--") != 0 ? 1 : 0;
        }
        out << "#" << s->unique_id();
        if (const objects::Entity* object = s->entity().get_pull(); object && object->ident() != 0) {
            out << " on " << name_of(object->ident());
        }
        out << ": ";
        // (the level running now first, then who called it)
        for (auto level = levels.rbegin(); level != levels.rend(); ++level) {
            out << (level == levels.rbegin() ? "" : " <- ") << name_of(level->name) << " [" << level->interpreter
                << "]";
        }
        if (const char* waiting = waiting_for(s->suspend_reason()); *waiting) {
            out << "  (" << waiting << ")";
        }
        out << "\n";
    }
    out << shown << " of " << total << " scripts shown; levels: " << ts_levels << " TS, " << qb_levels << " QB";
    return out.str();
}

constexpr const char* kHelp = "scripts [text]  the scripts running (names holding text): each level QB or TS\n"
                              "demo record [name]  the level again, recorded (F6); demo stop: saved\n"
                              "demo play <name>    a demo played back; demo status\n"
                              "demo view <name>    a demo in the viewer; viewer help: its commands\n"
                              "compare on | off | set <key> <value> | divider <x> | status  the split\n"
                              "clear           clear the console\n"
                              "rt summary      ray tracing's contract check: its counts and violations\n"
                              "rt dump [file]  the last frame's ray-tracing record as JSON (game folder)\n"
                              "rt sun <x y z> | off  the ray-traced shadows' sun set by hand (toward it)\n"
                              "textures [original | pbrify | seedvr2]  the texture set in use / switched (not saved)\n"
                              "help            this list";

// --- the window ------------------------------------------------------------------------------------------------------

bool begin_context(ImGuiContext** previous)
{
    *previous = ImGui::GetCurrentContext();
    if (!state().context) {
        if (state().context_failed) {
            return false;
        }
        state().context = ImGui::CreateContext();
        ImGui::SetCurrentContext(state().context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        // (a context has no font of its own - NewFrame needs a built one: JetBrains Mono, compressed into the exe (the
        // build has no ImGui default font), scaled to the window as 720 lines, built before the backends start)
        int height = 0;
        if (SDL_Window* window = sys::sdl_window()) {
            SDL_GetWindowSize(window, nullptr, &height);
        }
        const frontend::ui_fonts::CompressedFont mono = frontend::ui_fonts::get_mono();
        io.Fonts->AddFontFromMemoryCompressedTTF(
            mono.data, mono.size, kFontPixels * std::max(1.0F, static_cast<float>(height) / kBaseHeight));
        io.Fonts->Build();
        if (!frontend::ui_backend::prepare_all()) {
            ImGui::DestroyContext(state().context);
            state().context = nullptr;
            state().context_failed = true;
            ImGui::SetCurrentContext(*previous);
            get_log("console: ImGui couldn't start on the game's window / device - off");
            return false;
        }
    }
    ImGui::SetCurrentContext(state().context);
    return true;
}

void handle_event(const input::Event& event);

void set_open(bool open)
{
    State& s = state();
    s.open = open;
    s.focus_input = open;
    // (while open, every event is the console's alone - the other handlers (the menus' shortcuts, the editor's keys)
    // see none of what is typed; the game's own reading of the keys and pads stands aside: blocks_game_input)
    input::capture_events(open ? handle_event : nullptr);
    if (SDL_Window* window = sys::sdl_window()) {
        if (open) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

int on_input_line(ImGuiInputTextCallbackData* data)
{
    State& s = state();
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || s.history.empty()) {
        return 0;
    }
    const int count = static_cast<int>(s.history.size());
    if (data->EventKey == ImGuiKey_UpArrow) {
        s.history_at = s.history_at < 0 ? count - 1 : std::max(0, s.history_at - 1);
    } else if (data->EventKey == ImGuiKey_DownArrow && s.history_at >= 0) {
        s.history_at = s.history_at + 1 < count ? s.history_at + 1 : -1;
    }
    data->DeleteChars(0, data->BufTextLen);
    if (s.history_at >= 0) {
        data->InsertChars(0, s.history[static_cast<size_t>(s.history_at)].c_str());
    }
    return 0;
}

void draw()
{
    State& s = state();
    if (!s.open) {
        return;
    }
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous)) {
        s.open = false;
        return;
    }
    frontend::ui_backend::new_frame();
    ImGui::NewFrame();
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y * 0.5F));
    ImGui::SetNextWindowBgAlpha(0.88F);
    ImGui::Begin("Console", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    const float input_height = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("lines", ImVec2(0.0F, -input_height), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& line : s.lines) {
        ImGui::TextUnformatted(line.c_str());
    }
    if (s.scroll_to_end) {
        ImGui::SetScrollHereY(1.0F);
        s.scroll_to_end = false;
    }
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-1.0F);
    if (s.focus_input) {
        ImGui::SetKeyboardFocusHere();
        s.focus_input = false;
    }
    if (ImGui::InputText("##line", s.input.data(), s.input.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, on_input_line)) {
        const std::string line = s.input.data();
        s.input.fill('\0');
        if (!line.empty()) {
            s.history.push_back(line);
        }
        s.history_at = -1;
        (void)submit(line);
        s.focus_input = true;
    }
    ImGui::End();
    ImGui::Render();
    frontend::ui_backend::render();
    ImGui::SetCurrentContext(previous);
}

void handle_event(const input::Event& event)
{
    State& s = state();
    if (event.type == input::EventType::kKeyDown && !event.repeat && event.key == s.key) {
        set_open(!s.open);
        return;
    }
    if (!s.open) {
        return;
    }
    if (event.type == input::EventType::kKeyDown && event.key == SDL_SCANCODE_ESCAPE) {
        set_open(false);
        return;
    }
    // (the console's key typed as text: not into the line)
    if (event.type == input::EventType::kTextInput && event.native && std::strchr(event.native->text.text, '`')) {
        return;
    }
    if (event.native && s.context) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(s.context);
        frontend::ui_backend::process_event(event.native);
        ImGui::SetCurrentContext(previous);
    }
}

void prepare_input()
{
    if (!is_switched_on()) {
        get_log("console: off ([features] console=0)");
        return;
    }
    std::array<char, 64> setting = {""};
    build_info::fetch_text(build_info::kKeybinds, "Console", "", setting.data(), setting.size());
    state().key = sys::screenshot::parse_key(setting.data(), kGraveScancode, platform::scancode_from_name,
                                             platform::scancode_from_sdl2);
    get_log("console: %s opens it (scan code %d)", SDL_GetScancodeName(static_cast<SDL_Scancode>(state().key)),
            state().key);
    input::add_event_handler(handle_event);
    script::keep_qb_names(true); // (names for `scripts` - as the level editor keeps them)
}

void device_lost()
{
    if (!state().context) {
        return;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(state().context);
    frontend::ui_backend::invalidate_context_objects();
    ImGui::SetCurrentContext(previous);
}

// [features] console (default 1)
bool& switched()
{
    static bool on = config_int("features", "console", 1) != 0;
    return on;
}

class ConsoleExtension final : public ext::Extension {
public:
    void prepare_input() override { console::prepare_input(); }
    bool blocks_game_input() override { return state().open; }
    void draw_over_frame() override { draw(); }
    void device_lost() override { console::device_lost(); }
    bool* feature_switch(const char* key) override { return std::strcmp(key, "console") == 0 ? &switched() : nullptr; }
};

ConsoleExtension& the_extension()
{
    static ConsoleExtension console;
    return console;
}

} // namespace

bool is_switched_on() { return switched(); }

std::string run_command(const std::string& line)
{
    std::istringstream in(line);
    std::string command;
    in >> command;
    command = lower(command);
    std::string rest;
    std::getline(in >> std::ws, rest);
    if (command.empty()) {
        return "";
    }
    if (command == "help") {
        return kHelp;
    }
    if (command == "scripts") {
        return list_scripts(rest);
    }
    if (command == "demo") {
        std::istringstream words(rest);
        std::string action;
        std::string name;
        words >> action >> name;
        action = lower(action);
        if (action == "record") {
            return demo::record_now(name);
        }
        if (action == "stop") {
            return demo::stop_now();
        }
        if (action == "play") {
            return name.empty() ? "demo play <name>" : demo::play_now(name);
        }
        if (action == "view") {
            return name.empty() ? "demo view <name>" : demo::view_now(name);
        }
        return demo::status_text();
    }
    if (command == "viewer") {
        return demo_viewer::console_command(rest);
    }
    if (command == "compare") {
        return compare_split::console_command(rest);
    }
    if (command == "clear") {
        state().lines.clear();
        return "";
    }
    if (command == "rt") {
        return rt_check::console_command(rest);
    }
    if (command == "textures") {
        return render::texture_sets::console_command(rest);
    }
    return "unknown command: " + command + " (help lists them)";
}

std::string submit(const std::string& line)
{
    print("> " + line);
    std::string output = run_command(line);
    if (!output.empty()) {
        print(output);
    }
    return output;
}

ext::Extension& extension() { return the_extension(); }

} // namespace devtools::console
