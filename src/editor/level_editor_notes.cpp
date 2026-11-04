// NOT ORIGINAL - the level editor's note pins (ours). See level_editor_notes.h.

#include "editor/level_editor_notes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <array>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "editor/level_editor.h"
#include "editor/level_editor_checks.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "editor/level_editor_ui.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"
#include "engine/core/no_destroy.h"

namespace frontend::level_editor::notes {

namespace {

constexpr std::size_t kMaxWordLength = 63; // the longest note name
constexpr std::size_t kNoteLineSize = 160;
constexpr std::size_t kColourDigits = 6; // a colour is written as 6 hex digits
constexpr float kRayLength = 20000.0F;   // how far the view ray is cast to find a point
constexpr float kPlaceAheadDistance = 800.0F;  // where a note goes when nothing is hit
constexpr float kSelectedLineThickness = 2.0F; // a note stem
constexpr float kLineThickness = 1.5F;
constexpr float kSelectedDotRadius = 7.0F;
constexpr float kDotRadius = 5.5F;
constexpr float kRingRadius = 9.5F; // the ring round a note at its target
constexpr std::uint32_t kRingColour = 0xFFFFFFFFU;
constexpr float kRingThickness = 1.5F;
constexpr float kFootDotRadius = 2.5F; // the dot at a stem foot
constexpr float kLabelOffsetX = 10.0F;
constexpr float kLabelOffsetY = 8.0F;
constexpr float kSwatchLeft = 0.25F; // an Outliner row colour dot, as fractions of the line height
constexpr float kSwatchRadius = 0.22F;
constexpr float kSwatchTextOffset = 0.6F;
constexpr float kGoToDistance = 400.0F; // how far from a note the camera stands when going to it
constexpr float kPaneInset = 12.0F;     // the Notes pane gap from the view corner, at scale 1
constexpr float kPaneWidth = 340.0F;
constexpr float kTextFieldHeight = 90.0F;
constexpr float kColourButtonSize = 18.0F;
constexpr float kSelectionOutlineGrow = 2.0F;
constexpr float kSelectionOutlineRounding = 3.0F;
constexpr float kSelectionOutlineThickness = 2.0F;
constexpr float kPositionDragStep = 5.0F;

} // namespace

namespace {

constexpr float kHalf = 0.5F; // half

} // namespace

// --- the pure parts -------------------------------------------------------------------------------------------------

bool get_same(const Note& a, const Note& b)
{
    return a.name == b.name && a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.colour == b.colour &&
           a.text == b.text;
}

std::string pack_up(const std::string& text)
{
    std::string out;
    for (const unsigned char character : text.substr(0, kMaxText)) {
        if (character == '%' || character == '"' || character == '#' || character < kFirstPrintableChar ||
            character == kDeleteChar) {
            std::array<char, 4> hex{};
            text::format_to(hex, "%%%02X", character);
            out += hex.data();
        } else {
            out += static_cast<char>(character);
        }
    }
    return out;
}

bool decode(const std::string& encoded, std::string* text)
{
    text->clear();
    for (size_t i = 0; i < encoded.size(); ++i) {
        const char character = core::at(encoded, i);
        if (character == '"' || character == '#') {
            return false;
        }
        if (character != '%') {
            *text += character;
            continue;
        }
        if (i + 2 >= encoded.size() || (std::isxdigit(static_cast<unsigned char>(core::at(encoded, i + 1))) == 0) ||
            (std::isxdigit(static_cast<unsigned char>(core::at(encoded, i + 2))) == 0)) {
            return false;
        }
        *text += static_cast<char>(std::strtol(encoded.substr(i + 1, 2).c_str(), nullptr, kHexBase));
        i += 2;
    }
    if (text->size() > kMaxText) {
        text->resize(kMaxText);
    }
    return true;
}

namespace {

bool valid_name(const std::string& name)
{
    if (name.empty() || name.size() > kMaxWordLength) { // (as layout_file.ts's validWord)
        return false;
    }
    return std::ranges::all_of(name, [](const char character) {
        return (std::isalnum(static_cast<unsigned char>(character)) != 0) || character == '_';
    });
}

bool read_triple(const std::string& text, V3* out)
{
    std::array<float, 3> v{};
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        core::at(v, i) = std::strtof(at, &end);
        if (end == at || !std::isfinite(core::at(v, i))) {
            return false;
        }
        at = end;
        if (i < 2) {
            if (*at != ',') {
                return false;
            }
            at = std::next(at);
        }
    }
    if (*at != '\0') {
        return false;
    }
    *out = {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2)};
    return true;
}

} // namespace

std::string write_record(const Note& note)
{
    std::array<char, kNoteLineSize> text{};
    text::format_to(text, "note %s pos=%.9g,%.9g,%.9g color=%06X text=\"", note.name.c_str(), note.pos.x, note.pos.y,
                    note.pos.z, note.colour & kRgbMask);
    return text.data() + pack_up(note.text) + "\"";
}

bool read_record(std::istringstream& in, Note* out)
{
    *out = Note{};
    if (!(in >> out->name) || !valid_name(out->name)) {
        return false;
    }
    std::string rest;
    std::getline(in, rest);
    bool pos = false;
    bool colour = false;
    bool text = false;
    size_t at = 0;
    while (true) {
        at = rest.find_first_not_of(' ', at);
        if (at == std::string::npos) {
            break;
        }
        if (text) {
            return false; // (text= is last)
        }
        const size_t equals = rest.find('=', at);
        if (equals == std::string::npos) {
            return false;
        }
        const std::string key = rest.substr(at, equals - at);
        if (key.find(' ') != std::string::npos) {
            return false;
        }
        if (key == "text") {
            if (equals + 1 >= rest.size() || core::at(rest, equals + 1) != '"') {
                return false;
            }
            const size_t close = rest.find('"', equals + 2);
            if (close == std::string::npos || !decode(rest.substr(equals + 2, close - equals - 2), &out->text)) {
                return false;
            }
            text = true;
            at = close + 1;
            continue;
        }
        const size_t end = rest.find(' ', equals);
        const std::string value =
            rest.substr(equals + 1, end == std::string::npos ? std::string::npos : end - equals - 1);
        at = end == std::string::npos ? rest.size() : end;
        if (key == "pos") {
            if (pos || !read_triple(value, &out->pos)) {
                return false;
            }
            pos = true;
        } else if (key == "color") {
            char* stop = nullptr;
            const unsigned long rgb = std::strtoul(value.c_str(), &stop, 16);
            if (colour || value.size() != kColourDigits || (*stop != '\0')) {
                return false;
            }
            out->colour = static_cast<uint32_t>(rgb);
            colour = true;
        } // (another key: a later version's - skipped)
    }
    return pos;
}

std::string fresh_name(const std::vector<Note>& notes)
{
    for (int suffix = 1;; ++suffix) {
        std::array<char, 32> name{};
        text::format_to(name, "%s%02d", kStem, suffix);
        if (std::ranges::none_of(notes, [&](const Note& note) { return note.name == name.data(); })) {
            return name.data();
        }
    }
}

std::string caption(const std::string& text, size_t most)
{
    std::string first = text.substr(0, text.find('\n'));
    while (!first.empty() && (first.back() == '\r' || first.back() == ' ')) {
        first.pop_back();
    }
    if (first.empty()) {
        return text.find_first_not_of(" \r\n") == std::string::npos ? "(empty note)" : "...";
    }
    if (first.size() > most) {
        first = first.substr(0, most) + "...";
    } else if (first.size() < text.size() && text.find_first_not_of(" \r\n", first.size()) != std::string::npos) {
        first += " ...";
    }
    return first;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

// NOT ORIGINAL grouping - level_editor_notes.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_notes_state()).
struct LevelEditorNotesState {
    std::vector<Note> notes_;
    std::string selected_; // (the note whose window is open; "" none)
    std::array<char, kMaxText + 1> edit_text_{}; // (the window's text while typed)
    std::string edit_for_; // (the note the text buffer holds)
    std::vector<Note> before_; // (the notes as they were when a field was taken: its undo step)
    std::vector<std::function<void()>> queue_;
};

LevelEditorNotesState& level_editor_notes_state()
{
    static core::NoDestroy<LevelEditorNotesState> state;
    return state.get();
}

struct NotesStep : ModuleStep {
private:
    std::vector<Note> notes;

public:
    explicit NotesStep(std::vector<Note> list) : notes(std::move(list)) {}
    void get_restore() override
    {
        level_editor_notes_state().notes_ = notes;
        level_editor_notes_state().edit_for_.clear(); // (the window reads the text again)
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override { return std::make_shared<NotesStep>(level_editor_notes_state().notes_); }
};

void push_undo() { push_module_undo(std::make_shared<NotesStep>(level_editor_notes_state().notes_)); }

Note* get_find(const std::string& name)
{
    auto it =
        std::ranges::find_if(level_editor_notes_state().notes_, [&](const Note& note) { return note.name == name; });
    return it == level_editor_notes_state().notes_.end() ? nullptr : &*it;
}

// Where the view looks: the first surface under the middle of the view (else 800 units ahead).
bool view_point(V3* point)
{
    ViewNow view{};
    if (!current_view(&view)) {
        return false;
    }
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    const V3 dir = make_unit(frame.back * -1.0F);
    V3 hit{};
    if (checks::get_cast(view.pos, view.pos + dir * kRayLength, {.point = &hit})) {
        *point = hit;
    } else {
        *point = view.pos + dir * kPlaceAheadDistance;
    }
    return true;
}

void add_here()
{
    V3 at{};
    if (!view_point(&at)) {
        return;
    }
    push_undo();
    Note note;
    note.name = fresh_name(level_editor_notes_state().notes_);
    note.pos = at;
    level_editor_notes_state().notes_.push_back(note);
    level_editor_notes_state().selected_ = note.name;
    level_editor_notes_state().edit_for_.clear();
    hud::get_message("Added note " + note.name);
}

ImU32 get_colour(uint32_t rgb, int alpha = 255)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

bool get_matches(const std::string& text, const char* filter)
{
    if (!filter || (*filter == '\0')) {
        return true;
    }
    std::string haystack = text;
    std::string needle = filter;
    auto lower = [](std::string* subject) {
        for (char& character : *subject) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
    };
    lower(&haystack);
    lower(&needle);
    return haystack.find(needle) != std::string::npos;
}

} // namespace

const std::vector<Note>& get_all() { return level_editor_notes_state().notes_; }

std::vector<Note> get_capture() { return level_editor_notes_state().notes_; }

int get_apply(const std::vector<Note>& notes)
{
    level_editor_notes_state().notes_.clear();
    for (const Note& note : notes) {
        if (!get_find(note.name)) {
            level_editor_notes_state().notes_.push_back(note);
        }
    }
    level_editor_notes_state().selected_.clear();
    level_editor_notes_state().edit_for_.clear();
    return static_cast<int>(level_editor_notes_state().notes_.size());
}

void reset_all()
{
    level_editor_notes_state().notes_.clear();
    level_editor_notes_state().selected_.clear();
    level_editor_notes_state().edit_for_.clear();
}

void forget_level() { reset_all(); }

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(level_editor_notes_state().queue_);
    for (const auto& action : queue) {
        action();
    }
    if (!level_editor_notes_state().selected_.empty() && !get_find(level_editor_notes_state().selected_)) {
        level_editor_notes_state().selected_.clear();
    }
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::Circle>* circles,
                 std::vector<ui::Overlay::OverlayText>* texts)
{
    for (const Note& note : level_editor_notes_state().notes_) {
        const bool selected = note.name == level_editor_notes_state().selected_;
        // (a pin: a stem from the point up to its head - longer far away, so it stands out - and the text beside the head)
        const float distance = std::sqrt(get_dot(note.pos - camera_pos, note.pos - camera_pos));
        const float height = std::max(40.0F, std::min(400.0F, distance * 0.06F));
        const V3 top = note.pos + V3{.x = 0.0F, .y = height, .z = 0.0F};
        float ax = 0.0F;
        float ay = 0.0F;
        float bx = 0.0F;
        float by = 0.0F;
        float px = 0.0F;
        float py = 0.0F;
        float tx = 0.0F;
        float ty = 0.0F;
        if (!cast_onto(camera_pos, frame, lens, note.pos, near_z, &ax, &ay) ||
            !cast_onto(camera_pos, frame, lens, top, near_z, &bx, &by)) {
            continue;
        }
        ndc_to_pixel(viewport, ax, ay, &px, &py);
        ndc_to_pixel(viewport, bx, by, &tx, &ty);
        const uint32_t argb = 0xFF000000U | (note.colour & 0xFFFFFF);
        const uint32_t stem = selected ? 0xFFFFFFFFU : (0xC0000000U | (note.colour & 0xFFFFFF));
        lines->push_back({.ax = px, .ay = py, .bx = tx, .by = ty, .argb = stem, .thickness = selected ? kSelectedLineThickness : kLineThickness});
        circles->push_back({.x = tx,
                            .y = ty,
                            .radius = selected ? kSelectedDotRadius : kDotRadius,
                            .argb = argb,
                            .filled = true,
                            .thickness = 1.0F});
        if (selected) {
            circles->push_back({.x = tx, .y = ty, .radius = kRingRadius, .argb = kRingColour, .filled = false, .thickness = kRingThickness});
        }
        circles->push_back(
            {.x = px, .y = py, .radius = kFootDotRadius, .argb = argb, .filled = true, .thickness = 1.0F});
        // (the label: the first line - all of it when selected - cut short when far)
        const size_t unselected_most = distance > 6000.0F ? 18 : 36;
        const size_t most = selected ? 60 : unselected_most;
        texts->push_back({.x = tx + kLabelOffsetX, .y = ty - kLabelOffsetY, .argb = argb, .text = caption(note.text, most)});
    }
}

void outliner_section(ImFont* mono, const char* search)
{
    ImGui::Spacing();
    const bool open = ImGui::TreeNodeEx("Notes###notesSection", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    if (mono) {
        ImGui::PushFont(mono);
    }
    gui::text_disabled("%d", static_cast<int>(level_editor_notes_state().notes_.size()));
    if (mono) {
        ImGui::PopFont();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Note here")) {
        level_editor_notes_state().queue_.emplace_back([] { add_here(); });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Add a note at the view center");
    }
    if (!open) {
        return;
    }
    if (level_editor_notes_state().notes_.empty()) {
        gui::text_disabled("No notes");
    }
    for (size_t i = 0; i < level_editor_notes_state().notes_.size(); ++i) {
        const Note& note = core::at(level_editor_notes_state().notes_, i);
        if (!get_matches(note.name + " " + note.text, search)) {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + (height * kSwatchLeft), pos.y + (height * kHalf)),
                                                    height * kSwatchRadius, get_colour(note.colour));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + (height * kSwatchTextOffset), pos.y));
        const std::string label = caption(note.text, 40);
        const bool clicked =
            ImGui::Selectable(label.c_str(), note.name == level_editor_notes_state().selected_, ImGuiSelectableFlags_AllowDoubleClick);
        if (clicked) {
            const std::string name = note.name;
            const V3 at = note.pos;
            const bool go = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            level_editor_notes_state().queue_.emplace_back([name, at, go] {
                level_editor_notes_state().selected_ = name;
                level_editor_notes_state().edit_for_.clear();
                if (go) {
                    go_to_point(at, kGoToDistance);
                }
            });
        }
        if (ImGui::IsItemHovered()) {
            gui::tooltip("%s\n%s", note.name.c_str(), note.text.empty() ? "(empty)" : note.text.c_str());
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void pane(PaneArea area, float scale)
{
    const auto view_x = area.x;
    const auto view_y = area.y;
    const auto view_width = area.width;
    const auto view_height = area.height;
    (void)view_width;
    const Note* note = level_editor_notes_state().selected_.empty() ? nullptr : get_find(level_editor_notes_state().selected_);
    if (!note) {
        return;
    }
    if (level_editor_notes_state().edit_for_ != note->name) {
        text::format_to(level_editor_notes_state().edit_text_, "%s", note->text.c_str());
        level_editor_notes_state().edit_for_ = note->name;
    }
    ImGui::SetNextWindowPos(ImVec2(view_x + (kPaneInset * scale), view_y + view_height - (kPaneInset * scale)),
                            ImGuiCond_FirstUseEver, ImVec2(0.0F, 1.0F));
    ImGui::SetNextWindowSize(ImVec2(kPaneWidth * scale, 0.0F), ImGuiCond_FirstUseEver);
    bool open = true;
    const std::string title = "Note " + note->name + "###noteWindow";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) {
            level_editor_notes_state().queue_.emplace_back([] { level_editor_notes_state().selected_.clear(); });
        }
        return;
    }
    std::string name = note->name;
    gui::text_disabled("Editor only");
    // The text and the place: changed live, one undo step from the field taken to the field let go (pushed then - the
    // change complete: the autosave's snapshot follows it).
    auto held = [] {
        if (ImGui::IsItemActivated()) {
            level_editor_notes_state().queue_.emplace_back([] { level_editor_notes_state().before_ = level_editor_notes_state().notes_; });
        }
        if (ImGui::IsItemDeactivated()) {
            level_editor_notes_state().queue_.emplace_back([] {
                bool same = level_editor_notes_state().before_.size() == level_editor_notes_state().notes_.size();
                for (size_t i = 0; same && i < level_editor_notes_state().notes_.size(); ++i) {
                    same = get_same(core::at(level_editor_notes_state().before_, i), core::at(level_editor_notes_state().notes_, i));
                }
                if (!same) {
                    push_module_undo(std::make_shared<NotesStep>(level_editor_notes_state().before_));
                }
            });
        }
    };
    ImGui::InputTextMultiline("##text", level_editor_notes_state().edit_text_.data(),
                              sizeof(level_editor_notes_state().edit_text_), ImVec2(-1.0F, kTextFieldHeight * scale));
    if (ImGui::IsItemEdited()) {
        const std::string text = level_editor_notes_state().edit_text_.data();
        level_editor_notes_state().queue_.emplace_back([name, text] {
            if (Note* found_note = get_find(name)) {
                found_note->text = text;
            }
        });
    }
    held();
    // The colour.
    gui::text_disabled("Colour");
    for (int colour_index = 0; colour_index < kColourCount; ++colour_index) {
        ImGui::SameLine();
        ImGui::PushID(colour_index);
        const bool on = note->colour == core::at(kColours, colour_index);
        const ImVec4 colour = ImGui::ColorConvertU32ToFloat4(get_colour(core::at(kColours, colour_index)));
        if (ImGui::ColorButton("##colour", colour, ImGuiColorEditFlags_NoTooltip,
                               ImVec2(kColourButtonSize * scale, kColourButtonSize * scale))) {
            const uint32_t rgb = core::at(kColours, colour_index);
            level_editor_notes_state().queue_.emplace_back([name, rgb] {
                Note* found_note = get_find(name);
                if (!found_note || found_note->colour == rgb) {
                    return;
                }
                push_undo();
                found_note->colour = rgb;
            });
        }
        if (on) {
            const ImVec2 point_a = ImGui::GetItemRectMin();
            const ImVec2 point_b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(point_a.x - kSelectionOutlineGrow, point_a.y - kSelectionOutlineGrow),
                ImVec2(point_b.x + kSelectionOutlineGrow, point_b.y + kSelectionOutlineGrow),
                IM_COL32(255, 255, 255, 255), kSelectionOutlineRounding, 0, kSelectionOutlineThickness);
        }
        ImGui::PopID();
    }
    // Its place.
    std::array<float, 3> pos = {{note->pos.x, note->pos.y, note->pos.z}};
    ImGui::SetNextItemWidth(-1.0F);
    if (ImGui::DragFloat3("##pos", pos.data(), kPositionDragStep, 0.0F, 0.0F, "%.0f")) {
        const V3 to = {.x = core::at(pos, 0), .y = core::at(pos, 1), .z = core::at(pos, 2)};
        level_editor_notes_state().queue_.emplace_back([name, to] {
            if (Note* found_note = get_find(name)) {
                found_note->pos = to;
            }
        });
    }
    held();
    if (ImGui::Button("Go to")) {
        level_editor_notes_state().queue_.emplace_back([name] {
            if (const Note* found_note = get_find(name)) {
                go_to_point(found_note->pos, kGoToDistance);
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Move here")) {
        level_editor_notes_state().queue_.emplace_back([name] {
            V3 at{};
            Note* found_note = get_find(name);
            if (!found_note || !view_point(&at)) {
                return;
            }
            push_undo();
            found_note->pos = at;
        });
    }
    if (ImGui::IsItemHovered()) {
        gui::tooltip("Move to the view center");
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        level_editor_notes_state().queue_.emplace_back([name] {
            if (!get_find(name)) {
                return;
            }
            push_undo();
            std::erase_if(level_editor_notes_state().notes_,
                          [&](const Note& stored_note) { return stored_note.name == name; });
            level_editor_notes_state().selected_.clear();
            hud::get_message("Deleted note " + name);
        });
    }
    ImGui::End();
    if (!open) {
        level_editor_notes_state().queue_.emplace_back([] { level_editor_notes_state().selected_.clear(); });
    }
}

} // namespace frontend::level_editor::notes

namespace frontend::level_editor::notes {

void add_here_queued()
{
    level_editor_notes_state().queue_.emplace_back([] { add_here(); });
}

} // namespace frontend::level_editor::notes
