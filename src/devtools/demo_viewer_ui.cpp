// NOT ORIGINAL - the demo viewer's panels (demo_viewer.h): the transport bar and the timeline at the bottom, the
// Camera / Keys / Compare panel on the right, the seek's progress - Dear ImGui, a context of its own (as the console's).

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "devtools/compare_split.h"
#include "devtools/demo_camera_path.h"
#include "devtools/demo_viewer_internal.h"
#include "engine/ui/ui_backend.h"
#include "engine/ui/ui_fonts.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "platform/sdl_window.h"

namespace devtools::demo_viewer {

namespace {

using Clock = std::chrono::steady_clock;

// The sizes, at 720 lines (scaled with the window's height).
constexpr float kBaseHeight = 720.0F;
constexpr float kTextPixels = 15.0F;
constexpr float kTitlePixels = 17.0F;
constexpr float kBarHeight = 96.0F;  // the bottom bar
constexpr float kSideWidth = 330.0F; // the right panel
constexpr float kTimelineHeight = 30.0F;
constexpr float kKeyGrab = 7.0F; // (a key's diamond grabbed this close)
constexpr float kDiamond = 6.0F;
constexpr float kSpeedWidth = 80.0F;
constexpr float kKeyFieldWidth = 140.0F;
constexpr float kValueFieldWidth = 80.0F;
constexpr float kProgressPadding = 14.0F;
constexpr float kRounding = 6.0F;
constexpr float kSmallRounding = 4.0F;
constexpr float kTrackRounding = 3.0F;
constexpr float kLineWidth = 2.0F;
constexpr ImVec2 kFramePadding = {8.0F, 4.0F};
constexpr ImVec2 kItemSpacing = {8.0F, 6.0F};
// The timeline's track: its band (a share of the height, from the top), the ticks' half heights.
constexpr float kBandTop = 0.3F;
constexpr float kBandBottom = 0.7F;
constexpr float kTickHalf = 0.15F;
constexpr float kLongTickHalf = 0.3F;
constexpr int kTickEvery = 60;      // a tick a second
constexpr int kLongTickEvery = 300; // a longer one every 5 s

constexpr double kFramesPerSecond = 60.0;
constexpr int kSecondsPerMinute = 60;
constexpr double kNoticeSeconds = 5.0;
constexpr double kReadSeconds = 1.0; // (the Compare panel's A values read again this often)
constexpr std::size_t kValueBytes = 64;
constexpr float kHalf = 0.5F;
constexpr float kFovMost = 150.0F;
constexpr float kFovLeast = 0.0F;
constexpr float kFovStep = 0.25F;
constexpr float kAngleStep = 0.25F;
constexpr float kLeastSpeed = 25.0F;
constexpr float kMostSpeed = 5000.0F;
constexpr float kLeastDistance = 40.0F;
constexpr float kMostDistance = 3000.0F;
constexpr float kLowestOrbit = -60.0F;
constexpr float kLeastDivider = 0.05F;
constexpr float kMostDivider = 0.95F;
constexpr float kKeyListLines = 7.0F;
constexpr double kPercent = 100.0;

constexpr ImU32 kAccentDim = IM_COL32(250, 160, 30, 70);
constexpr ImU32 kTrack = IM_COL32(255, 255, 255, 28);
constexpr ImU32 kLoading = IM_COL32(0, 0, 0, 90);
constexpr ImU32 kTick = IM_COL32(255, 255, 255, 60);
constexpr ImU32 kKey = IM_COL32(235, 235, 235, 255);
constexpr ImU32 kKeySelected = IM_COL32(250, 160, 30, 255);
constexpr ImU32 kPlayhead = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kGhost = IM_COL32(255, 255, 255, 120);
constexpr ImU32 kShade = IM_COL32(0, 0, 0, 170);
constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 240);
// The panels' colours: dark, the game's orange for what is chosen.
constexpr ImVec4 kAccent = {0.98F, 0.63F, 0.12F, 1.0F};
constexpr ImVec4 kAccentHover = {1.0F, 0.72F, 0.30F, 1.0F};
constexpr ImVec4 kAccentSoft = {0.98F, 0.63F, 0.12F, 0.35F};
constexpr ImVec4 kAccentMiddle = {0.98F, 0.63F, 0.12F, 0.55F};
constexpr ImVec4 kAccentStrong = {0.98F, 0.63F, 0.12F, 0.80F};
constexpr ImVec4 kWindow = {0.05F, 0.05F, 0.06F, 0.88F};
constexpr ImVec4 kButton = {0.18F, 0.18F, 0.20F, 1.0F};
constexpr ImVec4 kFrame = {0.14F, 0.14F, 0.16F, 1.0F};
constexpr ImVec4 kFrameHover = {0.22F, 0.22F, 0.25F, 1.0F};
constexpr ImVec4 kFrameActive = {0.28F, 0.28F, 0.30F, 1.0F};

// The [Graphics] keys the Compare panel lists first (any other can be typed).
constexpr std::array<const char*, 16> kCompareKeys = {
    "rt_shadows", "rt_shadow_softness", "rt_ao",    "rt_ao_strength", "rt_gi",  "rt_gi_strength",
    "rt_reflections", "rt_fog",         "rt_sun",   "rt_bloom",       "rt_shafts", "rt_sky",
    "rt_clouds",  "rt_exposure",        "rt_tonemap", "rt_saturation"};
// What is drawn the same on both sides whatever [Compare] says.
constexpr std::array<const char*, 9> kBothSides = {"renderer",        "ray_tracing", "rt_hdr",
                                                   "upscaler (off while split)", "rt_tessellation",
                                                   "rt_relit",        "msaa",        "anisotropy", "texture sets"};

using Field = std::array<char, kValueBytes>;

struct Ui {
    ImGuiContext* context = nullptr;
    ImFont* text = nullptr;
    ImFont* title = nullptr;
    float scale = 1.0F;
    // the timeline's drag: scrubbing (a frame asked for when let go) or a key moved
    int64_t scrub_frame = -1;
    int64_t drag_frame = -1;
    int drag_key = -1;
    bool scrubbing = false;
    bool dragged = false;
    bool failed = false;
    bool b_filled = false;
    // a line in the bar for a while (the video's start, a refusal)
    std::string notice;
    Clock::time_point notice_since;
    // the Compare panel's fields
    std::array<Field, kCompareKeys.size()> b_values{};
    std::array<std::string, kCompareKeys.size()> a_values;
    Clock::time_point a_read;
    Field custom_key{};
    Field custom_value{};
    Field fixed_name{};
};

Ui& ui()
{
    static core::NoDestroy<Ui> value;
    return value.get();
}

// ImGui's text calls without a format string (no C varargs).
void dim_text(const std::string& text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}
void wrapped_text(const std::string& text)
{
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}
void tooltip(const std::string& text)
{
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(text.c_str());
    ImGui::EndTooltip();
}

void copy_into(Field* field, const std::string& text)
{
    std::ranges::fill(*field, '\0');
    const auto length = static_cast<std::ptrdiff_t>(std::min(text.size(), field->size() - 1));
    std::copy_n(text.begin(), length, field->begin());
}

void apply_style(float scale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = kRounding;
    style.FrameRounding = kSmallRounding;
    style.GrabRounding = kSmallRounding;
    style.TabRounding = kSmallRounding;
    style.WindowBorderSize = 0.0F;
    style.FramePadding = kFramePadding;
    style.ItemSpacing = kItemSpacing;
    style.Colors[ImGuiCol_WindowBg] = kWindow;
    style.Colors[ImGuiCol_Button] = kButton;
    style.Colors[ImGuiCol_ButtonHovered] = kAccentHover;
    style.Colors[ImGuiCol_ButtonActive] = kAccent;
    style.Colors[ImGuiCol_FrameBg] = kFrame;
    style.Colors[ImGuiCol_FrameBgHovered] = kFrameHover;
    style.Colors[ImGuiCol_FrameBgActive] = kFrameActive;
    style.Colors[ImGuiCol_CheckMark] = kAccent;
    style.Colors[ImGuiCol_SliderGrab] = kAccent;
    style.Colors[ImGuiCol_SliderGrabActive] = kAccentHover;
    style.Colors[ImGuiCol_Header] = kAccentSoft;
    style.Colors[ImGuiCol_HeaderHovered] = kAccentMiddle;
    style.Colors[ImGuiCol_HeaderActive] = kAccent;
    style.Colors[ImGuiCol_Tab] = kFrame;
    style.Colors[ImGuiCol_TabHovered] = kAccentHover;
    style.Colors[ImGuiCol_TabSelected] = kAccentStrong;
    style.ScaleAllSizes(scale);
}

bool begin_context(ImGuiContext** previous)
{
    Ui& panels = ui();
    *previous = ImGui::GetCurrentContext();
    if (panels.context == nullptr) {
        if (panels.failed) {
            return false;
        }
        panels.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(panels.context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        int height = 0;
        if (SDL_Window* window = sys::sdl_window()) {
            SDL_GetWindowSize(window, nullptr, &height);
        }
        panels.scale = std::max(1.0F, static_cast<float>(height) / kBaseHeight);
        const frontend::ui_fonts::CompressedFont regular = frontend::ui_fonts::inter();
        const frontend::ui_fonts::CompressedFont bold = frontend::ui_fonts::inter_semi_bold();
        panels.text = io.Fonts->AddFontFromMemoryCompressedTTF(regular.data, regular.size, kTextPixels * panels.scale);
        panels.title = io.Fonts->AddFontFromMemoryCompressedTTF(bold.data, bold.size, kTitlePixels * panels.scale);
        io.Fonts->Build();
        apply_style(panels.scale);
        if (!frontend::ui_backend::prepare_all()) {
            ImGui::DestroyContext(panels.context);
            panels.context = nullptr;
            panels.failed = true;
            ImGui::SetCurrentContext(*previous);
            get_log("viewer: ImGui couldn't start on the game's window / device - no panels");
            return false;
        }
    }
    ImGui::SetCurrentContext(panels.context);
    return true;
}

std::string time_text(int64_t frame)
{
    const double seconds = static_cast<double>(std::max<int64_t>(frame, 0)) / kFramesPerSecond;
    const auto minutes = static_cast<int>(seconds / kSecondsPerMinute);
    return text::format("%d:%05.2f", minutes, seconds - static_cast<double>(minutes * kSecondsPerMinute));
}

void show_notice(const std::string& text)
{
    ui().notice = text;
    ui().notice_since = Clock::now();
}

// The timeline's geometry: where a frame is across it and the frame under an x.
struct TimelineSpan {
    ImVec2 corner;
    float width = 0.0F;
    float height = 0.0F;
    float inset = 0.0F;
    double last = 1.0;

    [[nodiscard]] float x_of(double frame) const
    {
        return corner.x + inset + (static_cast<float>(frame / last) * (width - inset - inset));
    }
    [[nodiscard]] int64_t frame_at(float x) const
    {
        const float share = std::clamp((x - corner.x - inset) / (width - inset - inset), 0.0F, 1.0F);
        return std::lround(static_cast<double>(share) * last);
    }
    [[nodiscard]] float band(float share) const { return corner.y + (height * share); }
};

void draw_track(const TimelineSpan& span, ImDrawList* list)
{
    const State& view = state();
    const float scale = ui().scale;
    list->AddRectFilled(ImVec2(span.corner.x, span.band(kBandTop)), ImVec2(span.corner.x + span.width, span.band(kBandBottom)),
                        kTrack, kTrackRounding * scale);
    list->AddRectFilled(ImVec2(span.corner.x, span.band(kBandTop)),
                        ImVec2(span.x_of(view.playback.up_frame), span.band(kBandBottom)), kLoading, kTrackRounding * scale);
    const camera_path::Path& path = view.path;
    if (path.loop_in >= 0 && path.loop_out > path.loop_in) {
        list->AddRectFilled(ImVec2(span.x_of(path.loop_in), span.corner.y),
                            ImVec2(span.x_of(path.loop_out), span.corner.y + span.height), view.loop ? kAccentDim : kTrack);
    }
    for (int frame = 0; frame <= static_cast<int>(span.last); frame += kTickEvery) {
        const float x = span.x_of(frame);
        const float tall = frame % kLongTickEvery == 0 ? kLongTickHalf : kTickHalf;
        list->AddLine(ImVec2(x, span.band(kHalf - tall)), ImVec2(x, span.band(kHalf + tall)), kTick);
    }
}

// The keys' diamonds (the one dragged where it's going): the one under the mouse, -1 none.
int draw_keys(const TimelineSpan& span, ImDrawList* list)
{
    const State& view = state();
    const Ui& panels = ui();
    const float radius = kDiamond * panels.scale;
    const float mid = span.band(kHalf);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hovered = -1;
    for (std::size_t i = 0; i < view.path.keys.size(); ++i) {
        const auto index = static_cast<int>(i);
        const int64_t frame =
            panels.drag_key == index && panels.dragged ? panels.drag_frame : core::at(view.path.keys, i).frame;
        const float x = span.x_of(static_cast<double>(frame));
        list->AddQuadFilled(ImVec2(x, mid - radius), ImVec2(x + radius, mid), ImVec2(x, mid + radius),
                            ImVec2(x - radius, mid), index == view.selected_key ? kKeySelected : kKey);
        if (ImGui::IsItemHovered() && std::abs(mouse.x - x) <= kKeyGrab * panels.scale) {
            hovered = index;
        }
    }
    return hovered;
}

// The timeline let go: a seek to where it was scrubbed, a key moved, or a key clicked (shown).
void timeline_released()
{
    State& view = state();
    Ui& panels = ui();
    if (panels.scrubbing && panels.scrub_frame >= 0) {
        seek(panels.scrub_frame, !view.paused);
    } else if (panels.drag_key >= 0) {
        auto index = static_cast<std::size_t>(panels.drag_key);
        if (!panels.dragged) {
            seek(core::at(view.path.keys, index).frame, false);
        } else if (camera_path::move_key(&view.path, &index, static_cast<int32_t>(panels.drag_frame))) {
            view.selected_key = static_cast<int>(index);
            path_changed();
        } else {
            show_notice("A key is already at that frame");
        }
    }
    panels.scrubbing = false;
    panels.scrub_frame = -1;
    panels.drag_key = -1;
    panels.dragged = false;
}

// The timeline: the demo's frames across, the level's loading dimmed, the loop region, a tick a second, the keys'
// diamonds, the frame shown; a click or a drag seeks (when let go), a key dragged moves it, a key clicked is selected.
void timeline()
{
    State& view = state();
    Ui& panels = ui();
    TimelineSpan span;
    span.corner = ImGui::GetCursorScreenPos();
    span.width = std::max(kTimelineHeight, ImGui::GetContentRegionAvail().x);
    span.height = kTimelineHeight * panels.scale;
    span.inset = kDiamond * panels.scale;
    span.last = static_cast<double>(std::max(1U, last_frame()));
    ImGui::InvisibleButton("##timeline", ImVec2(span.width, span.height));
    ImDrawList* list = ImGui::GetWindowDrawList();
    draw_track(span, list);
    const int hovered = draw_keys(span, list);
    const float now_x = span.x_of(frame_shown());
    list->AddLine(ImVec2(now_x, span.corner.y), ImVec2(now_x, span.corner.y + span.height), kPlayhead,
                  kLineWidth * panels.scale);
    const int64_t ghost = panels.scrubbing ? panels.scrub_frame : view.target;
    if (ghost >= 0) {
        const float x = span.x_of(static_cast<double>(ghost));
        list->AddLine(ImVec2(x, span.corner.y), ImVec2(x, span.corner.y + span.height), kGhost, kLineWidth * panels.scale);
    }
    if (ImGui::IsItemActivated()) {
        panels.dragged = false;
        if (hovered >= 0) {
            panels.drag_key = hovered;
            panels.drag_frame = core::at(view.path.keys, static_cast<std::size_t>(hovered)).frame;
            view.selected_key = hovered;
        } else {
            panels.scrubbing = true;
        }
    }
    if (ImGui::IsItemActive()) {
        const int64_t frame = span.frame_at(ImGui::GetIO().MousePos.x);
        if (panels.scrubbing) {
            panels.scrub_frame = frame;
        } else if (panels.drag_key >= 0 && frame != panels.drag_frame) {
            panels.drag_frame = frame;
            panels.dragged = true;
        }
        if (panels.scrubbing || panels.dragged) {
            const int64_t shown = panels.scrubbing ? panels.scrub_frame : panels.drag_frame;
            tooltip(time_text(shown) + "  (frame " + std::to_string(shown) + ")");
        }
    }
    if (ImGui::IsItemDeactivated()) {
        timeline_released();
    }
}

void transport_buttons()
{
    const State& view = state();
    const Ui& panels = ui();
    if (ImGui::Button("|<")) {
        seek(view.playback.up_frame, !view.paused);
    }
    ImGui::SameLine();
    if (ImGui::Button("<")) {
        step_frames(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button(view.paused ? " PLAY " : "PAUSE")) {
        toggle_play();
    }
    ImGui::SameLine();
    if (ImGui::Button(">")) {
        step_frames(1);
    }
    ImGui::SameLine();
    if (ImGui::Button(">|")) {
        seek(last_frame(), false);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kSpeedWidth * panels.scale);
    const std::string speed = text::format("%.2gx", static_cast<double>(view.speed));
    if (ImGui::BeginCombo("##speed", speed.c_str())) {
        for (const float each : kSpeeds) {
            if (ImGui::Selectable(text::format("%.2gx", static_cast<double>(each)).c_str(), each == view.speed)) {
                set_speed(each);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::PushFont(panels.title);
    ImGui::TextUnformatted((time_text(frame_shown()) + " / " + time_text(last_frame())).c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    dim_text(std::to_string(frame_shown()));
}

void transport_bar()
{
    State& view = state();
    const Ui& panels = ui();
    const ImGuiIO& io = ImGui::GetIO();
    const float height = kBarHeight * panels.scale;
    ImGui::SetNextWindowPos(ImVec2(0.0F, io.DisplaySize.y - height));
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - (kSideWidth * panels.scale), height));
    ImGui::Begin("##viewer_bar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    transport_buttons();
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &view.loop);
    ImGui::SameLine();
    if (ImGui::Button("In")) {
        set_loop_in();
    }
    ImGui::SameLine();
    if (ImGui::Button("Out")) {
        set_loop_out();
    }
    ImGui::SameLine();
    ImGui::Checkbox("HUD", &view.hud);
    ImGui::SameLine();
    bool split = compare_split::is_on();
    if (ImGui::Checkbox("Split", &split)) {
        compare_split::set_on(split);
    }
    ImGui::SameLine();
    if (ImGui::Button("+ KEY")) {
        add_key_here();
    }
    ImGui::SameLine();
    if (ImGui::Button("SAVE AS VIDEO")) {
        show_notice(start_export(""));
    }
    ImGui::SameLine();
    if (ImGui::Button("EXIT")) {
        leave();
    }
    timeline();
    if (!panels.notice.empty() && std::chrono::duration<double>(Clock::now() - panels.notice_since).count() < kNoticeSeconds) {
        ImGui::TextUnformatted(panels.notice.c_str());
    } else {
        dim_text("Space play   Left / Right frame (Shift: 1 s)   [ ] speed   C camera   K key   I / O loop   U HUD   "
                 "V split   H hide   Esc exit");
    }
    ImGui::End();
}

void mode_settings()
{
    State& view = state();
    switch (view.mode) {
    case CameraMode::kFree:
        wrapped_text("WASD move, Q / E down / up, the right mouse button looks, the wheel sets the speed (Shift faster, "
                     "Ctrl slower). A pad's sticks and triggers too.");
        ImGui::SliderFloat("Speed", &view.free_speed, kLeastSpeed, kMostSpeed, "%.0f", ImGuiSliderFlags_Logarithmic);
        break;
    case CameraMode::kOrbit:
        wrapped_text("Around the skater: the right mouse button turns, the wheel sets the distance.");
        ImGui::Checkbox("Follow the skater's heading", &view.orbit_follow);
        ImGui::SliderFloat("Distance", &view.orbit_distance, kLeastDistance, kMostDistance, "%.0f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Height", &view.orbit_pitch, kLowestOrbit, camera_path::kMostPitch, "%.0f deg");
        break;
    case CameraMode::kGame:
        wrapped_text("The game's own camera, as recorded.");
        break;
    case CameraMode::kPath:
        wrapped_text("The keyframes (the Keys tab): the camera moves through them as the demo plays.");
        break;
    case CameraMode::kFixed:
    default:
        break;
    }
}

void fixed_cameras()
{
    State& view = state();
    Ui& panels = ui();
    ImGui::PushFont(panels.title);
    ImGui::TextUnformatted("Fixed cameras");
    ImGui::PopFont();
    for (std::size_t i = 0; i < view.path.fixed.size(); ++i) {
        const auto index = static_cast<int>(i);
        const std::string& name = core::at(view.path.fixed, i).name;
        if (ImGui::Selectable((name + "##fixed" + std::to_string(i)).c_str(),
                              view.mode == CameraMode::kFixed && view.fixed == index)) {
            view.fixed = index;
            view.mode = CameraMode::kFixed;
            copy_into(&panels.fixed_name, name);
        }
    }
    if (ImGui::Button("Place one here")) {
        add_fixed_here();
    }
    if (view.mode != CameraMode::kFixed || view.fixed < 0 || std::cmp_greater_equal(view.fixed, view.path.fixed.size())) {
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        view.path.fixed.erase(std::next(view.path.fixed.begin(), view.fixed));
        view.fixed = 0;
        view.mode = CameraMode::kGame;
        path_changed();
    } else if (ImGui::InputText("Name", panels.fixed_name.data(), panels.fixed_name.size())) {
        core::at(view.path.fixed, static_cast<std::size_t>(view.fixed)).name = panels.fixed_name.data();
        path_changed();
    }
    dim_text("(a name starting with \"track\" keeps the skater in view)");
}

void camera_tab()
{
    State& view = state();
    for (int i = 0; i <= static_cast<int>(CameraMode::kPath); ++i) {
        const auto mode = static_cast<CameraMode>(i);
        std::string name = camera_name(mode);
        name.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(name.front())));
        if (ImGui::RadioButton(name.c_str(), view.mode == mode)) {
            set_mode(mode);
        }
        if (i < static_cast<int>(CameraMode::kPath)) {
            ImGui::SameLine();
        }
    }
    ImGui::SliderFloat("FOV", &view.fov, kFovLeast, kFovMost, view.fov <= 1.0F ? "game's" : "%.0f deg");
    ImGui::Separator();
    mode_settings();
    ImGui::Separator();
    fixed_cameras();
}

// The selected key's fields (true when one changed).
bool key_fields(camera_path::Key* key)
{
    const State& view = state();
    bool changed = false;
    std::array<float, 3> position = {key->pose.position.x, key->pose.position.y, key->pose.position.z};
    if (ImGui::DragFloat3("Position", position.data(), 1.0F)) {
        key->pose.position = {.x = std::get<0>(position), .y = std::get<1>(position), .z = std::get<2>(position)};
        changed = true;
    }
    std::array<float, 3> angles = {key->pose.yaw, key->pose.pitch, key->pose.roll};
    if (ImGui::DragFloat3("Yaw pitch roll", angles.data(), kAngleStep)) {
        key->pose.yaw = std::get<0>(angles);
        key->pose.pitch = std::clamp(std::get<1>(angles), -camera_path::kMostPitch, camera_path::kMostPitch);
        key->pose.roll = std::get<2>(angles);
        changed = true;
    }
    changed = ImGui::DragFloat("FOV##key", &key->pose.fov, kFovStep, kFovLeast, kFovMost, "%.1f") || changed;
    int ease = static_cast<int>(key->ease);
    if (ImGui::Combo("To the next", &ease, "smooth\0linear\0ease\0hold\0")) {
        key->ease = static_cast<camera_path::Ease>(ease);
        changed = true;
    }
    if (ImGui::Button("Set from the view")) {
        key->pose = view.shown;
        changed = true;
    }
    return changed;
}

void keys_tab()
{
    State& view = state();
    if (ImGui::Button("Add a key here (K)")) {
        add_key_here();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete (Del)")) {
        delete_selected_key();
    }
    ImGui::SameLine();
    if (ImGui::Button("Watch")) {
        set_mode(CameraMode::kPath);
    }
    ImGui::BeginChild("##keys", ImVec2(0.0F, ImGui::GetTextLineHeightWithSpacing() * kKeyListLines), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < view.path.keys.size(); ++i) {
        const camera_path::Key& key = core::at(view.path.keys, i);
        const std::string label = text::format("%2zu   %s   frame %d   %s##key%zu", i, time_text(key.frame).c_str(),
                                               key.frame, camera_path::ease_name(key.ease), i);
        if (ImGui::Selectable(label.c_str(), std::cmp_equal(view.selected_key, i))) {
            view.selected_key = static_cast<int>(i);
            seek(key.frame, false);
        }
    }
    if (view.path.keys.empty()) {
        dim_text("No keys: move a camera, then K.");
    }
    ImGui::EndChild();
    if (view.selected_key < 0 || std::cmp_greater_equal(view.selected_key, view.path.keys.size())) {
        return;
    }
    auto index = static_cast<std::size_t>(view.selected_key);
    camera_path::Key& key = core::at(view.path.keys, index);
    int frame = key.frame;
    if (ImGui::InputInt("Frame", &frame) && camera_path::move_key(&view.path, &index, frame)) {
        view.selected_key = static_cast<int>(index);
        path_changed();
        return;
    }
    if (key_fields(&key)) {
        path_changed();
    }
}

// The A column read from the ini (again once a second), the B fields from the overrides once.
void refresh_compare_values()
{
    Ui& panels = ui();
    if (std::chrono::duration<double>(Clock::now() - panels.a_read).count() > kReadSeconds) {
        panels.a_read = Clock::now();
        for (std::size_t i = 0; i < kCompareKeys.size(); ++i) {
            Field value{};
            build_info::fetch_text(build_info::kGraphics, core::at(kCompareKeys, i), "", value.data(), value.size());
            core::at(panels.a_values, i) = value.data();
        }
    }
    if (panels.b_filled) {
        return;
    }
    panels.b_filled = true;
    for (const auto& [key, value] : compare_split::overrides()) {
        for (std::size_t i = 0; i < kCompareKeys.size(); ++i) {
            if (key == core::at(kCompareKeys, i)) {
                copy_into(&core::at(panels.b_values, i), value);
            }
        }
    }
}

void compare_table()
{
    Ui& panels = ui();
    if (!ImGui::BeginTable("##compare", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("[Graphics]");
    ImGui::TableSetupColumn("A");
    ImGui::TableSetupColumn("B");
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < kCompareKeys.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(core::at(kCompareKeys, i));
        ImGui::TableNextColumn();
        const std::string& side_a = core::at(panels.a_values, i);
        dim_text(side_a.empty() ? "-" : side_a);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        Field& field = core::at(panels.b_values, i);
        if (ImGui::InputText(("##b" + std::to_string(i)).c_str(), field.data(), field.size(),
                             ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
            compare_split::set_override(core::at(kCompareKeys, i), field.data());
        }
    }
    ImGui::EndTable();
}

void compare_tab()
{
    Ui& panels = ui();
    bool on = compare_split::is_on();
    if (ImGui::Checkbox("Split: A left, B right", &on)) {
        compare_split::set_on(on);
    }
    float divider = compare_split::divider();
    if (ImGui::SliderFloat("Divider", &divider, kLeastDivider, kMostDivider, "%.2f")) {
        compare_split::set_divider(divider);
    }
    bool labels = compare_split::labels();
    if (ImGui::Checkbox("Labels", &labels)) {
        compare_split::set_labels(labels);
    }
    if (on && !compare_split::renderer_splits()) {
        wrapped_text("This renderer has no effects to split: both sides are the same (Vulkan has them).");
    }
    refresh_compare_values();
    compare_table();
    ImGui::SetNextItemWidth(kKeyFieldWidth * panels.scale);
    ImGui::InputText("##custom_key", panels.custom_key.data(), panels.custom_key.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(kValueFieldWidth * panels.scale);
    ImGui::InputText("##custom_value", panels.custom_value.data(), panels.custom_value.size());
    ImGui::SameLine();
    if (ImGui::Button("Set B") && panels.custom_key.front() != '\0') {
        compare_split::set_override(panels.custom_key.data(), panels.custom_value.data());
    }
    std::string b_text;
    for (const auto& [key, value] : compare_split::overrides()) {
        if (!b_text.empty()) {
            b_text += ", ";
        }
        b_text += key;
        b_text += '=';
        b_text += value;
    }
    wrapped_text("B: " + (b_text.empty() ? std::string("as A") : b_text));
    ImGui::Separator();
    std::string both;
    for (const char* key : kBothSides) {
        if (!both.empty()) {
            both += ", ";
        }
        both += key;
    }
    for (const std::string& key : compare_split::both_sides()) {
        both += ", ";
        both += key;
        both += " (asked for B)";
    }
    wrapped_text("Both sides: " + both);
}

void side_panel()
{
    const Ui& panels = ui();
    const ImGuiIO& io = ImGui::GetIO();
    const float width = kSideWidth * panels.scale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - width, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(width, io.DisplaySize.y));
    ImGui::Begin("##viewer_side", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
    ImGui::PushFont(panels.title);
    ImGui::TextUnformatted(("REPLAY  " + state().playback.name).c_str());
    ImGui::PopFont();
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Camera")) {
            camera_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Keys")) {
            keys_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Compare")) {
            compare_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// A seek's progress in the middle of the frame (shown with the panels hidden too).
void seek_progress()
{
    const State& view = state();
    if (view.target < 0 && !view.restarting) {
        return;
    }
    const Ui& panels = ui();
    const ImGuiIO& io = ImGui::GetIO();
    std::string line = "LOADING";
    if (!view.restarting && view.target >= 0) {
        const auto whole = static_cast<double>(view.target + 1 - view.seek_started_frame);
        const double done = static_cast<double>(frame_shown() + 1) - static_cast<double>(view.seek_started_frame);
        const double share = whole > 0.0 ? std::clamp(done / whole, 0.0, 1.0) : 1.0;
        line = "SEEKING  " + std::to_string(static_cast<int>(share * kPercent)) + "%";
    }
    ImDrawList* list = ImGui::GetForegroundDrawList();
    ImGui::PushFont(panels.title);
    const ImVec2 size = ImGui::CalcTextSize(line.c_str());
    const float pad = kProgressPadding * panels.scale;
    const ImVec2 corner(((io.DisplaySize.x - size.x) * kHalf) - pad, ((io.DisplaySize.y - size.y) * kHalf) - pad);
    list->AddRectFilled(corner, ImVec2(corner.x + size.x + pad + pad, corner.y + size.y + pad + pad), kShade,
                        kRounding * panels.scale);
    list->AddText(ImVec2(corner.x + pad, corner.y + pad), kWhite, line.c_str());
    ImGui::PopFont();
}

// Runs `body` with the panels' context current (nothing, false: no context yet).
template <typename Body>
bool with_context(Body body)
{
    const Ui& panels = ui();
    if (panels.context == nullptr) {
        return false;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(panels.context);
    const bool result = body();
    ImGui::SetCurrentContext(previous);
    return result;
}

} // namespace

void draw_panels()
{
    const State& view = state();
    if (view.exporting) {
        return;
    }
    ImGuiContext* previous = nullptr;
    if (!begin_context(&previous)) {
        return;
    }
    frontend::ui_backend::new_frame();
    ImGui::NewFrame();
    if (view.ui_visible) {
        transport_bar();
        side_panel();
    }
    seek_progress();
    ImGui::Render();
    frontend::ui_backend::render();
    ImGui::SetCurrentContext(previous);
}

bool panel_event(const void* native_event, bool /*mouse*/, bool /*keyboard*/)
{
    return with_context(
        [native_event] { return frontend::ui_backend::process_event(static_cast<const SDL_Event*>(native_event)); });
}

bool panels_want_mouse()
{
    return with_context([] { return ImGui::GetIO().WantCaptureMouse; });
}

bool panels_want_keyboard()
{
    return state().ui_visible && with_context([] { return ImGui::GetIO().WantTextInput; });
}

void panels_device_lost()
{
    with_context([] {
        frontend::ui_backend::invalidate_context_objects();
        return true;
    });
}

} // namespace devtools::demo_viewer
