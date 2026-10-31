// The skater camera component's frame (0x004FA860): where it looks (built from the skater's state), slerped
// toward that, then placed behind and above the skater and kept clear of the world.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/gfx/view_cam.h"
#include "game/gfx/d3dx.h"
#include "game/maths/mat4.h"
#include "game/maths/rotation_blend.h"
#include "game/maths/vec4.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/entity.h"
#include "game/script/param_struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/rider.h"
#include "game/skater/skater_camera_component.h"
#include "game/sys/stopwatch.h"

namespace GameObjects {

float g_balanceRollScale{-0.00012783174f};
float g_airTiltRate{0.011635667f};
float g_tiltRecovery{0.046542667f};
float g_lipTurn;

namespace {

using Skater::Physics::At;
using Skater::Physics::VectorAt;

constexpr uint32_t kCamera = 0xC4E311FA;            // Camera (a component)
constexpr uint32_t kCorePhysics = 0x5BD63B29;       // SkaterCorePhysics (a component)
constexpr uint32_t kBalanceTrick = 0x589E5FB3;      // SkaterBalanceTrick (a component)
constexpr uint32_t kHideFlagBit = 0x16B8E4CB;       // (a script value: the skater's script flag hidden by a lip view)

// The game's constants (and tunable globals).
constexpr float kTwoPi = 6.2831854820251465f;       // 0x0064545C
constexpr float kTurnedAngle = 0.02f;               // 0x006492BC
constexpr float kSlowHorizontal = 0.1f;             // 0x00645D74
constexpr float kAirLookDown = 0.7f;                // 0x0064B2C8
constexpr float kGroundLookDown = 0.8f;             // 0x0064BB44
constexpr float kFramesPerSecond = 60.0f;           // 0x00645EA0
constexpr uint32_t kAirTiltMax = 0x3EB2B94E;        // (float bits: about 0.349, 20 degrees)
constexpr float kAirTiltMaxValue = 0.3490700125694275f;
constexpr float kAligned = 0.9999f;                 // 0x0064B7F4
constexpr float kCosineDecay = 0.9998f;             // 0x0064BB40
constexpr float kLipBack = 3.0f;                    // 0x006467A8
constexpr float kLipReach = 72.0f;                  // 0x00646244
constexpr float kHalfPi = 1.5707963705062866f;      // 0x3FC90FDB
float& BalanceRollScale() { return g_balanceRollScale; }
float& AirTiltRate() { return g_airTiltRate; }
float& TiltRecovery() { return g_tiltRecovery; }
float& LipTurn() { return g_lipTurn; }

bool VertAir(const uint8_t* state) { return state[0x30] && !state[0x48] && !state[0x78]; }

Maths::Mat4* SkaterDisplayMatrix(CGameEntity* skater) { return static_cast<CRider*>(skater)->GetDrawMatrix(); }
// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void Multiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// a x b (the original's cross product, inlined: each component a difference of two products) into `out`; w 0.
void Perpendicular(Maths::Vec4& out, const Maths::Vec4& a, const Maths::Vec4& b)
{
    double x = static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y;
    double y = static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z;
    double z = static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x;
    out.w = 0.0f;
    out.x = static_cast<float>(x);
    out.y = static_cast<float>(y);
    out.z = static_cast<float>(z);
}

// The rows' agreement: (z * z' + y * y') + x * x'.
double DotZYX(const Maths::Vec4& a, const Maths::Vec4& b)
{
    return (static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y) + static_cast<double>(a.x) * b.x;
}

} // namespace

// 0x004FA860
void CRiderChaseView::TickFrame()
{
    auto* skater = m_riderRef;
    if (!skater)
        return;
    uint8_t* control = m_control;
    if (control && At<uint8_t*>(control, 0x2C)[0x118]) {
        // Driving: the camera object not updated.
        m_entity->m_compositeFlags |= 1;
        return;
    }
    auto* cameraComponent = m_entity->FetchModule(kCamera);
    bool instant = false;
    if ((m_riderRef->m_compositeFlags >> 3) & 1) {
        instant = true;
        m_instantFrames = 3;
    }
    if (m_instantFrames > 0) {
        --m_instantFrames;
        instant = true;
    }
    float seconds = static_cast<float>(FrameClock::GetSlomoFrameSeconds());
    UpdateLerps();
    auto* camera = At<Gfx2D::ViewCam*>(cameraComponent, 0x18);
    camera->RememberPrevPos();
    Maths::Mat4& view = cameraComponent->m_entity->m_basisFrame;
    std::memcpy(&view, m_orientation, sizeof(m_orientation));
    float lastFacingZ = view.row[2].z;
    float lastFacingY = view.row[2].y;
    float lastFacingX = view.row[2].x;
    uint8_t* state = m_stateRef;
    int physicsState = At<int>(state, 0x18);
    uint8_t* core = reinterpret_cast<uint8_t*>(m_riderRef->FetchModule(kCorePhysics));
    bool turned = false;

    // The orientation to look along: the skater's (its display matrix), or on a wall ride and while +0x2EC holds
    // it, the last one; a wall ride notes how far the wall's facing turned.
    Maths::Mat4 orient;
    if (physicsState != 2 && (!core || !core[0x2EC])) {
        m_wallRideFrames = 0;
        skater = m_riderRef;
        orient = skater->m_unknownBC ? *SkaterDisplayMatrix(skater) : skater->m_displayMatrix;
        m_skaterMatrix = orient;
    } else {
        orient = m_skaterMatrix;
        if (physicsState == 2) {
            skater = m_riderRef;
            const Maths::Vec4& facing = skater->m_unknownBC ? SkaterDisplayMatrix(skater)->row[2] : skater->m_displayMatrix.row[2];
            if (m_wallRideFrames == 0) {
                Maths::Vec4& wall = m_wallFacing;
                wall = facing;
                wall.y = 0.0f;
                wall.MakeUnit();
            } else {
                Maths::Vec4 flat(facing.x, 0.0f, facing.z, facing.w);
                flat.MakeUnit();
                const Maths::Vec4& wall = m_wallFacing;
                double was = std::atan2(static_cast<double>(wall.z), static_cast<double>(wall.x));
                double now = std::atan2(static_cast<double>(flat.z), static_cast<double>(flat.x));
                if (was < 0.0)
                    was += kTwoPi;
                if (now < 0.0)
                    now += kTwoPi;
                if (std::fabs(now - was) > kTurnedAngle)
                    turned = true;
            }
            ++m_wallRideFrames;
        }
    }

    // The roll: on a rail on the skater's model, the balance's lean.
    double roll = 0.0;
    if (physicsState == 4 && m_riderRef->m_unknownBC) {
        auto* balance = reinterpret_cast<uint8_t*>(m_riderRef->FetchModule(kBalanceTrick));
        roll = static_cast<double>(BalanceRollScale()) * At<float>(balance, 0xAC);
    }
    float& rollNow = m_roll;
    if (rollNow != roll) {
        if (instant)
            rollNow = static_cast<float>(roll);
        else
            rollNow = static_cast<float>((roll - rollNow) * m_lookRate + rollNow);
    }

    // The up for the offsets: the orientation's (off the ground), else the state's ground normal (+0xF8).
    Maths::Vec4 up = physicsState != 0 ? orient.row[1] : VectorAt(state, 0xF8);

    // The facing from the velocity (or straight down).
    skater = m_riderRef;
    float velocityZ = skater->m_vel.z;
    float velocityX = skater->m_vel.x;
    bool lookDown = LookDown();
    if (!(control && control[0x18])) {
        double horizontal = std::sqrt(static_cast<double>(velocityX) * velocityX + static_cast<double>(velocityZ) * velocityZ);
        bool along;
        if (horizontal > kSlowHorizontal)
            along = !lookDown;
        else
            along = !lookDown && m_stateRef[0x78];
        bool skip = !(horizontal > kSlowHorizontal) && !lookDown && !m_stateRef[0x78];
        if (!skip) {
            Maths::Vec4& right = orient.row[0];
            Maths::Vec4& upRow = orient.row[1];
            Maths::Vec4& facing = orient.row[2];
            if (along) {
                skater = m_riderRef;
                facing = skater->m_vel;
                uint8_t* s = m_stateRef;
                if (s[0x78]) {
                    facing.x = 0.0f;
                    facing.z = 0.0f;
                    facing += VectorAt(s, 0xE4);
                }
                facing.MakeUnit();
                double ux, uy, uz, uw;
                double k;
                if (physicsState == 1) {
                    ux = 0.0, uy = 1.0, uz = 0.0, uw = 1.0;
                    if (s[0x78] && facing.y < 0.0f)
                        k = static_cast<float>(((static_cast<double>(facing.z) * 0.0 + static_cast<double>(facing.x) * 0.0) +
                                                facing.y) *
                                               kAirLookDown);
                    else
                        k = facing.y;
                    upRow = Maths::Vec4(0.0f, 1.0f, 0.0f, 1.0f);
                } else if (physicsState == 2) {
                    ux = 0.0, uy = 1.0, uz = 0.0, uw = 1.0;
                    k = facing.y;
                } else {
                    const Maths::Vec4& n = VectorAt(s, 0x108);
                    ux = n.x, uy = n.y, uz = n.z, uw = n.w;
                    k = static_cast<float>(((ux * facing.x + uy * facing.y) + uz * facing.z) * kGroundLookDown);
                }
                // The facing less its part along that up.
                float kw = static_cast<float>(uw * k);
                float kz = static_cast<float>(uz * k);
                double ky = uy * k;
                double kx = ux * k;
                facing.x = static_cast<float>(facing.x - kx);
                facing.y = static_cast<float>(facing.y - ky);
                facing.z -= kz;
                facing.w -= kw;
                facing.MakeUnit();
            } else {
                facing = Maths::Vec4(0.0f, -1.0f, 0.0f, 1.0f);
                CFreeLookModule* lookAround = m_freeLookModule;
                if (!lookAround->m_locked && !lookAround->m_turning && physicsState != 3) {
                    lookAround->m_leanAngle = 0.0f;
                    m_freeLookModule->m_heading = 0.0f;
                }
            }
            Perpendicular(right, upRow, facing);
            right.MakeUnit();
            Perpendicular(upRow, facing, right);
            upRow.MakeUnit();
        }
    }
    if (m_riderRef->m_unknownBC && m_stateRef[0xA8]) {
        auto* physics = reinterpret_cast<uint8_t*>(m_riderRef->FetchModule(kCorePhysics));
        orient = At<Maths::Mat4>(physics, 0x3B0);
    }

    // The tilt in the air: up to 20 degrees, back to level after.
    float& airTilt = m_airTilt;
    bool wallRide = false;
    if (physicsState == 1) {
        double tilt = FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond * AirTiltRate() + airTilt;
        airTilt = static_cast<float>(tilt);
        if (tilt > kAirTiltMaxValue)
            airTilt = std::bit_cast<float>(kAirTiltMax);
    } else {
        if (airTilt > 0.0f) {
            double tilt = airTilt - FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond * TiltRecovery();
            airTilt = static_cast<float>(tilt);
            if (tilt < 0.0)
                airTilt = 0.0f;
        } else if (airTilt < 0.0f) {
            double tilt = FrameClock::GetSlomoFrameSeconds() * kFramesPerSecond * TiltRecovery() + airTilt;
            airTilt = static_cast<float>(tilt);
            if (tilt > 0.0)
                airTilt = 0.0f;
        }
        wallRide = physicsState == 2;
    }
    Maths::Mat4 rotation;
    CFreeLookModule* lookAround = m_freeLookModule;
    bool lipTilted = false;
    if (!wallRide) {
        float heading = lookAround->m_modeHeading + lookAround->m_heading;
        Multiply(&orient, &Maths::MakeYawMatrix(rotation, heading), &orient);
        if (physicsState == 3) {
            Multiply(&orient, &Maths::MakePitchMatrix(rotation, m_lipTrickTilt + m_airTilt), &orient);
            lipTilted = true;
        }
    } else {
        if (lookAround) {
            lookAround->m_heading = 0.0f;
            m_freeLookModule->m_locked = false;
        }
        if (m_wallrideOffsetAngle != 0.0f && turned) {
            skater = m_riderRef;
            Maths::Vec4 worldUp(0.0f, 1.0f, 0.0f, 1.0f);
            Maths::Vec4 across = Maths::CrossVec(skater->m_basisFrame.row[1], worldUp);
            across.MakeUnit();
            double side = (static_cast<double>(across.z) * skater->m_basisFrame.row[2].z +
                           static_cast<double>(across.y) * skater->m_basisFrame.row[2].y) +
                          static_cast<double>(skater->m_basisFrame.row[2].x) * across.x;
            float sign = side < 0.0 ? 1.0f : -1.0f; // (-1 when facing along it, or not a number)
            orient.YawLocally(static_cast<float>(static_cast<double>(sign) * m_wallrideOffsetAngle));
        }
    }
    if (!lipTilted) {
        uint8_t* s = m_stateRef;
        if (!VertAir(s))
            Multiply(&orient, &Maths::MakePitchMatrix(rotation, m_leanAngle + m_airTilt), &orient);
    }
    if (physicsState != 2 || lipTilted) {
        float tilt = m_freeLookModule->m_leanAngle;
        Multiply(&orient, &Maths::MakePitchMatrix(rotation, tilt), &orient);
    }

    // On a lip: looking along it (a quarter turn), or - with something in the way that way - the other way.
    if (physicsState == 3) {
        LipTurn() = kHalfPi;
        int& lip = m_lipState;
        if (lip == 0) {
            orient.RotateY(kHalfPi);
            Engine3D::CProbeRay feeler;
            feeler.MakeNew();
            Maths::Vec4 side(orient.row[2].x, 0.0f, orient.row[2].z, 1.0f);
            side.MakeUnit();
            const Maths::Vec4& position = m_riderRef->m_atPoint;
            const Maths::Vec4& facing = orient.row[2];
            double backW = static_cast<double>(facing.w) * kLipBack;
            double backZ = static_cast<double>(facing.z) * kLipBack;
            float backY = static_cast<float>(facing.y * static_cast<double>(kLipBack));
            float backX = static_cast<float>(facing.x * static_cast<double>(kLipBack));
            float startW = static_cast<float>(position.w - backW);
            double startZ = position.z - backZ;
            float originCoordY = position.y - backY;
            Maths::Vec4 start(position.x - backX, originCoordY, static_cast<float>(startZ), startW);
            double reachW = static_cast<double>(side.w) * kLipReach;
            float reachZ = static_cast<float>(side.z * static_cast<double>(kLipReach));
            float reachY = static_cast<float>(side.y * static_cast<double>(kLipReach));
            float reachX = static_cast<float>(side.x * static_cast<double>(kLipReach));
            Maths::Vec4 end(start.x - reachX, start.y - reachY, static_cast<float>(startZ) - reachZ,
                            static_cast<float>(startW - reachW));
            feeler.SetExcluded(0x90, 0);
            if (feeler.GetCollisionAlong(&start, &end, true)) {
                float firstDistance = feeler.m_reach;
                orient.RotateY(LipTurn() * -2.0f);
                Maths::Vec4 other(orient.row[2].x, 0.0f, orient.row[2].z, 1.0f);
                other.MakeUnit();
                Maths::Vec4 back = m_riderRef->m_atPoint - orient.row[2] * kLipBack;
                Maths::Vec4 otherEnd = back - other * kLipReach;
                if (!feeler.GetCollisionAlong(&back, &otherEnd, true) || !(feeler.m_reach >= firstDistance ||
                                                                          feeler.m_reach != feeler.m_reach)) {
                    lip = 2;
                } else {
                    orient.RotateY(LipTurn() + LipTurn());
                    lip = 1;
                }
            } else {
                lip = 1;
            }
        } else if (lip == 1) {
            orient.RotateY(kHalfPi);
        } else {
            orient.RotateY(-kHalfPi);
        }
    } else {
        m_lipState = 0;
    }
    // Blocked on a lip: the skater's script flag (bit 0x16B8E4CB) set, else cleared.
    {
        auto* s = m_riderRef;
        if (s && m_lipState == 2)
            s->m_flagBits |= 1u << (Scripts::FetchIntValue(kHideFlagBit) & 31); // (SHL: the count's low 5 bits)
        else
            m_riderRef->m_flagBits &= ~(1u << (Scripts::FetchIntValue(kHideFlagBit) & 31));
    }

    // The view looking along the orientation (right and at reversed), slerped from the camera's.
    Maths::Mat4 goal;
    goal.SetFromEulerAngles(0.0f, 0.0f, 0.0f);
    goal.row[0] = Maths::Vec4(-orient.row[0].x, -orient.row[0].y, -orient.row[0].z, 0.0f);
    goal.row[1] = Maths::Vec4(orient.row[1].x, orient.row[1].y, orient.row[1].z, 0.0f);
    goal.row[2] = Maths::Vec4(-orient.row[2].x, -orient.row[2].y, -orient.row[2].z, 0.0f);
    goal.row[3] = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    view.row[3] = Maths::Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (instant)
        view = goal;
    auto* slerp = m_arcBlender;
    bool aligned = (static_cast<double>(goal.row[0].x) * view.row[0].x + static_cast<double>(goal.row[0].y) * view.row[0].y +
                    static_cast<double>(goal.row[0].z) * view.row[0].z) > kAligned &&
                   DotZYX(goal.row[1], view.row[1]) > kAligned && DotZYX(goal.row[2], view.row[2]) > kAligned;
    if (!aligned) {
        slerp->SetMatrices(view, goal);
        Maths::Mat4 from = view;
        view = goal;
        if (VertAir(m_stateRef)) {
            slerp->FetchMat(view, static_cast<float>(FrameBlend(m_vertAirSlerp, seconds)));
            m_settleTime = 1.0f / 6.0f;
        } else if (At<int>(m_stateRef, 0x18) == 0 && m_settleTime >= seconds) {
            m_settleTime = static_cast<float>(m_settleTime - static_cast<double>(seconds));
            slerp->FetchMat(view, static_cast<float>(FrameBlend(m_vertAirLandedSlerp, seconds)));
        } else {
            m_settleTime = 0.0f;
            slerp->FetchMat(view, static_cast<float>(FrameBlend(m_slerp, seconds)));
            // How far the facing turned this frame: a sharper turn than the last slows the slerp from the last view.
            double turn = (static_cast<double>(lastFacingZ) * view.row[2].z + static_cast<double>(lastFacingX) * view.row[2].x) +
                          static_cast<double>(lastFacingY) * view.row[2].y;
            double limit = static_cast<double>(m_turnCosine) * kCosineDecay;
            if (turn >= limit || turn != turn) {
                m_turnCosine = static_cast<float>(turn);
            } else {
                float rate = static_cast<float>(turn / limit * m_slerp);
                slerp->SetMatrices(from, goal);
                slerp->FetchMat(view, static_cast<float>(FrameBlend(rate, seconds)));
                m_turnCosine = static_cast<float>(m_turnCosine * static_cast<double>(kCosineDecay));
            }
        }
    }
    std::memcpy(m_orientation, &view, sizeof(m_orientation));

    // Where: the target, behind along the view and above along the up; looking at it from above the skater.
    Maths::Vec4 target;
    FetchGoal(&target, instant);
    float height, distance;
    if (instant) {
        float rate = m_zoomEaseRate;
        m_zoomEaseRate = 1.0f;
        GetZoomedOffsets(&height, &distance);
        m_zoomEaseRate = rate;
    } else {
        GetZoomedOffsets(&height, &distance);
    }
    {
        const Maths::Vec4& at = view.row[2];
        double bx = static_cast<double>(distance) * at.x;
        double by = static_cast<double>(distance) * at.y;
        float bz = static_cast<float>(static_cast<double>(distance) * at.z);
        float bw = static_cast<float>(static_cast<double>(distance) * at.w);
        target.x = static_cast<float>(target.x + bx);
        target.y = static_cast<float>(target.y + by);
        target.z += bz;
        target.w += bw;
    }
    Maths::Vec4 lift;
    {
        double ux = static_cast<double>(up.x) * height;
        double uy = static_cast<double>(height) * up.y;
        double uz = static_cast<double>(height) * up.z;
        float uw = static_cast<float>(static_cast<double>(height) * up.w);
        lift = Maths::Vec4(static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz), uw);
        target.x += lift.x;
        target.y = static_cast<float>(target.y + uy);
        target.z = static_cast<float>(target.z + uz);
        target.w += uw;
    }
    const Maths::Vec4& position = m_riderRef->m_atPoint;
    double eyeY = static_cast<double>(lift.y) + position.y;
    double eyeZ = static_cast<double>(lift.z) + position.z;
    Maths::Vec4 eye(lift.x + position.x, static_cast<float>(eyeY), static_cast<float>(eyeZ), lift.w + position.w);
    Maths::Vec4 look;
    look.w = 1.0f;
    look.x = eye.x - target.x;
    look.y = static_cast<float>(eyeY - target.y);
    look.z = static_cast<float>(eyeZ - target.z);
    look.MakeUnit();
    Maths::Vec4 lookUp = view.row[1];
    Maths::Vec4 lookRight;
    Perpendicular(lookRight, lookUp, look);
    lookRight.MakeUnit();
    Perpendicular(lookUp, look, lookRight);
    lookUp.MakeUnit();
    if (m_roll != 0.0f) {
        lookUp.RotateAboutAxis(look, m_roll);
        lookRight.RotateAboutAxis(look, m_roll);
    }
    view.row[0] = Maths::Vec4(-lookRight.x, -lookRight.y, -lookRight.z, 0.0f);
    view.row[1] = Maths::Vec4(lookUp.x, lookUp.y, lookUp.z, 0.0f);
    view.row[2] = Maths::Vec4(-look.x, -look.y, -look.z, 0.0f);
    if (m_shakeLeft > 0.0f)
        ApplyShake(&target, &view);

    // Kept clear of the world (unless riding something on a lip).
    bool avoid = true;
    if (m_riderRef->m_unknownBC) {
        auto* physics = reinterpret_cast<uint8_t*>(m_riderRef->FetchModule(kCorePhysics));
        if (At<void*>(At<uint8_t*>(physics, 0x152C), 0x18) && physicsState == 3)
            avoid = false;
    }
    if (avoid) {
        const Maths::Vec4& at = view.row[2];
        float bx = static_cast<float>(static_cast<double>(distance) * at.x);
        double by = static_cast<double>(distance) * at.y;
        double bz = static_cast<double>(distance) * at.z;
        float bw = static_cast<float>(static_cast<double>(distance) * at.w);
        Maths::Vec4 focus(target.x - bx, static_cast<float>(target.y - by), static_cast<float>(target.z - bz),
                          target.w - bw);
        AvoidCameraCollisions(&target, &view, &focus, &eye, true, true);
    }
    target.w = 1.0f;
    cameraComponent->m_entity->m_atPoint = target;
    if (instant)
        At<Gfx2D::ViewCam*>(cameraComponent, 0x18)->RememberPrevPos();
}

} // namespace Obj
