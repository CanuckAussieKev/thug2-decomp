// Blending animation poses (0x0047D420-0x0047DC31). See anim_blend.h.

#include "game/gfx/anim_blend.h"
#include "game/gfx/frame_source.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/gfx/anim_controller.h"
#include "game/mth/vector.h"
#include "game/obj/walk_hang.h"
#include "game/script/struct.h"

namespace Gfx {

namespace {

constexpr float kMinWeight = 9.99999975e-06f; // (0x006453B8)

// a + (b - a) * t (Walk::Lerp).
void LerpVector(float* out, const float* a, const float* b, float t)
{
    Walk::Lerp(reinterpret_cast<Mth::Vector*>(out), reinterpret_cast<const Mth::Vector*>(a),
               reinterpret_cast<const Mth::Vector*>(b), t);
}

void Copy4(float* to, const float* from)
{
    std::memcpy(to, from, 16);
}

// One pose (`to`) taking in a higher one (`from`) by `t`, bone by bone: where both have the bone, blended; where
// only `from` has it, copied (and marked valid in `to`, when `markCopied`).
void BlendBones(CPose& to, CPose& from, int bones, float t, bool markCopied)
{
    for (int k = 0; k < bones; ++k) {
        if (to.valid[k]) {
            if (from.valid[k]) {
                float blended[4];
                Copy4(to.rotations[k], InterpolateRotation(blended, to.rotations[k], from.rotations[k], t));
                LerpVector(blended, to.translations[k], from.translations[k], t);
                Copy4(to.translations[k], blended);
            }
        } else if (from.valid[k]) {
            Copy4(to.rotations[k], from.rotations[k]);
            if (markCopied)
                to.valid[k] = 1;
            Copy4(to.translations[k], from.translations[k]);
        }
    }
}

} // namespace

// 0x0047D470 (x87: the first two components and the length unrounded)
float* __cdecl InterpolateRotation(float* out, const float* from, float* to, float t)
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
    double wUnrounded = (static_cast<double>(to[3]) - from[3]) * t + from[3];
    float w = static_cast<float>(wUnrounded);
    double length = wUnrounded * w + static_cast<double>(z) * z;
    length += y * y;
    length += x * x;
    double inverse = 1.0f / std::sqrt(length);
    float rounded = static_cast<float>(inverse);
    to[0] = static_cast<float>(inverse * x);
    to[1] = static_cast<float>(rounded * y);
    to[2] = static_cast<float>(static_cast<double>(rounded) * z);
    to[3] = static_cast<float>(static_cast<double>(rounded) * w);
    Copy4(out, to);
    return out;
}

// 0x0047D680
void __cdecl BlendPoses(CPose* poses, float* weights, int bones, CPose* out, int count)
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
        BlendBones(poses[j], poses[i], bones, t, false);
    }
    // (as the original: `bones` poses' worth)
    if (out != poses)
        std::memcpy(out, poses, static_cast<size_t>(bones) * sizeof(CPose));
}

// 0x0047D8F0
void __cdecl BlendPosesById(CPose* poses, float* weights, const uint32_t* ids, int bones, CPose* out, int count,
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
        BlendBones(poses[j], poses[i], bones, t, true);
    }
    if (out != poses)
        std::memcpy(out, poses, static_cast<size_t>(bones) * sizeof(CPose));
}

uint32_t g_loopTypeChecksums[4]{0xE035F2F8, 0x4F792E6C, 0x3153E314, 0x6D941203}; // the loop types' checksums

// 0x0047D580
void __stdcall AddTimelineInfo(CAnimTimeline* timeline, Script::CStruct* info)
{
    constexpr uint32_t kAnim = 0x98549BA4, kSpeed = 0xF0D90109, kLoopType = 0x6B888E80;
    constexpr uint32_t kStartTime = 0xD16B61E6, kCurrentTime = 0xD4FC8176, kEndTime = 0xAB81BB3D;
    info->AddChecksum(kAnim, timeline->m_anim);
    info->AddFloat(kSpeed, timeline->m_speed);
    info->AddChecksum(kLoopType, (g_loopTypeChecksums)[timeline->m_loopType]);
    float start, current, end;
    timeline->GetTimes(&start, &current, &end);
    info->AddFloat(kStartTime, start);
    info->AddFloat(kCurrentTime, current);
    info->AddFloat(kEndTime, end);
}

// 0x0047D420
void CBlendChannel::SetFlag4C(bool value)
{
    m_flag4C = value;
}

// 0x0047D430
void CBlendChannel::BlendTo(uint32_t target, float period)
{
    m_target = target;
    m_periodLeft = period;
    m_period = period;
    if (period == 0.0f) {
        m_current = target;
        m_start = target;
    } else {
        m_start = m_current;
    }
}

// 0x0047D620
float CBlendChannel::GetValue(uint32_t offset)
{
    return *static_cast<float*>(Nx::GetAnimData(m_key + offset, 0));
}

// 0x0047D640
bool CBlendChannel::HasValue(uint32_t offset)
{
    return Nx::GetAnimData(m_key + offset, 0) != nullptr;
}

// 0x0047D660
void CBlendChannel::ClearFlag()
{
    if (auto* params = *reinterpret_cast<Script::CStruct**>(mp_owner + 0x20))
        params->RemoveComponent(0x5DB4115F);
}

} // namespace Gfx
