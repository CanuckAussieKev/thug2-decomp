// NOT ORIGINAL - the level editor's note pins (ours). See level_editor_notes.h.

#include "editor/level_editor_notes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <array>
#include <span>

#include "editor/level_editor_checks.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_modules.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "editor/level_editor_gui_text.h"

namespace frontend::level_editor::notes {

// --- the pure parts -------------------------------------------------------------------------------------------------

bool get_same(const Note& a, const Note& b)
{
    return a.name == b.name && a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.colour == b.colour &&
           a.text == b.text;
}

std::string pack_up(const std::string& text)
{
    std::string out;
    for (const unsigned char c : text.substr(0, kMaxText)) {
        if (c == '%' || c == '"' || c == '#' || c < 0x20 || c == 0x7F) {
            std::array<char, 4> hex{};
            text::format_to(hex, "%%%02X", c);
            out += hex.data();
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool decode(const std::string& encoded, std::string* text)
{
    text->clear();
    for (size_t i = 0; i < encoded.size(); ++i) {
        const char c = core::at(encoded, i);
        if (c == '"' || c == '#')
            return false;
        if (c != '%') {
            *text += c;
            continue;
        }
        if (i + 2 >= encoded.size() || !std::isxdigit(static_cast<unsigned char>(core::at(encoded, i + 1))) ||
            !std::isxdigit(static_cast<unsigned char>(core::at(encoded, i + 2))))
            return false;
        *text += static_cast<char>(std::strtol(encoded.substr(i + 1, 2).c_str(), nullptr, 16));
        i += 2;
    }
    if (text->size() > kMaxText)
        text->resize(kMaxText);
    return true;
}

namespace {

bool valid_name(const std::string& name)
{
    if (name.empty() || name.size() > 63) // (as layout_file.ts's validWord)
        return false;
    for (const char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    }
    return true;
}

bool read_triple(const std::string& text, V3* out)
{
    std::array<float, 3> v{};
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        core::at(v, i) = std::strtof(at, &end);
        if (end == at || !std::isfinite(core::at(v, i)))
            return false;
        at = end;
        if (i < 2) {
            if (*at != ',')
                return false;
            at = std::next(at);
        }
    }
    if (*at)
        return false;
    *out = {core::at(v, 0), core::at(v, 1), core::at(v, 2)};
    return true;
}

} // namespace

std::string write_record(const Note& note)
{
    std::array<char, 160> text{};
    text::format_to(text, "note %s pos=%.9g,%.9g,%.9g color=%06X text=\"", note.name.c_str(), note.pos.x, note.pos.y,
                    note.pos.z, note.colour & 0xFFFFFF);
    return text.data() + pack_up(note.text) + "\"";
}

bool read_record(std::istringstream& in, Note* out)
{
    *out = Note{};
    if (!(in >> out->name) || !valid_name(out->name))
        return false;
    std::string rest;
    std::getline(in, rest);
    bool pos = false, colour = false, text = false;
    size_t at = 0;
    while (true) {
        at = rest.find_first_not_of(' ', at);
        if (at == std::string::npos)
            break;
        if (text)
            return false; // (text= is last)
        const size_t equals = rest.find('=', at);
        if (equals == std::string::npos)
            return false;
        const std::string key = rest.substr(at, equals - at);
        if (key.find(' ') != std::string::npos)
            return false;
        if (key == "text") {
            if (equals + 1 >= rest.size() || core::at(rest, equals + 1) != '"')
                return false;
            const size_t close = rest.find('"', equals + 2);
            if (close == std::string::npos || !decode(rest.substr(equals + 2, close - equals - 2), &out->text))
                return false;
            text = true;
            at = close + 1;
            continue;
        }
        const size_t end = rest.find(' ', equals);
        const std::string value =
            rest.substr(equals + 1, end == std::string::npos ? std::string::npos : end - equals - 1);
        at = end == std::string::npos ? rest.size() : end;
        if (key == "pos") {
            if (pos || !read_triple(value, &out->pos))
                return false;
            pos = true;
        } else if (key == "color") {
            char* stop = nullptr;
            const unsigned long rgb = std::strtoul(value.c_str(), &stop, 16);
            if (colour || value.size() != 6 || *stop)
                return false;
            out->colour = static_cast<uint32_t>(rgb);
            colour = true;
        } // (another key: a later version's - skipped)
    }
    return pos;
}

std::string fresh_name(const std::vector<Note>& notes)
{
    for (int n = 1;; ++n) {
        std::array<char, 32> name{};
        text::format_to(name, "%s%02d", kStem, n);
        if (std::ranges::none_of(notes, [&](const Note& note) { return note.name == name.data(); }))
            return name.data();
    }
}

std::string caption(const std::string& text, size_t most)
{
    std::string first = text.substr(0, text.find('\n'));
    while (!first.empty() && (first.back() == '\r' || first.back() == ' '))
        first.pop_back();
    if (first.empty())
        return text.find_first_not_of(" \r\n") == std::string::npos ? "(empty note)" : "...";
    if (first.size() > most)
        first = first.substr(0, most) + "...";
    else if (first.size() < text.size() && text.find_first_not_of(" \r\n", first.size()) != std::string::npos)
        first += " ...";
    return first;
}

// --- the game side ------------------------------------------------------------------------------------------------

namespace {

std::vector<Note> g_notes;
std::string g_selected;         // (the note whose window is open; "" none)
std::array<char, kMaxText + 1> g_edit_text; // (the window's text while typed)
std::string g_edit_for;         // (the note the text buffer holds)
std::vector<Note> g_before;     // (the notes as they were when a field was taken: its undo step)
std::vector<std::function<void()>> g_queue;

struct NotesStep : ModuleStep {
    std::vector<Note> notes;
    explicit NotesStep(std::vector<Note> list) : notes(std::move(list)) {}
    void get_restore() override
    {
        g_notes = notes;
        g_edit_for.clear(); // (the window reads the text again)
    }
    [[nodiscard]] std::shared_ptr<ModuleStep> get_now() const override { return std::make_shared<NotesStep>(g_notes); }
};

void push_undo() { push_module_undo(std::make_shared<NotesStep>(g_notes)); }

Note* get_find(const std::string& name)
{
    auto it = std::ranges::find_if(g_notes, [&](const Note& n) { return n.name == name; });
    return it == g_notes.end() ? nullptr : &*it;
}

// Where the view looks: the first surface under the middle of the view (else 800 units ahead).
bool view_point(V3* point)
{
    ViewNow view{};
    if (!current_view(&view))
        return false;
    const TickCycle frame = frame_from_yaw_pitch(view.yaw, view.pitch);
    const V3 dir = make_unit(frame.back * -1.0f);
    V3 hit{};
    if (checks::get_cast(view.pos, view.pos + dir * 20000.0f, &hit))
        *point = hit;
    else
        *point = view.pos + dir * 800.0f;
    return true;
}

void add_here()
{
    V3 at{};
    if (!view_point(&at))
        return;
    push_undo();
    Note note;
    note.name = fresh_name(g_notes);
    note.pos = at;
    g_notes.push_back(note);
    g_selected = note.name;
    g_edit_for.clear();
    hud::get_message(("Added note " + note.name).c_str());
}

ImU32 get_colour(uint32_t rgb, int alpha = 255)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

bool get_matches(const std::string& text, const char* filter)
{
    if (!filter || !*filter)
        return true;
    std::string a = text, b = filter;
    auto lower = [](std::string* s) {
        for (char& c : *s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    lower(&a);
    lower(&b);
    return a.find(b) != std::string::npos;
}

} // namespace

const std::vector<Note>& get_all() { return g_notes; }

std::vector<Note> get_capture() { return g_notes; }

int get_apply(const std::vector<Note>& notes)
{
    g_notes.clear();
    for (const Note& note : notes) {
        if (!get_find(note.name))
            g_notes.push_back(note);
    }
    g_selected.clear();
    g_edit_for.clear();
    return static_cast<int>(g_notes.size());
}

void reset_all()
{
    g_notes.clear();
    g_selected.clear();
    g_edit_for.clear();
}

void forget_level() { reset_all(); }

void tick_frame()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (const auto& action : queue)
        action();
    if (!g_selected.empty() && !get_find(g_selected))
        g_selected.clear();
}

void add_overlay(V3 camera_pos, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::Circle>* circles,
                 std::vector<ui::Overlay::OverlayText>* texts)
{
    for (const Note& note : g_notes) {
        const bool selected = note.name == g_selected;
        // (a pin: a stem from the point up to its head - longer far away, so it stands out - and the text beside the head)
        const float distance = std::sqrt(get_dot(note.pos - camera_pos, note.pos - camera_pos));
        const float height = std::max(40.0f, std::min(400.0f, distance * 0.06f));
        const V3 top = note.pos + V3{0.0f, height, 0.0f};
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        float px = 0.0f, py = 0.0f, tx = 0.0f, ty = 0.0f;
        if (!cast_onto(camera_pos, frame, lens, note.pos, near_z, &ax, &ay) ||
            !cast_onto(camera_pos, frame, lens, top, near_z, &bx, &by))
            continue;
        ndc_to_pixel(viewport, ax, ay, &px, &py);
        ndc_to_pixel(viewport, bx, by, &tx, &ty);
        const uint32_t argb = 0xFF000000u | (note.colour & 0xFFFFFF);
        const uint32_t stem = selected ? 0xFFFFFFFFu : (0xC0000000u | (note.colour & 0xFFFFFF));
        lines->push_back({px, py, tx, ty, stem, selected ? 2.0f : 1.5f});
        circles->push_back({tx, ty, selected ? 7.0f : 5.5f, argb, true, 1.0f});
        if (selected)
            circles->push_back({tx, ty, 9.5f, 0xFFFFFFFFu, false, 1.5f});
        circles->push_back({px, py, 2.5f, argb, true, 1.0f});
        // (the label: the first line - all of it when selected - cut short when far)
        const size_t most = selected ? 60 : distance > 6000.0f ? 18 : 36;
        texts->push_back({tx + 10.0f, ty - 8.0f, argb, caption(note.text, most)});
    }
}

void outliner_section(ImFont* mono, const char* search)
{
    ImGui::Spacing();
    const bool open = ImGui::TreeNodeEx("Notes###notesSection", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    if (mono)
        ImGui::PushFont(mono);
    gui::text_disabled("%d", static_cast<int>(g_notes.size()));
    if (mono)
        ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Note here"))
        g_queue.emplace_back([] { add_here(); });
    if (ImGui::IsItemHovered())
        gui::tooltip("Add a note at the view center");
    if (!open)
        return;
    if (g_notes.empty())
        gui::text_disabled("No notes");
    for (size_t i = 0; i < g_notes.size(); ++i) {
        const Note& note = core::at(g_notes, i);
        if (!get_matches(note.name + " " + note.text, search))
            continue;
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + h * 0.25f, pos.y + h * 0.5f), h * 0.22f,
                                                    get_colour(note.colour));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + h * 0.6f, pos.y));
        const std::string label = caption(note.text, 40);
        const bool clicked =
            ImGui::Selectable(label.c_str(), note.name == g_selected, ImGuiSelectableFlags_AllowDoubleClick);
        if (clicked) {
            const std::string name = note.name;
            const V3 at = note.pos;
            const bool go = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            g_queue.emplace_back([name, at, go] {
                g_selected = name;
                g_edit_for.clear();
                if (go)
                    go_to_point(at, 400.0f);
            });
        }
        if (ImGui::IsItemHovered())
            gui::tooltip("%s\n%s", note.name.c_str(), note.text.empty() ? "(empty)" : note.text.c_str());
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void pane(float view_x, float view_y, float view_width, float view_height, float scale)
{
    (void)view_width;
    const Note* note = g_selected.empty() ? nullptr : get_find(g_selected);
    if (!note)
        return;
    if (g_edit_for != note->name) {
        text::format_to(g_edit_text, "%s", note->text.c_str());
        g_edit_for = note->name;
    }
    ImGui::SetNextWindowPos(ImVec2(view_x + 12.0f * scale, view_y + view_height - 12.0f * scale),
                            ImGuiCond_FirstUseEver, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    const std::string title = "Note " + note->name + "###noteWindow";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            g_queue.emplace_back([] { g_selected.clear(); });
        return;
    }
    std::string name = note->name;
    gui::text_disabled("Editor only");
    // The text and the place: changed live, one undo step from the field taken to the field let go (pushed then - the
    // change complete: the autosave's snapshot follows it).
    auto held = [] {
        if (ImGui::IsItemActivated())
            g_queue.emplace_back([] { g_before = g_notes; });
        if (ImGui::IsItemDeactivated())
            g_queue.emplace_back([] {
                bool same = g_before.size() == g_notes.size();
                for (size_t i = 0; same && i < g_notes.size(); ++i)
                    same = get_same(core::at(g_before, i), core::at(g_notes, i));
                if (!same)
                    push_module_undo(std::make_shared<NotesStep>(g_before));
            });
    };
    ImGui::InputTextMultiline("##text", g_edit_text.data(), sizeof(g_edit_text), ImVec2(-1.0f, 90.0f * scale));
    if (ImGui::IsItemEdited()) {
        const std::string text = g_edit_text.data();
        g_queue.emplace_back([name, text] {
            if (Note* n = get_find(name))
                n->text = text;
        });
    }
    held();
    // The colour.
    gui::text_disabled("Colour");
    for (int c = 0; c < kColourCount; ++c) {
        ImGui::SameLine();
        ImGui::PushID(c);
        const bool on = note->colour == core::at(kColours, c);
        const ImVec4 colour = ImGui::ColorConvertU32ToFloat4(get_colour(core::at(kColours, c)));
        if (ImGui::ColorButton("##colour", colour, ImGuiColorEditFlags_NoTooltip,
                               ImVec2(18.0f * scale, 18.0f * scale))) {
            const uint32_t rgb = core::at(kColours, c);
            g_queue.emplace_back([name, rgb] {
                Note* n = get_find(name);
                if (!n || n->colour == rgb)
                    return;
                push_undo();
                n->colour = rgb;
            });
        }
        if (on) {
            const ImVec2 a = ImGui::GetItemRectMin();
            const ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f),
                                                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.0f);
        }
        ImGui::PopID();
    }
    // Its place.
    std::array<float, 3> pos = {{note->pos.x, note->pos.y, note->pos.z}};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat3("##pos", pos.data(), 5.0f, 0.0f, 0.0f, "%.0f")) {
        const V3 to = {core::at(pos, 0), core::at(pos, 1), core::at(pos, 2)};
        g_queue.emplace_back([name, to] {
            if (Note* n = get_find(name))
                n->pos = to;
        });
    }
    held();
    if (ImGui::Button("Go to"))
        g_queue.emplace_back([name] {
            if (const Note* n = get_find(name))
                go_to_point(n->pos, 400.0f);
        });
    ImGui::SameLine();
    if (ImGui::Button("Move here"))
        g_queue.emplace_back([name] {
            V3 at{};
            Note* n = get_find(name);
            if (!n || !view_point(&at))
                return;
            push_undo();
            n->pos = at;
        });
    if (ImGui::IsItemHovered())
        gui::tooltip("Move to the view center");
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
        g_queue.emplace_back([name] {
            if (!get_find(name))
                return;
            push_undo();
            std::erase_if(g_notes, [&](const Note& n) { return n.name == name; });
            g_selected.clear();
            hud::get_message(("Deleted note " + name).c_str());
        });
    ImGui::End();
    if (!open)
        g_queue.emplace_back([] { g_selected.clear(); });
}

} // namespace frontend::level_editor::notes

namespace frontend::level_editor::notes {

void add_here_queued()
{
    g_queue.emplace_back([] { add_here(); });
}

} // namespace frontend::level_editor::notes
