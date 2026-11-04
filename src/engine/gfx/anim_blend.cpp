// Blending animation poses (0x0047D420-0x0047DC31). See anim_blend.h.

#include "engine/gfx/anim_blend.h"
#include "engine/gfx/frame_source.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <array>
#include <span>

#include "engine/gfx/anim_controller.h"
#include "engine/maths/quat_rotation.h"
#include "engine/maths/vec4.h"
#include "engine/objects/walk_hang.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/core/checksums.h"

namespace gfx {

constexpr int kFourFloatBytes = 16;
constexpr uint32_t kHold = 0xE035F2F8U;     // Hold
constexpr uint32_t kCycle = 0x4F792E6CU;    // Cycle
constexpr uint32_t kPingPong = 0x3153E314U; // PingPong
constexpr uint32_t kWobble = 0x6D941203U;   // Wobble
constexpr uint32_t kAnimTags = 0x5DB4115FU; // AnimTags

namespace {

constexpr float kMinWeight = 9.99999975e-06F; // (0x006453B8)

// a + (b - a) * t (Walk::Lerp).
void lerp_vector(float* out, const float* a, const float* b, float blend)
{
    maths::Vec4 from{};
    maths::Vec4 to{};
    maths::Vec4 between{};
    std::memcpy(&from, a, sizeof(from));
    std::memcpy(&to, b, sizeof(to));
    walk::get_lerp(&between, &from, &to, blend);
    std::memcpy(out, &between, sizeof(between));
}

void copy4(float* to, const float* from) { std::memcpy(to, from, kFourFloatBytes); }

// How many bones are blended and how far (0: from, 1: to).
struct BoneBlend {
    int bones{};
    float t{};
};

// One pose (`to`) taking in a higher one (`from`) by `t`, bone by bone: where both have the bone, blended; where
// only `from` has it, copied (and marked valid in `to`, when `mark_copied`).
void blend_bones(Stance& to, Stance& from, BoneBlend bone_blend, bool mark_copied)
{
    const auto bones = bone_blend.bones;
    const auto blend = bone_blend.t;
    for (int k = 0; k < bones; ++k) {
        if (core::at(to.valid, k) != 0U) {
            if (core::at(from.valid, k) != 0U) {
                std::array<float, 4> blended{};
                maths::QuatRotation& rotation = core::at(to.rotations, k);
                interpolate_rotation(blended, std::span<const float, 4>(rotation.data(), 4),
                                     std::span<float, 4>(core::at(from.rotations, k).data(), 4), blend);
                copy4(rotation.data(), blended.data());
                lerp_vector(blended.data(), core::at(to.translations, k).data(), core::at(from.translations, k).data(),
                            blend);
                copy4(core::at(to.translations, k).data(), blended.data());
            }
        } else if (core::at(from.valid, k) != 0U) {
            copy4(core::at(to.rotations, k).data(), core::at(from.rotations, k).data());
            if (mark_copied) {
                core::at(to.valid, k) = 1;
            }
            copy4(core::at(to.translations, k).data(), core::at(from.translations, k).data());
        }
    }
}

} // namespace

// 0x0047D470 (x87: the first two components and the length unrounded)
std::span<float, 4> interpolate_rotation(std::span<float, 4> out, std::span<const float, 4> from,
                                         std::span<float, 4> to, float blend)
{
    const auto from_at = [from](int component) { return static_cast<double>(core::at(from, component)); };
    const auto to_at = [to](int component) { return static_cast<double>(core::at(to, component)); };
    double dot = from_at(0) * to_at(0);
    dot += to_at(3) * from_at(3);
    dot += to_at(2) * from_at(2);
    dot += to_at(1) * from_at(1);
    if (dot < 0.0) {
        for (float& component : to) {
            component = -component;
        }
    }
    const double x = ((to_at(0) - from_at(0)) * blend) + from_at(0);
    const double y = ((to_at(1) - from_at(1)) * blend) + from_at(1);
    auto z = static_cast<float>(((to_at(2) - from_at(2)) * blend) + from_at(2));
    const double w_unrounded = ((to_at(3) - from_at(3)) * blend) + from_at(3);
    auto w = static_cast<float>(w_unrounded);
    double length = (w_unrounded * w) + (static_cast<double>(z) * z);
    length += y * y;
    length += x * x;
    const double inverse = 1.0F / std::sqrt(length);
    auto rounded = static_cast<float>(inverse);
    core::at(to, 0) = static_cast<float>(inverse * x);
    core::at(to, 1) = static_cast<float>(rounded * y);
    core::at(to, 2) = static_cast<float>(static_cast<double>(rounded) * z);
    core::at(to, 3) = static_cast<float>(static_cast<double>(rounded) * w);
    std::ranges::copy(to, out.begin());
    return out;
}

// 0x0047D680
void mix_poses(std::span<Stance> poses, std::span<float> weights, int bones, Stance& out, int count)
{
    core::at(weights, 0) = 1.0F;
    double others = 0.0;
    for (int k = 1; k < count; ++k) {
        others += core::at(weights, k);
    }
    core::at(weights, 0) = static_cast<float>(1.0F - others);
    // from the top down: each pose taken into the next lower one with a weight
    float taken = 0.0F;
    for (int i = count - 1; i > 0; --i) {
        const float weight = core::at(weights, i);
        if (weight == 0.0F) { // (NaN goes on)
            continue;
        }
        const double above = static_cast<double>(weight) + taken;
        int lower = i - 1;
        while (!(core::at(weights, lower) > 0.0F) && !std::isnan(core::at(weights, lower)) && lower > 0) {
            --lower;
        }
        const double below = core::at(weights, lower);
        taken = static_cast<float>(static_cast<double>(taken) + core::at(weights, i));
        auto blend = static_cast<float>(1.0F - (below / (below + above)));
        blend_bones(core::at(poses, lower), core::at(poses, i), {.bones = bones, .t = blend}, false);
    }
    // The mixed pose (the original copied `bones` poses' worth from the first on, reading past the poses; never
    // reached - the only caller mixes in place).
    if (&out != &core::at(poses, 0)) {
        out = core::at(poses, 0);
    }
}

// 0x0047D8F0
void blend_poses_by_id(std::span<Stance> poses, std::span<float> weights, std::span<const uint32_t> ids, int bones,
                       Stance& out, int count, checksums::Name anim)
{
    const uint32_t id = anim.value();
    double others = 0.0;
    for (int k = 1; k < count; ++k) {
        if (core::at(weights, k) != 0.0F && core::at(ids, k) == id) { // (NaN counts)
            others += core::at(weights, k);
        }
    }
    core::at(weights, 0) = static_cast<float>(1.0F - others);
    float taken = 0.0F;
    for (int i = count - 1; i > 0; --i) {
        const float weight = core::at(weights, i);
        if (!(weight >= kMinWeight) || core::at(ids, i) != id) {
            continue;
        }
        const double above = static_cast<double>(weight) + taken;
        // the next pose down with a weight and the id (the first: any weight). The original's search had no end:
        // with no pose of the id at or below the first it read the words before the arrays and blended into the
        // memory before the poses (the heap block's header and whatever preceded it). It stops at the first pose
        // here, and a pose with no lower one of its id is left as it is.
        int lower = i - 1;
        for (; lower >= 0; --lower) {
            const bool weighted = core::at(weights, lower) > kMinWeight || std::isnan(core::at(weights, lower));
            if ((weighted || lower <= 0) && core::at(ids, lower) == id) {
                break;
            }
        }
        if (lower < 0) {
            continue;
        }
        const double below = core::at(weights, lower);
        taken = static_cast<float>(static_cast<double>(taken) + core::at(weights, i));
        auto blend = static_cast<float>(1.0F - (below / (below + above)));
        blend_bones(core::at(poses, lower), core::at(poses, i), {.bones = bones, .t = blend}, true);
    }
    // (as mix_poses: the original copied `bones` poses' worth; never reached)
    if (&out != &core::at(poses, 0)) {
        out = core::at(poses, 0);
    }
}

namespace {
std::array<uint32_t, 4>& loop_type_checksums()
{
    static std::array<uint32_t, 4> loop_type_checksums{{kHold, kCycle, kPingPong, kWobble}}; // the loop types' checksums
    return loop_type_checksums;
}
} // namespace

// 0x0047D580
void add_timeline_info(AnimTimeline* timeline, script::Struct* info)
{
    constexpr uint32_t kAnim = 0x98549BA4;
    constexpr uint32_t kSpeed = 0xF0D90109;
    constexpr uint32_t kRepeatMode = 0x6B888E80;
    constexpr uint32_t kStartTime = 0xD16B61E6;
    constexpr uint32_t kCurrentTime = 0xD4FC8176;
    constexpr uint32_t kEndTime = 0xAB81BB3D;
    info->put_name_key(checksums::Name{kAnim}, timeline->anim());
    info->push_float(checksums::Name{kSpeed}, timeline->speed());
    info->put_name_key(checksums::Name{kRepeatMode}, core::at((loop_type_checksums()), timeline->repeat_mode()));
    float start = 0.0F;
    float current = 0.0F;
    float end = 0.0F;
    timeline->get_times(&start, &current, &end);
    info->push_float(checksums::Name{kStartTime}, start);
    info->push_float(checksums::Name{kCurrentTime}, current);
    info->push_float(checksums::Name{kEndTime}, end);
}

// 0x0047D420
void BlendChannel::set_flag_4c(bool value) { flag_4c_ = value; }

// 0x0047D430
void BlendChannel::blend_to(BlendGoal blend_goal)
{
    const auto target = blend_goal.target;
    const auto period = blend_goal.period;
    aim_point_ = target;
    period_left_ = period;
    period_ = period;
    if (period == 0.0F) {
        ongoing_ = target;
        outset_ = target;
    } else {
        outset_ = ongoing_;
    }
}

// 0x0047D620
float BlendChannel::fetch_value(uint32_t offset) const
{
    return *static_cast<float*>(render::get_anim_data(lookup_key_ + offset, 0));
}

// 0x0047D640
bool BlendChannel::has_value(uint32_t offset) const
{
    return render::get_anim_data(lookup_key_ + offset, 0) != nullptr;
}

// 0x0047D660
void BlendChannel::clear_flag()
{
    if (script::Struct* params = owner_ptr_->tag_table()) {
        params->erase_module(kAnimTags);
    }
}

} // namespace gfx
