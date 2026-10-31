// The collision component (GameObjects::CContactShapeModule): an object's own movable collision object, made from its node's
// CollisionMode and its model's (or its level sector's) collision, placed where the object is.

#include <cstdint>

#include "engine/objects/collision_component.h"
#include "engine/gfx/d3dx.h"
#include "engine/gfx/model.h"
#include "engine/gfx/engine.h"

#include "engine/assets/resource_registry.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/maths/mat4.h"
#include "engine/net/buddy_status.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_world.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/gfx/weather.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kModelModuleName = 0x286A8D26; // Model
constexpr uint32_t kCollisionMode = 0x2D7E583B;  // CollisionMode
constexpr uint32_t kClass = 0x12B4E660;          // Class
constexpr uint32_t kLevelObject = 0xB7B3BD86;    // LevelObject
constexpr uint32_t kName = 0xA1DC81F9;           // Name

// The model component's model (+0x208).
uint8_t* ModelOf(CObjectModule* model) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0x208); }

// A level sector by name in any scene (0x0048C510) and its collision object (0x005480F0: +0x18).
uint8_t* LookUpSector(uint32_t name) { return static_cast<uint8_t*>(Engine3D::Engine::LookUpSector(name)); }
Engine3D::CHitMesh* SectorCollision(uint8_t* sector)
{
    return reinterpret_cast<Engine3D::CHitMesh*>(reinterpret_cast<OnlinePlay::FoldedGetters*>(sector)->Word18());
}

// The model's bone matrix (0x00493040: a single-bone model's, from the asset manager; null otherwise): its rotation at
// +0x10, its translation at +0x40 (the matrix's fourth row).
const uint8_t* BoneOf(uint8_t* model) { return reinterpret_cast<const uint8_t*>(reinterpret_cast<Engine3D::CRenderModel*>(model)->GetAssetHierarchy()); }

void D3dxMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The collision object's slots 5 / 6: its world position, its orientation.
void PlaceAtAbsolutePoint(Engine3D::CHitMesh* collision, const Maths::Vec4* position) { collision->Call<void>(5, position); }
void ChangeFacing(Engine3D::CHitMesh* collision, const Maths::Mat4* orientation) { collision->Call<void>(6, orientation); }

} // namespace

// 0x0041C920
CObjectModule* __cdecl CContactShapeModule::Fabricate()
{
    auto* component = static_cast<CContactShapeModule*>(MemSys::AllocZeroed(sizeof(CContactShapeModule)));
    if (!component)
        return nullptr;
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    return component;
}

// 0x0041C710
void CContactShapeModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_hitShape) {
        Engine3D::UnregisterMovable(m_hitShape);
        if (m_hitShape)
            m_hitShape->Call<void*>(0, 1); // (its scalar deleting destructor)
    }
    CObjectModule::DestroyBody();
}

// 0x0041C780
CContactShapeModule* CContactShapeModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041C7B0
void CContactShapeModule::ConfigureFromData(Scripts::CStruct* params)
{
    uint32_t mode = 0;
    params->ReadHashValue(kCollisionMode, &mode, 0);
    int type = Engine3D::ShapeTypeFromChecksum(mode);
    if (!type)
        return;
    using InitCollisionSlot = void(__thiscall*)(CContactShapeModule*, int, Engine3D::CTriMeshSource*);
    auto initCollision = reinterpret_cast<InitCollisionSlot>(m_vtable[13]);
    uint32_t cls = 0;
    params->ReadHashValue(kClass, &cls, 0);
    if (cls != kLevelObject) {
        initCollision(this, type, nullptr);
        return;
    }
    // (the original passes on whatever was in the name's slot when there's no Name - an assertion's case; ours 0)
    uint32_t name = 0;
    params->ReadHashValue(kName, &name, 1);
    uint8_t* sector = LookUpSector(name); // (no such sector: a crash, as in the original)
    if (SectorCollision(sector))
        initCollision(this, type, SectorCollision(sector)->m_geometry);
    else
        initCollision(this, type, nullptr);
}

// 0x0041C890
void CContactShapeModule::SetUpHitShape(int type, Engine3D::CTriMeshSource* geometry)
{
    if (geometry) {
        m_hitShape = Engine3D::BuildDynamicHitMesh(type, geometry, 1, reinterpret_cast<int>(m_entity));
        return;
    }
    uint8_t* model = ModelOf(m_entity->FetchModule(kModelModuleName));
    auto* data = static_cast<uint8_t*>(AssetStore::AssetManager()->GetAssetData(*reinterpret_cast<uint32_t*>(model + 0x30), 0));
    if (data) {
        m_hitShape = Engine3D::BuildDynamicHitMesh(type, *reinterpret_cast<Engine3D::CTriMeshSource**>(data + 0x8C),
                                                  *reinterpret_cast<int*>(data + 0x88), reinterpret_cast<int>(m_entity));
    } else {
        m_hitShape = Engine3D::BuildDynamicHitMesh(type, nullptr, 0, reinterpret_cast<int>(m_entity));
    }
}

// 0x0041C980
void CContactShapeModule::TickFrame()
{
    Engine3D::CHitMesh* collision = m_hitShape;
    if (!collision)
        return;
    CObjectModule* model = m_entity->FetchModule(kModelModuleName);
    const uint8_t* bone = model ? BoneOf(ModelOf(model)) : nullptr;
    if (!bone) {
        PlaceAtAbsolutePoint(collision, &m_entity->m_atPoint);
        ChangeFacing(collision, &m_entity->m_displayMatrix);
        return;
    }
    const auto* translation = reinterpret_cast<const float*>(bone + 0x40);
    const Maths::Vec4& pos = m_entity->m_atPoint;
    Maths::Vec4 position = {translation[0] + pos.x, translation[1] + pos.y, translation[2] + pos.z, pos.w};
    PlaceAtAbsolutePoint(collision, &position);
    Maths::Mat4 orientation;
    D3dxMultiply(&orientation, reinterpret_cast<const Maths::Mat4*>(bone + 0x10), &m_entity->m_displayMatrix);
    ChangeFacing(collision, &orientation);
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CContactShapeModule::Teardown,
         &GameObjects::CContactShapeModule::TickFrame,
         &GameObjects::CContactShapeModule::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Engine3D::VirtualSlot1Jump,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName,
         &GameObjects::CContactShapeModule::SetUpHitShape};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
