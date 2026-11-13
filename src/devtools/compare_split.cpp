// NOT ORIGINAL - a split comparison (compare_split.h).

#include "devtools/compare_split.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <istream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "devtools/demo_overlay.h"
#include "engine/ext/extension.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/viewport.h"
#include "engine/pad_input/input_event.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"

namespace devtools::compare_split {

namespace {

constexpr float kLeast = 0.05F; // (the divider's range: neither side thinner than this)
constexpr float kMost = 0.95F;
constexpr float kNudge = 0.01F;
constexpr int kGrabPixels = 6; // (the mouse grabs the divider this close)
constexpr std::size_t kSectionBytes = 32768;
constexpr float kDefaultDivider = 0.5F;
constexpr int kPerCent = 100;

struct State {
    bool on = false;
    bool prepared = false; // (the backend's two sides made)
    bool splits = false;   // (the backend has a split: Vulkan's)
    float divider = kDefaultDivider;
    bool labels = true;
    bool dragging = false;
    std::vector<std::pair<std::string, std::string>> overrides;
    std::vector<std::string> both_sides;
};

State& state()
{
    static core::NoDestroy<State> value;
    return value.get();
}

// [Compare]'s lines: side B's [Graphics] values.
std::vector<std::pair<std::string, std::string>> read_section()
{
    std::vector<std::pair<std::string, std::string>> settings;
    std::vector<char> buffer(kSectionBytes);
    const DWORD length = GetPrivateProfileSectionA("Compare", buffer.data(), static_cast<DWORD>(buffer.size()),
                                                   game_folder_path(build_info::kFileName).c_str());
    std::size_t at = 0;
    while (at < length) {
        const std::string line(&core::at(buffer, at));
        at += line.size() + 1;
        const std::size_t equals = line.find('=');
        if (line.empty() || line.front() == ';' || equals == std::string::npos) {
            continue;
        }
        settings.emplace_back(line.substr(0, equals), line.substr(equals + 1));
    }
    return settings;
}

// The backend's sides made again from the overrides (while on).
void prepare()
{
    State& split = state();
    split.both_sides.clear();
    split.splits = render::graphics_options::compare_prepare(split.overrides, &split.both_sides);
    split.prepared = true;
    for (const std::string& key : split.both_sides) {
        get_log("compare: [Compare] %s can't differ per side - drawn as [Graphics] has it on both", key.c_str());
    }
}

std::string summary_of(const std::vector<std::pair<std::string, std::string>>& settings)
{
    std::string text;
    for (const auto& [key, value] : settings) {
        if (!text.empty()) {
            text += ", ";
        }
        text += key;
        text += '=';
        text += value;
    }
    return text;
}

int window_width()
{
    int width = 0;
    if (SDL_Window* window = sys::sdl_window()) {
        SDL_GetWindowSize(window, &width, nullptr);
    }
    return width;
}

void handle_event(const input::Event& event)
{
    State& split = state();
    if (!split.on || event.other_window) {
        return;
    }
    const int width = window_width();
    if (width <= 0) {
        return;
    }
    const float line_x = split.divider * static_cast<float>(width);
    switch (event.type) {
    case input::EventType::kMouseButtonDown:
        if (event.button == input::kMouseLeft && std::abs(static_cast<float>(event.x) - line_x) <= kGrabPixels) {
            split.dragging = true;
        }
        break;
    case input::EventType::kMouseMotion:
        if (split.dragging) {
            set_divider(static_cast<float>(event.x) / static_cast<float>(width));
        }
        break;
    case input::EventType::kMouseButtonUp:
        if (event.button == input::kMouseLeft) {
            split.dragging = false;
        }
        break;
    case input::EventType::kKeyDown:
        if ((event.modifiers & input::kModifierAlt) != 0U &&
            (event.key == SDL_SCANCODE_LEFT || event.key == SDL_SCANCODE_RIGHT)) {
            set_divider(split.divider + (event.key == SDL_SCANCODE_LEFT ? -kNudge : kNudge));
        }
        break;
    default:
        break;
    }
}

void start()
{
    State& split = state();
    {
        constexpr std::size_t kLongest = 32;
        std::array<char, kLongest> text{};
        build_info::fetch_text(build_info::kGraphics, "compare_divider", "0.5", text.data(), text.size());
        char* end = nullptr;
        const double value = std::strtod(text.data(), &end);
        split.divider = std::clamp(end != text.data() && std::isfinite(value) ? static_cast<float>(value) : kDefaultDivider,
                                   kLeast, kMost);
    }
    split.labels = build_info::fetch_flag(build_info::kGraphics, "compare_labels", true);
    split.overrides = read_section();
    input::add_event_handler(handle_event);
    if (build_info::fetch_flag(build_info::kGraphics, "compare_split", false)) {
        set_on(true);
        get_log("compare: [Graphics] compare_split=1 - A ([Graphics]) left of %.0f%%, B right: %s", split.divider * kPerCent,
                split.overrides.empty() ? "nothing differs ([Compare] is empty)" : summary_of(split.overrides).c_str());
    }
}

class CompareExtension final : public ext::Extension {
public:
    void prepare_input() override { start(); }
    // (the first viewport twice - one viewport only: split screen's two players keep theirs)
    int view_count() override { return state().on && modules::viewport_count() == 1 ? 2 : 0; }
    bool begin_view(int index, ext::ViewPass* pass) override
    {
        const float x = state().divider;
        pass->rect = index == 0 ? std::array<float, 4>{{0.0F, 0.0F, x, 1.0F}} : std::array<float, 4>{{x, 0.0F, 1.0F - x, 1.0F}};
        pass->mode = 0; // (Lit: as the game draws)
        pass->fog = true;
        pass->crop = true;
        render::graphics_options::compare_apply(index);
        return true;
    }
    void end_views() override { render::graphics_options::compare_apply(0); }
    void draw_over_frame() override
    {
        const State& split = state();
        if (split.on && modules::viewport_count() == 1) {
            demo_overlay::draw_split(split.divider, "A", split.overrides.empty() ? "B" : "B  " + summary_of(split.overrides),
                                     split.labels);
        }
    }
    // (the overlay's device objects: the demos' extension drops them - demo_overlay::device_lost)
};

} // namespace

ext::Extension& extension()
{
    static CompareExtension compare;
    return compare;
}

bool is_on() { return state().on; }

void set_on(bool on)
{
    State& split = state();
    if (on == split.on) {
        return;
    }
    split.on = on;
    if (on) {
        prepare();
    } else {
        render::graphics_options::compare_end();
        split.prepared = false;
        split.dragging = false;
    }
    get_log("compare: the split %s", on ? "on" : "off");
}

float divider() { return state().divider; }
void set_divider(float x) { state().divider = std::clamp(std::isfinite(x) ? x : kDefaultDivider, kLeast, kMost); }
bool labels() { return state().labels; }
void set_labels(bool on) { state().labels = on; }

const std::vector<std::pair<std::string, std::string>>& overrides() { return state().overrides; }

void set_override(const std::string& key, const std::string& value)
{
    State& split = state();
    const auto found = std::ranges::find_if(split.overrides, [&key](const auto& each) { return each.first == key; });
    if (value.empty()) {
        if (found != split.overrides.end()) {
            split.overrides.erase(found);
        }
    } else if (found != split.overrides.end()) {
        found->second = value;
    } else {
        split.overrides.emplace_back(key, value);
    }
    if (split.on) {
        prepare();
    }
}

const std::vector<std::string>& both_sides() { return state().both_sides; }
bool renderer_splits() { return state().splits; }

std::string console_command(const std::string& args)
{
    std::istringstream words(args);
    std::string action;
    words >> action;
    State& split = state();
    if (action == "on" || action == "off") {
        set_on(action == "on");
        return std::string("split ") + (split.on ? "on" : "off");
    }
    if (action == "divider") {
        float x = kDefaultDivider;
        words >> x;
        set_divider(x);
        return text::format("divider at %.3f", static_cast<double>(split.divider));
    }
    if (action == "labels") {
        int on = 1;
        words >> on;
        set_labels(on != 0);
        return std::string("labels ") + (split.labels ? "on" : "off");
    }
    if (action == "set" || action == "unset") {
        std::string key;
        std::string value;
        words >> key;
        std::getline(words >> std::ws, value);
        if (key.empty() || (action == "set" && value.empty())) {
            return "compare set <key> <value> | compare unset <key>";
        }
        set_override(key, action == "set" ? value : "");
        return "B: " + summary_of(split.overrides);
    }
    if (action == "clear") {
        split.overrides.clear();
        if (split.on) {
            prepare();
        }
        return "B: nothing differs";
    }
    std::string both;
    for (const std::string& key : split.both_sides) {
        both += (both.empty() ? "" : ", ") + key;
    }
    return text::format("split %s, divider %.3f, labels %s; B: %s%s%s%s", split.on ? "on" : "off",
                        static_cast<double>(split.divider), split.labels ? "on" : "off",
                        split.overrides.empty() ? "nothing differs" : summary_of(split.overrides).c_str(),
                        both.empty() ? "" : "; both sides: ", both.c_str(),
                        split.on && !split.splits ? " (this renderer draws both sides the same)" : "");
}

} // namespace devtools::compare_split
