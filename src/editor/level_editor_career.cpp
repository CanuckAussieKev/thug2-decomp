// NOT ORIGINAL - the level editor and the career save (ours; level_editor_career.h).

#include "editor/level_editor_career.h"

#include <algorithm>
#include <string>
#include <vector>
#include <array>

#include "editor/level_editor_triggers.h"
#include "base/maths/vec4.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace frontend::level_editor::career {

bool editor_gap(const std::string& name, const std::vector<std::string>& editor_gaps,
                const std::vector<std::string>& level_gaps)
{
    return std::ranges::find(editor_gaps, name) != editor_gaps.end() &&
           std::ranges::find(level_gaps, name) == level_gaps.end();
}

Checklist for_save(const Checklist& live, const Checklist* baseline, const std::vector<std::string>& editor_gaps,
                   const std::vector<std::string>& level_gaps)
{
    const Checklist& from = baseline ? *baseline : live;
    Checklist out;
    out.built = from.built;
    for (const Gap& gap : from.gaps) {
        if (!editor_gap(gap.name, editor_gaps, level_gaps)) {
            out.gaps.push_back(gap);
        }
    }
    return out;
}

namespace {

// NOT ORIGINAL grouping - level_editor_career.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_career_state()).
struct LevelEditorCareerState {
    // The baseline (level_editor_career.h) and the checklist it is of.
    bool on_{};
    goals::GapTally* of_{};
    Checklist baseline_{};
};

LevelEditorCareerState& level_editor_career_state()
{
    static core::NoDestroy<LevelEditorCareerState> state;
    return state.get();
}

goals::GapTally* current_checklist()
{
    modules::Skate* skate = modules::Skate::sole();
    return skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
}

int get_landed(const Checklist& list)
{
    int landed = 0;
    for (const Gap& gap : list.gaps) {
        landed += (gap.count != 0) ? 1 : 0;
    }
    return landed;
}

void take_baseline(const char* why)
{
    level_editor_career_state().of_ = current_checklist();
    level_editor_career_state().baseline_ = level_editor_career_state().of_ ? get_take_in(level_editor_career_state().of_) : Checklist{};
    if (level_editor_career_state().of_) {
        get_log("level editor: the career's gap list kept as it is (%s: %u gaps, %d landed) - the edited copy doesn't "
                "change it",
                why, static_cast<unsigned>(level_editor_career_state().baseline_.gaps.size()), get_landed(level_editor_career_state().baseline_));
    }
}

} // namespace

Checklist get_take_in(goals::GapTally* checklist)
{
    Checklist out;
    out.built = checklist->built();
    const int count = checklist->total_of();
    for (int i = 0; i < count; ++i) {
        Gap gap;
        gap.name = checklist->get_gap_title(i);
        gap.score = checklist->get_gap_points(i);
        gap.count = checklist->count_gaps_found(i);
        if (gap.count != 0) {
            maths::Vec4 pos{};
            maths::Vec4 dir{};
            checklist->get_gap_positions(i, {.pos = &pos, .dir = &dir});
            const std::array<float, 4> position = {{pos.x, pos.y, pos.z, pos.w}};
            const std::array<float, 4> distance = {{dir.x, dir.y, dir.z, dir.w}};
            std::ranges::copy(position, gap.pos.begin());
            std::ranges::copy(distance, gap.dir.begin());
        }
        out.gaps.push_back(gap);
    }
    return out;
}

void get_edited_copy(bool on)
{
    if (on == level_editor_career_state().on_) {
        return;
    }
    level_editor_career_state().on_ = on;
    if (on) {
        take_baseline("the edited copy");
        return;
    }
    if (goals::GapTally* checklist = level_editor_career_state().of_) {
        // Put back as the career's load puts a checklist (RiderStoryProgress::decode_from_struct): cleared, the gaps added.
        const Checklist now = get_take_in(checklist);
        checklist->get_clear();
        for (const Gap& gap : level_editor_career_state().baseline_.gaps) {
            checklist->register_gap_entry(gap.name.c_str(), {.count = gap.count, .score = gap.score});
            if (gap.count != 0) {
                checklist->set_gap_positions(gap.name.c_str(), {.pos = {.x = core::at(gap.pos, 0), .y = core::at(gap.pos, 1), .z = core::at(gap.pos, 2), .w = core::at(gap.pos, 3)}, .dir = {.x = core::at(gap.dir, 0), .y = core::at(gap.dir, 1), .z = core::at(gap.dir, 2), .w = core::at(gap.dir, 3)}});
            }
        }
        checklist->set_built(level_editor_career_state().baseline_.built); // (register_gap_entry marks it built)
        get_log(
            "level editor: the career's gap list put back as before the edited copy (%u gaps, %d landed; it had %u, "
            "%d landed)",
            static_cast<unsigned>(level_editor_career_state().baseline_.gaps.size()), get_landed(level_editor_career_state().baseline_),
            static_cast<unsigned>(now.gaps.size()), get_landed(now));
    }
    level_editor_career_state().of_ = nullptr;
    level_editor_career_state().baseline_ = {};
}

void career_replaced()
{
    if (level_editor_career_state().on_) {
        take_baseline("the career replaced");
    }
}

bool save_view(goals::GapTally* checklist, Checklist* out)
{
    std::vector<std::string> editor_gaps;
    std::vector<std::string> level_gaps;
    const bool editor = triggers::get_editor_gaps(checklist, &editor_gaps, &level_gaps);
    const bool baseline =
        level_editor_career_state().on_ && (checklist != nullptr) && checklist == level_editor_career_state().of_;
    if (!baseline && !editor) {
        return false;
    }
    const Checklist live = get_take_in(checklist);
    *out = for_save(live, baseline ? &level_editor_career_state().baseline_ : nullptr, editor_gaps, level_gaps);
    get_log("level editor: the career save gets this level's gap list %s (%u gaps, %d landed; it has %u, %d landed)",
            baseline ? "as before the edited copy" : "without the editor's gaps",
            static_cast<unsigned>(out->gaps.size()), get_landed(*out), static_cast<unsigned>(live.gaps.size()),
            get_landed(live));
    return true;
}

} // namespace frontend::level_editor::career
