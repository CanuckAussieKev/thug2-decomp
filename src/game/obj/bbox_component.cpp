// The bounding box component (Obj::CBBoxComponent): Obj_ObjectInRadius, Obj_ObjectInRadiusOfNode and
// Obj_ObjectInRect. From the disassembly of 0x0041A810-0x0041B274. The x87 roundings are kept: values the original
// stores are rounded to float, the ones it keeps on the FPU stack are worked in double.

#include "game/obj/bbox_component.h"

#include <cmath>
#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/geometry.h"
#include "game/obj/objman.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00645DC4);
void** const kObjectSearchVtable = reinterpret_cast<void**>(0x00645DFC);

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
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

// A manager's object list (its vtable +0x18).
Mem::ListNode* ManagerObjectList(CBaseManager* manager)
{
    return reinterpret_cast<Mem::ListNode*(__thiscall*)(CBaseManager*)>(manager->m_vtable[6])(manager);
}

// Whether `object` is nearer than the square root of `radiusSqr` to `point` (or to the position of `self`'s object).
// The squares summed z, y, then x on the FPU stack, compared unrounded.
bool IsNear(const CCompositeObject* object, const CCompositeObject* self, float radiusSqr, const Mth::Vector* point)
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
    return dz * dz + dy * dy + dx * dx < radiusSqr;
}

} // namespace

// 0x0041ADC0
CBaseComponent* __cdecl CBBoxComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CBBoxComponent));
    return memory ? static_cast<CBBoxComponent*>(memory)->Construct() : nullptr;
}

// 0x0041AAD0
CBBoxComponent* CBBoxComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    m_boxMax = {10.0f, 10.0f, 10.0f, 1.0f};
    m_boxMin = {-10.0f, -10.0f, -10.0f, 1.0f};
    m_offset = {0.0f, 0.0f, 0.0f, 1.0f};
    return this;
}

// 0x0041A810
void CBBoxComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041A880
CBBoxComponent* CBBoxComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041A950
void CBBoxComponent::InitRect(Rect* rect, Script::CStruct* params)
{
    rect->m_computed = false;
    rect->m_angle = 0.0f;
    rect->m_radiusComputed = false;
    rect->m_offset = m_offset;
    rect->m_dist = 0.0f;
    rect->m_width = 0.0f;
    rect->m_height = 0.0f;
    Mth::Vector offset;
    offset.w = 0.0f; // (x, y and z uninitialised in the original until found)
    if (params->GetVector(kOffset, reinterpret_cast<Script::VectorValue*>(&offset), 0)) {
        offset.FeetToInches();
        rect->m_offset.x = offset.x + rect->m_offset.x;
        rect->m_offset.y = offset.y + rect->m_offset.y;
        rect->m_offset.z = offset.z + rect->m_offset.z;
        rect->m_offset.w = offset.w + rect->m_offset.w;
    }
    if (params->GetFloat(kAngle, &rect->m_angle, 0))
        rect->m_angle = rect->m_angle * kDegreesToRadians;
    if (params->GetFloat(kHeight, &rect->m_height, 0))
        rect->m_height = rect->m_height * kFeetToInches;
    if (params->GetFloat(kDist, &rect->m_dist, 0))
        rect->m_dist = rect->m_dist * kFeetToInches;
    if (params->GetFloat(kWidth, &rect->m_width, 0))
        rect->m_width = rect->m_width * kFeetToInches;
}

// 0x0041AB20
void CBBoxComponent::ComputeRect(Rect* rect)
{
    if (rect->m_computed)
        return;
    rect->m_matrix = m_object->m_matrix;
    if (rect->m_angle != 0.0f) {
        Mth::Matrix rotation;
        MatrixMultiply(&rect->m_matrix, &Mth::CreateRotateYMatrix(rotation, rect->m_angle), &rect->m_matrix);
        rect->m_matrix = rect->m_matrix; // (the original copies it onto itself)
    }
    rect->m_matrix.Invert();
    rect->m_max.w = 1.0f;
    rect->m_max.x = rect->m_width * kHalf;
    rect->m_max.y = rect->m_height * kHalf;
    rect->m_max.z = rect->m_dist;
    rect->m_min.x = -rect->m_max.x;
    rect->m_min.y = -rect->m_max.y;
    rect->m_min.w = -rect->m_max.w;
    rect->m_min.z = 0.0f; // (the original first stores -m_max.z there)
    rect->m_max.x = rect->m_max.x + rect->m_offset.x;
    rect->m_max.y = rect->m_offset.y + rect->m_max.y;
    rect->m_max.z = rect->m_offset.z + rect->m_max.z;
    rect->m_max.w = rect->m_offset.w + rect->m_max.w;
    rect->m_min.x = rect->m_min.x + rect->m_offset.x;
    rect->m_min.y = rect->m_offset.y + rect->m_min.y;
    rect->m_min.z = rect->m_offset.z + rect->m_min.z;
    rect->m_min.w = rect->m_offset.w + rect->m_min.w;
    rect->m_computed = true;
}

// 0x0041A8B0
double RectRadiusSqr(CBBoxComponent::Rect* rect)
{
    if (rect->m_radiusComputed)
        return rect->m_radiusSqr;
    float size = std::fabs(rect->m_dist);
    if (std::fabs(rect->m_height) > size)
        size = std::fabs(rect->m_height);
    if (std::fabs(rect->m_width) > size)
        size = std::fabs(rect->m_width);
    float offset = std::fabs(rect->m_offset.z);
    if (std::fabs(rect->m_offset.y) > offset)
        offset = std::fabs(rect->m_offset.y);
    if (std::fabs(rect->m_offset.x) > offset)
        offset = std::fabs(rect->m_offset.x);
    rect->m_radiusComputed = true;
    double sum = static_cast<double>(size) + offset;
    double square = sum * sum;
    rect->m_radiusSqr = static_cast<float>(square);
    return square;
}

// 0x0041AC40
bool CBBoxComponent::IsInRect(CCompositeObject* object, Rect* rect)
{
    float offsetX = rect->m_offset.x;
    float offsetY = rect->m_offset.y;
    float offsetZ = rect->m_offset.z;
    ComputeRect(rect);
    // From the box's middle (rounded where the original stores: x, the middle's z, the sum).
    const Mth::Vector& self = m_object->m_pos;
    float dx = static_cast<float>((static_cast<double>(offsetX) + object->m_pos.x) - self.x);
    double dy = (static_cast<double>(offsetY) + object->m_pos.y) - self.y;
    float middleZ = static_cast<float>(static_cast<double>(offsetZ) + object->m_pos.z);
    double dz = static_cast<double>(middleZ) - self.z;
    float distanceSqr = static_cast<float>(dz * dz + dy * dy + static_cast<double>(dx) * dx);
    if (RectRadiusSqr(rect) < distanceSqr)
        return false;
    // The middle of its box in the box's frame.
    Mth::Vector point = object->m_pos;
    auto* query = static_cast<CBBoxComponent*>(object->GetComponent(kType));
    point.y = static_cast<float>((static_cast<double>(query->m_boxMax.y) - query->m_boxMin.y) * kHalf + point.y);
    point.x = point.x - self.x;
    point.y = point.y - self.y;
    point.z = point.z - self.z;
    point.w = point.w - self.w;
    Mth::Vector local;
    point = rect->m_matrix.Transform(local, point);
    return Mth::PointInBox(&point, &rect->m_max, &rect->m_min);
}

// 0x0041AE20
bool CBBoxComponent::AnySkaterNear(Rect* rect, float radiusSqr, const Mth::Vector* point)
{
    int count = Mdl::Skate::Instance()->GetNumSkaters();
    for (int i = 0; i < count; ++i) {
        CCompositeObject* skater = Mdl::Skate::Instance()->GetSkater(i);
        if (skater == m_object)
            continue;
        if (rect) {
            if (IsInRect(skater, rect))
                return true;
        } else if (IsNear(skater, m_object, radiusSqr, point)) {
            return true;
        }
    }
    return false;
}

// 0x0041AF10
bool CBBoxComponent::AnyObjectNear(Script::CStruct* params, Script::CScript* script, float radiusSqr, Rect* rect,
                                          uint32_t type, const Mth::Vector* point)
{
    (void)params;
    (void)script;
    uint32_t objectType;
    switch (type) {
    case kSkater:
        return AnySkaterNear(rect, radiusSqr, point);
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
    search.m_vtable = kObjectSearchVtable;
    search.m_node = nullptr;
    for (auto* object = static_cast<CCompositeObject*>(
             search.FirstItemOfType(ManagerObjectList(m_object->mp_manager), objectType));
         object; object = static_cast<CCompositeObject*>(search.NextItemOfType())) {
        if (object == m_object)
            continue;
        if (rect) {
            if (IsInRect(object, rect))
                return true;
        } else if (IsNear(object, m_object, radiusSqr, point)) {
            return true;
        }
    }
    return false;
}

// 0x0041B0A0
bool CBBoxComponent::QueryObjects(Script::CStruct* params, Script::CScript* script, Rect* rect,
                                         const Mth::Vector* point)
{
    float radiusSqr = 0.0f;
    if (!rect) {
        if (!params->GetFloat(kRadius, &radiusSqr, 0))
            return false;
        double radius = static_cast<double>(radiusSqr) * kFeetToInches;
        radiusSqr = static_cast<float>(radius * radius);
    }
    uint32_t type;
    if (params->GetChecksum(kTypeParam, &type, 0))
        return AnyObjectNear(params, script, radiusSqr, rect, type, point);
    Script::CArray* types = nullptr;
    if (params->GetArray(kTypeParam, &types, 1)) {
        for (uint32_t i = 0; i < types->m_size; ++i) {
            if (AnyObjectNear(params, script, radiusSqr, rect, types->GetElement(i), point))
                return true;
        }
    }
    return false;
}

// 0x0041B1A0
int CBBoxComponent::CallMemberFunction(uint32_t name, Script::CStruct* params, Script::CScript* script)
{
    Rect rect;
    Mth::Vector position;
    switch (name) {
    case kObjObjectInRect:
        InitRect(&rect, params);
        return QueryObjects(params, script, &rect, nullptr) ? 1 : 0;
    case kObjObjectInRadius:
        return QueryObjects(params, script, nullptr, nullptr) ? 1 : 0;
    case kObjObjectInRadiusOfNode: {
        uint32_t node;
        if (!params->GetChecksum(kNode, &node, 0))
            return 0;
        SkateScript::GetPosition(node, &position);
        return QueryObjects(params, script, nullptr, &position) ? 1 : 0;
    }
    default:
        return 2;
    }
}

} // namespace Obj

REPLACE(0x0041ADC0, Obj::CBBoxComponent::Create);
REPLACE(0x0041AAD0, &Obj::CBBoxComponent::Construct);
REPLACE(0x0041A810, &Obj::CBBoxComponent::DestroyBody);
REPLACE(0x0041A880, &Obj::CBBoxComponent::Destroy);
REPLACE(0x0041B1A0, &Obj::CBBoxComponent::CallMemberFunction);
REPLACE(0x0041A950, &Obj::CBBoxComponent::InitRect);
REPLACE(0x0041AB20, &Obj::CBBoxComponent::ComputeRect);
REPLACE(0x0041AC40, &Obj::CBBoxComponent::IsInRect);
REPLACE(0x0041AE20, &Obj::CBBoxComponent::AnySkaterNear);
REPLACE(0x0041AF10, &Obj::CBBoxComponent::AnyObjectNear);
REPLACE(0x0041B0A0, &Obj::CBBoxComponent::QueryObjects);
REPLACE(0x0041A8B0, Obj::RectRadiusSqr);
