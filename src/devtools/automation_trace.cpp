// The test-automation channel's frame trace (devtools/automation.h): the skater's or every object's motion per frame,
// logged. NOT ORIGINAL.

#include <windows.h>
#include <cstdint>
#include <string>
#include <utility>
#include "engine/gfx/engine_glue.h"
#include "game/online/gamenet_manager.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/motion.h"
#include "engine/objects/rigidbody_component.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "game/rider/core_physics.h"
#include "game/rider/skater_state.h"
#include "game/skate/skate_module.h"
#include "engine/sys/mlp.h"
#include "platform/run_log.h"
#include "game/skate/point_tally.h"
#include "game/rider/skater_score.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include <gsl/pointers>
#include "base/core/format.h"
#include "devtools/automation_internal.h"

namespace frontend::automation {
// What the source file's anonymous namespace held that its pieces share (automation_internal.h): visible as it was. (In
// each piece, not the header: SF.7.)
using namespace internal;
} // namespace frontend::automation

namespace frontend::automation {
namespace {

constexpr uint32_t kStateModuleKey = 0x43686585;             // SkaterState
constexpr uint32_t kScoreModuleKey = 0xD262802F;             // SkaterScore
constexpr uint32_t kSkaterCorePhysicsComponent = 0x5BD63B29; // SkaterCorePhysics

} // namespace
namespace internal {

Trace& trace_state()
{
    static Trace trace;
    return trace;
}

} // namespace internal
namespace {

void stop_trace()
{
    Trace& trace = trace_state();
    trace.file.reset();
    trace.objects.reset();
    trace.left = 0;
}

} // namespace
namespace internal {

std::string start_trace(int frames, bool objects)
{
    stop_trace();
    const std::string path = game_folder_path("thug2-decomp-trace.txt");
    fs::OwnedStream file = fs::open_stream(path.c_str(), "w", fs::StreamSharing::kDenyWrite);
    if (!file) {
        return "can't write " + path;
    }
    fs::print_to(
        file.get(),
        "# frame x y z vx vy vz up_y state physics bailing trick pot multiplier landed ground_flags ground_object\n");
    fs::disk().stream_flush(file.get());
    trace_state().file = std::move(file);
    trace_state().left = frames;
    if (objects) {
        const std::string objects_path = game_folder_path("thug2-decomp-trace-objects.txt");
        fs::OwnedStream objects_file = fs::open_stream(objects_path.c_str(), "w", fs::StreamSharing::kDenyWrite);
        if (!objects_file) {
            stop_trace();
            return "can't write " + objects_path;
        }
        fs::print_to(objects_file.get(), "# frame id kind x y z vx vy vz at_x at_y at_z\n");
        fs::disk().stream_flush(objects_file.get());
        trace_state().objects = std::move(objects_file);
    }
    return {};
}

} // namespace internal
namespace {

// trace ... objects: a line for each object with a motion or rigid body component (kind m / r / b both), in the
// object list's order - its id, position, velocity and the at row of its matrix.
void trace_object(objects::Entity* object, void* data)
{
    auto* composite = core::down_cast<objects::GameEntity>(object);
    const bool motion = composite->fetch_module(objects::KinematicsModule::kType) != nullptr;
    const bool body = composite->fetch_module(objects::BodyDynamicsModule::kType) != nullptr;
    if (!motion && !body) {
        return;
    }
    char kind = 'r';
    if (motion) {
        kind = body ? 'b' : 'm';
    }
    const maths::Vec4& pos = composite->at_point();
    const maths::Vec4& vel = composite->vel();
    const maths::Vec4& at = ::core::at(composite->basis_frame().row, 2);
    fs::print_to(static_cast<fs::DiskStream*>(data), "%u 0x%08X %c %.3f %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f\n",
                 frame_loop::frames_run(), composite->ident(), kind, static_cast<double>(pos.x),
                 static_cast<double>(pos.y), static_cast<double>(pos.z), static_cast<double>(vel.x),
                 static_cast<double>(vel.y), static_cast<double>(vel.z), static_cast<double>(at.x),
                 static_cast<double>(at.y), static_cast<double>(at.z));
}

} // namespace
namespace internal {

// Once a frame: the skater's line (nothing while there's none - a level loading).
void trace_frame()
{
    Trace& trace = trace_state();
    if (!trace.file) {
        return;
    }
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (skater) {
        auto* state = core::down_cast<objects::RiderPoseState>(skater->fetch_module(kStateModuleKey));
        const int where = state ? state->cur_phase() : -1;
        const int physics = state ? static_cast<int>(state->physics_mode()) : -1;
        const int bailing = state ? state->is_on(objects::PoseFlag::kBailing) : -1;
        const int trick = state ? state->in_trick_move() : -1;
        // the score: the combo so far (+0x14) and its multiplier (+0x60), the last score landed (+0x20)
        const auto* score_component = core::down_cast<objects::RiderPointKeeper>(skater->fetch_module(kScoreModuleKey));
        const modules::PointTally* score = score_component ? score_component->points_held() : nullptr;
        const int pot = score ? score->pot() : -1;
        const float multiplier = score ? score->multiplier() : 0.0F;
        const int landed = score ? score->pot_score() : -1;
        // the core physics' last ground hit (its feeler copy at +0x158): the face's flags and the object's checksum
        auto* core = core::down_cast<skater::SkaterPhysicsModule>(skater->fetch_module(kSkaterCorePhysicsComponent));
        const unsigned ground_flags = core ? static_cast<unsigned>(core->floor_feeler().hit().face_flags_) : 0;
        const unsigned ground_object = core ? core->floor_feeler().hit().object_checksum_ : 0;
        fs::print_to(trace.file.get(), "%u %.2f %.2f %.2f %.2f %.2f %.2f %.3f %d %d %d %d %d %.2f %d 0x%04X 0x%08X\n",
                       frame_loop::frames_run(), static_cast<double>(skater->at_point().x),
                       static_cast<double>(skater->at_point().y), static_cast<double>(skater->at_point().z),
                       static_cast<double>(skater->vel().x), static_cast<double>(skater->vel().y),
                       static_cast<double>(skater->vel().z),
                       static_cast<double>(::core::at(skater->basis_frame().row, 1).y), where, physics, bailing, trick,
                       pot, static_cast<double>(multiplier), landed, ground_flags, ground_object);
        fs::disk().stream_flush(trace.file.get());
    }
    if (trace.objects) {
        if (objects::EntityHub* manager = objects::object_hub_slot().get()) {
            manager->for_each(trace_object, trace.objects.get());
        }
        fs::disk().stream_flush(trace.objects.get());
    }
    if (--trace.left <= 0) {
        stop_trace();
    }
}

} // namespace internal
} // namespace frontend::automation
