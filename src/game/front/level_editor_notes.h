#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "game/front/level_editor.h"
#include "game/front/level_editor_ui.h"

struct ImFont;

// NOT ORIGINAL - the level editor's note pins (ours; see level_editor.h - the UX research's #17, after Unreal's Note
// actor): a short text put at a point in the level - a to-do, a play-test finding ("raise this ledge") - shown in the
// editor as a pin with its text over the view, listed in the Outliner (its Notes section: + Note here, the list, a
// click selects and opens the note's window - its text, colour, Move here, Go to, Delete). Editor-only: a note is no
// node and no object, nothing of it exists in play (the pins are drawn only while the editor runs), and the bake
// (tools/game/apply_layout.ts) passes its records over silently (tools/game/layout_file.ts reads them).
//
// Layout record (version 2 still; an older reader skips the kind):
//   note <name> pos=x,y,z color=RRGGBB text="..."
// The text is percent-encoded where the line needs it: % " # (the layout's comment mark), control characters (a new
// line is %0A) - everything else as typed (UTF-8). At most kMaxText bytes; text= comes last.
namespace Front::LevelEditor::Notes {

// --- the pure parts (self-tested: src/hook/selftests_editor_ux5.cpp) -------------------------------------------------

constexpr size_t kMaxText = 500;
constexpr const char* kStem = "ed_note";
// The colours offered (RGB) - the first is the default.
constexpr uint32_t kColours[] = {0xFFD84A, 0xFF7A59, 0x5AC8FA, 0x7BD88F, 0xC18CFF, 0xF2F2F2};
constexpr int kColourCount = static_cast<int>(sizeof(kColours) / sizeof(kColours[0]));

struct Note {
    std::string name;            // ed_note01...
    V3 pos = {};
    uint32_t colour = kColours[0]; // RGB
    std::string text;
};
bool Same(const Note& a, const Note& b);
// The text as the record keeps it, and back (false: a bad escape).
std::string Encode(const std::string& text);
bool Decode(const std::string& encoded, std::string* text);
// The record's text ("note ..."), and one read (after the word "note"; false: malformed - no pos, a bad name, colour,
// number or escape, a key twice, text= not last / not quoted).
std::string WriteRecord(const Note& note);
bool ReadRecord(std::istringstream& in, Note* out);
// A fresh name: the stem and the lowest two-digit number no note in `notes` has.
std::string FreshName(const std::vector<Note>& notes);
// The pin's label: the text's first line, cut to `most` characters ("..." added); "(empty note)" for none.
std::string Label(const std::string& text, size_t most);

// --- the game side ------------------------------------------------------------------------------------------------

const std::vector<Note>& All();
// Layouts: the notes now; put in place (replacing the current - how many); all removed; a new level (forgotten).
std::vector<Note> Capture();
int Apply(const std::vector<Note>& notes);
void ResetAll();
void ForgetLevel();
// "+ Note here" from elsewhere (the command search - level_editor_ux3.h): queued as the Outliner's button does.
void AddHereQueued();
// Once a frame while the editor runs: what the UI asked for (queued).
void Update();
// The pins over the view (the selected one brighter): a stem from the point up, a head, the text beside it.
void AddOverlay(V3 cameraPos, const Frame& frame, const Lens& lens, const uint32_t viewport[4], float nearZ,
                std::vector<Ui::Line>* lines, std::vector<Ui::Overlay::Circle>* circles,
                std::vector<Ui::Overlay::Text>* texts);
// The Outliner's Notes section (`search`: the Outliner's search text - rows matching it) and the selected note's window.
void OutlinerSection(ImFont* mono, const char* search);
void Window(float viewX, float viewY, float viewWidth, float viewHeight, float scale);

} // namespace Front::LevelEditor::Notes
