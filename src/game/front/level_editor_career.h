#pragma once

#include <string>
#include <vector>

namespace Game {
class CGapChecklist;
}

// NOT ORIGINAL - the level editor and the career save (ours; see level_editor.h). An edited copy of a level
// (level_editor_layout.h) is a level of its own: playing it must never change the user's career.
//
// The career's gap checklists (one per level, Obj::CSkaterCareer +0x63C; the skate module's 0x005656C0 is the current
// level's) are what the save keeps of gaps: the game save's builder (0x005A7560, the memory card / save game's
// structure) calls the career's WriteToStructure (0x00585550, ours: src/game/skate/career.cpp - also the script command
// GetCareerSaveInfo, 0x00585E20), which writes each built checklist's gaps (name, times landed, score, the skater's
// start position / direction). The editor adds its gaps to the current level's checklist (level_editor_triggers.h: the
// game's own Build), and gaps landed in an edited copy count in that checklist like any other. So:
//   - when the level becomes its edited copy (EditedCopy(true): the layout put in place, the editor opened in it, a
//     switch in place), the current level's checklist is copied first - the BASELINE: its gaps (names, scores, times
//     landed, positions) and whether it was built;
//   - while that baseline is kept, the save writes the baseline for that checklist instead of the checklist as it is
//     (ForSave): gaps landed meanwhile (the level's own, the editor's) and the editor's gaps never reach the save;
//   - when the level stops being its edited copy (EditedCopy(false): another level loaded - or the same one again -,
//     switched back in place to the level as shipped), the checklist is put back as the baseline (cleared and its
//     gaps added again, as the career's load does), so a save made later doesn't get them either;
//   - with no baseline (none taken - no checklist then), the save still leaves out the editor's gaps (by name: not a
//     level gap of that name) of the checklist they went into;
//   - the career replaced (a save loaded: ReadFromStructure; a new career: Reset with the gaps) while a baseline is
//     kept: the baseline is taken again from the career as it now is (the old one would bring back the old career).
// What isn't covered: goals completed in an edited copy (the career's goal / flag
// words, cash, stat points - the goal manager's), the goal manager told of gaps landed (gap goals), the level's high
// scores (Records, 0x0057FC50).
namespace Front::LevelEditor::Career {

// A gap as the save writes it (pos / dir only when landed; zero, w 1 otherwise).
struct Gap {
    std::string name;
    int score = 0;
    int count = 0;
    float pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float dir[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};
struct Checklist {
    bool built = false;
    std::vector<Gap> gaps;
};

// The pure part (self-tested: src/hook/selftests_editor_career.cpp).
// What the save writes for a checklist: the baseline when there is one, else the checklist as it is (`live`) - either
// way without the editor's gaps (a name of `editorGaps` that isn't one of `levelGaps`).
Checklist ForSave(const Checklist& live, const Checklist* baseline, const std::vector<std::string>& editorGaps,
                  const std::vector<std::string>& levelGaps);
// Whether a gap is the editor's (its name one of `editorGaps`, not one of `levelGaps`; case counts, as the checklist's).
bool EditorGap(const std::string& name, const std::vector<std::string>& editorGaps,
               const std::vector<std::string>& levelGaps);

// The game side.
// The checklist as it is now.
Checklist Read(Game::CGapChecklist* checklist);
// The level became its edited copy (true: the baseline taken from the current level's checklist) or stopped being it
// (false: that checklist put back as the baseline, the baseline dropped). Repeats are ignored.
void EditedCopy(bool on);
// The career's gaps were replaced (a save loaded, a career reset): a baseline kept is taken again.
void CareerReplaced();
// The save's view of `checklist` (career.cpp's WriteToStructure): false - write it as it is; true - write `out`.
bool SaveView(Game::CGapChecklist* checklist, Checklist* out);

} // namespace Front::LevelEditor::Career
