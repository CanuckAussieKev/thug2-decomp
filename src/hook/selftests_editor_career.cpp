// Self-tests for the level editor and the career save (src/game/front/level_editor_career.*, not original): what the
// save writes of a checklist while a level is its edited copy (the baseline), and without one (the editor's gaps left
// out, a level gap of the same name kept).

#include <string>
#include <vector>

#include "game/front/level_editor_career.h"
#include "hook/selftest.h"

namespace {

namespace C = Front::LevelEditor::Career;

C::Gap G(const char* name, int score, int count, float x = 0.0f)
{
    C::Gap gap;
    gap.name = name;
    gap.score = score;
    gap.count = count;
    if (count) {
        gap.pos[0] = x;
        gap.pos[1] = 2.0f;
        gap.pos[2] = 3.0f;
        gap.dir[2] = 1.0f;
    }
    return gap;
}

bool Same(const C::Gap& a, const C::Gap& b)
{
    for (int i = 0; i < 4; ++i) {
        if (a.pos[i] != b.pos[i] || a.dir[i] != b.dir[i])
            return false;
    }
    return a.name == b.name && a.score == b.score && a.count == b.count;
}

bool Same(const C::Checklist& a, bool built, const std::vector<C::Gap>& gaps)
{
    if (a.built != built || a.gaps.size() != gaps.size())
        return false;
    for (size_t i = 0; i < gaps.size(); ++i) {
        if (!Same(a.gaps[i], gaps[i]))
            return false;
    }
    return true;
}

void TestEditorCareerSave()
{
    const std::vector<std::string> editor = {"Editor Gap 1", "Statehouse Stair Spine"};
    const std::vector<std::string> level = {"Statehouse Stair Spine", "High Slap"};
    Check(C::EditorGap("Editor Gap 1", editor, level), "editor career: an editor gap's name is the editor's");
    Check(!C::EditorGap("Statehouse Stair Spine", editor, level),
          "editor career: a level gap of an editor gap's name is the level's");
    Check(!C::EditorGap("High Slap", editor, level) && !C::EditorGap("editor gap 1", editor, level),
          "editor career: other names (and another case) aren't the editor's");
    Check(C::EditorGap("Editor Gap 1", editor, {}), "editor career: no level gaps read - the editor's name is the editor's");

    // No baseline: the checklist as it is, the editor's gaps left out (score, place and order kept).
    C::Checklist live;
    live.built = true;
    live.gaps = {G("Editor Gap 1", 100, 2, 5.0f), G("High Slap", 50, 1, 7.0f), G("Statehouse Stair Spine", 250, 0)};
    Check(Same(C::ForSave(live, nullptr, editor, level), true, {G("High Slap", 50, 1, 7.0f),
                                                               G("Statehouse Stair Spine", 250, 0)}),
          "editor career: no baseline - the live list without the editor's gaps");
    Check(Same(C::ForSave(live, nullptr, {}, {}), true, live.gaps), "editor career: no editor gaps - the live list");

    // A baseline: written instead of the live list (gaps landed since, and the editor's, never reach the save).
    C::Checklist baseline;
    baseline.built = true;
    baseline.gaps = {G("High Slap", 50, 0), G("Statehouse Stair Spine", 250, 3, 9.0f)};
    Check(Same(C::ForSave(live, &baseline, editor, level), true, baseline.gaps),
          "editor career: a baseline - written as it was");
    C::Checklist dirty = baseline;
    dirty.gaps.insert(dirty.gaps.begin(), G("Editor Gap 1", 100, 1, 1.0f));
    Check(Same(C::ForSave(live, &dirty, editor, level), true, baseline.gaps),
          "editor career: an editor gap in the baseline is left out too");
    C::Checklist unbuilt;
    Check(Same(C::ForSave(live, &unbuilt, editor, level), false, {}),
          "editor career: a baseline of an unbuilt checklist - written unbuilt");
    C::Checklist liveUnbuilt;
    liveUnbuilt.gaps = {G("Editor Gap 1", 100, 0)};
    Check(Same(C::ForSave(liveUnbuilt, nullptr, editor, level), false, {}),
          "editor career: an unbuilt live list stays unbuilt");
}
SELFTEST(Early, TestEditorCareerSave);

} // namespace
