// Pieces of the Walk component (Obj::CWalkComponent): see walk_physics.h. From the disassembly of 0x00524E10-
// 0x00529F62.

#include "game/skater/walk_physics.h"

#include <cstdint>

#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

constexpr float kCameraOffset = -18.0f; // 0x0064C614

} // namespace

// 0x00524E10
bool CWalkComponent::UsingDpad()
{
    uint8_t* input = At<uint8_t*>(this, 0x1770);
    if (!(At<float>(input, 0x228) == 0.0f) || !(At<float>(input, 0x22C) == 0.0f))
        return false;
    return At<bool>(this, 0x98);
}

// 0x00524E60
Mth::Vector* CWalkComponent::GetCameraOffset(Mth::Vector* out)
{
    int state = At<int>(this, 0x18);
    bool offset;
    if (state == 1) {
        int previous = At<int>(this, 0x20);
        offset = (previous == 2 || previous == 3) &&
                 static_cast<uint32_t>(Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - At<int>(this, 0x1C))) < 150;
    } else {
        offset = state > 1 && state <= 4;
    }
    if (!offset) {
        *out = {0.0f, 0.0f, 0.0f, 1.0f};
        return out;
    }
    const Mth::Vector& facing = At<Mth::Vector>(this, 0x48);
    *out = {facing.x * kCameraOffset, facing.y * kCameraOffset, facing.z * kCameraOffset, facing.w * kCameraOffset};
    return out;
}

} // namespace Obj

REPLACE(0x00524E10, &Obj::CWalkComponent::UsingDpad);
REPLACE(0x00524E60, &Obj::CWalkComponent::GetCameraOffset);
