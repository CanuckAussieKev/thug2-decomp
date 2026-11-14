// NOT ORIGINAL - the world drawn between two simulation ticks (see frame_interp.h).

#include "render_pc/frame_interp.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/maths/mat4.h"
#include "base/maths/transform_blend.h"
#include "engine/gfx/view_cam.h"
#include "engine/sys/frame_blend.h"
#include "engine/sys/stopwatch.h"
#include "engine/sys/tick_rate.h"
#include "platform/run_log.h"
#include "render_pc/geom.h"

namespace render::frame_interp {

namespace blend = maths::transform_blend;

namespace {

constexpr uint64_t kNoTick = std::numeric_limits<uint64_t>::max();

// An instance as a tick left it.
struct InstanceRecord {
    const SceneRef* scene = nullptr;
    maths::Mat4 transform{};
    const maths::Mat4* bones = nullptr;
    int bone_count = 0;
    std::vector<maths::Mat4> palette; // (a copy of the bones)
};

// A viewport's camera as a tick left it: its orientation with its position as row 3.
struct CameraRecord {
    const gfx::ViewCam* camera = nullptr;
    maths::Mat4 pose{};
    bool placed = false; // an extension placed it
    uint64_t tick = kNoTick;
};
struct ViewportCameras {
    CameraRecord before;
    CameraRecord now;
};

struct InstanceRestore {
    Singleton* instance;
    maths::Mat4 transform;
    maths::Mat4* bones;
};
struct CameraRestore {
    gfx::ViewCam* camera;
    maths::Mat4 basis;
    maths::Vec4 at;
};

struct InterpState {
    uint64_t tick = kNoTick;     // the tick `current` is from
    uint64_t cut_tick = kNoTick; // a tick a cut was reported before (nothing blended into it)
    std::unordered_map<const Singleton*, InstanceRecord> previous;
    std::unordered_map<const Singleton*, InstanceRecord> current;
    std::unordered_map<int, ViewportCameras> cameras;
    std::vector<InstanceRestore> instance_restores;
    std::vector<CameraRestore> camera_restores;
    std::unordered_map<const maths::Mat4*, std::size_t> palette_of; // a palette's blended copy this frame
    std::vector<std::vector<maths::Mat4>> palettes;
    std::size_t palettes_used = 0;
    Counts counts;
    Counts last;
    bool logged = false;
};

InterpState& interp_state()
{
    static core::NoDestroy<InterpState> state;
    return state.get();
}

maths::Mat4 camera_pose(gfx::ViewCam* camera)
{
    maths::Mat4 pose = *camera->fetch_mat();
    core::at(pose.row, 3) = *camera->fetch_location();
    return pose;
}

// The instances as this tick left them (the records' buffers reused).
void capture(InterpState& state)
{
    std::swap(state.previous, state.current);
    state.current.clear();
    for (Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        if (!instance->active()) {
            continue;
        }
        InstanceRecord& record = state.current[instance];
        record.scene = instance->scene();
        record.transform = instance->transform();
        record.bones = instance->bones_;
        record.bone_count = instance->bones_ != nullptr ? instance->bone_count_ : 0;
        record.palette.assign(record.bones, record.bones + record.bone_count); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
}

// The blended copy of a palette (nullptr: a bone jumped - the palette as it is).
maths::Mat4* blended_palette(InterpState& state, const InstanceRecord& before, const InstanceRecord& now, float alpha)
{
    if (const auto found = state.palette_of.find(now.bones); found != state.palette_of.end()) {
        return found->second == SIZE_MAX ? nullptr : core::at(state.palettes, found->second).data();
    }
    const auto count = static_cast<std::size_t>(now.bone_count);
    for (std::size_t i = 0; i < count; ++i) {
        if (blend::is_jump(core::at(before.palette, i), core::at(now.palette, i), blend::kBoneJump)) {
            state.palette_of.emplace(now.bones, SIZE_MAX);
            return nullptr;
        }
    }
    if (state.palettes_used == state.palettes.size()) {
        state.palettes.emplace_back();
    }
    std::vector<maths::Mat4>& out = core::at(state.palettes, state.palettes_used);
    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        core::at(out, i) = blend::blend(core::at(before.palette, i), core::at(now.palette, i), alpha);
    }
    state.palette_of.emplace(now.bones, state.palettes_used);
    ++state.palettes_used;
    return out.data();
}

void blend_instances(InterpState& state, float alpha)
{
    for (Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        const auto now = state.current.find(instance);
        const auto before = state.previous.find(instance);
        if (now == state.current.end() || before == state.previous.end()) {
            ++state.counts.as_is;
            continue;
        }
        const InstanceRecord& a = before->second;
        const InstanceRecord& b = now->second;
        if (a.scene != b.scene || a.bone_count != b.bone_count || instance->bones_ != b.bones ||
            blend::is_jump(a.transform, b.transform, blend::kObjectJump)) {
            ++state.counts.as_is;
            continue;
        }
        state.instance_restores.push_back({.instance = instance, .transform = instance->transform_, .bones = instance->bones_});
        instance->transform_ = blend::blend(a.transform, b.transform, alpha);
        if (b.bone_count > 0) {
            if (maths::Mat4* palette = blended_palette(state, a, b, alpha)) {
                instance->bones_ = palette; // (read only while drawn; put back by end_frame)
            }
        }
        ++state.counts.blended;
    }
}

} // namespace

void begin_frame()
{
    InterpState& state = interp_state();
    state.counts = {};
    const frame_blend::Frame& frame = frame_blend::frame();
    if (!frame.on) {
        return;
    }
    if (frame_blend::take_cut()) {
        state.cut_tick = frame.tick;
    }
    if (frame.tick != state.tick) {
        const bool follows = state.tick != kNoTick && frame.tick == state.tick + 1 && state.cut_tick != frame.tick;
        capture(state);
        if (!follows) {
            state.previous.clear();
        }
        state.tick = frame.tick;
    }
    state.palette_of.clear();
    state.palettes_used = 0;
    if (frame.alpha < 1.0F && !state.previous.empty()) {
        blend_instances(state, frame.alpha);
    }
}

void camera(gfx::ViewCam* camera, int index, bool placed_by_extension)
{
    const frame_blend::Frame& frame = frame_blend::frame();
    if (!frame.on || camera == nullptr) {
        return;
    }
    InterpState& state = interp_state();
    ViewportCameras& cameras = state.cameras[index];
    if (cameras.now.tick != frame.tick) {
        cameras.before = cameras.now;
        cameras.now = {.camera = camera, .pose = camera_pose(camera), .placed = placed_by_extension, .tick = frame.tick};
    }
    const CameraRecord& a = cameras.before;
    const CameraRecord& b = cameras.now;
    if (placed_by_extension || b.placed || a.placed || frame.alpha >= 1.0F || a.tick == kNoTick ||
        a.tick + 1 != frame.tick || state.cut_tick == frame.tick || a.camera != camera || b.camera != camera ||
        blend::is_jump(a.pose, b.pose, blend::kCameraJump)) {
        return;
    }
    state.camera_restores.push_back({.camera = camera, .basis = *camera->fetch_mat(), .at = *camera->fetch_location()});
    const maths::Mat4 pose = blend::blend(a.pose, b.pose, frame.alpha);
    maths::Mat4& basis = *camera->fetch_mat();
    for (int row = 0; row < 3; ++row) {
        const float w = core::at(basis.row, row).w;
        core::at(basis.row, row) = core::at(pose.row, row);
        core::at(basis.row, row).w = w;
    }
    maths::Vec4& at = *camera->fetch_location();
    const float w = at.w;
    at = core::at(pose.row, 3);
    at.w = w;
    ++state.counts.cameras;
}

void end_frame()
{
    InterpState& state = interp_state();
    // (in the reverse order: an instance or camera noted twice ends as it first was)
    for (auto it = state.instance_restores.rbegin(); it != state.instance_restores.rend(); ++it) {
        it->instance->transform_ = it->transform;
        it->instance->bones_ = it->bones;
    }
    for (auto it = state.camera_restores.rbegin(); it != state.camera_restores.rend(); ++it) {
        *it->camera->fetch_mat() = it->basis;
        *it->camera->fetch_location() = it->at;
    }
    state.instance_restores.clear();
    state.camera_restores.clear();
    if (frame_blend::frame().on) {
        state.last = state.counts;
        if (!state.logged && state.counts.blended > 0) {
            state.logged = true;
            get_log("frame pacing: the first blended frame - %d instances blended, %d as they are, %d cameras",
                    state.counts.blended, state.counts.as_is, state.counts.cameras);
        }
    }
}

uint32_t shown_time_ms()
{
    const uint32_t now = frame_clock::get_time();
    const frame_blend::Frame& frame = frame_blend::frame();
    if (!frame.on) {
        return now;
    }
    return frame_pacing::shown_time_ms(now, frame.alpha);
}

double shown_game_seconds(double seconds)
{
    const frame_blend::Frame& frame = frame_blend::frame();
    if (!frame.on) {
        return seconds;
    }
    return seconds - ((1.0 - static_cast<double>(frame.alpha)) * tick_rate::tick_seconds());
}

void forget()
{
    InterpState& state = interp_state();
    state.previous.clear();
    state.current.clear();
    state.cameras.clear();
    state.tick = kNoTick;
}

Counts last_counts() { return interp_state().last; }

} // namespace render::frame_interp
