// The skater core physics component: the gravity, a vert target to land on, the speed override, wall push-offs,
// turning round when going backwards, flip spins and stepping up (0x004FEE10-0x00500012).
//
// Float arithmetic follows the original's x87 code at 53-bit precision: intermediate results are doubles, rounded
// to float only where the original stores them.

#include <cmath>
#include <cstdint>

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/mth/geometry.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/coll_cache.h"
#include "game/nx/feeler.h"
#include "game/obj/anim_blender.h"
#include "game/obj/control_pad.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skate/career.h"
#include "game/skater/core_physics.h"
#include "game/skater/core_physics_fields.h"
#include "game/skater/skater.h"
#include "game/skater/trick_component.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Skater {

using namespace Physics;

namespace {

// Physics values, stats, script values and scripts (names not recovered).
constexpr uint32_t kOverrideStat = 0x2EACDDB3;
constexpr uint32_t kOverrideMaxFactor = 0xC6B38BE0;
constexpr uint32_t kOverrideRunDown = 0xF9B006AA;
constexpr uint32_t kGravity = 0xFAA40754;
constexpr uint32_t kGravityScale = 0xC31CA696;
constexpr uint32_t kGravityScaleVert = 0x441C38A0;
constexpr uint32_t kMoonCheat = 0x9C8C6DF1;
constexpr uint32_t kMoonGravity = 0x0EC128F0;
constexpr uint32_t kFallScale = 0xBE33A41E;
constexpr uint32_t kPushOffWait = 0x0017D543;
constexpr uint32_t kPushOffAngle = 0x1483FD01;
constexpr uint32_t kPushOffEvent = 0x4C03635B;
constexpr uint32_t kPushOffSlowing = 0x1112FB1C;
constexpr uint32_t kPushOffSpeed = 0xB78542C2;
constexpr uint32_t kBackwardsSpeed = 0x2BF71EEB;
constexpr uint32_t kTurnRoundScript = 0x3A8D6AB2;
constexpr uint32_t kNoTurnRoundScript = 0xB1C84B5A;
constexpr uint32_t kBackwardsScript = 0xA496AEBD;
constexpr uint32_t kJumpReleaseEvent = 0x8FFEFB28;
constexpr uint32_t kFlipSpins = 0xB75D73E5;
constexpr uint32_t kSpinButton1 = 0x24715AF5;
constexpr uint32_t kSpinButton2 = 0xBD780B4F;
constexpr uint32_t kSpinX = 0x0AFFD09D;
constexpr uint32_t kSpinY = 0x7DF8E00B;
constexpr uint32_t kSpinZ = 0xE4F1B1B1;
constexpr uint32_t kSpinScript = 0xD8FA65D7;
constexpr uint32_t kStepUpHeight = 0x786B3272;
constexpr uint32_t kTrickComponent = 0x270F56E1;      // Trick
constexpr uint32_t kModelComponent = 0x286A8D26;      // Model

// The balance tricks and boards for which going backwards isn't turned round (their checksums).
constexpr uint32_t kBalanceTrickA = 0xEF24413B;
constexpr uint32_t kBalanceTrickB = 0x0AC90769;
constexpr uint32_t kBoards[] = {0xEF24413B, 0x89B7DBE4, 0xA84EF276, 0x2FD8E618, 0xEFF59585, 0xC692E824};

// The game's constants.
constexpr float kTargetFirst = 10.0f;        // 0x00646710
constexpr float kTargetLast = 500.0f;        // 0x0064BC80
constexpr float kTargetStep = 6.0f;          // 0x00646284
constexpr float kTargetDrop = 4000.0f;       // 0x0064B8F8
constexpr float kTargetLift = 400.0f;        // 0x00646288
constexpr float kTargetVertY = 0.707f;       // 0x006467BC
constexpr float kTargetFacing = 0.95f;       // 0x0064BC7C
constexpr float kTargetOpposite = -0.866f;   // 0x00646960
constexpr float kOverrideStart = 1.25f;      // 0x00649AC4
constexpr float kOverrideAir = 1.1f;         // 0x0064A300
constexpr uint32_t kNoLimit = 0x60AD78EC;    // (float bits: about 1e20)
constexpr float kBarelyMoving = 0.00001f;    // 0x006453B8
constexpr float kDegrees = 0.01745329238474369f; // 0x00645464 (pi / 180)
constexpr float kPushOffStill = 0.001f;      // 0x006461D8
constexpr float kStepUpGround = 0.5f;        // 0x006453BC
constexpr float kStepUpLift = 0.1f;          // 0x00645D74
constexpr float kFlipSpinTime = 50.0f;

// The component's fields used here.
constexpr uint32_t kHitNormalField = 0x130;
constexpr uint32_t kOverrideOn = 0x255;
constexpr uint32_t kModelRotating = 0x2DB;
constexpr uint32_t kModelFlipped = 0x2DC;
constexpr uint32_t kModelRotationLeft = 0x2E0;
constexpr uint32_t kModelRotationFlag = 0x2E4;
constexpr uint32_t kOnVehicle = 0x2ED;
constexpr uint32_t kBackwardsScriptRun = 0x304;
constexpr uint32_t kBraking = 0x324;
constexpr uint32_t kLimitsTime = 0x350;
constexpr uint32_t kLimitsMax = 0x354;
constexpr uint32_t kLimitsMaxMax = 0x358;
constexpr uint32_t kJumpHeld = 0x378;
constexpr uint32_t kRailTricksOff = 0x3A4;
constexpr uint32_t kOverrideFactor = 0x3AC;
constexpr uint32_t kPushOffStart = 0x4E0;
constexpr uint32_t kLastPushOff = 0x4E4;
constexpr uint32_t kBalanceTrick = 0x1524;           // (+0x18 the trick)

// The state component's flags.
constexpr uint32_t kFlagJump = 0x20;
constexpr uint32_t kFlagVertAir = 0x30;
constexpr uint32_t kFlagBail = 0x70;
constexpr uint32_t kFlagAcidDrop = 0x78;
constexpr uint32_t kFlagSkitching = 0x88;
constexpr uint32_t kFlagCancelWallpush = 0xB8;

// The pad's buttons (offsets into the input component).
constexpr uint32_t kFlipButton1 = 0x160;
constexpr uint32_t kFlipButton2 = 0x180;
constexpr uint32_t kPushOffButton = 0x1A0;
constexpr uint32_t kJumpButton = 0x1C0;

double Seconds(uint32_t milliseconds) { return static_cast<double>(milliseconds); }
Obj::CCompositeObject* Composite(void* object) { return static_cast<Obj::CCompositeObject*>(object); }

// 0x00487AD0 (cdecl): the least of three.
float LeastOf(float a, float b, float c) { return GameFunction<float(__cdecl*)(float, float, float)>(0x00487AD0)(a, b, c); }

} // namespace

// 0x004FEE10
bool CorePhysicsComponent::FindVertTarget(const Mth::Vector* direction, const Mth::Vector* facing, bool* notOpposite,
                                           Mth::Vector* point, Mth::Vector* normal, bool* objectAlive)
{
    Nx::CFeeler feeler;
    feeler.Construct();
    Nx::CCollCache cache;
    cache.Construct();
    Mth::BBox box;
    // The box: from 10 out to 500 out, 4000 down.
    for (float out : {kTargetFirst, kTargetLast}) {
        const Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
        double w = static_cast<double>(direction->w) * out;
        float z = static_cast<float>(direction->z * static_cast<double>(out));
        float y = static_cast<float>(direction->y * static_cast<double>(out));
        float x = static_cast<float>(direction->x * static_cast<double>(out));
        Mth::Vector corner(x + position.x, y + position.y, z + position.z, static_cast<float>(w + position.w));
        box.AddPoint(corner);
        corner.y = static_cast<float>(corner.y - static_cast<double>(kTargetDrop));
        box.AddPoint(corner);
    }
    cache.Update(box);
    feeler.mp_cache = &cache;

    float distance = kTargetFirst;
    double along = 0.0;
    bool found = false;
    do {
        const Mth::Vector& position = VectorAt(ObjectOf(this), kPosition);
        double w = static_cast<double>(distance) * direction->w;
        float z = static_cast<float>(static_cast<double>(distance) * direction->z);
        float y = static_cast<float>(static_cast<double>(distance) * direction->y);
        float x = static_cast<float>(static_cast<double>(distance) * direction->x);
        float atY = y + position.y;
        Mth::Vector at(x + position.x, atY, z + position.z, static_cast<float>(w + position.w));
        feeler.m_line.start = at;
        feeler.m_line.end = at;
        feeler.m_line.end.y = static_cast<float>(atY - static_cast<double>(kTargetDrop));
        feeler.m_line.start.y = static_cast<float>(atY + static_cast<double>(kTargetLift));
        if (feeler.GetCollision(true, false) && (feeler.m_hit.faceFlags & 8) &&
            !(std::fabs(static_cast<double>(feeler.m_normal.y)) >= kTargetVertY ||
              feeler.m_normal.y != feeler.m_normal.y)) {
            Mth::Vector flat(feeler.m_normal.x, 0.0f, feeler.m_normal.z, feeler.m_normal.w);
            flat.Normalize();
            along = (static_cast<double>(flat.z) * facing->z + static_cast<double>(flat.y) * facing->y) +
                    static_cast<double>(flat.x) * facing->x;
            if (along <= kTargetFacing) {
                found = true;
                break;
            }
        }
        double next = static_cast<double>(distance) + kTargetStep;
        distance = static_cast<float>(next);
        if (!(next < kTargetLast))
            break;
    } while (true);
    if (!found) {
        cache.Destruct();
        return false;
    }
    *point = feeler.m_point;
    *normal = feeler.m_normal;
    *objectAlive = feeler.HitObjectAlive();
    *notOpposite = static_cast<float>(along) > kTargetOpposite;
    cache.Destruct();
    return true;
}

// 0x004FF200
void CorePhysicsComponent::UpdateSpeedOverride()
{
    void* object = ObjectOf(this);
    float& factor = At<float>(this, kOverrideFactor);
    if (At<uint8_t>(this, kOverrideOn) && !FlagsOf(this)[kFlagAcidDrop] && !FlagsOf(this)[kFlagBail]) {
        float stat = static_cast<float>(StatValue(object, kOverrideStat));
        double speed = LengthUnrounded(VectorAt(ObjectOf(this), kVelocity));
        float speedRounded = static_cast<float>(speed);
        if (speed < static_cast<double>(stat) * kOverrideStart)
            return;
        float most = PhysicsFloat(kOverrideMaxFactor, object);
        double over = static_cast<double>(speedRounded) / stat;
        factor = over < most ? static_cast<float>(over) : most;
        At<uint32_t>(this, kLimitsMax) = kNoLimit;
        At<uint32_t>(this, kLimitsMaxMax) = kNoLimit;
    } else if (At<uint32_t>(this, kOverrideFactor) == 0x3F800000u) { // (1: off)
        return;
    }
    At<float>(this, kLimitsTime) = -1.0f;
    double left = factor - static_cast<double>(PhysicsFloat(kOverrideRunDown, object)) * At<float>(this, kFrameLength);
    factor = static_cast<float>(left);
    if (left >= 1.0 || left != left) {
        float stat = static_cast<float>(StatValue(ObjectOf(this), kOverrideStat));
        float max = static_cast<float>(static_cast<double>(stat) * factor);
        object = ObjectOf(this);
        float limit;
        if (At<int>(FlagsOf(this), kState) != 1) {
            float speed = static_cast<float>(LengthUnrounded(VectorAt(object, kVelocity)) * kOverrideAir);
            limit = LeastOf(max, speed, At<float>(this, kLimitsMax));
        } else {
            float current = At<float>(this, kLimitsMax);
            limit = max < current ? max : current;
        }
        if (limit >= stat || limit != limit) {
            At<float>(this, kLimitsMax) = limit;
            float inverse = static_cast<float>(1.0 / stat);
            float share = static_cast<float>(static_cast<double>(StatValue(object, kOverrideStat)) * inverse *
                                             At<float>(this, kLimitsMax));
            int& counter = GameVariable<int>(0x007CCDCC);
            counter = (counter + 1) % 10;
            At<float>(this, kLimitsMaxMax) = share;
            // (Every tenth time: a line printed, compiled out - an inlined loop that runs once.)
            while (counter == 0) {
                Dbg::Printf(reinterpret_cast<const char*>(0x0064BC88),
                            static_cast<double>(inverse) * At<float>(this, kLimitsMax));
                counter = (counter + 1) % 10;
            }
            return;
        }
    }
    factor = 1.0f;
    if (At<uint32_t>(this, kLimitsTime) == 0xC0000000u) // (never: it was just set to -1)
        return;
    At<float>(this, kLimitsTime) = 0.0f;
}

// 0x004FF470
double CorePhysicsComponent::GetGravity()
{
    void* object = ObjectOf(this);
    uint8_t* flags = FlagsOf(this);
    float gravity = PhysicsFloat(kGravity, object);
    bool vert = flags[kFlagVertAir] || flags[kFlagAcidDrop];
    float result = static_cast<float>(gravity / static_cast<double>(Script::GetFloat(vert ? kGravityScaleVert : kGravityScale)));
    auto* career = At<Obj::CSkaterCareer*>(GameVariable<uint8_t*>(0x007CE478), 0x20);
    if (career->GetCheat(kMoonCheat)) {
        career->SetGlobalFlag(Script::GetInteger(kMoonCheat));
        result = static_cast<float>(static_cast<double>(PhysicsFloat(kMoonGravity, object)) * result);
        GameVariable<uint8_t>(0x007CE4FD) = 1;
    }
    float scale = Script::GetFloat(kFallScale);
    if (scale != 0.0f && !(VectorAt(ObjectOf(this), kVelocity).y > 0.0f) &&
        VectorAt(ObjectOf(this), kVelocity).y == VectorAt(ObjectOf(this), kVelocity).y)
        return static_cast<double>(scale) * result;
    return result;
}

// 0x004FF590
void CorePhysicsComponent::AlongFacing(Mth::Vector* velocity)
{
    double length = LengthUnrounded(*velocity);
    if (!(length > kBarelyMoving))
        return;
    double inverse = 1.0 / length;
    velocity->x = static_cast<float>(inverse * velocity->x);
    velocity->y = static_cast<float>(inverse * velocity->y);
    velocity->z = static_cast<float>(inverse * velocity->z);
    velocity->w = static_cast<float>(inverse * velocity->w);
    const Mth::Vector& facing = VectorAt(ObjectOf(this), kForward);
    double along = (static_cast<double>(facing.y) * velocity->y + static_cast<double>(facing.z) * velocity->z) +
                   static_cast<double>(facing.x) * velocity->x;
    double scale = (along < 0.0 ? -1.0 : 1.0) * length;
    *velocity = facing;
    velocity->x = static_cast<float>(scale * velocity->x);
    velocity->y = static_cast<float>(scale * velocity->y);
    velocity->z = static_cast<float>(scale * velocity->z);
    velocity->w = static_cast<float>(scale * velocity->w);
}

// 0x004FF670
bool CorePhysicsComponent::WallPushOff()
{
    if (!InputOf(this)[kPushOffButton])
        return false;
    float sinceLast = static_cast<float>(Seconds(Since(At<uint32_t>(this, kLastPushOff))));
    if (Script::GetFloat(kPushOffWait) > sinceLast)
        return false;
    double angle = X87Sin((Script::GetFloat(kPushOffAngle) - 1.0) * kDegrees);
    const Mth::Vector& facing = VectorAt(ObjectOf(this), kForward);
    const Mth::Vector& wall = VectorAt(this, kHitNormalField);
    double into = (static_cast<double>(facing.z) * wall.z + static_cast<double>(facing.y) * wall.y) +
                  static_cast<double>(facing.x) * wall.x;
    if (-angle <= into)
        return false;
    float sinceStart = static_cast<float>(Seconds(Since(At<uint32_t>(this, kPushOffStart))));
    if (Script::GetFloat(kPushOffWait) > sinceStart)
        return false;
    static_cast<Obj::CObject*>(ObjectOf(this))->SelfEvent(kPushOffEvent, nullptr);
    if (FlagsOf(this)[kFlagCancelWallpush]) {
        SetTimedFlag(this, kFlagCancelWallpush, 0);
        return false;
    }
    Mth::Vector& velocity = VectorAt(ObjectOf(this), kVelocity);
    float bounce = Mth::DotProduct(velocity, wall);
    velocity -= 2.0f * (bounce * wall);
    double speedUnrounded = LengthUnrounded(VectorAt(ObjectOf(this), kVelocity));
    float speed = static_cast<float>(speedUnrounded);
    Mth::Vector& v = VectorAt(ObjectOf(this), kVelocity);
    if (speedUnrounded > kPushOffStill) {
        float slower = static_cast<float>(speed - static_cast<double>(Script::GetFloat(kPushOffSlowing)));
        float least = Script::GetFloat(kPushOffSpeed);
        v *= static_cast<float>(Obj::LargerOf(least, slower) / static_cast<double>(speed));
    } else {
        v = -Script::GetFloat(kPushOffSpeed) * VectorAt(ObjectOf(this), kForward);
    }
    VectorAt(ObjectOf(this), kVelocity).RotateToPlane(VectorAt(this, kGroundNormal));
    void* object = ObjectOf(this);
    VectorAt(object, kForward) = VectorAt(object, kVelocity);
    VectorAt(ObjectOf(this), kForward).Normalize();
    VectorAt(ObjectOf(this), kUp) = VectorAt(this, kGroundNormal);
    object = ObjectOf(this);
    VectorAt(object, kMatrix) = Mth::CrossProduct(VectorAt(object, kUp), VectorAt(object, kForward));
    At<Mth::Matrix>(this, kDisplayMatrix) = At<Mth::Matrix>(ObjectOf(this), kMatrix);
    At<int>(this, kLastPushOff) = Now();
    return true;
}

// 0x004FF920
void CorePhysicsComponent::TurnRoundIfBackwards()
{
    uint8_t* flags = FlagsOf(this);
    void* object = ObjectOf(this);
    if (flags[kFlagBail] || flags[kFlagSkitching] || At<uint8_t>(this, kBraking))
        return;
    const Mth::Vector& v = VectorAt(object, kVelocity);
    const Mth::Vector& f = VectorAt(object, kForward);
    double along = (static_cast<double>(v.z) * f.z + static_cast<double>(v.y) * f.y) + static_cast<double>(v.x) * f.x;
    if (!(along < 0.0))
        return;
    double least = PhysicsFloat(kBackwardsSpeed, object);
    if (!(least * least < LengthSqrUnrounded(v)))
        return;
    bool special = At<uint8_t>(this, kOnVehicle);
    if (!special) {
        uint32_t trick = At<uint32_t>(At<void*>(this, kBalanceTrick), 0x18);
        special = trick == kBalanceTrickA || trick == kBalanceTrickB;
    }
    if (special) {
        uint32_t board = Crc::GenerateCRCFromString(At<char*>(this, kSkaterComponent) + 0x1331);
        bool plain = false;
        for (uint32_t b : kBoards)
            plain = plain || board == b;
        if (!plain) {
            if (At<uint8_t>(this, kBackwardsScriptRun))
                return;
            At<uint8_t>(this, kBackwardsScriptRun) = 1;
            Script::RunScript(kBackwardsScript, nullptr, static_cast<Obj::CObject*>(ObjectOf(this)), false, nullptr);
            return;
        }
    }
    Mth::Vector& facing = VectorAt(ObjectOf(this), kForward);
    facing = -facing;
    Mth::Vector& right = VectorAt(ObjectOf(this), kMatrix);
    right = -right;
    Mth::Vector& displayFacing = VectorAt(this, kDisplayMatrix + 0x20);
    displayFacing = -displayFacing;
    Mth::Vector& displayRight = VectorAt(this, kDisplayMatrix);
    displayRight = -displayRight;
    if (!Script::GetInteger(kNoTurnRoundScript))
        Script::RunScript(kTurnRoundScript, nullptr, static_cast<Obj::CObject*>(ObjectOf(this)), false, nullptr);
}

// 0x004FFB10
bool CorePhysicsComponent::JumpReleased()
{
    uint8_t* flags = FlagsOf(this);
    uint8_t* button = InputOf(this) + kJumpButton;
    if (!flags[kFlagJump] || *button)
        return false;
    At<uint32_t>(this, kJumpHeld) = Since(At<uint32_t>(flags, kFlagJump + 4));
    SetTimedFlag(this, kFlagJump, 0);
    reinterpret_cast<Obj::CControlButton*>(button)->ClearRelease();
    static_cast<Obj::CObject*>(ObjectOf(this))->SelfEvent(kJumpReleaseEvent, nullptr);
    return true;
}

// 0x004FFB90
void CorePhysicsComponent::CheckFlipSpin()
{
    if (FlagsOf(this)[kFlagBail])
        return;
    uint8_t* input = InputOf(this);
    if (At<uint8_t>(this, kModelRotating))
        return;
    if (!input[kFlipButton1] && !input[kFlipButton2])
        return;
    At<uint8_t>(this, kModelFlipped) = 0;
    auto* trick = static_cast<Obj::CTrickComponent*>(Composite(ObjectOf(this))->GetComponent(kTrickComponent));
    Script::CArray* spins = Script::GetArray(kFlipSpins);
    int count = static_cast<int>(spins->m_size);
    Script::CStruct* spin = nullptr;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        spin = reinterpret_cast<Script::CStruct*>(spins->GetElement(static_cast<uint32_t>(i)));
        uint32_t sequence[3]; // (an entry without the two buttons leaves them unset, as the original does)
        spin->GetChecksum(kSpinButton1, &sequence[0], 0);
        spin->GetChecksum(kSpinButton2, &sequence[1], 0);
        sequence[2] = input[kFlipButton1] ? 0x2B489A86u : 0x321C9756u;
        if (trick->CheckSpinSequence(sequence, At<uint8_t>(this, kModelRotationFlag) != 0)) {
            found = true;
            break;
        }
    }
    if (!found)
        return;
    At<uint8_t>(this, kModelRotating) = 1;
    At<float>(this, kModelRotationLeft) = kFlipSpinTime;
    At<uint8_t>(this, kRailTricksOff) = 1;
    At<uint8_t>(this, kModelRotationFlag) = 1;
    auto* model = static_cast<Obj::CModelComponent*>(Composite(ObjectOf(this))->GetComponent(kModelComponent));
    int x = 0, y = 0, z = 0;
    if (spin->GetInteger(kSpinX, &x, 0) && x)
        model->RotateDisplay(At<float>(this, kModelRotationLeft), 0.0f, static_cast<float>(x), 0, true, true, false, false);
    if (spin->GetInteger(kSpinY, &y, 0) && y) {
        if (y == 180 || y == -180)
            At<uint8_t>(this, kModelFlipped) = 1;
        model->RotateDisplay(At<float>(this, kModelRotationLeft), 0.0f, static_cast<float>(y), 0, true, false, true, false);
    }
    if (spin->GetInteger(kSpinZ, &z, 0) && z)
        model->RotateDisplay(At<float>(this, kModelRotationLeft), 0.0f, static_cast<float>(z), 0, true, false, false, true);
    Script::RunScript(kSpinScript, spin, static_cast<Obj::CObject*>(ObjectOf(this)), false, nullptr);
}

// 0x004FFDB0
bool CorePhysicsComponent::StepUp(const Mth::Vector* point)
{
    if (FlagsOf(this)[kFlagBail])
        return false;
    Nx::CFeeler feeler;
    feeler.Construct();
    float height = PhysicsFloat(kStepUpHeight, ObjectOf(this));
    void* object = ObjectOf(this);
    const Mth::Vector& position = VectorAt(object, kPosition);
    // Down from above the higher of the skater and the point to the lower.
    feeler.m_line.start = position;
    feeler.m_line.start.y = static_cast<float>(static_cast<double>(position.y) + height);
    feeler.m_line.end = position;
    if (point->y < position.y)
        feeler.m_line.end.y = point->y;
    double above = static_cast<double>(height) + point->y;
    if (above > feeler.m_line.start.y)
        feeler.m_line.start.y = static_cast<float>(above);
    if (!feeler.GetCollision(true, false))
        return false;
    if (!(feeler.m_normal.y > kStepUpGround))
        return false;
    // Across from above the point to above the hit (the normal added): nothing in the way.
    float w = static_cast<float>(static_cast<double>(feeler.m_normal.w) + feeler.m_point.w);
    float z = static_cast<float>(static_cast<double>(feeler.m_normal.z) + feeler.m_point.z);
    float y = static_cast<float>(static_cast<double>(feeler.m_point.y) + feeler.m_normal.y);
    float x = static_cast<float>(static_cast<double>(feeler.m_normal.x) + feeler.m_point.x);
    feeler.m_line.start = Mth::Vector(point->x, static_cast<float>(static_cast<double>(point->y) + height), point->z,
                                      point->w);
    feeler.m_line.end = Mth::Vector(x, static_cast<float>(static_cast<double>(y) + height), z, w);
    if (feeler.GetCollision(true, false))
        return false;
    Mth::Vector& moved = VectorAt(ObjectOf(this), kPosition);
    moved = Mth::Vector(x, y, z, w);
    moved.y = static_cast<float>(static_cast<double>(moved.y) + kStepUpLift);
    return true;
}

} // namespace Skater

using Skater::CorePhysicsComponent;
REPLACE(0x004FEE10, &CorePhysicsComponent::FindVertTarget);
REPLACE(0x004FF200, &CorePhysicsComponent::UpdateSpeedOverride);
REPLACE(0x004FF470, &CorePhysicsComponent::GetGravity);
REPLACE(0x004FF590, &CorePhysicsComponent::AlongFacing);
REPLACE(0x004FF670, &CorePhysicsComponent::WallPushOff);
REPLACE(0x004FF920, &CorePhysicsComponent::TurnRoundIfBackwards);
REPLACE(0x004FFB10, &CorePhysicsComponent::JumpReleased);
REPLACE(0x004FFB90, &CorePhysicsComponent::CheckFlipSpin);
REPLACE(0x004FFDB0, &CorePhysicsComponent::StepUp);
