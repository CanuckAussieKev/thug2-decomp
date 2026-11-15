// NOT ORIGINAL - the paint trails' brushes, read from the skaters (paint_brushes.h).
#include "game/rider/paint_brushes.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "base/core/checksums.h"
#include "base/core/down_cast.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/gfx/paint_trails.h"
#include "engine/objects/entity.h"
#include "engine/script/context.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"

namespace skater::paint_brushes {

namespace {

constexpr uint32_t kSkaterState = 0x43686585; // SkaterState (the component)
constexpr uint32_t kNetLobby = 0x1C471C60;    // NetLobby
constexpr std::size_t kUpRow = 1;
constexpr std::size_t kAtRow = 2;

bool mode_paints()
{
    goals::MatchRules* mode = goals::game_mode();
    if (mode == nullptr) {
        return false;
    }
    static const uint32_t is_freeskate = checksums::hash_from_text("is_freeskate");
    static const uint32_t is_singlesession = checksums::hash_from_text("is_singlesession");
    return mode->is_true(is_freeskate) || mode->is_true(is_singlesession) || mode->get_name_key() == kNetLobby;
}

render::paint::Contact contact_of(const objects::RiderPoseState& pose)
{
    using Pose = objects::RiderPoseState;
    if (pose.physics_mode() != Pose::PhysicsMode::kSkating || pose.in_vehicle() != 0U || pose.is_on(objects::PoseFlag::kBailing) != 0U) {
        return render::paint::Contact::kNone;
    }
    switch (pose.cur_phase()) {
    case Pose::kGround:
        return render::paint::Contact::kGround;
    case Pose::kRail:
    case Pose::kLip:
        return render::paint::Contact::kRail;
    case Pose::kWall:
        return render::paint::Contact::kWall;
    default:
        return render::paint::Contact::kNone;
    }
}

std::array<float, 3> row_of(const maths::Mat4& matrix, std::size_t row)
{
    const auto values = matrix.floats();
    constexpr std::size_t kRow = 4;
    return {core::at(values, row * kRow), core::at(values, (row * kRow) + 1), core::at(values, (row * kRow) + 2)};
}

// A remote player's total score as last seen (its rise: a combo it landed).
std::map<uint32_t, int>& remote_totals()
{
    static std::map<uint32_t, int> totals;
    return totals;
}

// PaintTrailsClear: the paint wiped (true).
bool cfunc_clear(script::Struct* /*params*/, script::Context /*context*/)
{
    render::paint::clear();
    get_log("paint: wiped (PaintTrailsClear)");
    return true;
}

} // namespace

void register_script_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "PaintTrailsClear", .function = &cfunc_clear},
    });
    script::install_script_functions(kFunctions);
}

void gather(std::vector<render::paint::Brush>* brushes)
{
    brushes->clear();
    modules::Skate* skate = modules::Skate::sole();
    if (skate == nullptr || !mode_paints()) {
        remote_totals().clear();
        return;
    }
    const int count = skate->count_riders();
    for (int index = 0; index < count; ++index) {
        auto* rider = static_cast<objects::Rider*>(skate->get_rider_at(index));
        if (rider == nullptr) {
            continue;
        }
        const auto* pose = core::down_cast<objects::RiderPoseState>(rider->fetch_module(kSkaterState));
        if (pose == nullptr) {
            continue;
        }
        const maths::Vec4& at = rider->at_point();
        const bool local = rider->is_local_skater() != 0U;
        render::paint::Brush brush{.id = rider->ident(),
                                   .slot = static_cast<int>(rider->ident()),
                                   .own = local && rider->get_skater_number() == 0,
                                   .contact = contact_of(*pose),
                                   .at = {at.x, at.y, at.z},
                                   .normal = row_of(rider->basis_frame(), kUpRow),
                                   .forward = row_of(rider->basis_frame(), kAtRow)};
        const modules::PointTally* points = rider->get_points_total();
        if (points != nullptr && local) {
            brush.combo = static_cast<float>(points->pot()) * std::max(points->multiplier(), 1.0F);
        } else if (points != nullptr) {
            const int total = points->get_total_score();
            auto [seen, made] = remote_totals().try_emplace(rider->ident(), total);
            if (!made && total > seen->second) {
                brush.landed = static_cast<float>(total - seen->second);
            }
            seen->second = total;
        }
        brushes->push_back(brush);
    }
}

} // namespace skater::paint_brushes
