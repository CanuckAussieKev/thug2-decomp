// Blending animation poses (0x0047D420-0x0047DC31). See anim_blend.h.

#include "engine/gfx/anim_blend.h"
#include "engine/gfx/frame_source.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/gfx/anim_controller.h"
#include "engine/maths/vec4.h"
#include "engine/objects/walk_hang.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"

namespace gfx {

namespace {

constexpr float kMinWeight = 9.99999975e-06f; // (0x006453B8)

// a + (b - a) * t (Walk::Lerp).
void lerp_vector(float* out, const float* a, const float* b, float t)
{
    walk::get_lerp(reinterpret_cast<maths::Vec4*>(out), reinterpret_cast<const maths::Vec4*>(a),
                   reinterpret_cast<const maths::Vec4*>(b), t);
}

void copy4(float* to, const float* from) { std::memcpy(to, from, 16); }

// One pose (`to`) taking in a higher one (`from`) by `t`, bone by bone: where both have the bone, blended; where
// only `from` has it, copied (and marked valid in `to`, when `mark_copied`).
void blend_bones(Stance& to, Stance& from, int bones, float t, bool mark_copied)
{
    for (int k = 0; k < bones; ++k) {
        if (to.valid[k]) {
            if (from.valid[k]) {
                float blended[4];
                copy4(to.rotations[k], interpolate_rotation(blended, to.rotations[k], from.rotations[k], t));
                lerp_vector(blended, to.translations[k], from.translations[k], t);
                copy4(to.translations[k], blended);
            }
        } else if (from.valid[k]) {
            copy4(to.rotations[k], from.rotations[k]);
            if (mark_copied)
                to.valid[k] = 1;
            copy4(to.translations[k], from.translations[k]);
        }
    }
}

} // namespace

// 0x0047D470 (x87: the first two components and the length unrounded)
float* interpolate_rotation(float* out, const float* from, float* to, float t)
{
    double dot = static_cast<double>(from[0]) * to[0];
    dot += static_cast<double>(to[3]) * from[3];
    dot += static_cast<double>(to[2]) * from[2];
    dot += static_cast<double>(to[1]) * from[1];
    if (dot < 0.0) {
        for (int i = 0; i < 4; ++i)
            to[i] = -to[i];
    }
    double x = (static_cast<double>(to[0]) - from[0]) * t + from[0];
    double y = (static_cast<double>(to[1]) - from[1]) * t + from[1];
    float z = static_cast<float>((static_cast<double>(to[2]) - from[2]) * t + from[2]);
    double w_unrounded = (static_cast<double>(to[3]) - from[3]) * t + from[3];
    float w = static_cast<float>(w_unrounded);
    double length = w_unrounded * w + static_cast<double>(z) * z;
    length += y * y;
    length += x * x;
    double inverse = 1.0f / std::sqrt(length);
    float rounded = static_cast<float>(inverse);
    to[0] = static_cast<float>(inverse * x);
    to[1] = static_cast<float>(rounded * y);
    to[2] = static_cast<float>(static_cast<double>(rounded) * z);
    to[3] = static_cast<float>(static_cast<double>(rounded) * w);
    copy4(out, to);
    return out;
}

// 0x0047D680
void mix_poses(Stance* poses, float* weights, int bones, Stance* out, int count)
{
    weights[0] = 1.0f;
    double others = 0.0;
    for (int k = 1; k < count; ++k)
        others += weights[k];
    weights[0] = static_cast<float>(1.0f - others);
    // from the top down: each pose taken into the next lower one with a weight
    float taken = 0.0f;
    for (int i = count - 1; i > 0; --i) {
        float weight = weights[i];
        if (weight == 0.0f) // (NaN goes on)
            continue;
        double above = static_cast<double>(weight) + taken;
        int j = i - 1;
        while (!(weights[j] > 0.0f) && !std::isnan(weights[j]) && j > 0)
            --j;
        double below = weights[j];
        taken = static_cast<float>(static_cast<double>(taken) + weights[i]);
        float t = static_cast<float>(1.0f - below / (below + above));
        blend_bones(poses[j], poses[i], bones, t, false);
    }
    // (as the original: `bones` poses' worth)
    if (out != poses)
        std::memcpy(out, poses, static_cast<size_t>(bones) * sizeof(Stance));
}

// 0x0047D8F0
void blend_poses_by_id(Stance* poses, float* weights, const uint32_t* ids, int bones, Stance* out, int count,
                       uint32_t id)
{
    double others = 0.0;
    for (int k = 1; k < count; ++k) {
        if (weights[k] != 0.0f && ids[k] == id) // (NaN counts)
            others += weights[k];
    }
    weights[0] = static_cast<float>(1.0f - others);
    float taken = 0.0f;
    for (int i = count - 1; i > 0; --i) {
        float weight = weights[i];
        if (!(weight >= kMinWeight) || ids[i] != id)
            continue;
        double above = static_cast<double>(weight) + taken;
        // the next pose down with a weight and the id (below the first, any weight; as the original, nothing
        // stops the search past the first pose)
        int j = i - 1;
        for (;; --j) {
            bool weighted = weights[j] > kMinWeight || std::isnan(weights[j]);
            if ((weighted || j <= 0) && ids[j] == id)
                break;
        }
        double below = weights[j];
        taken = static_cast<float>(static_cast<double>(taken) + weights[i]);
        float t = static_cast<float>(1.0f - below / (below + above));
        blend_bones(poses[j], poses[i], bones, t, true);
    }
    if (out != poses)
        std::memcpy(out, poses, static_cast<size_t>(bones) * sizeof(Stance));
}

uint32_t g_loop_type_checksums[4]{0xE035F2F8, 0x4F792E6C, 0x3153E314, 0x6D941203}; // the loop types' checksums

// 0x0047D580
void add_timeline_info(AnimTimeline* timeline, script::Struct* info)
{
    constexpr uint32_t kAnim = 0x98549BA4, kSpeed = 0xF0D90109, kRepeatMode = 0x6B888E80;
    constexpr uint32_t kStartTime = 0xD16B61E6, kCurrentTime = 0xD4FC8176, kEndTime = 0xAB81BB3D;
    info->put_name_key(kAnim, timeline->anim_);
    info->push_float(kSpeed, timeline->speed_);
    info->put_name_key(kRepeatMode, (g_loop_type_checksums)[timeline->repeat_mode_]);
    float start, current, end;
    timeline->get_times(&start, &current, &end);
    info->push_float(kStartTime, start);
    info->push_float(kCurrentTime, current);
    info->push_float(kEndTime, end);
}

// 0x0047D420
void BlendChannel::set_flag_4c(bool value) { flag_4c_ = value; }

// 0x0047D430
void BlendChannel::blend_to(uint32_t target, float period)
{
    aim_point_ = target;
    period_left_ = period;
    period_ = period;
    if (period == 0.0f) {
        ongoing_ = target;
        outset_ = target;
    } else {
        outset_ = ongoing_;
    }
}

// 0x0047D620
float BlendChannel::fetch_value(uint32_t offset)
{
    return *static_cast<float*>(render::get_anim_data(lookup_key_ + offset, 0));
}

// 0x0047D640
bool BlendChannel::has_value(uint32_t offset) { return render::get_anim_data(lookup_key_ + offset, 0) != nullptr; }

// 0x0047D660
void BlendChannel::clear_flag()
{
    if (script::Struct* params = reinterpret_cast<objects::Entity*>(owner_ptr_)->tag_table_)
        params->erase_module(0x5DB4115F);
}

} // namespace gfx
