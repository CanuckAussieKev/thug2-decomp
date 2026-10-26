// The skater's matrix queries component (Obj::CSkaterMatrixQueriesComponent): see matrix_queries.h.

#include "game/skater/matrix_queries.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/math.h"
#include "game/mth/vector.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skater_x87.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BF70);
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
const Mth::Vector& CoreVector78(uint8_t* corePhysics) { return *reinterpret_cast<Mth::Vector*>(corePhysics + 0x78); }

// The unnamed float parameter (0 without one; asserted).
float AngleParameter(Script::CStruct* params)
{
    float angle = 0.0f;
    params->GetFloat(uint32_t{0}, &angle, 1);
    return angle;
}
// The cosine of `degrees`, as the original's x87.
double CosDegrees(float degrees) { return Skater::X87Cos(static_cast<double>(degrees) * kDegreesToRadians); }

} // namespace

// 0x00514430
CBaseComponent* __cdecl CSkaterMatrixQueriesComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterMatrixQueriesComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterMatrixQueriesComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterMatrixQueries;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x00513FD0
void CSkaterMatrixQueriesComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00513FE0
CSkaterMatrixQueriesComponent* CSkaterMatrixQueriesComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00514010
void CSkaterMatrixQueriesComponent::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
}

// 0x00514030
void CSkaterMatrixQueriesComponent::Update() { m_matrix = *reinterpret_cast<Mth::Matrix*>(m_corePhysics + 0x18); }

// 0x00514040
int CSkaterMatrixQueriesComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    const Mth::Vector& right = m_matrix.row[0];
    const Mth::Vector& up = m_matrix.row[1];
    const Mth::Vector& at = m_matrix.row[2];
    switch (name) {
    case kGetHeading: {
        const Mth::Vector& facing = m_object->m_matrix.row[2];
        float heading = static_cast<float>(static_cast<double>(Mth::FastACos(facing.x)) * kRadiansToDegrees);
        if (facing.z < 0.0f)
            heading = kFullCircle - heading;
        script->m_params->AddFloat(kHeading, heading);
        script->m_params->AddFloat(kCosine, m_object->m_matrix.row[2].x);
        script->m_params->AddFloat(kSine, m_object->m_matrix.row[2].z);
        return 1;
    }
    case kAbsolutePitchGreaterThan: {
        float angle = AngleParameter(params);
        return CosDegrees(angle) > up.y ? 1 : 0;
    }
    case kGetSlope: {
        // The at row's angle from its own flattened copy (negative when it points down).
        const Mth::Vector& facing = m_object->m_matrix.row[2];
        Mth::Vector flat = facing;
        flat.y = 0.0f;
        float slope = static_cast<float>(Mth::GetAngle(facing, flat));
        if (m_object->m_matrix.row[2].y < 0.0f)
            slope = -slope;
        script->m_params->AddFloat(kSlope, slope);
        script->m_params->AddFloat(kChangeInSlope, slope - m_lastSlope);
        m_lastSlope = slope;
        return 1;
    }
    case kPitchGreaterThan: {
        float angle = AngleParameter(params);
        const Mth::Vector& v = CoreVector78(m_corePhysics);
        double dot = static_cast<double>(up.z) * v.z + static_cast<double>(up.y) * v.y + static_cast<double>(v.x) * up.x;
        return CosDegrees(angle) > dot ? 1 : 0;
    }
    case kYawingLeft: {
        Mth::Vector v = m_object->m_vel;
        v.RotateToPlane(up);
        return static_cast<double>(v.z) * at.x - static_cast<double>(v.x) * at.z > 0.0 ? 1 : 0;
    }
    case kYawingRight: {
        Mth::Vector v = m_object->m_vel;
        v.RotateToPlane(up);
        return Mth::CrossProduct(v, at).y < 0.0f ? 1 : 0;
    }
    case kRollGreaterThan: {
        float angle = AngleParameter(params);
        Mth::Vector v = right;
        v.RotateToPlane(CoreVector78(m_corePhysics));
        return Mth::IsAngleBeyond(v, right, angle) ? 1 : 0;
    }
    case kYawBetween: {
        Script::CPair range;
        range.Init();
        params->GetPair(uint32_t{0}, &range, true);
        Mth::Vector v = m_object->m_vel;
        v.RotateToPlane(up);
        if (!Mth::IsAngleBeyond(v, at, range.x))
            return 0;
        return Mth::IsAngleBeyond(v, at, range.y) ? 0 : 1;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x00514430, Obj::CSkaterMatrixQueriesComponent::Create);
REPLACE(0x00513FD0, &Obj::CSkaterMatrixQueriesComponent::DestroyBody);
REPLACE(0x00513FE0, &Obj::CSkaterMatrixQueriesComponent::Destroy);
REPLACE(0x00514010, &Obj::CSkaterMatrixQueriesComponent::Finalize);
REPLACE(0x00514030, &Obj::CSkaterMatrixQueriesComponent::Update);
REPLACE(0x00514040, &Obj::CSkaterMatrixQueriesComponent::CallMemberFunction);
