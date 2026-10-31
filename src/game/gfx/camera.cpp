#include "game/gfx/camera.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/gfx/graphics_options.h"
#include "game/gfx/screen.h"
#include "game/gfx/settings.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/object.h"

namespace Gfx2D {

static_assert(offsetof(ViewCam, m_oldPos) == 0x4C && offsetof(ViewCam, m_atPoint) == 0x5C &&
                  offsetof(ViewCam, m_basisFrame) == 0x6C && offsetof(ViewCam, m_hFov) == 0xAC &&
                  offsetof(ViewCam, m_adjustedHFov) == 0xB0 && offsetof(ViewCam, m_unknownB4) == 0xB4 &&
                  offsetof(ViewCam, m_nearClip) == 0xB8 && offsetof(ViewCam, m_farClip) == 0xBC &&
                  offsetof(ViewCam, m_unknownC0) == 0xC0 && sizeof(ViewCam) == 0xC4,
              "the original's layout");

namespace {

extern void** const kVtable;
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

int g_drawDistanceSetting;

// 0x00485BA0
ViewCam* ViewCam::MakeNew()
{
    GameObjects::CEntity::MakeNew();
    *reinterpret_cast<void***>(this) = kVtable;
    m_unknownC0 = 1.0f;
    m_hFov = Engine3D::GetScreenAngle();
    RefreshEffectiveHFov();
    m_basisFrame.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    m_basisFrame.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    m_basisFrame.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    m_basisFrame.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    m_nearClip = 12.0f;
    m_farClip = 12000.0f;
    if (Engine3D::g_distanceClipping) { // distance clipping (setting 0x007D6444)
        // (the setting as an unsigned number: FILD of a signed one, 2^32 added when negative)
        double distance = static_cast<double>(g_drawDistanceSetting);
        if (g_drawDistanceSetting < 0)
            distance += 4294967296.0;
        m_farClip = static_cast<float>(distance * 30.0);
    }
    // NOT ORIGINAL - [Graphics] draw_distance_scale (game/gfx/graphics_options.h).
    m_farClip = Engine3D::GraphicsOptions::Scaled(m_farClip, Engine3D::GraphicsOptions::Pull().drawDistanceScale);
    m_atPoint = {0.0f, 0.0f, 0.0f, 1.0f};
    m_oldPos = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x004859F0
void ViewCam::DestroyBody()
{
    *reinterpret_cast<void***>(this) = kVtable;
    GameObjects::CEntity::DestroyBody();
}

// 0x00485A20 (vtable slot 0)
ViewCam* ViewCam::ScalarDelete(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00485AA0 (x87 as the original, so results round the same): the angle as drawn is the 4:3 one widened by the
// screen's angle factor (and divided by m_unknownC0); m_unknownB4 is tan(half that) over tan(half the cameras'
// default angle).
void ViewCam::RefreshEffectiveHFov()
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
    float factor = Engine3D::GetScreenAngleFactor();
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
    float defaultAngle = Engine3D::GetScreenAngle();
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
bool ViewCam::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    if (name != kSetHFOVRadians)
        return GameObjects::CEntity::InvokeMethodByName(name, params, script);
    // (the original reads the float into the stack slot of `params`: without one, the pointer's bits are the angle)
    float radians;
    std::memcpy(&radians, &params, 4);
    params->FetchFloat(0u, &radians, 1);
    m_hFov = radians * kRadiansToDegrees;
    RefreshEffectiveHFov();
    return true;
}

// 0x00485A50
void ViewCam::RememberPrevPos()
{
    std::memcpy(&m_oldPos, &m_atPoint, sizeof(m_atPoint)); // (copied as words)
}

// 0x00485A70
void ViewCam::ChangeLocation(const Maths::Vec4& pos)
{
    std::memcpy(&m_atPoint, &pos, sizeof(m_atPoint)); // (copied as words)
}

// 0x00485A90
Maths::Mat4& ViewCam::ChangeMat(const Maths::Mat4& matrix)
{
    m_basisFrame = matrix;
    return m_basisFrame;
}

// 0x00485B10
float ViewCam::FetchEffectiveHFov()
{
    RefreshEffectiveHFov();
    return m_adjustedHFov;
}

// 0x00485B20
void ViewCam::ChangeHFov(float degrees)
{
    m_hFov = degrees;
    RefreshEffectiveHFov();
}

namespace {
static VtableSlot kVtable_slots[] = {&Gfx2D::ViewCam::ScalarDelete, &GameObjects::CEntity::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &Gfx2D::ViewCam::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
} // namespace

} // namespace Gfx
