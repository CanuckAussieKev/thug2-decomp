// The WalkCamera component (Obj::CWalkCameraComponent): see walk_camera.h. From the disassembly of
// 0x00524C20-0x00525E3F (and 0x00525F70).

#include "game/skater/walk_camera.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/mth/slerp.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/compositemgr.h"
#include "game/script/struct.h"
#include "game/skater/walk_physics.h"
#include "game/skater/x87_math.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

template <typename T>
T& At(void* object, uint32_t offset)
{
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

void** const kVtable = reinterpret_cast<void**>(0x0064C5E0);
constexpr uint32_t kWalkCamera = 0xC7D37EC2;
constexpr uint32_t kWalk = 0x726E85AA, kSkaterPhysicsControl = 0xD9151D4D, kCameraLookAround = 0x81C7122D,
                   kCamera = 0xC4E311FA, kSkaterCamera = 0x5E43A604, kInput = 0x27D7CD28;
constexpr uint32_t kSkaterId = 0x4C48DA58; // (an object id; the name isn't recovered)
constexpr uint32_t kFlushRequest = 0x73FEBD0F, kReset = 0xD1A485D1;
constexpr uint32_t kParameters = 0x1CDA02AE; // walk_camera_parameters
constexpr uint32_t kLockAngle = 0xB8DA8A73, kFullSlerpSpeed = 0xBCEF6DDA, kMinSlerpSpeed = 0x824349E5,
                   kDpadFullSlerpSpeed = 0x73DA1EC0, kDpadMinSlerpSpeed = 0x0DA5CAD3, kFacingControl = 0xF6F69DC8,
                   kMatrixSlerpRate = 0x0C39B639, kRunSlerpFactor = 0xD25348EB, kControlSlerpFactor = 0xB2DA3C87,
                   kFlushSlerpFactor = 0x7178E048;

constexpr float kDegrees = 0.01745329238474369f;   // 0x00645464
constexpr float kAligned = 0.9900000095367432f;    // 0x00645450
constexpr float kNearHeight = 3.0f;                // 0x006467A8
constexpr float kTiltOffset = 0.628000020980835f;  // 0x0064BB00
constexpr float kTiltScale = 1.5923566818237305f;  // 0x0064BAFC
constexpr float kTiltRange = 0.6000000238418579f;  // 0x006463C4
constexpr float kTiltBase = 0.4000000059604645f;   // 0x0064BAF8

// D3DXMatrixMultiply (linked later; 0x005BFA8C).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}

// The right stick's turn of the facing (0x00525468-): the sine rounded to float, the cosine kept at full
// precision: x' = z * sin + cos * x, z' = -(sin * x) + z * cos.
void TurnByStick(double angle, float* x, float* z)
{
    float sine;
    __asm {
        mov ecx, x
        mov edx, z
        fld angle
        fld st(0)
        fsin
        fstp sine
        fcos
        fld sine
        fmul dword ptr [ecx]
        fchs
        fld dword ptr [edx]
        fmul st, st(2)
        faddp st(1), st
        fld dword ptr [edx]
        fmul sine
        fxch st(2)
        fmul dword ptr [ecx]
        faddp st(2), st
        fxch st(1)
        fstp dword ptr [ecx]
        fstp dword ptr [edx]
    }
}

} // namespace

// 0x00524D70
Mth::Vector* __cdecl LerpByRange(Mth::Vector* out, const Mth::Vector* a, const Mth::Vector* b, float t, float low,
                                 float high)
{
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    float dz = b->z - a->z;
    float dw = b->w - a->w;
    float f = static_cast<float>((static_cast<double>(t) - low) / (static_cast<double>(high) - low));
    float sx = static_cast<float>(dx * f);
    double sy = dy * f;
    float sz = dz * f;
    float sw = dw * f;
    float x = sx + a->x;
    float y = static_cast<float>(sy + a->y);
    float z = sz + a->z;
    float w = sw + a->w;
    out->w = w;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}

// 0x00525DC0
CBaseComponent* __cdecl CWalkCameraComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CWalkCameraComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CWalkCameraComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kWalkCamera;
    component->m_target = {0.0f, 0.0f, 0.0f, 1.0f};
    return component;
}

// 0x00524C20
void CWalkCameraComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00524F20
CWalkCameraComponent* CWalkCameraComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00524C50
void CWalkCameraComponent::SetSkater(CCompositeObject* skater)
{
    if (!skater) {
        m_skater = nullptr;
        m_walk = nullptr;
        m_physicsControl = nullptr;
        return;
    }
    m_skater = skater;
    m_walk = static_cast<CWalkComponent*>(skater->GetComponent(kWalk));
    m_physicsControl = reinterpret_cast<uint8_t*>(m_skater->GetComponent(kSkaterPhysicsControl));
}

// 0x00524F50
void CWalkCameraComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t id = 0;
    params->GetChecksum(kSkaterId, &id, 1);
    SetSkater(static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(id)));
    m_turnCosine = 1.0f;
    m_zoom = 1.0f;
    m_orientation = m_object->m_matrix;
}

// 0x00524FB0
void CWalkCameraComponent::Finalize()
{
    m_lookAround = static_cast<CCameraLookAroundComponent*>(m_object->GetComponent(kCameraLookAround));
    m_camera = m_object->GetComponent(kCamera);
    m_skaterCamera = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCamera));
}

// 0x00524C30
void CWalkCameraComponent::ResetNow()
{
    m_reset = true;
    CBaseComponent::Update();
    m_reset = false;
}

// 0x005250A0
void CWalkCameraComponent::FlushRequest()
{
    m_flush = true;
    At<bool>(m_walk, 0x70C) = true;
    m_lookAround->m_heading = 0.0f;
    m_lookAround->m_tilt = 0.0f;
    m_lookAround->m_factor = 1.0f;
    m_lookAround->m_locked = false;
}

// 0x00525D40
int CWalkCameraComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/, Script::CScript* /*script*/)
{
    if (name == kFlushRequest) {
        FlushRequest();
        return 1;
    }
    if (name == kReset) {
        ResetNow();
        return 1;
    }
    return 2;
}

// 0x00525F70
void CWalkCameraComponent::SetOverrideFacing(const Mth::Vector* facing, float rate)
{
    m_override = true;
    m_overrideFacing = *facing;
    m_overrideRate = rate;
}

// 0x00525060
void CWalkCameraComponent::SaveState(WalkCameraState* state)
{
    state->orientation = m_orientation;
    state->target = m_target;
    state->turnCosine = m_turnCosine;
    state->zoom = m_zoom;
}

// 0x00524FF0
void CWalkCameraComponent::RestoreState(const WalkCameraState* state)
{
    m_target = state->target;
    m_orientation = state->orientation;
    m_turnCosine = state->turnCosine;
    m_zoom = state->zoom;
    m_flush = false;
    At<bool>(m_walk, 0x70C) = false;
    m_instantFrames = 0;
    m_lookAround->m_heading = 0.0f;
    m_lookAround->m_locked = false;
    m_override = false;
}

// 0x00524EF0
float __stdcall CWalkCameraComponent::Param(uint32_t name)
{
    float value = 0.0f; // (the original's is uninitialised: the value is asserted)
    Script::GetStructure(kParameters)->GetFloat(name, &value, 1);
    return value;
}

// 0x005250E0
void CWalkCameraComponent::GetTarget(Mth::Vector* out, bool instant)
{
    if (instant) {
        m_target = m_skater->m_pos;
    } else {
        float across = static_cast<float>(
            FrameBlend(At<float>(m_skaterCamera, 0xE4), static_cast<float>(Tmr::GetSlomoFrameSeconds())));
        float up = static_cast<float>(
            FrameBlend(At<float>(m_skaterCamera, 0xE8), static_cast<float>(Tmr::GetSlomoFrameSeconds())));
        const Mth::Vector& skater = m_skater->m_pos;
        float x = m_target.x, y = m_target.y, z = m_target.z;
        m_target.w = 1.0f;
        m_target.x = static_cast<float>((static_cast<double>(skater.x) - x) * across + x);
        m_target.y = static_cast<float>((static_cast<double>(skater.y) - y) * up + y);
        m_target.z = static_cast<float>((static_cast<double>(skater.z) - z) * across + z);
    }
    *out = m_target;
}

// 0x00524CB0
void CWalkCameraComponent::GetZoomedOffsets(float* height, float* distance)
{
    float factor = 1.0f;
    uint32_t factorBits;
    std::memcpy(&factorBits, &m_lookAround->m_factor, 4);
    if (m_lookAround->m_turning && factorBits != 0x3F800000)
        factor = m_lookAround->m_factor;
    double zoom = (static_cast<double>(factor) - m_zoom) * At<float>(m_skaterCamera, 0x110) + m_zoom;
    m_zoom = static_cast<float>(zoom);
    double behind = zoom * At<float>(m_skaterCamera, 0xC4);
    *distance = static_cast<float>(behind);
    float tilt = m_lookAround->m_tilt;
    if (tilt < 0.0f)
        *distance = static_cast<float>(
            (((static_cast<double>(tilt) + kTiltOffset) * kTiltScale) * kTiltRange + kTiltBase) * behind);
    float above = At<float>(m_skaterCamera, 0xD0);
    if (m_zoom < 1.0f)
        *height = static_cast<float>((static_cast<double>(above) - kNearHeight) * m_zoom + kNearHeight);
    else
        *height = above;
}

// 0x005251D0
void CWalkCameraComponent::Update()
{
    if (!m_skater)
        return;
    if (m_physicsControl && At<bool>(At<uint8_t*>(m_physicsControl, 0x2C), 0x118)) {
        m_object->m_compositeFlags |= COMPOSITE_FLAG_PAUSED;
        return;
    }
    if (m_skater->m_compositeFlags & COMPOSITE_FLAG_TELEPORTED)
        m_instantFrames = 3;
    bool instant;
    if (m_instantFrames > 0) {
        --m_instantFrames;
        instant = true;
    } else {
        instant = m_reset;
    }
    At<Gfx::Camera*>(m_camera, 0x18)->StoreOldPos();
    float frame = static_cast<float>(Tmr::GetSlomoFrameSeconds());
    float turn = At<float>(m_object->GetComponent(kInput), 0x230);
    m_object->m_matrix = m_orientation;

    // Where to look: along the camera's flat facing, swung round to the skater's.
    const Mth::Vector& at = m_object->m_matrix.row[2];
    Mth::Vector facing = {-at.x, 0.0f, -at.z, -at.w};
    facing.Normalize();
    const Mth::Vector& skaterAt = m_skater->m_matrix.row[2];
    Mth::Vector skaterFacing = {skaterAt.x, 0.0f, skaterAt.z, skaterAt.w};
    skaterFacing.Normalize();
    if (m_flush) {
        facing = skaterFacing;
    } else {
        bool swing = false;
        if (turn == 0.0f) {
            float cosine = static_cast<float>(static_cast<double>(skaterFacing.x) * facing.x +
                                              static_cast<double>(skaterFacing.z) * facing.z +
                                              static_cast<double>(skaterFacing.y) * facing.y);
            if (!X87::CosAbove(static_cast<double>(Param(kLockAngle)) * kDegrees, cosine)) {
                float full, least;
                if (!m_walk->UsingDpad()) {
                    full = Param(kFullSlerpSpeed);
                    least = Param(kMinSlerpSpeed);
                } else {
                    full = Param(kDpadFullSlerpSpeed);
                    least = Param(kDpadMinSlerpSpeed);
                }
                const Mth::Vector& velocity = m_skater->m_vel;
                double speed = X87::Sqrt(static_cast<double>(velocity.x) * velocity.x +
                                         static_cast<double>(velocity.z) * velocity.z);
                float speedF = static_cast<float>(speed);
                if (!(speed < full)) {
                    swing = true;
                } else if (speedF > least) {
                    Mth::Vector lerped;
                    facing = *LerpByRange(&lerped, &facing, &skaterFacing, speedF, least, full);
                    facing.Normalize();
                }
            }
        }
        if (swing || m_reset)
            facing = skaterFacing;
    }
    if (!(turn == 0.0f) && !m_flush)
        TurnByStick(static_cast<double>(Param(kFacingControl)) * turn, &facing.x, &facing.z);
    if (m_override) {
        float x = m_overrideFacing.x;
        facing.y = m_overrideFacing.y;
        facing.z = m_overrideFacing.z;
        facing.w = m_overrideFacing.w;
        double heading = m_lookAround->m_heading;
        float s = X87::SinF(heading);
        float c = X87::CosF(heading);
        float z = facing.z;
        float turnedZ = static_cast<float>(-(static_cast<double>(s) * x) + static_cast<double>(z) * c);
        facing.x = static_cast<float>(static_cast<double>(z) * s + static_cast<double>(c) * x);
        facing.z = turnedZ;
    }

    // The orientation that looks along it, tilted down; its right and at rows negated.
    Mth::Matrix look;
    look.row[0] = {facing.z, 0.0f, -facing.x, 1.0f};
    look.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    look.row[2] = facing;
    look.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Matrix tilt;
    Mth::CreateRotateXMatrix(tilt, m_lookAround->m_tilt + At<float>(m_skaterCamera, 0x12C));
    MatrixMultiply(&look, &tilt, &look);
    look.row[0] = {-look.row[0].x, -look.row[0].y, -look.row[0].z, -look.row[0].w};
    look.row[2] = {-look.row[2].x, -look.row[2].y, -look.row[2].z, -look.row[2].w};
    Mth::Vector skaterUp = m_skater->m_matrix.row[1];
    Mth::Matrix& matrix = m_object->m_matrix;
    if (instant) {
        matrix = look;
    } else {
        auto aligned = [](const Mth::Vector& a, const Mth::Vector& b) {
            return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x >
                   kAligned;
        };
        if (aligned(look.row[0], matrix.row[0]) && aligned(look.row[1], matrix.row[1]) &&
            aligned(look.row[2], matrix.row[2])) {
            if (m_flush) {
                m_flush = false;
                At<bool>(m_walk, 0x70C) = false;
            }
        } else {
            // Slerped toward it: at matrix_slerp_rate, faster running (and twice that hanging or climbing), with
            // the stick, flushing; an override's own rate.
            Mth::SlerpInterpolator slerp(matrix, look);
            float base;
            Script::GetStructure(kParameters)->GetFloat(kMatrixSlerpRate, &base, 1);
            float rate = base;
            if (At<bool>(m_walk, 0x17A4))
                rate = static_cast<float>(static_cast<double>(Param(kRunSlerpFactor)) * base);
            int state = At<int>(m_walk, 0x18);
            if (state == 4 || state == 3)
                rate = rate + rate;
            if (!(turn == 0.0f) && !m_flush) {
                float control = Param(kControlSlerpFactor);
                rate = static_cast<float>(static_cast<double>(control) * std::fabs(turn) * rate);
            }
            if (m_flush) {
                At<bool>(m_walk, 0x70C) = true;
                rate = static_cast<float>(static_cast<double>(Param(kFlushSlerpFactor)) * rate);
            }
            if (m_override) {
                if (m_flush) {
                    float flush = Param(kFlushSlerpFactor);
                    rate = static_cast<float>(static_cast<double>(Param(kMatrixSlerpRate)) * flush);
                } else {
                    rate = m_overrideRate;
                }
            }
            slerp.GetMatrix(matrix, static_cast<float>(FrameBlend(rate, frame)));
            const Mth::Vector& now = matrix.row[2];
            const Mth::Vector& before = m_orientation.row[2];
            m_turnCosine = static_cast<float>(static_cast<double>(now.z) * before.z +
                                              static_cast<double>(now.y) * before.y +
                                              static_cast<double>(now.x) * before.x);
        }
    }
    m_orientation = matrix;

    // Where to be: behind and above the point followed, looking at a point above the skater.
    Mth::Vector target;
    GetTarget(&target, instant);
    float height, distance;
    GetZoomedOffsets(&height, &distance);
    double upX = static_cast<double>(skaterUp.x) * height;
    double upY = static_cast<double>(skaterUp.y) * height;
    double upZ = static_cast<double>(skaterUp.z) * height;
    double upW = static_cast<double>(skaterUp.w) * height;
    float upXF = static_cast<float>(upX), upZF = static_cast<float>(upZ), upWF = static_cast<float>(upW);
    const Mth::Vector& lookAt = matrix.row[2];
    double backX = static_cast<double>(distance) * lookAt.x;
    double backY = static_cast<double>(distance) * lookAt.y;
    float backZ = distance * lookAt.z;
    float backW = distance * lookAt.w;
    float stepX = static_cast<float>(backX + upXF);
    float stepY = static_cast<float>(backY + upY);
    double stepZ = backZ + upZ;
    double stepW = static_cast<double>(backW) + upWF;
    target.x = stepX + target.x;
    target.y = target.y + stepY;
    target.z = static_cast<float>(target.z + stepZ);
    target.w = static_cast<float>(target.w + stepW);
    const Mth::Vector& skaterPos = m_skater->m_pos;
    Mth::Vector focus = {upXF + skaterPos.x, static_cast<float>(upY + skaterPos.y), upZF + skaterPos.z,
                         upWF + skaterPos.w};

    Mth::Matrix aim;
    Mth::Vector& aimAt = aim.row[2];
    aimAt = {focus.x - target.x, focus.y - target.y, focus.z - target.z, focus.w - target.w};
    aimAt.Normalize();
    const Mth::Vector up = matrix.row[1];
    Mth::Vector& right = aim.row[0];
    right = {static_cast<float>(static_cast<double>(up.y) * aimAt.z - static_cast<double>(up.z) * aimAt.y),
             static_cast<float>(static_cast<double>(up.z) * aimAt.x - static_cast<double>(up.x) * aimAt.z),
             static_cast<float>(static_cast<double>(up.x) * aimAt.y - static_cast<double>(up.y) * aimAt.x), 0.0f};
    right.Normalize();
    Mth::Vector& aimUp = aim.row[1];
    aimUp = {static_cast<float>(static_cast<double>(right.z) * aimAt.y - static_cast<double>(right.y) * aimAt.z),
             static_cast<float>(static_cast<double>(right.x) * aimAt.z - static_cast<double>(right.z) * aimAt.x),
             static_cast<float>(static_cast<double>(right.y) * aimAt.x - static_cast<double>(right.x) * aimAt.y), 0.0f};
    aimUp.Normalize();
    matrix.row[0] = {-right.x, -right.y, -right.z, -right.w};
    matrix.row[1] = aimUp;
    matrix.row[2] = {-aimAt.x, -aimAt.y, -aimAt.z, -aimAt.w};
    matrix.row[0].w = 0.0f;
    matrix.row[1].w = 0.0f;
    matrix.row[2].w = 0.0f;
    matrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};

    // The collision check pulls the camera toward the point `distance` ahead of it (and the walk's offset).
    double aheadX = static_cast<double>(distance) * matrix.row[2].x;
    double aheadY = static_cast<double>(distance) * matrix.row[2].y;
    float aheadZ = distance * matrix.row[2].z;
    float aheadW = distance * matrix.row[2].w;
    Mth::Vector clear = {static_cast<float>(target.x - aheadX), static_cast<float>(target.y - aheadY),
                         target.z - aheadZ, target.w - aheadW};
    Mth::Vector offset;
    m_walk->GetCameraOffset(&offset);
    Mth::Vector pull = {clear.x + offset.x, clear.y + offset.y, clear.z + offset.z, clear.w + offset.w};
    AvoidCameraCollisions(&target, &matrix, &pull, &focus, true, true);
    target.w = 1.0f;
    m_object->m_pos = target;
    if (instant)
        At<Gfx::Camera*>(m_camera, 0x18)->StoreOldPos();
    At<Mth::Matrix>(m_skaterCamera, 0x74) = matrix;
}

} // namespace Obj

REPLACE(0x00525DC0, Obj::CWalkCameraComponent::Create);
REPLACE(0x00524C20, &Obj::CWalkCameraComponent::DestroyBody);
REPLACE(0x00524F20, &Obj::CWalkCameraComponent::Destroy);
REPLACE(0x00524C50, &Obj::CWalkCameraComponent::SetSkater);
REPLACE(0x00524F50, &Obj::CWalkCameraComponent::InitFromStructure);
REPLACE(0x00524FB0, &Obj::CWalkCameraComponent::Finalize);
REPLACE(0x00524C30, &Obj::CWalkCameraComponent::ResetNow);
REPLACE(0x005250A0, &Obj::CWalkCameraComponent::FlushRequest);
REPLACE(0x00525D40, &Obj::CWalkCameraComponent::CallMemberFunction);
REPLACE(0x00525F70, &Obj::CWalkCameraComponent::SetOverrideFacing);
REPLACE(0x00525060, &Obj::CWalkCameraComponent::SaveState);
REPLACE(0x00524FF0, &Obj::CWalkCameraComponent::RestoreState);
REPLACE(0x00524EF0, Obj::CWalkCameraComponent::Param);
REPLACE(0x005250E0, &Obj::CWalkCameraComponent::GetTarget);
REPLACE(0x00524CB0, &Obj::CWalkCameraComponent::GetZoomedOffsets);
REPLACE(0x00524D70, Obj::LerpByRange);
REPLACE(0x005251D0, &Obj::CWalkCameraComponent::Update);
