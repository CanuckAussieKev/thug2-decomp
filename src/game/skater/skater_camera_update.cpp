// The skater camera component's frame (0x004FA860): where it looks (built from the skater's state), slerped
// toward that, then placed behind and above the skater and kept clear of the world.
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/slerp.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/obj/camera_util.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/skater_camera_component.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

using Skater::Physics::At;

constexpr uint32_t kCamera = 0xC4E311FA;            // Camera (a component)
constexpr uint32_t kCorePhysics = 0x5BD63B29;       // SkaterCorePhysics (a component)
constexpr uint32_t kBalanceTrick = 0x589E5FB3;      // SkaterBalanceTrick (a component)
constexpr uint32_t kHideFlagBit = 0x16B8E4CB;       // (a script value: the skater's script flag hidden by a lip view)

// The component's fields.
constexpr uint32_t kObject = 0x0C;
constexpr uint32_t kInstantFrames = 0x28;
constexpr uint32_t kTurnCosine = 0x2C;
constexpr uint32_t kOrientation = 0x40;
constexpr uint32_t kWallRideFrames = 0x70;
constexpr uint32_t kSkaterMatrix = 0x74;             // the skater's orientation last frame (a matrix)
constexpr uint32_t kWallFacing = 0xB4;
constexpr uint32_t kSettleTime = 0xDC;
constexpr uint32_t kLookRate = 0xF4;
constexpr uint32_t kZoomRate = 0x110;
constexpr uint32_t kLipState = 0x120;               // 0 none, 1 looking along the lip, 2 blocked: the other way
constexpr uint32_t kWallRideTurn = 0x124;
constexpr uint32_t kRoll = 0x128;
constexpr uint32_t kTilt = 0x12C;
constexpr uint32_t kAirTilt = 0x130;
constexpr uint32_t kLipTilt = 0x134;
constexpr uint32_t kSlerpRate = 0x140;
constexpr uint32_t kVertSlerpRate = 0x144;
constexpr uint32_t kSettleSlerpRate = 0x148;
constexpr uint32_t kShakeLeft = 0x150;
constexpr uint32_t kLookAround = 0x168;             // +0x18 / +0x19 on, +0x20 tilt, +0x24 / +0x28 heading
constexpr uint32_t kInterpolator = 0x16C;
constexpr uint32_t kSkater = 0x170;
constexpr uint32_t kState = 0x174;
constexpr uint32_t kControl = 0x178;                // +0x18 paused, +0x2C the state (+0x118 driving)

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
float& BalanceRollScale() { return GameVariable<float>(0x0067F004); }
float& AirTiltRate() { return GameVariable<float>(0x0067F008); }
float& TiltRecovery() { return GameVariable<float>(0x0067F010); }
float& LipTurn() { return GameVariable<float>(0x007CCDB4); }

uint8_t* Bytes(void* p) { return static_cast<uint8_t*>(p); }
Mth::Vector& VectorAt(void* p, uint32_t offset) { return At<Mth::Vector>(p, offset); }
bool VertAir(const uint8_t* state) { return state[0x30] && !state[0x48] && !state[0x78]; }

// 0x00580030 (thiscall on the skater): its core physics' display matrix.
Mth::Matrix* SkaterDisplayMatrix(CCompositeObject* skater)
{
    return GameFunction<Mth::Matrix*(__thiscall*)(void*)>(0x00580030)(skater);
}
// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void Multiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}
// 0x004F9970 (thiscall; quarter 3's CSkaterCameraModes::UpdateLerps): the distance and height eased.
void EaseOffsets(void* camera) { GameFunction<void(__thiscall*)(void*)>(0x004F9970)(camera); }

// a x b (the original's cross product, inlined: each component a difference of two products) into `out`; w 0.
void Cross(Mth::Vector& out, const Mth::Vector& a, const Mth::Vector& b)
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
double DotZYX(const Mth::Vector& a, const Mth::Vector& b)
{
    return (static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y) + static_cast<double>(a.x) * b.x;
}

} // namespace

// 0x004FA860
void CSkaterCameraComponent::Update()
{
    auto* skater = At<CCompositeObject*>(this, kSkater);
    if (!skater)
        return;
    uint8_t* control = At<uint8_t*>(this, kControl);
    if (control && At<uint8_t*>(control, 0x2C)[0x118]) {
        // Driving: the camera object not updated.
        At<CCompositeObject*>(this, kObject)->m_compositeFlags |= 1;
        return;
    }
    auto* cameraComponent = At<CCompositeObject*>(this, kObject)->GetComponent(kCamera);
    bool instant = false;
    if ((At<CCompositeObject*>(this, kSkater)->m_compositeFlags >> 3) & 1) {
        instant = true;
        At<int>(this, kInstantFrames) = 3;
    }
    if (At<int>(this, kInstantFrames) > 0) {
        --At<int>(this, kInstantFrames);
        instant = true;
    }
    float seconds = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    EaseOffsets(this);
    auto* camera = At<Gfx::Camera*>(cameraComponent, 0x18);
    camera->StoreOldPos();
    Mth::Matrix& view = At<CCompositeObject*>(cameraComponent, kObject)->m_matrix;
    for (uint32_t i = 0; i < 0x30; i += 4)
        At<uint32_t>(&view, i) = At<uint32_t>(this, kOrientation + i);
    float lastFacingZ = view.row[2].z;
    float lastFacingY = view.row[2].y;
    float lastFacingX = view.row[2].x;
    uint8_t* state = At<uint8_t*>(this, kState);
    int physicsState = At<int>(state, 0x18);
    uint8_t* core = reinterpret_cast<uint8_t*>(At<CCompositeObject*>(this, kSkater)->GetComponent(kCorePhysics));
    bool turned = false;

    // The orientation to look along: the skater's (its display matrix), or on a wall ride and while +0x2EC holds
    // it, the last one; a wall ride notes how far the wall's facing turned.
    Mth::Matrix orient;
    if (physicsState != 2 && (!core || !core[0x2EC])) {
        At<int>(this, kWallRideFrames) = 0;
        skater = At<CCompositeObject*>(this, kSkater);
        orient = skater->m_unknownBC ? *SkaterDisplayMatrix(skater) : skater->m_displayMatrix;
        At<Mth::Matrix>(this, kSkaterMatrix) = orient;
    } else {
        orient = At<Mth::Matrix>(this, kSkaterMatrix);
        if (physicsState == 2) {
            skater = At<CCompositeObject*>(this, kSkater);
            const Mth::Vector& facing = skater->m_unknownBC ? SkaterDisplayMatrix(skater)->row[2] : skater->m_displayMatrix.row[2];
            if (At<int>(this, kWallRideFrames) == 0) {
                Mth::Vector& wall = VectorAt(this, kWallFacing);
                wall = facing;
                wall.y = 0.0f;
                wall.Normalize();
            } else {
                Mth::Vector flat(facing.x, 0.0f, facing.z, facing.w);
                flat.Normalize();
                const Mth::Vector& wall = VectorAt(this, kWallFacing);
                double was = std::atan2(static_cast<double>(wall.z), static_cast<double>(wall.x));
                double now = std::atan2(static_cast<double>(flat.z), static_cast<double>(flat.x));
                if (was < 0.0)
                    was += kTwoPi;
                if (now < 0.0)
                    now += kTwoPi;
                if (std::fabs(now - was) > kTurnedAngle)
                    turned = true;
            }
            ++At<int>(this, kWallRideFrames);
        }
    }

    // The roll: on a rail on the skater's model, the balance's lean.
    double roll = 0.0;
    if (physicsState == 4 && At<CCompositeObject*>(this, kSkater)->m_unknownBC) {
        auto* balance = reinterpret_cast<uint8_t*>(At<CCompositeObject*>(this, kSkater)->GetComponent(kBalanceTrick));
        roll = static_cast<double>(BalanceRollScale()) * At<float>(balance, 0xAC);
    }
    float& rollNow = At<float>(this, kRoll);
    if (rollNow != roll) {
        if (instant)
            rollNow = static_cast<float>(roll);
        else
            rollNow = static_cast<float>((roll - rollNow) * At<float>(this, kLookRate) + rollNow);
    }

    // The up for the offsets: the orientation's (off the ground), else the state's ground normal (+0xF8).
    Mth::Vector up = physicsState != 0 ? orient.row[1] : VectorAt(state, 0xF8);

    // The facing from the velocity (or straight down).
    skater = At<CCompositeObject*>(this, kSkater);
    float velocityZ = skater->m_vel.z;
    float velocityX = skater->m_vel.x;
    bool lookDown = LookDown();
    if (!(control && control[0x18])) {
        double horizontal = std::sqrt(static_cast<double>(velocityX) * velocityX + static_cast<double>(velocityZ) * velocityZ);
        bool along;
        if (horizontal > kSlowHorizontal)
            along = !lookDown;
        else
            along = !lookDown && At<uint8_t*>(this, kState)[0x78];
        bool skip = !(horizontal > kSlowHorizontal) && !lookDown && !At<uint8_t*>(this, kState)[0x78];
        if (!skip) {
            Mth::Vector& right = orient.row[0];
            Mth::Vector& upRow = orient.row[1];
            Mth::Vector& facing = orient.row[2];
            if (along) {
                skater = At<CCompositeObject*>(this, kSkater);
                facing = skater->m_vel;
                uint8_t* s = At<uint8_t*>(this, kState);
                if (s[0x78]) {
                    facing.x = 0.0f;
                    facing.z = 0.0f;
                    facing += VectorAt(s, 0xE4);
                }
                facing.Normalize();
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
                    upRow = Mth::Vector(0.0f, 1.0f, 0.0f, 1.0f);
                } else if (physicsState == 2) {
                    ux = 0.0, uy = 1.0, uz = 0.0, uw = 1.0;
                    k = facing.y;
                } else {
                    const Mth::Vector& n = VectorAt(s, 0x108);
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
                facing.Normalize();
            } else {
                facing = Mth::Vector(0.0f, -1.0f, 0.0f, 1.0f);
                uint8_t* lookAround = At<uint8_t*>(this, kLookAround);
                if (!lookAround[0x18] && !lookAround[0x19] && physicsState != 3) {
                    At<float>(lookAround, 0x20) = 0.0f;
                    At<float>(At<uint8_t*>(this, kLookAround), 0x24) = 0.0f;
                }
            }
            Cross(right, upRow, facing);
            right.Normalize();
            Cross(upRow, facing, right);
            upRow.Normalize();
        }
    }
    if (At<CCompositeObject*>(this, kSkater)->m_unknownBC && At<uint8_t*>(this, kState)[0xA8]) {
        auto* physics = reinterpret_cast<uint8_t*>(At<CCompositeObject*>(this, kSkater)->GetComponent(kCorePhysics));
        orient = At<Mth::Matrix>(physics, 0x3B0);
    }

    // The tilt in the air: up to 20 degrees, back to level after.
    float& airTilt = At<float>(this, kAirTilt);
    bool wallRide = false;
    if (physicsState == 1) {
        double tilt = Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * AirTiltRate() + airTilt;
        airTilt = static_cast<float>(tilt);
        if (tilt > kAirTiltMaxValue)
            At<uint32_t>(this, kAirTilt) = kAirTiltMax;
    } else {
        if (airTilt > 0.0f) {
            double tilt = airTilt - Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * TiltRecovery();
            airTilt = static_cast<float>(tilt);
            if (tilt < 0.0)
                airTilt = 0.0f;
        } else if (airTilt < 0.0f) {
            double tilt = Tmr::GetSlomoFrameSeconds() * kFramesPerSecond * TiltRecovery() + airTilt;
            airTilt = static_cast<float>(tilt);
            if (tilt > 0.0)
                airTilt = 0.0f;
        }
        wallRide = physicsState == 2;
    }
    Mth::Matrix rotation;
    uint8_t* lookAround = At<uint8_t*>(this, kLookAround);
    bool lipTilted = false;
    if (!wallRide) {
        float heading = At<float>(lookAround, 0x28) + At<float>(lookAround, 0x24);
        Multiply(&orient, &Mth::CreateRotateYMatrix(rotation, heading), &orient);
        if (physicsState == 3) {
            Multiply(&orient, &Mth::CreateRotateXMatrix(rotation, At<float>(this, kLipTilt) + At<float>(this, kAirTilt)),
                     &orient);
            lipTilted = true;
        }
    } else {
        if (lookAround) {
            At<float>(lookAround, 0x24) = 0.0f;
            At<uint8_t*>(this, kLookAround)[0x18] = 0;
        }
        if (At<float>(this, kWallRideTurn) != 0.0f && turned) {
            skater = At<CCompositeObject*>(this, kSkater);
            Mth::Vector worldUp(0.0f, 1.0f, 0.0f, 1.0f);
            Mth::Vector across = Mth::CrossProduct(skater->m_matrix.row[1], worldUp);
            across.Normalize();
            double side = (static_cast<double>(across.z) * skater->m_matrix.row[2].z +
                           static_cast<double>(across.y) * skater->m_matrix.row[2].y) +
                          static_cast<double>(skater->m_matrix.row[2].x) * across.x;
            float sign = side < 0.0 ? 1.0f : -1.0f; // (-1 when facing along it, or not a number)
            orient.RotateYLocal(static_cast<float>(static_cast<double>(sign) * At<float>(this, kWallRideTurn)));
        }
    }
    if (!lipTilted) {
        uint8_t* s = At<uint8_t*>(this, kState);
        if (!VertAir(s))
            Multiply(&orient, &Mth::CreateRotateXMatrix(rotation, At<float>(this, kTilt) + At<float>(this, kAirTilt)),
                     &orient);
    }
    if (physicsState != 2 || lipTilted) {
        float tilt = At<float>(At<uint8_t*>(this, kLookAround), 0x20);
        Multiply(&orient, &Mth::CreateRotateXMatrix(rotation, tilt), &orient);
    }

    // On a lip: looking along it (a quarter turn), or - with something in the way that way - the other way.
    if (physicsState == 3) {
        LipTurn() = kHalfPi;
        int& lip = At<int>(this, kLipState);
        if (lip == 0) {
            orient.RotateY(kHalfPi);
            Nx::CFeeler feeler;
            feeler.Construct();
            Mth::Vector side(orient.row[2].x, 0.0f, orient.row[2].z, 1.0f);
            side.Normalize();
            const Mth::Vector& position = At<CCompositeObject*>(this, kSkater)->m_pos;
            const Mth::Vector& facing = orient.row[2];
            double backW = static_cast<double>(facing.w) * kLipBack;
            double backZ = static_cast<double>(facing.z) * kLipBack;
            float backY = static_cast<float>(facing.y * static_cast<double>(kLipBack));
            float backX = static_cast<float>(facing.x * static_cast<double>(kLipBack));
            float startW = static_cast<float>(position.w - backW);
            double startZ = position.z - backZ;
            float startY = position.y - backY;
            Mth::Vector start(position.x - backX, startY, static_cast<float>(startZ), startW);
            double reachW = static_cast<double>(side.w) * kLipReach;
            float reachZ = static_cast<float>(side.z * static_cast<double>(kLipReach));
            float reachY = static_cast<float>(side.y * static_cast<double>(kLipReach));
            float reachX = static_cast<float>(side.x * static_cast<double>(kLipReach));
            Mth::Vector end(start.x - reachX, start.y - reachY, static_cast<float>(startZ) - reachZ,
                            static_cast<float>(startW - reachW));
            feeler.SetIgnore(0x90, 0);
            if (feeler.GetCollisionAlong(&start, &end, true)) {
                float firstDistance = feeler.m_distance;
                orient.RotateY(LipTurn() * -2.0f);
                Mth::Vector other(orient.row[2].x, 0.0f, orient.row[2].z, 1.0f);
                other.Normalize();
                Mth::Vector back = At<CCompositeObject*>(this, kSkater)->m_pos - orient.row[2] * kLipBack;
                Mth::Vector otherEnd = back - other * kLipReach;
                if (!feeler.GetCollisionAlong(&back, &otherEnd, true) || !(feeler.m_distance >= firstDistance ||
                                                                          feeler.m_distance != feeler.m_distance)) {
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
        At<int>(this, kLipState) = 0;
    }
    // Blocked on a lip: the skater's script flag (bit 0x16B8E4CB) set, else cleared.
    {
        auto* s = At<CCompositeObject*>(this, kSkater);
        if (s && At<int>(this, kLipState) == 2)
            s->m_flags |= 1u << (Script::GetInteger(kHideFlagBit) & 31); // (SHL: the count's low 5 bits)
        else
            At<CCompositeObject*>(this, kSkater)->m_flags &= ~(1u << (Script::GetInteger(kHideFlagBit) & 31));
    }

    // The view looking along the orientation (right and at reversed), slerped from the camera's.
    Mth::Matrix goal;
    goal.SetFromEulerAngles(0.0f, 0.0f, 0.0f);
    goal.row[0] = Mth::Vector(-orient.row[0].x, -orient.row[0].y, -orient.row[0].z, 0.0f);
    goal.row[1] = Mth::Vector(orient.row[1].x, orient.row[1].y, orient.row[1].z, 0.0f);
    goal.row[2] = Mth::Vector(-orient.row[2].x, -orient.row[2].y, -orient.row[2].z, 0.0f);
    goal.row[3] = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    view.row[3] = Mth::Vector(0.0f, 0.0f, 0.0f, 1.0f);
    if (instant)
        view = goal;
    auto* slerp = At<Mth::SlerpInterpolator*>(this, kInterpolator);
    bool aligned = (static_cast<double>(goal.row[0].x) * view.row[0].x + static_cast<double>(goal.row[0].y) * view.row[0].y +
                    static_cast<double>(goal.row[0].z) * view.row[0].z) > kAligned &&
                   DotZYX(goal.row[1], view.row[1]) > kAligned && DotZYX(goal.row[2], view.row[2]) > kAligned;
    if (!aligned) {
        slerp->SetMatrices(view, goal);
        Mth::Matrix from = view;
        view = goal;
        if (VertAir(At<uint8_t*>(this, kState))) {
            slerp->GetMatrix(view, static_cast<float>(FrameBlend(At<float>(this, kVertSlerpRate), seconds)));
            At<float>(this, kSettleTime) = 1.0f / 6.0f;
        } else if (At<int>(At<uint8_t*>(this, kState), 0x18) == 0 && At<float>(this, kSettleTime) >= seconds) {
            At<float>(this, kSettleTime) = static_cast<float>(At<float>(this, kSettleTime) - static_cast<double>(seconds));
            slerp->GetMatrix(view, static_cast<float>(FrameBlend(At<float>(this, kSettleSlerpRate), seconds)));
        } else {
            At<float>(this, kSettleTime) = 0.0f;
            slerp->GetMatrix(view, static_cast<float>(FrameBlend(At<float>(this, kSlerpRate), seconds)));
            // How far the facing turned this frame: a sharper turn than the last slows the slerp from the last view.
            double turn = (static_cast<double>(lastFacingZ) * view.row[2].z + static_cast<double>(lastFacingX) * view.row[2].x) +
                          static_cast<double>(lastFacingY) * view.row[2].y;
            double limit = static_cast<double>(At<float>(this, kTurnCosine)) * kCosineDecay;
            if (turn >= limit || turn != turn) {
                At<float>(this, kTurnCosine) = static_cast<float>(turn);
            } else {
                float rate = static_cast<float>(turn / limit * At<float>(this, kSlerpRate));
                slerp->SetMatrices(from, goal);
                slerp->GetMatrix(view, static_cast<float>(FrameBlend(rate, seconds)));
                At<float>(this, kTurnCosine) = static_cast<float>(At<float>(this, kTurnCosine) * static_cast<double>(kCosineDecay));
            }
        }
    }
    for (uint32_t i = 0; i < 0x30; i += 4)
        At<uint32_t>(this, kOrientation + i) = At<uint32_t>(&view, i);

    // Where: the target, behind along the view and above along the up; looking at it from above the skater.
    Mth::Vector target;
    GetTarget(&target, instant);
    float height, distance;
    if (instant) {
        float rate = At<float>(this, kZoomRate);
        At<float>(this, kZoomRate) = 1.0f;
        GetZoomedOffsets(&height, &distance);
        At<float>(this, kZoomRate) = rate;
    } else {
        GetZoomedOffsets(&height, &distance);
    }
    {
        const Mth::Vector& at = view.row[2];
        double bx = static_cast<double>(distance) * at.x;
        double by = static_cast<double>(distance) * at.y;
        float bz = static_cast<float>(static_cast<double>(distance) * at.z);
        float bw = static_cast<float>(static_cast<double>(distance) * at.w);
        target.x = static_cast<float>(target.x + bx);
        target.y = static_cast<float>(target.y + by);
        target.z += bz;
        target.w += bw;
    }
    Mth::Vector lift;
    {
        double ux = static_cast<double>(up.x) * height;
        double uy = static_cast<double>(height) * up.y;
        double uz = static_cast<double>(height) * up.z;
        float uw = static_cast<float>(static_cast<double>(height) * up.w);
        lift = Mth::Vector(static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz), uw);
        target.x += lift.x;
        target.y = static_cast<float>(target.y + uy);
        target.z = static_cast<float>(target.z + uz);
        target.w += uw;
    }
    const Mth::Vector& position = At<CCompositeObject*>(this, kSkater)->m_pos;
    double eyeY = static_cast<double>(lift.y) + position.y;
    double eyeZ = static_cast<double>(lift.z) + position.z;
    Mth::Vector eye(lift.x + position.x, static_cast<float>(eyeY), static_cast<float>(eyeZ), lift.w + position.w);
    Mth::Vector look;
    look.w = 1.0f;
    look.x = eye.x - target.x;
    look.y = static_cast<float>(eyeY - target.y);
    look.z = static_cast<float>(eyeZ - target.z);
    look.Normalize();
    Mth::Vector lookUp = view.row[1];
    Mth::Vector lookRight;
    Cross(lookRight, lookUp, look);
    lookRight.Normalize();
    Cross(lookUp, look, lookRight);
    lookUp.Normalize();
    if (At<float>(this, kRoll) != 0.0f) {
        lookUp.RotateAboutAxis(look, At<float>(this, kRoll));
        lookRight.RotateAboutAxis(look, At<float>(this, kRoll));
    }
    view.row[0] = Mth::Vector(-lookRight.x, -lookRight.y, -lookRight.z, 0.0f);
    view.row[1] = Mth::Vector(lookUp.x, lookUp.y, lookUp.z, 0.0f);
    view.row[2] = Mth::Vector(-look.x, -look.y, -look.z, 0.0f);
    if (At<float>(this, kShakeLeft) > 0.0f)
        ApplyShake(&target, &view);

    // Kept clear of the world (unless riding something on a lip).
    bool avoid = true;
    if (At<CCompositeObject*>(this, kSkater)->m_unknownBC) {
        auto* physics = reinterpret_cast<uint8_t*>(At<CCompositeObject*>(this, kSkater)->GetComponent(kCorePhysics));
        if (At<void*>(At<uint8_t*>(physics, 0x152C), 0x18) && physicsState == 3)
            avoid = false;
    }
    if (avoid) {
        const Mth::Vector& at = view.row[2];
        float bx = static_cast<float>(static_cast<double>(distance) * at.x);
        double by = static_cast<double>(distance) * at.y;
        double bz = static_cast<double>(distance) * at.z;
        float bw = static_cast<float>(static_cast<double>(distance) * at.w);
        Mth::Vector focus(target.x - bx, static_cast<float>(target.y - by), static_cast<float>(target.z - bz),
                          target.w - bw);
        AvoidCameraCollisions(&target, &view, &focus, &eye, true, true);
    }
    target.w = 1.0f;
    At<CCompositeObject*>(cameraComponent, kObject)->m_pos = target;
    if (instant)
        At<Gfx::Camera*>(cameraComponent, 0x18)->StoreOldPos();
}

} // namespace Obj

REPLACE(0x004FA860, &Obj::CSkaterCameraComponent::Update);
