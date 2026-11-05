// NOT ORIGINAL - following a skater's gaps and combos (play_trackers.h).

#include "game/skate/play_trackers.h"

#include <cstdint>
#include <vector>

#include "base/core/down_cast.h"
#include "engine/memory/arena_alloc.h"
#include "engine/objects/entity.h"
#include "game/rider/skater_score.h"
#include "game/rider/skater_state.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"

namespace frontend::level_editor::play_test {

namespace {

constexpr uint32_t kSkaterState = 0x43686585; // SkaterState
constexpr uint32_t kSkaterScore = 0xD262802F; // SkaterScore
constexpr double kMaxScorePoints = 2.0e9;     // a score above this is taken as a misread
constexpr int kMaxGapNodes = 4096;            // gap list nodes walked before giving up

} // namespace

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

std::vector<GapRead> read_level_gaps()
{
    std::vector<GapRead> out;
    modules::Skate* skate = modules::Skate::sole();
    const goals::GapTally* list = skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
    if (!list) {
        return out;
    }
    // (the list's nodes: the head's data is -1 - gap_checklist.cpp)
    int guard = 0;
    for (const memory::ListNode* node = list->gaps().next(); node && node != &list->gaps() && ++guard < kMaxGapNodes; node = node->next()) {
        if (node->data() == memory::ListNode::head_marker()) {
            break;
        }
        auto* gap = static_cast<goals::GapEntry*>(node->data());
        out.push_back({.name = gap->label().c_str() ? gap->label().c_str() : "", .pending = gap->pending(), .count = gap->num(), .score = gap->points_held()});
    }
    return out;
}

int combo_points_pending(objects::GameEntity* skater)
{
    int pending = 0;
    if (const objects::ObjectModule* score = skater->fetch_module(kSkaterScore)) {
        if (const modules::PointTally* point_tally =
                core::down_cast<const objects::RiderPointKeeper>(score)->points_held()) {
            const double points = static_cast<double>(point_tally->pot()) * point_tally->multiplier();
            if (points > 0.0 && points < kMaxScorePoints) {
                pending = static_cast<int>(points);
            }
        }
    }
    return pending;
}

bool skater_bailing(objects::GameEntity* skater)
{
    if (objects::ObjectModule* state = skater->fetch_module(kSkaterState)) {
        return core::down_cast<objects::RiderPoseState>(state)->is_on(objects::PoseFlag::kBailing) != 0;
    }
    return false;
}

} // namespace frontend::level_editor::play_test
