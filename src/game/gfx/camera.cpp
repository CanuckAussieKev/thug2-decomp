#include "game/gfx/camera.h"

#include <cstddef>
#include <cstring>

#include "game/core/mem.h"
#include "game/front/menu_backdrop.h"
#include "game/gfx/screen.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

static_assert(offsetof(Camera, m_oldPos) == 0x4C && offsetof(Camera, m_pos) == 0x5C &&
                  offsetof(Camera, m_matrix) == 0x6C && offsetof(Camera, m_hFov) == 0xAC &&
                  offsetof(Camera, m_adjustedHFov) == 0xB0 && offsetof(Camera, m_unknownB4) == 0xB4 &&
                  offsetof(Camera, m_nearClip) == 0xB8 && offsetof(Camera, m_farClip) == 0xBC &&
                  offsetof(Camera, m_unknownC0) == 0xC0 && sizeof(Camera) == 0xC4,
              "the original's layout");

namespace {

constexpr uint32_t kVtable = 0x006487E0;
constexpr uint32_t kSetHFOVRadians = 0x44EA2B6D; // the member function (name unknown): the angle in radians

float FloatFromBits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
// The original's constants (0x00648800 pi / 360, 0x00648804 360 / pi, 0x006453BC 0.5, 0x00645464 pi / 180,
// 0x00645468 180 / pi).
const float kDegreesToHalfRadians = FloatFromBits(0x3C0EFA35);
const float kHalfRadiansToDegrees = FloatFromBits(0x42E52EE0);
const float kHalf = 0.5f;
const float kDegreesToRadians = FloatFromBits(0x3C8EFA35);
const float kRadiansToDegrees = FloatFromBits(0x42652EE0);

} // namespace

// 0x00485BA0
Camera* Camera::Construct()
{
    Obj::CObject::Construct();
    *reinterpret_cast<uint32_t*>(this) = kVtable;
    m_unknownC0 = 1.0f;
    m_hFov = Nx::GetScreenAngle();
    UpdateAdjustedHFOV();
    m_matrix.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_matrix.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_matrix.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_nearClip = 12.0f;
    m_farClip = 12000.0f;
    if (GameVariable<bool>(0x007D643A)) { // distance clipping (setting 0x007D6444)
        // (the setting as an unsigned number: FILD of a signed one, 2^32 added when negative)
        double distance = static_cast<double>(GameVariable<int>(0x007D6444));
        if (GameVariable<int>(0x007D6444) < 0)
            distance += 4294967296.0;
        m_farClip = static_cast<float>(distance * 30.0);
    }
    m_pos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_oldPos = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x004859F0
void Camera::DestroyBody()
{
    *reinterpret_cast<uint32_t*>(this) = kVtable;
    Obj::CObject::DestroyBody();
}

// 0x00485A20 (vtable slot 0)
Camera* Camera::ScalarDelete(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00485AA0 (x87 as the original, so results round the same): the angle as drawn is the 4:3 one widened by the
// screen's angle factor (and divided by m_unknownC0); m_unknownB4 is tan(half that) over tan(half the cameras'
// default angle).
void Camera::UpdateAdjustedHFOV()
{
    const float toHalfRadians = kDegreesToHalfRadians, toDegrees = kHalfRadiansToDegrees, half = kHalf,
                toRadians = kDegreesToRadians;
    float halfAngle;
    float hFov = m_hFov;
    __asm {
        fld hFov
        fmul toHalfRadians
        fstp halfAngle
    }
    float factor = Nx::GetScreenAngleFactor();
    float divisor = m_unknownC0;
    float adjusted, tanAdjusted;
    __asm {
        fld factor
        fld halfAngle
        fptan
        fstp st(0)
        fdiv divisor
        fmulp st(1), st(0)
        fld1
        fpatan
        fmul toDegrees
        fst adjusted
        fmul toHalfRadians
        fptan
        fstp st(0)
        fstp tanAdjusted
    }
    m_adjustedHFov = adjusted;
    float defaultAngle = Nx::GetScreenAngle();
    float ratio;
    __asm {
        fld defaultAngle
        fmul half
        fmul toRadians
        fptan
        fstp st(0)
        fdivr tanAdjusted
        fstp ratio
    }
    m_unknownB4 = ratio;
}

// 0x00485B40
bool Camera::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    if (name != kSetHFOVRadians)
        return Obj::CObject::CallMemberFunction(name, params, script);
    // (the original reads the float into the stack slot of `params`: without one, the pointer's bits are the angle)
    float radians;
    std::memcpy(&radians, &params, 4);
    params->GetFloat(0u, &radians, 1);
    m_hFov = radians * kRadiansToDegrees;
    UpdateAdjustedHFOV();
    return true;
}

// 0x00485A50
void Camera::StoreOldPos()
{
    std::memcpy(&m_oldPos, &m_pos, sizeof(m_pos)); // (copied as words)
}

// 0x00485A70
void Camera::SetPos(const Mth::Vector& pos)
{
    std::memcpy(&m_pos, &pos, sizeof(m_pos)); // (copied as words)
    // Not original - the menu_backdrop feature (see menu_backdrop.h): the front end's camera held at the THPS4 shop
    // view.
    // Original behaviour: the position is kept as given; with menu_backdrop the viewport's camera, while a menu camera
    // plays, is put back at the THPS4 skate shop view.
    Front::MenuBackdrop::HoldCamera(this);
}

// 0x00485A90
Mth::Matrix& Camera::SetMatrix(const Mth::Matrix& matrix)
{
    m_matrix = matrix;
    // Not original - the menu_backdrop feature.
    // Original behaviour: the matrix is kept as given; with menu_backdrop the viewport's camera, while a menu camera
    // plays, is turned back to the THPS4 skate shop view.
    Front::MenuBackdrop::HoldCamera(this);
    return m_matrix;
}

// 0x00485B10
float Camera::GetAdjustedHFOV()
{
    UpdateAdjustedHFOV();
    return m_adjustedHFov;
}

// 0x00485B20
void Camera::SetHFOV(float degrees)
{
    m_hFov = degrees;
    UpdateAdjustedHFOV();
    // Not original - the menu_backdrop feature.
    // Original behaviour: the angle is kept as given; with menu_backdrop the viewport's camera, while a menu camera
    // plays, gets the held view's default angle back.
    Front::MenuBackdrop::HoldCamera(this);
}

} // namespace Gfx

REPLACE(0x00485BA0, &Gfx::Camera::Construct);
REPLACE(0x004859F0, &Gfx::Camera::DestroyBody);
REPLACE(0x00485A20, &Gfx::Camera::ScalarDelete);
REPLACE(0x00485B40, &Gfx::Camera::CallMemberFunction);
REPLACE_SOURCE_ONLY(0x00485A00, &Gfx::Camera::GetPos);    // 4 bytes
REPLACE_SOURCE_ONLY(0x00485A10, &Gfx::Camera::GetMatrix); // 4 bytes
REPLACE(0x00485A50, &Gfx::Camera::StoreOldPos);
REPLACE(0x00485A70, &Gfx::Camera::SetPos);
REPLACE(0x00485A90, &Gfx::Camera::SetMatrix);
REPLACE(0x00485AA0, &Gfx::Camera::UpdateAdjustedHFOV);
REPLACE(0x00485B10, &Gfx::Camera::GetAdjustedHFOV);
REPLACE(0x00485B20, &Gfx::Camera::SetHFOV);
