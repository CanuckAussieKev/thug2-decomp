// The skater's non-local network logic component (GameObjects::CRiderRemoteSync): see nonlocal_net_logic.h.
// The movement code follows the original's x87 maths: products and sums kept unrounded (double, at the game's 53-bit
// precision) until the original stores them as floats.

#include "game/rider/nonlocal_net_logic.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/shadow_component.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/looping_sound.h"
#include "game/rider/rider.h"
#include "game/rider/skater_x87.h"
#include "platform/stopwatch.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include "game/skate/game_mode.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterNonLocalNetLogic = 0x850EEF66;
constexpr uint32_t kSkaterStateHistory = 0x18223FD6, kSkaterState = 0x43686585, kSkaterEndRun = 0xB1D5A3E1,
                   kAnimBlender = 0x7149B753, kSkaterFlipAndRotate = 0x488AF07A, kModel = 0x286A8D26,
                   kShadow = 0x8A897DD2, kSkaterLoopingSound = 0xB5F1E9D2;
// Physics and script values
constexpr uint32_t kGroundSnapUp = 0xE4D79235, kGroundSnapDown = 0x899BA3D0, kWallNonSkatableAngle = 0x3EEDE4D3,
                   kGroundStickAngleForward = 0x4138283E, kFirstForwardCollisionHeight = 0xD4205C9B,
                   kFirstForwardCollisionLength = 0x20102726, kWallBounceDontSlowAngle = 0x1483FD01,
                   kWallBounceAngleMultiplier = 0xE07EE1A9;
// Scripts
constexpr uint32_t kSkaterId = 0x5B24FAAA, kControlType = 0x81CFF663, kStartDrivingScript = 0x7853F8C4,
                   kStopDrivingScript = 0x0F02E44F;
constexpr uint32_t kNetFirefight = 0xBFF33600, kFirefight = 0x3D6D444F;

constexpr float kRadiansPerDegree = 0.01745329238474369f; // 0x00645464
constexpr float kHalfPi = 1.5707963705062866f;            // 0x00645484
constexpr float kPi = 3.1415927410125732f;                // 0x0064BFA4
constexpr float kLightingRise = 0.001f;                   // 0x006461D8 (of the up row)
constexpr float kLightingAbove = 8.0f;                    // 0x006461D4
constexpr float kLightingBelow = 4800.0f;                 // 0x006461D0
constexpr float kSnapBelow = -200.0f;                     // 0x0064A258
constexpr float kOutOfGround = 0.001f;                    // 0x006461D8
constexpr float kWallOut = 6.0f;                          // 0x00646284
constexpr float kBounceReach = 240.0f;                    // 0x00646558
constexpr float kLightTime = 0.02f;                       // 0x3CA3D70A
constexpr float kSpeedToSoundFraction = 0.001f;           // 0x006461D8

constexpr int kStateAir = 1, kStateLip = 3, kStateRail = 4;
// Face flags (as the level editor names them)
constexpr uint16_t kFaceSkatable = 0x1, kFaceNotSkatable = 0x2, kFaceWallride = 0x4, kFaceVert = 0x8;

template <typename T>
T& Prop(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// A physics value of the skater (GameObjects::ReadTuningFloat, asserting).
double Physics(CGameEntity* skater, uint32_t name) { return ReadTuningFloat(name, 1, skater); }

// m = rotation * m (D3DXMatrixMultiply, linked library code).
void TurnBy(Maths::Mat4& m, const Maths::Mat4& rotation)
{
    D3dx::MatrixMultiply(&m, &rotation, &m);
}

// The C runtime's acos (0x00623630) as its x87 code computes it: atan2(sqrt((1 + x)(1 - x)), x).
double X87Acos(double x)
{
    double result;
    __asm {
        fld x
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fxch st(1)
        fpatan
        fstp result
    }
    return result;
}

} // namespace

// 0x005147F0
CObjectModule* __cdecl CRiderRemoteSync::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderRemoteSync));
    return memory ? static_cast<CRiderRemoteSync*>(memory)->MakeNew() : nullptr;
}

// 0x005144E0
CRiderRemoteSync* CRiderRemoteSync::MakeNew()
{
    CObjectModule::MakeNew();
    m_pastFrames = nullptr;
    m_curPhase = nullptr;
    m_runOver = nullptr;
    m_animBlender = nullptr;
    m_flipAndRotate = nullptr;
    m_meshSet = nullptr;
    m_shadow = nullptr;
    m_playIdx = 0;
    m_vtable = kVtable;
    m_kindTag = kSkaterNonLocalNetLogic;
    m_playPos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_lastPlayPos = {0.0f, 0.0f, 0.0f, 1.0f};
    m_updates = 0;
    m_lastEventTime = 0;
    return this;
}

// 0x005144B0
void CRiderRemoteSync::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00514550
CRiderRemoteSync* CRiderRemoteSync::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00514580
void CRiderRemoteSync::Seal()
{
    m_pastFrames = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterStateHistory));
    m_curPhase = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterState));
    m_runOver = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterEndRun));
    m_animBlender = m_entity->FetchModule(kAnimBlender);
    m_flipAndRotate = m_entity->FetchModule(kSkaterFlipAndRotate);
    m_meshSet = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kModel));
    m_shadow = m_entity->FetchModule(kShadow);
}

// 0x005144D0
void CRiderRemoteSync::ResetPlayback()
{
    m_syncFrame = 0;
    m_lastEventTime = 0;
}

// 0x00514610
void CRiderRemoteSync::UpdateGroundLighting()
{
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    const Maths::Vec4& pos = m_entity->m_atPoint;
    const Maths::Vec4& up = m_entity->m_basisFrame.row[1];
    Maths::Vec4& start = feeler.m_line.start;
    Maths::Vec4& end = feeler.m_line.end;
    start = pos;
    end = pos;
    // (from just above the skater: a thousandth of the up row, then 8 up; down 4800)
    start.x = static_cast<float>(static_cast<double>(up.x) * kLightingRise + start.x);
    start.y = static_cast<float>(static_cast<double>(up.y) * kLightingRise + start.y);
    start.z = static_cast<float>(static_cast<double>(up.z) * kLightingRise + start.z);
    start.w = static_cast<float>(static_cast<double>(up.w) * kLightingRise + start.w);
    start.y = static_cast<float>(static_cast<double>(start.y) + kLightingAbove);
    end.y = static_cast<float>(static_cast<double>(end.y) - kLightingBelow);
    if (!feeler.GetHitShape(true, false))
        return;
    if (Prop<int>(m_curPhase, 0x18) != kStateRail && feeler.IsHitTriangleCollision()) {
        // The model's light from the face's vertex values (the model's light object, its +0x2C - read through the
        // folded getter 0x00532EB0; else the global).
        void* model = Prop<void*>(m_meshSet, 0x208);
        void* light = Prop<void*>(model, 0x2C);
        if (light)
            static_cast<Engine3D::CModelLights*>(light)->MoveBrightness(static_cast<float>(feeler.GetHitVertexValue()),
                                                                  kLightTime);
        else
            Engine3D::SetWorldBrightness(static_cast<float>(feeler.GetHitVertexValue()));
    }
    // The shadow at the hit.
    static_cast<CGroundShadowModule*>(m_shadow)->ChangePlace(&feeler.m_contactPos);
    static_cast<CGroundShadowModule*>(m_shadow)->SetNormal(&feeler.m_surfaceDir);
}

// 0x00514850
void CRiderRemoteSync::SettleOntoFloor()
{
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    CGameEntity* skater = m_entity;
    const Maths::Vec4& up = skater->m_basisFrame.row[1];
    const Maths::Vec4& pos = skater->m_atPoint;
    // From Physics_Ground_Snap_Up above (the product unrounded) to 200 below (rounded first).
    double snapUp = Physics(skater, kGroundSnapUp);
    Maths::Vec4 start, end;
    start.x = static_cast<float>(snapUp * up.x + pos.x);
    start.y = static_cast<float>(snapUp * up.y + pos.y);
    start.z = static_cast<float>(snapUp * up.z + pos.z);
    start.w = static_cast<float>(snapUp * up.w + pos.w);
    float downW = static_cast<float>(static_cast<double>(up.w) * kSnapBelow);
    float downZ = static_cast<float>(static_cast<double>(up.z) * kSnapBelow);
    float downY = static_cast<float>(static_cast<double>(up.y) * kSnapBelow);
    float downX = static_cast<float>(static_cast<double>(up.x) * kSnapBelow);
    end.x = static_cast<float>(static_cast<double>(downX) + pos.x);
    end.y = static_cast<float>(static_cast<double>(downY) + pos.y);
    end.z = static_cast<float>(static_cast<double>(downZ) + pos.z);
    end.w = static_cast<float>(static_cast<double>(downW) + pos.w);
    feeler.ChangeStrand(&start, &end);
    feeler.SetExcluded(0x10, 0);
    if (!feeler.GetHitShape(true, false))
        return;

    const Maths::Vec4& point = feeler.m_contactPos;
    const Maths::Vec4& normal = feeler.m_surfaceDir;
    double dz = static_cast<double>(pos.z) - point.z;
    double dy = static_cast<double>(pos.y) - point.y;
    double dx = static_cast<double>(pos.x) - point.x;
    float distance = static_cast<float>(std::sqrt((dx * dx + dy * dy) + dz * dz));
    // How far above the hit the skater is, along its up row.
    float height = static_cast<float>((dy * up.y + dz * up.z) + dx * up.x);

    // (only its low byte is tested: skatable, vert, not "not skatable" and not wallride)
    uint8_t flags = static_cast<uint8_t>(feeler.m_hit.faceFlags);
    bool skatable = (flags & kFaceSkatable) && !(flags & kFaceNotSkatable) && (flags & kFaceVert) &&
                    !(flags & kFaceWallride);
    if (skatable &&
        !(Skater::X87Sin(static_cast<double>(Scripts::FetchFloat(kWallNonSkatableAngle)) * kRadiansPerDegree) >
          normal.y)) {
        // Ground: unless it turns the skater's heading too far from its last ground's, close enough to snap to.
        const Maths::Vec4 ground = normal;
        const Maths::Vec4& atRow = skater->m_basisFrame.row[2];
        Maths::Vec4 onGround = atRow;
        float facing = static_cast<float>((static_cast<double>(normal.x) * atRow.x + static_cast<double>(normal.z) * atRow.z) +
                                          static_cast<double>(atRow.y) * normal.y);
        Maths::Vec4 onLast = atRow;
        onGround.AlignToPlane(ground);
        onLast.AlignToPlane(m_surfaceUp);
        float turn = static_cast<float>((static_cast<double>(onLast.x) * onGround.x +
                                         static_cast<double>(onLast.z) * onGround.z) +
                                        static_cast<double>(onLast.y) * onGround.y);
        float stick = static_cast<float>(
            Skater::X87Cos(Physics(skater, kGroundStickAngleForward) * kRadiansPerDegree));
        if (facing > 0.0f && turn > 0.0f && turn < stick)
            return;
        if (height > 0.0f) {
            // Above it: only within this frame's move times the slope between the two grounds (at least
            // Physics_Ground_Snap_Down).
            float angle = static_cast<float>(X87Acos(turn));
            Maths::Vec4 move = skater->m_atPoint - m_lastPos;
            float slope = static_cast<float>(Skater::X87Tan(angle));
            // (its length unrounded, as Vec4::LenOf's x87 code leaves it)
            double length = std::sqrt((static_cast<double>(move.x) * move.x + static_cast<double>(move.y) * move.y) +
                                      static_cast<double>(move.z) * move.z);
            float reach = static_cast<float>(length * slope);
            double snapDown = Physics(skater, kGroundSnapDown);
            if (reach < snapDown)
                reach = static_cast<float>(snapDown);
            if (distance > reach)
                return;
        }
        m_surfaceUp = ground;
        skater->m_basisFrame.row[1] = ground;
        skater->m_basisFrame.SquareUpAxes(1);
        skater->m_atPoint = point;
        return;
    }
    // Not ground: if the skater is (nearly) at or below it, out by one along its normal.
    if (height < kOutOfGround) {
        skater->m_atPoint.x = static_cast<float>(static_cast<double>(normal.x) + point.x);
        skater->m_atPoint.y = static_cast<float>(static_cast<double>(point.y) + normal.y);
        skater->m_atPoint.z = static_cast<float>(static_cast<double>(normal.z) + point.z);
        skater->m_atPoint.w = static_cast<float>(static_cast<double>(point.w) + normal.w);
    }
}

// 0x00514D10
double CRiderRemoteSync::TurnTowards(Maths::Vec4 dir, float* turn)
{
    const Maths::Vec4& right = m_entity->m_basisFrame.row[0];
    float cosine = static_cast<float>((static_cast<double>(dir.z) * right.z + static_cast<double>(dir.y) * right.y) +
                                      static_cast<double>(dir.x) * right.x);
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    double exact = X87Acos(cosine);
    float angle = static_cast<float>(exact);
    if (exact > kHalfPi) // (the unrounded angle compared; the rounded one adjusted)
        angle = static_cast<float>(static_cast<double>(angle) - kPi);
    double rotation = static_cast<double>(Scripts::FetchFloat(kWallBounceAngleMultiplier)) * angle;
    *turn = static_cast<float>(rotation);
    // The velocity turned about y (sine / cosine unrounded), then the matrix by the rounded turn.
    double s = Skater::X87Sin(rotation), c = Skater::X87Cos(rotation);
    Maths::Vec4& vel = m_entity->m_vel;
    double vx = vel.x, vz = vel.z;
    vel.x = static_cast<float>(s * vz + c * vx);
    vel.z = static_cast<float>(-(vx * s) + c * vz);
    Maths::Mat4 rotate;
    Maths::MakeYawMatrix(rotate, *turn);
    TurnBy(m_entity->m_basisFrame, rotate);
    return angle;
}

// 0x00514E20
void CRiderRemoteSync::DeflectFromWall(const Maths::Vec4* normal, const Maths::Vec4* point)
{
    CGameEntity* skater = m_entity;
    float awayZ = static_cast<float>(static_cast<double>(skater->m_atPoint.z) - m_lastPos.z);
    float awayY = static_cast<float>(static_cast<double>(skater->m_atPoint.y) - m_lastPos.y);
    float awayX = static_cast<float>(static_cast<double>(skater->m_atPoint.x) - m_lastPos.x);
    Engine3D::CProbeRay feeler;
    feeler.MakeNew();
    // The collision height up the up row (the products rounded).
    double height = Physics(skater, kFirstForwardCollisionHeight);
    const Maths::Vec4& up = skater->m_basisFrame.row[1];
    float raiseW = static_cast<float>(height * up.w);
    float raiseZ = static_cast<float>(height * up.z);
    float raiseY = static_cast<float>(height * up.y);
    float raiseX = static_cast<float>(height * up.x);
    float turn;
    float angle = static_cast<float>(TurnTowards(*normal, &turn));
    // Slowed by how far past Wall_Bounce_Dont_Slow_Angle it turned (to nothing at a right angle).
    double dontSlow = static_cast<double>(Scripts::FetchFloat(kWallBounceDontSlowAngle)) * kRadiansPerDegree;
    double magnitude = std::fabs(static_cast<double>(angle));
    if (magnitude > dontSlow) {
        double keep = 1.0 - (magnitude - dontSlow) / (kHalfPi - dontSlow);
        Maths::Vec4& vel = skater->m_vel;
        vel.x = static_cast<float>(keep * vel.x);
        vel.y = static_cast<float>(keep * vel.y);
        vel.z = static_cast<float>(keep * vel.z);
        vel.w = static_cast<float>(keep * vel.w);
    }
    // Back at the wall's point, the height below it, 6 out along the normal.
    Maths::Vec4& pos = skater->m_atPoint;
    pos.x = static_cast<float>(static_cast<double>(point->x) - raiseX);
    pos.y = static_cast<float>(static_cast<double>(point->y) - raiseY);
    pos.z = static_cast<float>(static_cast<double>(point->z) - raiseZ);
    pos.w = static_cast<float>(static_cast<double>(point->w) - raiseW);
    pos.x = static_cast<float>(static_cast<double>(normal->x) * kWallOut + pos.x);
    pos.y = static_cast<float>(static_cast<double>(normal->y) * kWallOut + pos.y);
    pos.z = static_cast<float>(static_cast<double>(normal->z) * kWallOut + pos.z);
    pos.w = static_cast<float>(static_cast<double>(normal->w) * kWallOut + pos.w);

    // A feeler from twice the height up, along the velocity as far as the skater went this frame (then the
    // collision length further).
    double moved = std::sqrt((static_cast<double>(awayX) * awayX + static_cast<double>(awayY) * awayY) +
                             static_cast<double>(awayZ) * awayZ);
    Maths::Vec4 dir = skater->m_vel;
    double speed = std::sqrt((static_cast<double>(dir.z) * dir.z + static_cast<double>(dir.y) * dir.y) +
                             static_cast<double>(dir.x) * dir.x);
    if (speed > 0.0) {
        double inverse = 1.0 / speed;
        dir.x = static_cast<float>(dir.x * inverse);
        dir.y = static_cast<float>(dir.y * inverse);
        dir.z = static_cast<float>(dir.z * inverse);
    }
    float aheadW = static_cast<float>(dir.w * moved);
    float aheadZ = static_cast<float>(dir.z * moved);
    float aheadY = static_cast<float>(dir.y * moved);
    float aheadX = static_cast<float>(dir.x * moved);
    Maths::Vec4 from;
    from.x = static_cast<float>((static_cast<double>(raiseX) + pos.x) + raiseX);
    from.y = static_cast<float>((static_cast<double>(raiseY) + pos.y) + raiseY);
    from.z = static_cast<float>((static_cast<double>(raiseZ) + pos.z) + raiseZ);
    from.w = static_cast<float>((static_cast<double>(raiseW) + pos.w) + raiseW);
    double length = Physics(skater, kFirstForwardCollisionLength);
    float reachW = static_cast<float>(dir.w * length);
    float reachZ = static_cast<float>(dir.z * length);
    float reachY = static_cast<float>(dir.y * length);
    float reachX = static_cast<float>(length * dir.x);
    float baseY = static_cast<float>(static_cast<double>(raiseY) + pos.y);
    float baseX = static_cast<float>(static_cast<double>(raiseX) + pos.x);
    float midW = static_cast<float>((static_cast<double>(raiseW) + pos.w) + aheadW);
    float midZ = static_cast<float>((static_cast<double>(raiseZ) + pos.z) + aheadZ);
    float midY = static_cast<float>(static_cast<double>(baseY) + aheadY);
    float midX = static_cast<float>(static_cast<double>(baseX) + aheadX);
    Maths::Vec4 to;
    double toW = static_cast<double>(midW) + reachW;
    double toZ = static_cast<double>(midZ) + reachZ;
    to.y = static_cast<float>(static_cast<double>(midY) + reachY);
    to.x = static_cast<float>(static_cast<double>(midX) + reachX);
    to.z = static_cast<float>(toZ);
    to.w = static_cast<float>(toW);
    double ex = static_cast<double>(from.x) - to.x, ey = static_cast<double>(from.y) - to.y,
           ez = static_cast<double>(from.z) - toZ;
    if (std::sqrt((ex * ex + ey * ey) + ez * ez) > kBounceReach)
        return;
    feeler.ChangeStrand(&from, &to);
    feeler.SetExcluded(0x10, 0);
    if (!feeler.GetHitShape(true, false) || !skater->m_unknownBC)
        return;
    // Another wall: turned round by what's left of a half turn, and halved.
    double around = static_cast<double>(kPi) - turn;
    float s = static_cast<float>(Skater::X87Sin(around));
    float c = static_cast<float>(Skater::X87Cos(around));
    Maths::Vec4& vel = skater->m_vel;
    double vx = vel.x, vz = vel.z;
    double nx = vx * c + vz * s;
    double nz = -(vx * s) + vz * c;
    vel.x = static_cast<float>(nx);
    vel.z = static_cast<float>(nz);
    Maths::Mat4 rotate;
    Maths::MakeYawMatrix(rotate, static_cast<float>(around));
    TurnBy(skater->m_basisFrame, rotate);
    vel.x = static_cast<float>(vel.x * 0.5);
    vel.y = static_cast<float>(vel.y * 0.5);
    vel.z = static_cast<float>(vel.z * 0.5);
    vel.w = static_cast<float>(vel.w * 0.5);
}

// 0x00517270
void CRiderRemoteSync::TickFrame()
{
    auto* netManager = OnlinePlay::g_onlineHub;
    m_frameSeconds = static_cast<float>(FrameClock::GetFrameSeconds());
    int state = Prop<int>(m_curPhase, 0x18);
    bool wasLip = state == kStateLip;
    if (state != kStateAir) {
        // (on the ground the state's two vectors follow the up row)
        Prop<Maths::Vec4>(m_curPhase, 0xF8) = m_entity->m_basisFrame.row[1];
        Prop<Maths::Vec4>(m_curPhase, 0x108) = m_entity->m_basisFrame.row[1];
    }
    UpdateGroundLighting();
    m_lastPos = m_entity->m_atPoint;
    m_entity->m_oldPos = m_entity->m_atPoint;
    Interpolate();
    // Extrapolated when this machine plays (not observing or waiting) in a game that wants it.
    auto* player = static_cast<OnlinePlay::PlayerRecord*>(netManager->GetOwnPlayer());
    if (!player->IsObserver() && !player->IsScoutingLevel()) {
        void* gameMode = static_cast<void*>(GoalSystem::g_gameMode);
        bool extrapolate = OnlinePlay::g_onlineHub->PlayersCanCollide() ||
                           static_cast<GoalSystem::CMatchRules*>(gameMode)->GetNameKey() == kNetFirefight ||
                           static_cast<GoalSystem::CMatchRules*>(gameMode)->GetNameKey() == kFirefight;
        if (extrapolate && Prop<int>(m_pastFrames, 0x18) >= kHistory) {
            if (Prop<int>(m_curPhase, 0x18) == kStateRail) {
                ExtrapolateOnRail();
            } else {
                Extrapolate();
                SettleOntoFloor();
            }
        }
    }
    PlayHistoryEvents();

    // Out of a lip straight up (as the finalize physics component's).
    if (wasLip && Prop<int>(m_curPhase, 0x18) != kStateLip) {
        Prop<uint8_t>(m_curPhase, 0xF4) = 0;
        if (Prop<int>(m_curPhase, 0x18) == kStateAir && std::fabs(m_entity->m_vel.x) < 1.0f &&
            std::fabs(m_entity->m_vel.z) < 1.0f)
            Prop<uint8_t>(m_curPhase, 0xF4) = 1;
    }
    if (Prop<int>(m_curPhase, 0x18) != kStateAir)
        Prop<uint8_t>(m_curPhase, 0xF4) = 0;

    // Driving in or out: the scripts told (with the vehicle's control type when it starts).
    uint8_t driving = Prop<uint8_t>(m_curPhase, 0x118);
    uint32_t steerKind = Prop<uint32_t>(m_pastFrames, 0x1104);
    if ((driving && steerKind) || (!driving && m_wasDriving)) {
        Scripts::CStruct* params = Scripts::SpawnParamBag();
        if (params)
            params = params->PrepareAll();
        params->PutNameKey(kSkaterId, m_entity->m_ident);
        uint32_t script = kStopDrivingScript;
        if (driving) {
            params->PutNameKey(kControlType, steerKind);
            script = kStartDrivingScript;
        }
        Scripts::LaunchScript(script, params, nullptr, false, nullptr);
        if (params) {
            params->Clear();
            Scripts::DeleteStruct(params);
        }
    }
    m_wasDriving = Prop<uint8_t>(m_curPhase, 0x118);

    // The looping sound: on while skating, its speed and state.
    auto* sound = static_cast<CRiderRollingAudio*>(m_entity->FetchModule(kSkaterLoopingSound));
    if (Prop<int>(m_curPhase, 0x1C) != 0) {
        if (sound->m_on) {
            sound->VirtualSuspend(true);
            sound->m_on = 0;
        }
        return;
    }
    if (sound->m_on != 1) {
        sound->VirtualSuspend(false);
        sound->m_on = 1;
    }
    double vz = m_entity->m_vel.z, vx = m_entity->m_vel.x;
    sound->SetPaceRatio(static_cast<float>(std::sqrt(vx * vx + vz * vz) * kSpeedToSoundFraction));
    uint8_t changed = sound->m_soundChanged;
    changed |= sound->m_curPhase != Prop<int>(m_curPhase, 0x18) ? 1 : 0;
    sound->m_curPhase = Prop<int>(m_curPhase, 0x18);
    changed |= sound->m_surfaceType != Prop<uint8_t>(m_curPhase, 0xE1) ? 1 : 0;
    sound->m_soundChanged = changed;
    sound->m_surfaceType = Prop<uint8_t>(m_curPhase, 0xE1);
    sound->m_noRailSound = Prop<uint8_t>(m_curPhase, 0x70);
    changed |= sound->m_sliding != Prop<uint8_t>(m_curPhase, 0x58) ? 1 : 0;
    sound->m_soundChanged = changed;
    sound->m_sliding = Prop<uint8_t>(m_curPhase, 0x58);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderRemoteSync::Teardown,
         &GameObjects::CRiderRemoteSync::TickFrame,
         Stubs::IgnoreArgument,
         &GameObjects::CRiderRemoteSync::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
