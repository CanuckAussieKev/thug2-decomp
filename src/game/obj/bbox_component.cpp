// The bounding box component (GameObjects::CBBoxComponent): Obj_ObjectInRadius, Obj_ObjectInRadiusOfNode and
// Obj_ObjectInRect. From the disassembly of 0x0041A810-0x0041B274. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "game/obj/bbox_component.h"
#include "game/gfx/d3dx.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/obj/objman.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"
#include "game/obj/objman.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kObjObjectInRect = 0x6520B902;         // Obj_ObjectInRect
constexpr uint32_t kObjObjectInRadius = 0x67660FE5;       // Obj_ObjectInRadius
constexpr uint32_t kObjObjectInRadiusOfNode = 0xDCE760A1; // Obj_ObjectInRadiusOfNode
constexpr uint32_t kOffset = 0xA6F5352F;                  // offset
constexpr uint32_t kAngle = 0xFF7EBAF6;                   // angle
constexpr uint32_t kHeight = 0x0AB21AF0;                  // height
constexpr uint32_t kDist = 0x7E832F08;                    // dist
constexpr uint32_t kWidth = 0x73E5BAD0;                   // width
constexpr uint32_t kRadius = 0xC48391A5;                  // radius
constexpr uint32_t kTypeParam = 0x7321A8D6;               // type
constexpr uint32_t kNode = 0x7A8017BA;                    // node
// type's values and the object types they stand for
constexpr uint32_t kSkater = 0x5B8AB877;  // skater
constexpr uint32_t kPed = 0x061A741E;     // ped
constexpr uint32_t kGameObj = 0x3B5737A6; // gameobj
constexpr uint32_t kCar = 0x88C21962;     // car
constexpr uint32_t kObjectTypePed = 0xD2DB7F97;
constexpr uint32_t kObjectTypeGameObj = 0xBC0D91D8;
constexpr uint32_t kObjectTypeCar = 0x5C0312EB;

constexpr float kFeetToInches = 12.0f;               // 0x0064546C
constexpr float kDegreesToRadians = 0.0174532924f;   // 0x00645464
constexpr float kHalf = 0.5f;                        // 0x006453BC

// out = a * b (the statically linked D3DXMatrixMultiply, 0x005BFA8C: a library function, not decompiled).
void MatrixMultiply(Maths::Matrix* out, const Maths::Matrix* a, const Maths::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// A manager's object list (its vtable +0x18).
MemSys::ListNode* ManagerObjectList(CObjectHubBase* manager)
{
    return reinterpret_cast<MemSys::ListNode*(__thiscall*)(CObjectHubBase*)>(manager->m_vtable[6])(manager);
}

// Whether `object` is nearer than the square root of `reachSquared` to `point` (or to the position of `self`'s object).
// The squares summed z, y, then x on the FPU stack, compared unrounded.
bool IsNear(const CGameEntity* object, const CGameEntity* self, float reachSquared, const Maths::Vector* point)
{
    float x = self->m_pos.x;
    float y = self->m_pos.y;
    float z = self->m_pos.z;
    if (point) {
        x = point->x;
        y = point->y;
        z = point->z;
    }
    double dx = static_cast<double>(object->m_pos.x) - x;
    double dy = static_cast<double>(object->m_pos.y) - y;
    double dz = static_cast<double>(object->m_pos.z) - z;
    return dz * dz + dy * dy + dx * dx < reachSquared;
}

} // namespace

// 0x0041ADC0
CObjectModule* __cdecl CBBoxComponent::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CBBoxComponent));
    return memory ? static_cast<CBBoxComponent*>(memory)->Construct() : nullptr;
}

// 0x0041AAD0
CBBoxComponent* CBBoxComponent::Construct()
{
    CObjectModule::Construct();
    m_vtable = kVtable;
    m_type = kType;
    m_boxMax = {10.0f, 10.0f, 10.0f, 1.0f};
    m_boxMin = {-10.0f, -10.0f, -10.0f, 1.0f};
    m_shift = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x0041A810
void CBBoxComponent::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041A880
CBBoxComponent* CBBoxComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0041A950
void CBBoxComponent::InitRect(Rect* rect, Scripts::CStruct* params)
{
    rect->m_computed = false;
    rect->m_angle = 0.0f;
    rect->m_radiusComputed = false;
    rect->m_shift = m_shift;
    rect->m_dist = 0.0f;
    rect->m_span = 0.0f;
    rect->m_levelY = 0.0f;
    Maths::Vector offset;
    offset.w = 0.0f; // (x, y and z uninitialised in the original until found)
    if (params->GetVector(kOffset, reinterpret_cast<Scripts::VectorValue*>(&offset), 0)) {
        offset.FeetAsInches();
        rect->m_shift.x = offset.x + rect->m_shift.x;
        rect->m_shift.y = offset.y + rect->m_shift.y;
        rect->m_shift.z = offset.z + rect->m_shift.z;
        rect->m_shift.w = offset.w + rect->m_shift.w;
    }
    if (params->GetFloat(kAngle, &rect->m_angle, 0))
        rect->m_angle = rect->m_angle * kDegreesToRadians;
    if (params->GetFloat(kHeight, &rect->m_levelY, 0))
        rect->m_levelY = rect->m_levelY * kFeetToInches;
    if (params->GetFloat(kDist, &rect->m_dist, 0))
        rect->m_dist = rect->m_dist * kFeetToInches;
    if (params->GetFloat(kWidth, &rect->m_span, 0))
        rect->m_span = rect->m_span * kFeetToInches;
}

// 0x0041AB20
void CBBoxComponent::ComputeRect(Rect* rect)
{
    if (rect->m_computed)
        return;
    rect->m_basisFrame = m_object->m_basisFrame;
    if (rect->m_angle != 0.0f) {
        Maths::Matrix rotation;
        MatrixMultiply(&rect->m_basisFrame, &Maths::MakeYawMatrix(rotation, rect->m_angle), &rect->m_basisFrame);
        rect->m_basisFrame = rect->m_basisFrame; // (the original copies it onto itself)
    }
    rect->m_basisFrame.Invert();
    rect->m_upperLimit.w = 1.0f;
    rect->m_upperLimit.x = rect->m_span * kHalf;
    rect->m_upperLimit.y = rect->m_levelY * kHalf;
    rect->m_upperLimit.z = rect->m_dist;
    rect->m_lowerLimit.x = -rect->m_upperLimit.x;
    rect->m_lowerLimit.y = -rect->m_upperLimit.y;
    rect->m_lowerLimit.w = -rect->m_upperLimit.w;
    rect->m_lowerLimit.z = 0.0f; // (the original first stores -m_upperLimit.z there)
    rect->m_upperLimit.x = rect->m_upperLimit.x + rect->m_shift.x;
    rect->m_upperLimit.y = rect->m_shift.y + rect->m_upperLimit.y;
    rect->m_upperLimit.z = rect->m_shift.z + rect->m_upperLimit.z;
    rect->m_upperLimit.w = rect->m_shift.w + rect->m_upperLimit.w;
    rect->m_lowerLimit.x = rect->m_lowerLimit.x + rect->m_shift.x;
    rect->m_lowerLimit.y = rect->m_shift.y + rect->m_lowerLimit.y;
    rect->m_lowerLimit.z = rect->m_shift.z + rect->m_lowerLimit.z;
    rect->m_lowerLimit.w = rect->m_shift.w + rect->m_lowerLimit.w;
    rect->m_computed = true;
}

// 0x0041A8B0
double RectRadiusSqr(CBBoxComponent::Rect* rect)
{
    if (rect->m_radiusComputed)
        return rect->m_radiusSqr;
    float size = std::fabs(rect->m_dist);
    if (std::fabs(rect->m_levelY) > size)
        size = std::fabs(rect->m_levelY);
    if (std::fabs(rect->m_span) > size)
        size = std::fabs(rect->m_span);
    float offset = std::fabs(rect->m_shift.z);
    if (std::fabs(rect->m_shift.y) > offset)
        offset = std::fabs(rect->m_shift.y);
    if (std::fabs(rect->m_shift.x) > offset)
        offset = std::fabs(rect->m_shift.x);
    rect->m_radiusComputed = true;
    double sum = static_cast<double>(size) + offset;
    double square = sum * sum;
    rect->m_radiusSqr = static_cast<float>(square);
    return square;
}

// 0x0041AC40
bool CBBoxComponent::IsInRect(CGameEntity* object, Rect* rect)
{
    float offsetX = rect->m_shift.x;
    float offsetY = rect->m_shift.y;
    float offsetZ = rect->m_shift.z;
    ComputeRect(rect);
    // From the box's middle (rounded where the original stores: x, the middle's z, the sum).
    const Maths::Vector& self = m_object->m_pos;
    float dx = static_cast<float>((static_cast<double>(offsetX) + object->m_pos.x) - self.x);
    double dy = (static_cast<double>(offsetY) + object->m_pos.y) - self.y;
    float middleZ = static_cast<float>(static_cast<double>(offsetZ) + object->m_pos.z);
    double dz = static_cast<double>(middleZ) - self.z;
    float distanceSqr = static_cast<float>(dz * dz + dy * dy + static_cast<double>(dx) * dx);
    if (RectRadiusSqr(rect) < distanceSqr)
        return false;
    // The middle of its box in the box's frame.
    Maths::Vector point = object->m_pos;
    auto* query = static_cast<CBBoxComponent*>(object->GetComponent(kType));
    point.y = static_cast<float>((static_cast<double>(query->m_boxMax.y) - query->m_boxMin.y) * kHalf + point.y);
    point.x = point.x - self.x;
    point.y = point.y - self.y;
    point.z = point.z - self.z;
    point.w = point.w - self.w;
    Maths::Vector local;
    point = rect->m_basisFrame.Transform(local, point);
    return Maths::PointInBox(&point, &rect->m_upperLimit, &rect->m_lowerLimit);
}

// 0x0041AE20
bool CBBoxComponent::AnySkaterNear(Rect* rect, float reachSquared, const Maths::Vector* point)
{
    int count = GameModules::Skate::Instance()->CountRiders();
    for (int i = 0; i < count; ++i) {
        CGameEntity* skater = GameModules::Skate::Instance()->GetRiderAt(i);
        if (skater == m_object)
            continue;
        if (rect) {
            if (IsInRect(skater, rect))
                return true;
        } else if (IsNear(skater, m_object, reachSquared, point)) {
            return true;
        }
    }
    return false;
}

// 0x0041AF10
bool CBBoxComponent::AnyObjectNear(Scripts::CStruct* params, Scripts::CScript* script, float reachSquared, Rect* rect,
                                          uint32_t type, const Maths::Vector* point)
{
    (void)params;
    (void)script;
    uint32_t objectType;
    switch (type) {
    case kSkater:
        return AnySkaterNear(rect, reachSquared, point);
    case kPed:
        objectType = kObjectTypePed;
        break;
    case kGameObj:
        objectType = kObjectTypeGameObj;
        break;
    case kCar:
        objectType = kObjectTypeCar;
        break;
    default:
        return false;
    }
    CObjectSearch search;
    search.m_vtable = g_objectSearchVtable;
    search.m_listLink = nullptr;
    for (auto* object = static_cast<CGameEntity*>(
             search.FirstItemOfType(ManagerObjectList(m_object->m_parkOwner), objectType));
         object; object = static_cast<CGameEntity*>(search.NextItemOfType())) {
        if (object == m_object)
            continue;
        if (rect) {
            if (IsInRect(object, rect))
                return true;
        } else if (IsNear(object, m_object, reachSquared, point)) {
            return true;
        }
    }
    return false;
}

// 0x0041B0A0
bool CBBoxComponent::QueryObjects(Scripts::CStruct* params, Scripts::CScript* script, Rect* rect,
                                         const Maths::Vector* point)
{
    float reachSquared = 0.0f;
    if (!rect) {
        if (!params->GetFloat(kRadius, &reachSquared, 0))
            return false;
        double radius = static_cast<double>(reachSquared) * kFeetToInches;
        reachSquared = static_cast<float>(radius * radius);
    }
    uint32_t type;
    if (params->ReadHashValue(kTypeParam, &type, 0))
        return AnyObjectNear(params, script, reachSquared, rect, type, point);
    Scripts::CArray* types = nullptr;
    if (params->GetArray(kTypeParam, &types, 1)) {
        for (uint32_t i = 0; i < types->m_size; ++i) {
            if (AnyObjectNear(params, script, reachSquared, rect, types->GetElement(i), point))
                return true;
        }
    }
    return false;
}

// 0x0041B1A0
int CBBoxComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript* script)
{
    Rect rect;
    Maths::Vector position;
    switch (name) {
    case kObjObjectInRect:
        InitRect(&rect, params);
        return QueryObjects(params, script, &rect, nullptr) ? 1 : 0;
    case kObjObjectInRadius:
        return QueryObjects(params, script, nullptr, nullptr) ? 1 : 0;
    case kObjObjectInRadiusOfNode: {
        uint32_t node;
        if (!params->ReadHashValue(kNode, &node, 0))
            return 0;
        LevelNodes::GetPosition(node, &position);
        return QueryObjects(params, script, nullptr, &position) ? 1 : 0;
    }
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CBBoxComponent::Destroy,
         &GameObjects::ComponentWithoutUpdate::Update,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CBBoxComponent::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj

namespace GameObjects {

static VtableSlot g_objectSearchVtable_slots[] = {&GameObjects::CObjectSearch::Destroy};
void** const g_objectSearchVtable = reinterpret_cast<void**>(g_objectSearchVtable_slots);

} // namespace Obj
