// NOT ORIGINAL - the level editor's note pins (ours). See level_editor_notes.h.

#include "game/front/level_editor_notes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>

#include "game/front/level_editor_checks.h"
#include "game/front/level_editor_hud.h"
#include "game/front/level_editor_modules.h"
#include "hook/log.h"
#include "imgui.h"

namespace Front::LevelEditor::Notes {

// --- the pure parts -------------------------------------------------------------------------------------------------

bool Same(const Note& a, const Note& b)
{
    return a.name == b.name && a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.colour == b.colour &&
           a.text == b.text;
}

std::string Encode(const std::string& text)
{
    std::string out;
    for (unsigned char c : text.substr(0, kMaxText)) {
        if (c == '%' || c == '"' || c == '#' || c < 0x20 || c == 0x7F) {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool Decode(const std::string& encoded, std::string* text)
{
    text->clear();
    for (size_t i = 0; i < encoded.size(); ++i) {
        char c = encoded[i];
        if (c == '"' || c == '#')
            return false;
        if (c != '%') {
            *text += c;
            continue;
        }
        if (i + 2 >= encoded.size() || !std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(encoded[i + 2])))
            return false;
        *text += static_cast<char>(std::strtol(encoded.substr(i + 1, 2).c_str(), nullptr, 16));
        i += 2;
    }
    if (text->size() > kMaxText)
        text->resize(kMaxText);
    return true;
}

namespace {

bool ValidName(const std::string& name)
{
    if (name.empty() || name.size() > 63) // (as layout_file.ts's validWord)
        return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    }
    return true;
}

bool ReadTriple(const std::string& text, V3* out)
{
    float v[3];
    const char* at = text.c_str();
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        v[i] = std::strtof(at, &end);
        if (end == at || !std::isfinite(v[i]))
            return false;
        at = end;
        if (i < 2) {
            if (*at != ',')
                return false;
            ++at;
        }
    }
    if (*at)
        return false;
    *out = {v[0], v[1], v[2]};
    return true;
}

} // namespace

std::string WriteRecord(const Note& note)
{
    char text[160];
    std::snprintf(text, sizeof(text), "note %s pos=%.9g,%.9g,%.9g color=%06X text=\"", note.name.c_str(), note.pos.x,
                  note.pos.y, note.pos.z, note.colour & 0xFFFFFF);
    return text + Encode(note.text) + "\"";
}

bool ReadRecord(std::istringstream& in, Note* out)
{
    *out = Note{};
    if (!(in >> out->name) || !ValidName(out->name))
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
        size_t equals = rest.find('=', at);
        if (equals == std::string::npos)
            return false;
        std::string key = rest.substr(at, equals - at);
        if (key.find(' ') != std::string::npos)
            return false;
        if (key == "text") {
            if (equals + 1 >= rest.size() || rest[equals + 1] != '"')
                return false;
            size_t close = rest.find('"', equals + 2);
            if (close == std::string::npos || !Decode(rest.substr(equals + 2, close - equals - 2), &out->text))
                return false;
            text = true;
            at = close + 1;
            continue;
        }
        size_t end = rest.find(' ', equals);
        std::string value = rest.substr(equals + 1, end == std::string::npos ? std::string::npos : end - equals - 1);
        at = end == std::string::npos ? rest.size() : end;
        if (key == "pos") {
            if (pos || !ReadTriple(value, &out->pos))
                return false;
            pos = true;
        } else if (key == "color") {
            char* stop = nullptr;
            unsigned long rgb = std::strtoul(value.c_str(), &stop, 16);
            if (colour || value.size() != 6 || *stop)
                return false;
            out->colour = static_cast<uint32_t>(rgb);
            colour = true;
        } // (another key: a later version's - skipped)
    }
    return pos;
}

std::string FreshName(const std::vector<Note>& notes)
{
    for (int n = 1;; ++n) {
        char name[32];
        std::snprintf(name, sizeof(name), "%s%02d", kStem, n);
        if (std::none_of(notes.begin(), notes.end(), [&](const Note& note) { return note.name == name; }))
            return name;
    }
}

std::string Label(const std::string& text, size_t most)
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
std::string g_selected;                       // (the note whose window is open; "" none)
char g_editText[kMaxText + 1];                // (the window's text while typed)
std::string g_editFor;                        // (the note the text buffer holds)
std::vector<Note> g_before;                   // (the notes as they were when a field was taken: its undo step)
std::vector<std::function<void()>> g_queue;

struct NotesStep : ModuleStep {
    std::vector<Note> notes;
    explicit NotesStep(std::vector<Note> list) : notes(std::move(list)) {}
    void Restore() override
    {
        g_notes = notes;
        g_editFor.clear(); // (the window reads the text again)
    }
    std::shared_ptr<ModuleStep> Now() const override { return std::make_shared<NotesStep>(g_notes); }
};

void PushUndo() { PushModuleUndo(std::make_shared<NotesStep>(g_notes)); }

Note* Find(const std::string& name)
{
    auto it = std::find_if(g_notes.begin(), g_notes.end(), [&](const Note& n) { return n.name == name; });
    return it == g_notes.end() ? nullptr : &*it;
}

// Where the view looks: the first surface under the middle of the view (else 800 units ahead).
bool ViewPoint(V3* point)
{
    ViewNow view;
    if (!CurrentView(&view))
        return false;
    Frame frame = FrameFromYawPitch(view.yaw, view.pitch);
    V3 dir = Normalize(frame.back * -1.0f);
    V3 hit;
    if (Checks::Cast(view.pos, view.pos + dir * 20000.0f, &hit))
        *point = hit;
    else
        *point = view.pos + dir * 800.0f;
    return true;
}

void AddHere()
{
    V3 at;
    if (!ViewPoint(&at))
        return;
    PushUndo();
    Note note;
    note.name = FreshName(g_notes);
    note.pos = at;
    g_notes.push_back(note);
    g_selected = note.name;
    g_editFor.clear();
    Hud::Message(("Added note " + note.name).c_str());
}

ImU32 Colour(uint32_t rgb, int alpha = 255)
{
    return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, alpha);
}

bool Matches(const std::string& text, const char* filter)
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

const std::vector<Note>& All() { return g_notes; }

std::vector<Note> Capture() { return g_notes; }

int Apply(const std::vector<Note>& notes)
{
    g_notes.clear();
    for (const Note& note : notes) {
        if (!Find(note.name))
            g_notes.push_back(note);
    }
    g_selected.clear();
    g_editFor.clear();
    return static_cast<int>(g_notes.size());
}

void ResetAll()
{
    g_notes.clear();
    g_selected.clear();
    g_editFor.clear();
}

void ForgetLevel() { ResetAll(); }

void Update()
{
    std::vector<std::function<void()>> queue;
    queue.swap(g_queue);
    for (const auto& action : queue)
        action();
    if (!g_selected.empty() && !Find(g_selected))
        g_selected.clear();
}

void AddOverlay(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Circle>* circles,
                std::vector<Ui::Overlay::Text>* texts)
{
    for (const Note& note : g_notes) {
        bool selected = note.name == g_selected;
        // (a pin: a stem from the point up to its head - longer far away, so it stands out - and the text beside the head)
        float distance = std::sqrt(Dot(note.pos - cameraPos, note.pos - cameraPos));
        float height = std::max(40.0f, std::min(400.0f, distance * 0.06f));
        V3 top = note.pos + V3{0.0f, height, 0.0f};
        float ax, ay, bx, by;
        float px, py, tx, ty;
        if (!Project(cameraPos, frame, lens, note.pos, nearZ, &ax, &ay) ||
            !Project(cameraPos, frame, lens, top, nearZ, &bx, &by))
            continue;
        NdcToPixel(viewport, ax, ay, &px, &py);
        NdcToPixel(viewport, bx, by, &tx, &ty);
        uint32_t argb = 0xFF000000u | (note.colour & 0xFFFFFF);
        uint32_t stem = selected ? 0xFFFFFFFFu : (0xC0000000u | (note.colour & 0xFFFFFF));
        lines->push_back({px, py, tx, ty, stem, selected ? 2.0f : 1.5f});
        circles->push_back({tx, ty, selected ? 7.0f : 5.5f, argb, true, 1.0f});
        if (selected)
            circles->push_back({tx, ty, 9.5f, 0xFFFFFFFFu, false, 1.5f});
        circles->push_back({px, py, 2.5f, argb, true, 1.0f});
        // (the label: the first line - all of it when selected - cut short when far)
        size_t most = selected ? 60 : distance > 6000.0f ? 18 : 36;
        texts->push_back({tx + 10.0f, ty - 8.0f, argb, Label(note.text, most)});
    }
}

void OutlinerSection(ImFont* mono, const char* search)
{
    ImGui::Spacing();
    bool open = ImGui::TreeNodeEx("Notes###notesSection", ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::SameLine();
    if (mono)
        ImGui::PushFont(mono);
    ImGui::TextDisabled("%d", static_cast<int>(g_notes.size()));
    if (mono)
        ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Note here"))
        g_queue.push_back([] { AddHere(); });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a note at the view center");
    if (!open)
        return;
    if (g_notes.empty())
        ImGui::TextDisabled("No notes");
    for (size_t i = 0; i < g_notes.size(); ++i) {
        const Note& note = g_notes[i];
        if (!Matches(note.name + " " + note.text, search))
            continue;
        ImGui::PushID(static_cast<int>(i));
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float h = ImGui::GetTextLineHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + h * 0.25f, pos.y + h * 0.5f), h * 0.22f,
                                                     Colour(note.colour));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + h * 0.6f, pos.y));
        std::string label = Label(note.text, 40);
        bool clicked = ImGui::Selectable(label.c_str(), note.name == g_selected, ImGuiSelectableFlags_AllowDoubleClick);
        if (clicked) {
            std::string name = note.name;
            V3 at = note.pos;
            bool go = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            g_queue.push_back([name, at, go] {
                g_selected = name;
                g_editFor.clear();
                if (go)
                    GoToPoint(at, 400.0f);
            });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", note.name.c_str(),
                              note.text.empty() ? "(empty)" : note.text.c_str());
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale)
{
    (void)viewWidth;
    Note* note = g_selected.empty() ? nullptr : Find(g_selected);
    if (!note)
        return;
    if (g_editFor != note->name) {
        std::snprintf(g_editText, sizeof(g_editText), "%s", note->text.c_str());
        g_editFor = note->name;
    }
    ImGui::SetNextWindowPos(ImVec2(viewX + 12.0f * scale, viewY + viewHeight - 12.0f * scale), ImGuiCond_FirstUseEver,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(340.0f * scale, 0.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    std::string title = "Note " + note->name + "###noteWindow";
    if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open)
            g_queue.push_back([] { g_selected.clear(); });
        return;
    }
    std::string name = note->name;
    ImGui::TextDisabled("Editor only");
    // The text and the place: changed live, one undo step from the field taken to the field let go (pushed then - the
    // change complete: the autosave's snapshot follows it).
    auto held = [] {
        if (ImGui::IsItemActivated())
            g_queue.push_back([] { g_before = g_notes; });
        if (ImGui::IsItemDeactivated())
            g_queue.push_back([] {
                bool same = g_before.size() == g_notes.size();
                for (size_t i = 0; same && i < g_notes.size(); ++i)
                    same = Same(g_before[i], g_notes[i]);
                if (!same)
                    PushModuleUndo(std::make_shared<NotesStep>(g_before));
            });
    };
    ImGui::InputTextMultiline("##text", g_editText, sizeof(g_editText), ImVec2(-1.0f, 90.0f * scale));
    if (ImGui::IsItemEdited()) {
        std::string text = g_editText;
        g_queue.push_back([name, text] {
            if (Note* n = Find(name))
                n->text = text;
        });
    }
    held();
    // The colour.
    ImGui::TextDisabled("Colour");
    for (int c = 0; c < kColourCount; ++c) {
        ImGui::SameLine();
        ImGui::PushID(c);
        bool on = note->colour == kColours[c];
        ImVec4 colour = ImGui::ColorConvertU32ToFloat4(Colour(kColours[c]));
        if (ImGui::ColorButton("##colour", colour, ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f * scale, 18.0f * scale))) {
            uint32_t rgb = kColours[c];
            g_queue.push_back([name, rgb] {
                Note* n = Find(name);
                if (!n || n->colour == rgb)
                    return;
                PushUndo();
                n->colour = rgb;
            });
        }
        if (on) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - 2.0f, a.y - 2.0f), ImVec2(b.x + 2.0f, b.y + 2.0f),
                                                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.0f);
        }
        ImGui::PopID();
    }
    // Its place.
    float pos[3] = {note->pos.x, note->pos.y, note->pos.z};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::DragFloat3("##pos", pos, 5.0f, 0.0f, 0.0f, "%.0f")) {
        V3 to = {pos[0], pos[1], pos[2]};
        g_queue.push_back([name, to] {
            if (Note* n = Find(name))
                n->pos = to;
        });
    }
    held();
    if (ImGui::Button("Go to"))
        g_queue.push_back([name] {
            if (Note* n = Find(name))
                GoToPoint(n->pos, 400.0f);
        });
    ImGui::SameLine();
    if (ImGui::Button("Move here"))
        g_queue.push_back([name] {
            V3 at;
            Note* n = Find(name);
            if (!n || !ViewPoint(&at))
                return;
            PushUndo();
            n->pos = at;
        });
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move to the view center");
    ImGui::SameLine();
    if (ImGui::Button("Delete"))
        g_queue.push_back([name] {
            if (!Find(name))
                return;
            PushUndo();
            g_notes.erase(std::remove_if(g_notes.begin(), g_notes.end(), [&](const Note& n) { return n.name == name; }),
                          g_notes.end());
            g_selected.clear();
            Hud::Message(("Deleted note " + name).c_str());
        });
    ImGui::End();
    if (!open)
        g_queue.push_back([] { g_selected.clear(); });
}

} // namespace Front::LevelEditor::Notes

namespace Front::LevelEditor::Notes {

void AddHereQueued() { g_queue.push_back([] { AddHere(); }); }

} // namespace Front::LevelEditor::Notes
