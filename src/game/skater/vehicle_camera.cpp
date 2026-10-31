// The VehicleCamera component (GameObjects::CCarView): see vehicle_camera.h. From the disassembly of
// 0x00524420-0x00524C1E (0x00524420 isn't a Ghidra function).

#include "game/skater/vehicle_camera.h"
#include "game/gfx/d3dx.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/view_cam.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/camera_util.h"
#include "game/obj/compositemgr.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/mth/x87_math.h"
#include "game/sys/stopwatch.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"

namespace GameObjects {

uint32_t g_vehicleCameraMemberFunctions[12]{
    0x000469FD, 0x2C747D8A, 0, 0, 0, 0,
    0, 0x2C747D8A, 0, 0, 0, 0};

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
Gfx2D::ViewCam* CameraOf(CGameEntity* object)
{
    return *reinterpret_cast<Gfx2D::ViewCam**>(reinterpret_cast<uint8_t*>(object->FetchModule(kCamera)) + 0x18);
}

// D3DXMatrixMultiply (linked later; 0x005BFA8C).
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The camera position the collision check pulls toward: `distance` along the orientation's at-row before
// `position` (x with the product unrounded, as the original).
Maths::Vec4 Behind(const Maths::Vec4& position, float distance, const Maths::Mat4& orientation)
{
    const Maths::Vec4& at = orientation.row[2];
    double x = static_cast<double>(distance) * at.x;
    float y = distance * at.y;
    float z = distance * at.z;
    float w = distance * at.w;
    return {static_cast<float>(position.x - x), position.y - y, position.z - z, position.w - w};
}

} // namespace

// 0x005248C0
CObjectModule* __cdecl CCarView::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CCarView));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CCarView*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kVehicleCamera;
    component->m_levelY = 60.0f;
    component->m_reach = 144.0f;
    component->m_lerp = 3.0f;
    return component;
}

// 0x00524420
void __cdecl CCarView::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kVehicleCamera),
                                        g_vehicleCameraMemberFunctions, 0x18);
}

// 0x00524440
void CCarView::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00524470
CCarView* CCarView::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x005244A0
void CCarView::ConfigureFromData(Scripts::CStruct* params)
{
    if (params->HasEntryNamed(kSubject)) {
        uint32_t id;
        params->ReadHashValue(kSubject, &id, 1);
        m_subject = static_cast<CGameEntity*>(CompositeObjectManager()->LookUpEntity(id));
        m_vehicle = reinterpret_cast<uint8_t*>(m_subject->FetchModule(kVehicle));
    }
    params->FetchFloat(kLerp, &m_lerp, 0);
    params->FetchFloat(kHeight, &m_levelY, 0);
    params->FetchFloat(kDistance, &m_reach, 0);
    params->FetchFloat(kAngle, &m_angle, 0);
}

// 0x00524C00
void CCarView::Seal()
{
    m_lookAround = static_cast<CFreeLookModule*>(m_entity->FetchModule(kCameraLookAround));
    Reset();
}

// 0x00524450
bool CCarView::VehicleCamera_Reset(Scripts::CStruct* params, Scripts::CScript* /*script*/)
{
    reinterpret_cast<void(__thiscall*)(CObjectModule*, Scripts::CStruct*)>(m_vtable[5])(this, params);
    reinterpret_cast<void(__thiscall*)(CObjectModule*)>(m_vtable[3])(this);
    return true;
}

// 0x00524550
void CCarView::UpdateTargetDirection()
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
    const Maths::Vec4& at = m_subject->m_basisFrame.row[2];
    double a = -at.x, b = -at.y, c = -at.z;
    float along = static_cast<float>((c * z + b * y + a * x) * scale);
    m_targetDirection = {-at.x, -at.y, -at.z, -at.w};
    m_targetDirection.y = 0.0f;
    m_targetDirection.MakeUnit();
    if (along > 0.0f) {
        m_targetDirection.x = static_cast<float>(static_cast<double>(x) * along + m_targetDirection.x);
        m_targetDirection.y = static_cast<float>(static_cast<double>(y) * along + m_targetDirection.y);
        m_targetDirection.z = z * along + m_targetDirection.z;
        m_targetDirection.w = w * along + m_targetDirection.w;
        m_targetDirection.MakeUnit();
    }
}

// 0x00524720
void CCarView::ApplyToCamera()
{
    m_anchorPoint.w = 1.0f;
    m_orientation.row[0].w = 0.0f;
    m_orientation.row[1].w = 0.0f;
    m_orientation.row[2].w = 0.0f;
    m_entity->m_atPoint = m_anchorPoint;
    m_entity->m_basisFrame = m_orientation;
    m_entity->m_displayMatrix = m_orientation;
}

// 0x00524780
void CCarView::UpdateOrientation()
{
    double heading = m_lookAround->m_heading;
    double rotatedX, rotatedZ;
    X87::SpinBy(heading, m_direction.x, m_direction.z, &rotatedX, &rotatedZ);
    float right = static_cast<float>(rotatedZ);
    float forward = static_cast<float>(rotatedX);
    m_orientation.row[0] = {right, 0.0f, -forward, 1.0f};
    m_orientation.row[1] = {0.0f, 1.0f, 0.0f, 1.0f};
    m_orientation.row[2] = {forward, m_direction.y, right, m_direction.w};
    m_orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    const Maths::Vec4& subject = m_subject->m_atPoint;
    float back = right * m_reach;
    m_anchorPoint.x = static_cast<float>(static_cast<double>(forward) * m_reach + subject.x);
    m_anchorPoint.y = m_levelY + subject.y;
    m_anchorPoint.z = back + subject.z;
    m_anchorPoint.w = subject.w + 1.0f;
    Maths::Mat4 tilt;
    Maths::MakeRollMatrix(tilt, m_angle * kDegrees);
    MatrixMultiply(&m_orientation, &tilt, &m_orientation);
}

// 0x00524B40
void CCarView::Reset()
{
    m_lookAround->m_heading = 0.0f;
    m_lookAround->m_leanAngle = 0.0f;
    m_lookAround->m_locked = false;
    const Maths::Vec4& at = m_subject->m_basisFrame.row[2];
    m_targetDirection = {-at.x, -at.y, -at.z, -at.w};
    m_targetDirection.y = 0.0f;
    m_targetDirection.MakeUnit();
    UpdateTargetDirection();
    m_direction = m_targetDirection;
    UpdateOrientation();
    ApplyToCamera();
    m_entity->FetchModule(kCamera)->TickFrame();
}

// 0x00524940
void CCarView::TickFrame()
{
    CameraOf(m_entity)->RememberPrevPos();
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
    double step = FrameClock::GetSlomoFrameSeconds() * m_lerp * angle;
    if (step > angle)
        step = angle;
    double turn = (left ? 1.0f : -1.0f) * step;
    double x, z;
    X87::SpinBy(turn, m_direction.x, m_direction.z, &x, &z);
    m_direction.x = static_cast<float>(x);
    m_direction.z = static_cast<float>(z);
    m_direction.MakeUnit();
    UpdateOrientation();
    Maths::Vec4 focus = Behind(m_anchorPoint, m_reach, m_orientation);
    Maths::Vec4 target = Behind(m_anchorPoint, m_reach, m_orientation);
    AvoidCameraCollisions(&m_anchorPoint, &m_orientation, &target, &focus, false, false);
    ApplyToCamera();
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CCarView::Teardown,
         &GameObjects::CCarView::TickFrame,
         &GameObjects::CCarView::ConfigureFromData,
         &GameObjects::CCarView::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
