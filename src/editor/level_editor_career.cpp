// NOT ORIGINAL - the level editor and the career save (ours; level_editor_career.h).

#include "editor/level_editor_career.h"

#include <algorithm>
#include <vector>

#include "editor/level_editor_triggers.h"
#include "engine/maths/vec4.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"

namespace frontend::level_editor::career {

bool editor_gap(const std::string& name, const std::vector<std::string>& editor_gaps,
               const std::vector<std::string>& level_gaps)
{
    return std::find(editor_gaps.begin(), editor_gaps.end(), name) != editor_gaps.end() &&
           std::find(level_gaps.begin(), level_gaps.end(), name) == level_gaps.end();
}

Checklist for_save(const Checklist& live, const Checklist* baseline, const std::vector<std::string>& editor_gaps,
                  const std::vector<std::string>& level_gaps)
{
    const Checklist& from = baseline ? *baseline : live;
    Checklist out;
    out.built = from.built;
    for (const Gap& gap : from.gaps) {
        if (!editor_gap(gap.name, editor_gaps, level_gaps))
            out.gaps.push_back(gap);
    }
    return out;
}

namespace {

// The baseline (level_editor_career.h) and the checklist it is of.
bool g_on;
goals::GapTally* g_of;
Checklist g_baseline;

goals::GapTally* current_checklist()
{
    void* skate = reinterpret_cast<void*>(modules::g_skate_module);
    return skate ? static_cast<goals::GapTally*>(reinterpret_cast<modules::Skate*>(skate)->fetch_gap_list()) : nullptr;
}

int get_landed(const Checklist& list)
{
    int landed = 0;
    for (const Gap& gap : list.gaps)
        landed += gap.count ? 1 : 0;
    return landed;
}

void take_baseline(const char* why)
{
    g_of = current_checklist();
    g_baseline = g_of ? get_take_in(g_of) : Checklist{};
    if (g_of)
        get_log("level editor: the career's gap list kept as it is (%s: %u gaps, %d landed) - the edited copy doesn't "
            "change it",
            why, static_cast<unsigned>(g_baseline.gaps.size()), get_landed(g_baseline));
}

} // namespace

Checklist get_take_in(goals::GapTally* checklist)
{
    Checklist out;
    out.built = checklist->built_;
    int count = checklist->total_of();
    for (int i = 0; i < count; ++i) {
        Gap gap;
        gap.name = checklist->get_gap_title(i);
        gap.score = checklist->get_gap_points(i);
        gap.count = checklist->count_gaps_found(i);
        if (gap.count) {
            maths::Vec4 pos, dir;
            checklist->get_gap_positions(i, &pos, &dir);
            const float p[4] = {pos.x, pos.y, pos.z, pos.w}, d[4] = {dir.x, dir.y, dir.z, dir.w};
            std::copy(p, p + 4, gap.pos);
            std::copy(d, d + 4, gap.dir);
        }
        out.gaps.push_back(gap);
    }
    return out;
}

void get_edited_copy(bool on)
{
    if (on == g_on)
        return;
    g_on = on;
    if (on) {
        take_baseline("the edited copy");
        return;
    }
    if (goals::GapTally* checklist = g_of) {
        // Put back as the career's load puts a checklist (RiderStoryProgress::decode_from_struct): cleared, the gaps added.
        Checklist now = get_take_in(checklist);
        checklist->get_clear();
        for (const Gap& gap : g_baseline.gaps) {
            checklist->register_gap_entry(gap.name.c_str(), gap.count, gap.score);
            if (gap.count)
                checklist->set_gap_positions(gap.name.c_str(), {gap.pos[0], gap.pos[1], gap.pos[2], gap.pos[3]},
                                           {gap.dir[0], gap.dir[1], gap.dir[2], gap.dir[3]});
        }
        checklist->built_ = g_baseline.built; // (register_gap_entry marks it built)
        get_log("level editor: the career's gap list put back as before the edited copy (%u gaps, %d landed; it had %u, "
            "%d landed)",
            static_cast<unsigned>(g_baseline.gaps.size()), get_landed(g_baseline), static_cast<unsigned>(now.gaps.size()),
            get_landed(now));
    }
    g_of = nullptr;
    g_baseline = {};
}

void career_replaced()
{
    if (g_on)
        take_baseline("the career replaced");
}

bool save_view(goals::GapTally* checklist, Checklist* out)
{
    std::vector<std::string> editor_gaps, level_gaps;
    bool editor = triggers::get_editor_gaps(checklist, &editor_gaps, &level_gaps);
    bool baseline = g_on && checklist && checklist == g_of;
    if (!baseline && !editor)
        return false;
    Checklist live = get_take_in(checklist);
    *out = for_save(live, baseline ? &g_baseline : nullptr, editor_gaps, level_gaps);
    get_log("level editor: the career save gets this level's gap list %s (%u gaps, %d landed; it has %u, %d landed)",
        baseline ? "as before the edited copy" : "without the editor's gaps", static_cast<unsigned>(out->gaps.size()),
        get_landed(*out), static_cast<unsigned>(live.gaps.size()), get_landed(live));
    return true;
}

} // namespace MenuSystem::level_editor::Career
