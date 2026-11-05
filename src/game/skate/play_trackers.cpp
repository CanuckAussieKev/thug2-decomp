// NOT ORIGINAL - following a skater's gaps and combos (play_trackers.h).

#include "game/skate/play_trackers.h"

#include <vector>

namespace frontend::level_editor::play_test {

std::vector<GapChange> GapTracker::tick_frame(const std::vector<GapRead>& now)
{
    std::vector<GapChange> changes;
    if (!known) {
        known = true;
        last = now;
        return changes;
    }
    for (const GapRead& gap_read : now) {
        const GapRead* old = nullptr;
        for (const GapRead& last_gap_read : last) {
            if (last_gap_read.name == gap_read.name) {
                old = &last_gap_read;
                break;
            }
        }
        const int old_pending = old ? old->pending : 0;
        const int old_count = old ? old->count : gap_read.count;
        if (gap_read.pending > old_pending) {
            changes.push_back({.kind = GapChange::Kind::kGot, .name = gap_read.name, .score = gap_read.score});
        }
        if (gap_read.count > old_count) {
            // (got and landed between two reads: the got first)
            if (old_pending == 0 && gap_read.pending <= old_pending) {
                changes.push_back({.kind = GapChange::Kind::kGot, .name = gap_read.name, .score = gap_read.score});
            }
            changes.push_back({.kind = GapChange::Kind::kLanded, .name = gap_read.name, .score = gap_read.score});
        } else if (old_pending > 0 && gap_read.pending == 0) {
            changes.push_back({.kind = GapChange::Kind::kLost, .name = gap_read.name, .score = gap_read.score});
        }
    }
    last = now;
    return changes;
}

bool ComboTracker::tick_frame(int pending_now, bool bailing, ComboEnd* end)
{
    bool decided = false;
    if (ending_ > 0) {
        if (bailing) {
            *end = {.points = ending_, .landed = false};
            ending_ = 0;
            decided = true;
        } else if (--settle <= 0) {
            *end = {.points = ending_, .landed = true};
            ending_ = 0;
            decided = true;
        }
    }
    if (pending > 0 && pending_now <= 0) {
        if (!decided && bailing) {
            *end = {.points = pending, .landed = false};
            decided = true;
        } else {
            ending_ = pending;
            settle = kSettleFrames;
        }
    }
    pending = pending_now > 0 ? pending_now : 0;
    return decided;
}

} // namespace frontend::level_editor::play_test
