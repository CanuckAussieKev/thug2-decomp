#pragma once

// Blending animation poses (0x0047D420-0x0047DC31): the poses the controllers of an object's animation blender
// fill in are mixed by their channels' weights, lower channels first taking in the higher ones; a few small pieces
// of the blender channel and the timeline sit here too.

#include <cstddef>
#include <cstdint>

namespace Script {
class CStruct;
}

namespace Gfx {

struct CAnimTimeline;

// A pose (0x840 bytes): a rotation (quaternion) and a translation per bone, and whether each bone has one.
struct CPose {
    float rotations[64][4];    // +0x000
    float translations[64][4]; // +0x400
    uint8_t valid[64];         // +0x800
};
static_assert(sizeof(CPose) == 0x840 && offsetof(CPose, valid) == 0x800, "layout must match the original");

// 0x0047D470: the rotation a fraction `t` of the way from `from` to `to` (the shorter way: `to` is negated first
// when the two point apart), normalised. As the original, `to` is overwritten with the result too.
float* __cdecl InterpolateRotation(float* out, const float* from, float* to, float t);

// 0x0047D680: `count` poses mixed into the first by `weights` (weights[0] becomes 1 minus the others), `bones`
// bones each; the result copied to `out` if that isn't `poses`. The poses' rotations change on the way.
void __cdecl BlendPoses(CPose* poses, float* weights, int bones, CPose* out, int count);
// 0x0047D8F0: the same for the poses whose `ids` are `id` only (weights below 0.00001 left out); a bone a pose
// takes from a higher one is marked valid.
void __cdecl BlendPosesById(CPose* poses, float* weights, const uint32_t* ids, int bones, CPose* out, int count,
                            uint32_t id);

// 0x0047D580 (code nothing calls): the timeline's animation, speed, loop type and times added to `info`.
void __stdcall AddTimelineInfo(CAnimTimeline* timeline, Script::CStruct* info);

// Pieces of the animation blender's channel (the rest isn't rewritten yet).
class CBlendChannel {
public:
    // 0x0047D420
    void SetFlag4C(bool value);
    // 0x0047D430: blending to `target` over `period` (at once when it's 0: the current value is the target too);
    // the blend starts from the current value.
    void BlendTo(uint32_t target, float period);
    // 0x0047D620 / 0x0047D640: the value `offset` past the channel's key (0x0048CF50 finds it), and whether there
    // is one.
    float GetValue(uint32_t offset);
    bool HasValue(uint32_t offset);
    // 0x0047D660 (code nothing calls): flag 0x5DB4115F taken out of the owner's parameters.
    void ClearFlag();

    uint8_t m_unknown00[0x30];
    uint32_t m_current;   // +0x30
    uint32_t m_target;    // +0x34
    uint32_t m_start;     // +0x38
    float m_period;       // +0x3C
    float m_periodLeft;   // +0x40
    uint8_t m_unknown44[8];
    bool m_flag4C;        // +0x4C
    uint8_t m_pad4D[3];
    uint32_t m_key;       // +0x50
    uint8_t m_unknown54[0x14];
    uint8_t* mp_owner;    // +0x68 (its parameters at +0x20)
};
static_assert(offsetof(CBlendChannel, m_current) == 0x30 && offsetof(CBlendChannel, m_flag4C) == 0x4C &&
                  offsetof(CBlendChannel, m_key) == 0x50 && offsetof(CBlendChannel, mp_owner) == 0x68,
              "layout must match the original");

} // namespace Gfx
