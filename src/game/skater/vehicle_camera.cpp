// The VehicleCamera component (Obj::CVehicleCameraComponent): see vehicle_camera.h. From the disassembly of
// 0x00524420-0x00524C1E (0x00524420 isn't a Ghidra function).

#include "game/skater/vehicle_camera.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/camera.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/compositemgr.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/mth/x87_math.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "hook/game_global.h"

namespace Obj {

GAME_GLOBAL(uint32_t[12], g_vehicleCameraMemberFunctions, 0x00684150,
    0x000469FD, 0x2C747D8A, 0, 0, 0, 0,
    0, 0x2C747D8A, 0, 0, 0, 0);

namespace {

extern void** const kVtable;
constexpr uint32_t kVehicleCamera = 0x2C747D8A;
constexpr uint32_t kVehicle = 0xE47F1B79;
constexpr uint32_t kCamera = 0xC4E311FA;
constexpr uint32_t kCameraLookAround = 0x81C7122D;
constexpr uint32_t kSubject = 0x0431C185, kLerp = 0x9213625F, kHeight = 0x14849B6D, kDistance = 0xBD3D3CA9,
                   kAngle = 0xFF7EBAF6;

constexpr float kSpeedScale = 0.0037878789007663727f; // 0x0064C5D4
constexpr float kMinFlatSpeed = 88000000.0f;          // 0x0064C5D8
constexpr float kPi = 3.1415927410125732f;            // 0x0064C59C
constexpr float kHalfPi = 1.5707963705062866f;        // 0x00645484
constexpr float kDegrees = 0.01745329238474369f;      // 0x00645464 (degrees to radians)

// Its Camera component's camera (+0x18).
Gfx::Camera* CameraOf(CCompositeObject* object)
{
    return *reinterpret_cast<Gfx::Camera**>(reinterpret_cast<uint8_t*>(object->GetComponent(kCamera)) + 0x18);
}

// D3DXMatrixMultiply (linked later; 0x005BFA8C).
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(void*, const void*, const void*)>(0x005BFA8C)(out, a, b);
}

// The camera position the collision check pulls toward: `distance` along the orientation's at-row before
// `position` (x with the product unrounded, as the original).
Mth::Vector Behind(const Mth::Vector& position, float distance, const Mth::Matrix& orientation)
{
    const Mth::Vector& at = orientation.row[2];
    double x = static_cast<double>(distance) * at.x;
    float y = distance * at.y;
    float z = distance * at.z;
    float w = distance * at.w;
    return {static_cast<float>(position.x - x), position.y - y, position.z - z, position.w - w};
}

} // namespace

// 0x005248C0
CBaseComponent* __cdecl CVehicleCameraComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CVehicleCameraComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CVehicleCameraComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kVehicleCamera;
    component->m_height = 60.0f;
    component->m_distance = 144.0f;
    component->m_lerp = 3.0f;
    return component;
}

// 0x00524420
void __cdecl CVehicleCameraComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kVehicleCamera),
                                        g_vehicleCameraMemberFunctions, 0x18);
}

// 0x00524440
void CVehicleCameraComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00524470
CVehicleCameraComponent* CVehicleCameraComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x005244A0
void CVehicleCameraComponent::InitFromStructure(Script::CStruct* params)
{
    if (params->ContainsComponentNamed(kSubject)) {
        uint32_t id;
        params->GetChecksum(kSubject, &id, 1);
        m_subject = static_cast<CCompositeObject*>(CompositeObjectManager()->GetObjectByID(id));
        m_vehicle = reinterpret_cast<uint8_t*>(m_subject->GetComponent(kVehicle));
    }
    params->GetFloat(kLerp, &m_lerp, 0);
    params->GetFloat(kHeight, &m_height, 0);
    params->GetFloat(kDistance, &m_distance, 0);
    params->GetFloat(kAngle, &m_angle, 0);
}

// 0x00524C00
void CVehicleCameraComponent::Finalize()
{
    m_lookAround = static_cast<CCameraLookAroundComponent*>(m_object->GetComponent(kCameraLookAround));
    Reset();
}

// 0x00524450
bool CVehicleCameraComponent::VehicleCamera_Reset(Script::CStruct* params, Script::CScript* /*script*/)
{
    reinterpret_cast<void(__thiscall*)(CBaseComponent*, Script::CStruct*)>(m_vtable[5])(this, params);
    reinterpret_cast<void(__thiscall*)(CBaseComponent*)>(m_vtable[3])(this);
    return true;
}

// 0x00524550
void CVehicleCameraComponent::UpdateTargetDirection()
{
    const float* velocity = reinterpret_cast<const float*>(m_vehicle + 0xA4);
    float x = -velocity[0];
    float y = 0.0f;
    float z = -velocity[2];
    float w = -velocity[3];
    double length = X87::Sqrt(static_cast<double>(z) * z + static_cast<double>(x) * x);
    float speed = static_cast<float>(length);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(0.0f * inverse);
        z = static_cast<float>(z * inverse);
    }
    if (*reinterpret_cast<int*>(m_vehicle + 0x114) < 2) {
        if (!(speed < kMinFlatSpeed))
            m_targetDirection = {x, y, z, w};
        return;
    }
    double scale = static_cast<double>(speed) * kSpeedScale;
    if (scale > 1.0)
        scale = 1.0;
    const Mth::Vector& at = m_subject->m_matrix.row[2];
    double a = -at.x, b = -at.y, c = -at.z;
    float along = static_cast<float>((c * z + b * y + a * x) * scale);
    m_targetDirection = {-at.x, -at.y, -at.z, -at.w};
    m_targetDirection.y = 0.0f;
    m_targetDirection.Normalize();
    if (along > 0.0f) {
        m_targetDirection.x = static_cast<float>(static_cast<double>(x) * along + m_targetDirection.x);
        m_targetDirection.y = static_cast<float>(static_cast<double>(y) * along + m_targetDirection.y);
        m_targetDirection.z = z * along + m_targetDirection.z;
        m_targetDirection.w = w * along + m_targetDirection.w;
        m_targetDirection.Normalize();
    }
}

// 0x00524720
void CVehicleCameraComponent::ApplyToCamera()
{
    m_position.w = 1.0f;
    m_orientation.row[0].w = 0.0f;
    m_orientation.row[1].w = 0.0f;
    m_orientation.row[2].w = 0.0f;
    m_object->m_pos = m_position;
    m_object->m_matrix = m_orientation;
    m_object->m_displayMatrix = m_orientation;
}

// 0x00524780
void CVehicleCameraComponent::UpdateOrientation()
{
    double heading = m_lookAround->m_heading;
    double rotatedX, rotatedZ;
    X87::Rotate(heading, m_direction.x, m_direction.z, &rotatedX, &rotatedZ);
    float right = static_cast<float>(rotatedZ);
    float forward = static_cast<float>(rotatedX);
    m_orientation.row[0] = {right, 0.0f, -forward, 1.0f};
    m_orientation.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    m_orientation.row[2] = {forward, m_direction.y, right, m_direction.w};
    m_orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    const Mth::Vector& subject = m_subject->m_pos;
    float back = right * m_distance;
    m_position.x = static_cast<float>(static_cast<double>(forward) * m_distance + subject.x);
    m_position.y = m_height + subject.y;
    m_position.z = back + subject.z;
    m_position.w = subject.w + 1.0f;
    Mth::Matrix tilt;
    Mth::CreateRotateZMatrix(tilt, m_angle * kDegrees);
    MatrixMultiply(&m_orientation, &tilt, &m_orientation);
}

// 0x00524B40
void CVehicleCameraComponent::Reset()
{
    m_lookAround->m_heading = 0.0f;
    m_lookAround->m_tilt = 0.0f;
    m_lookAround->m_locked = false;
    const Mth::Vector& at = m_subject->m_matrix.row[2];
    m_targetDirection = {-at.x, -at.y, -at.z, -at.w};
    m_targetDirection.y = 0.0f;
    m_targetDirection.Normalize();
    UpdateTargetDirection();
    m_direction = m_targetDirection;
    UpdateOrientation();
    ApplyToCamera();
    m_object->GetComponent(kCamera)->Update();
}

// 0x00524940
void CVehicleCameraComponent::Update()
{
    CameraOf(m_object)->StoreOldPos();
    UpdateTargetDirection();
    double dot = static_cast<double>(m_direction.z) * m_targetDirection.z +
                 static_cast<double>(m_direction.y) * m_targetDirection.y +
                 static_cast<double>(m_direction.x) * m_targetDirection.x;
    float cosine = static_cast<float>(dot);
    if (dot > 1.0)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    bool obtuse;
    float angle = X87::AcosF(cosine, kHalfPi, &obtuse);
    if (obtuse)
        angle = kPi - angle;
    bool left = static_cast<double>(m_direction.z) * m_targetDirection.x -
                    static_cast<double>(m_targetDirection.z) * m_direction.x >
                0.0;
    double step = Tmr::GetSlomoFrameSeconds() * m_lerp * angle;
    if (step > angle)
        step = angle;
    double turn = (left ? 1.0f : -1.0f) * step;
    double x, z;
    X87::Rotate(turn, m_direction.x, m_direction.z, &x, &z);
    m_direction.x = static_cast<float>(x);
    m_direction.z = static_cast<float>(z);
    m_direction.Normalize();
    UpdateOrientation();
    Mth::Vector focus = Behind(m_position, m_distance, m_orientation);
    Mth::Vector target = Behind(m_position, m_distance, m_orientation);
    AvoidCameraCollisions(&m_position, &m_orientation, &target, &focus, false, false);
    ApplyToCamera();
}

} // namespace Obj

REPLACE(0x005248C0, Obj::CVehicleCameraComponent::Create);
REPLACE(0x00524420, Obj::CVehicleCameraComponent::RegisterMemberFunctions);
REPLACE(0x00524440, &Obj::CVehicleCameraComponent::DestroyBody);
REPLACE(0x00524470, &Obj::CVehicleCameraComponent::Destroy);
REPLACE(0x005244A0, &Obj::CVehicleCameraComponent::InitFromStructure);
REPLACE(0x00524C00, &Obj::CVehicleCameraComponent::Finalize);
REPLACE(0x00524450, &Obj::CVehicleCameraComponent::VehicleCamera_Reset);
REPLACE(0x00524550, &Obj::CVehicleCameraComponent::UpdateTargetDirection);
REPLACE(0x00524720, &Obj::CVehicleCameraComponent::ApplyToCamera);
REPLACE(0x00524780, &Obj::CVehicleCameraComponent::UpdateOrientation);
REPLACE(0x00524B40, &Obj::CVehicleCameraComponent::Reset);
REPLACE(0x00524940, &Obj::CVehicleCameraComponent::Update);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x0064C5A0,
    &Obj::CVehicleCameraComponent::Destroy,
    &Obj::CVehicleCameraComponent::Update,
    &Obj::CVehicleCameraComponent::InitFromStructure,
    &Obj::CVehicleCameraComponent::Finalize,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    &Obj::CBaseComponent::RunMemberFunction,
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
