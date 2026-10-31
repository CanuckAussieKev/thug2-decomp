// The skater's matrix queries component (GameObjects::CRiderFrameLookups): see matrix_queries.h.

#include "game/skater/matrix_queries.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/math.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skater_x87.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterMatrixQueries = 0xBF518E27, kSkaterCorePhysics = 0x5BD63B29;

// Script member functions
constexpr uint32_t kGetHeading = 0x8E7833BE, kAbsolutePitchGreaterThan = 0x5E269B2B, kGetSlope = 0x97201739,
                   kPitchGreaterThan = 0xA0551543, kYawingLeft = 0xA745C080, kYawingRight = 0xC8C4D2F4,
                   kRollGreaterThan = 0xD3313E92, kYawBetween = 0xB992F3CC;
// Their results
constexpr uint32_t kHeading = 0xFD4BC03E, kCosine = 0x2315EF17, kSine = 0x26910CC0, kSlope = 0xA733BA7A,
                   kChangeInSlope = 0x21AFFF16;

constexpr float kRadiansToDegrees = 57.2957763671875f; // 0x00645468
constexpr float kDegreesToRadians = 0.01745329238474369f; // 0x00645464
constexpr float kFullCircle = 360.0f;                  // 0x00645EA8

// The core physics' vector at +0x78 (the plane PitchGreaterThan and RollGreaterThan measure against).
const Maths::Vector& CoreVector78(uint8_t* corePhysics) { return *reinterpret_cast<Maths::Vector*>(corePhysics + 0x78); }

// The unnamed float parameter (0 without one; asserted).
float AngleParameter(Scripts::CStruct* params)
{
    float angle = 0.0f;
    params->GetFloat(uint32_t{0}, &angle, 1);
    return angle;
}
// The cosine of `degrees`, as the original's x87.
double CosDegrees(float degrees) { return Skater::X87Cos(static_cast<double>(degrees) * kDegreesToRadians); }

} // namespace

// 0x00514430
CObjectModule* __cdecl CRiderFrameLookups::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderFrameLookups));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderFrameLookups*>(memory);
    component->CObjectModule::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterMatrixQueries;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x00513FD0
void CRiderFrameLookups::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00513FE0
CRiderFrameLookups* CRiderFrameLookups::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x00514010
void CRiderFrameLookups::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
}

// 0x00514030
void CRiderFrameLookups::Update() { m_basisFrame = *reinterpret_cast<Maths::Matrix*>(m_corePhysics + 0x18); }

// 0x00514040
int CRiderFrameLookups::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    const Maths::Vector& right = m_basisFrame.row[0];
    const Maths::Vector& up = m_basisFrame.row[1];
    const Maths::Vector& at = m_basisFrame.row[2];
    switch (name) {
    case kGetHeading: {
        const Maths::Vector& facing = m_object->m_basisFrame.row[2];
        float heading = static_cast<float>(static_cast<double>(Maths::FastACos(facing.x)) * kRadiansToDegrees);
        if (facing.z < 0.0f)
            heading = kFullCircle - heading;
        script->m_params->PushFloat(kHeading, heading);
        script->m_params->PushFloat(kCosine, m_object->m_basisFrame.row[2].x);
        script->m_params->PushFloat(kSine, m_object->m_basisFrame.row[2].z);
        return 1;
    }
    case kAbsolutePitchGreaterThan: {
        float angle = AngleParameter(params);
        return CosDegrees(angle) > up.y ? 1 : 0;
    }
    case kGetSlope: {
        // The at row's angle from its own flattened copy (negative when it points down).
        const Maths::Vector& facing = m_object->m_basisFrame.row[2];
        Maths::Vector flat = facing;
        flat.y = 0.0f;
        float slope = static_cast<float>(Maths::ReadTurnAngle(facing, flat));
        if (m_object->m_basisFrame.row[2].y < 0.0f)
            slope = -slope;
        script->m_params->PushFloat(kSlope, slope);
        script->m_params->PushFloat(kChangeInSlope, slope - m_lastSlope);
        m_lastSlope = slope;
        return 1;
    }
    case kPitchGreaterThan: {
        float angle = AngleParameter(params);
        const Maths::Vector& v = CoreVector78(m_corePhysics);
        double dot = static_cast<double>(up.z) * v.z + static_cast<double>(up.y) * v.y + static_cast<double>(v.x) * up.x;
        return CosDegrees(angle) > dot ? 1 : 0;
    }
    case kYawingLeft: {
        Maths::Vector v = m_object->m_vel;
        v.AlignToPlane(up);
        return static_cast<double>(v.z) * at.x - static_cast<double>(v.x) * at.z > 0.0 ? 1 : 0;
    }
    case kYawingRight: {
        Maths::Vector v = m_object->m_vel;
        v.AlignToPlane(up);
        return Maths::CrossProduct(v, at).y < 0.0f ? 1 : 0;
    }
    case kRollGreaterThan: {
        float angle = AngleParameter(params);
        Maths::Vector v = right;
        v.AlignToPlane(CoreVector78(m_corePhysics));
        return Maths::IsAngleBeyond(v, right, angle) ? 1 : 0;
    }
    case kYawBetween: {
        Scripts::CPair range;
        range.Init();
        params->FetchPair(uint32_t{0}, &range, true);
        Maths::Vector v = m_object->m_vel;
        v.AlignToPlane(up);
        if (!Maths::IsAngleBeyond(v, at, range.x))
            return 0;
        return Maths::IsAngleBeyond(v, at, range.y) ? 0 : 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderFrameLookups::Destroy,
         &GameObjects::CRiderFrameLookups::Update,
         Stubs::IgnoreArgument,
         &GameObjects::CRiderFrameLookups::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderFrameLookups::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
