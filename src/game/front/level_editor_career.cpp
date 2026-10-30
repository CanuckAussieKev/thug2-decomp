// NOT ORIGINAL - the level editor and the career save (ours; level_editor_career.h).

#include "game/front/level_editor_career.h"

#include <algorithm>

#include "game/front/level_editor_triggers.h"
#include "game/mth/vector.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::LevelEditor::Career {

bool EditorGap(const std::string& name, const std::vector<std::string>& editorGaps,
               const std::vector<std::string>& levelGaps)
{
    return std::find(editorGaps.begin(), editorGaps.end(), name) != editorGaps.end() &&
           std::find(levelGaps.begin(), levelGaps.end(), name) == levelGaps.end();
}

Checklist ForSave(const Checklist& live, const Checklist* baseline, const std::vector<std::string>& editorGaps,
                  const std::vector<std::string>& levelGaps)
{
    const Checklist& from = baseline ? *baseline : live;
    Checklist out;
    out.built = from.built;
    for (const Gap& gap : from.gaps) {
        if (!EditorGap(gap.name, editorGaps, levelGaps))
            out.gaps.push_back(gap);
    }
    return out;
}

namespace {

// The baseline (level_editor_career.h) and the checklist it is of.
bool g_on;
Game::CGapChecklist* g_of;
Checklist g_baseline;

Game::CGapChecklist* CurrentChecklist()
{
    void* skate = GameVariable<void*>(0x007CE478);
    return skate ? static_cast<Game::CGapChecklist*>(reinterpret_cast<Mdl::Skate*>(skate)->GetGapChecklist()) : nullptr;
}

int Landed(const Checklist& list)
{
    int landed = 0;
    for (const Gap& gap : list.gaps)
        landed += gap.count ? 1 : 0;
    return landed;
}

void TakeBaseline(const char* why)
{
    g_of = CurrentChecklist();
    g_baseline = g_of ? Read(g_of) : Checklist{};
    if (g_of)
        Log("level editor: the career's gap list kept as it is (%s: %u gaps, %d landed) - the edited copy doesn't "
            "change it",
            why, static_cast<unsigned>(g_baseline.gaps.size()), Landed(g_baseline));
}

} // namespace

Checklist Read(Game::CGapChecklist* checklist)
{
    Checklist out;
    out.built = checklist->m_built;
    int count = checklist->Count();
    for (int i = 0; i < count; ++i) {
        Gap gap;
        gap.name = checklist->GetGapName(i);
        gap.score = checklist->GetGapScore(i);
        gap.count = checklist->GetGapCount(i);
        if (gap.count) {
            Mth::Vector pos, dir;
            checklist->GetGapPositions(i, &pos, &dir);
            const float p[4] = {pos.x, pos.y, pos.z, pos.w}, d[4] = {dir.x, dir.y, dir.z, dir.w};
            std::copy(p, p + 4, gap.pos);
            std::copy(d, d + 4, gap.dir);
        }
        out.gaps.push_back(gap);
    }
    return out;
}

void EditedCopy(bool on)
{
    if (on == g_on)
        return;
    g_on = on;
    if (on) {
        TakeBaseline("the edited copy");
        return;
    }
    if (Game::CGapChecklist* checklist = g_of) {
        // Put back as the career's load puts a checklist (CSkaterCareer::ReadFromStructure): cleared, the gaps added.
        Checklist now = Read(checklist);
        checklist->Clear();
        for (const Gap& gap : g_baseline.gaps) {
            checklist->AddGap(gap.name.c_str(), gap.count, gap.score);
            if (gap.count)
                checklist->SetGapPositions(gap.name.c_str(), {gap.pos[0], gap.pos[1], gap.pos[2], gap.pos[3]},
                                           {gap.dir[0], gap.dir[1], gap.dir[2], gap.dir[3]});
        }
        checklist->m_built = g_baseline.built; // (AddGap marks it built)
        Log("level editor: the career's gap list put back as before the edited copy (%u gaps, %d landed; it had %u, "
            "%d landed)",
            static_cast<unsigned>(g_baseline.gaps.size()), Landed(g_baseline), static_cast<unsigned>(now.gaps.size()),
            Landed(now));
    }
    g_of = nullptr;
    g_baseline = {};
}

void CareerReplaced()
{
    if (g_on)
        TakeBaseline("the career replaced");
}

bool SaveView(Game::CGapChecklist* checklist, Checklist* out)
{
    std::vector<std::string> editorGaps, levelGaps;
    bool editor = Triggers::EditorGaps(checklist, &editorGaps, &levelGaps);
    bool baseline = g_on && checklist && checklist == g_of;
    if (!baseline && !editor)
        return false;
    Checklist live = Read(checklist);
    *out = ForSave(live, baseline ? &g_baseline : nullptr, editorGaps, levelGaps);
    Log("level editor: the career save gets this level's gap list %s (%u gaps, %d landed; it has %u, %d landed)",
        baseline ? "as before the edited copy" : "without the editor's gaps", static_cast<unsigned>(out->gaps.size()),
        Landed(*out), static_cast<unsigned>(live.gaps.size()), Landed(live));
    return true;
}

} // namespace Front::LevelEditor::Career
