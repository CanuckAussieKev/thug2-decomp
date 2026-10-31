// The collision component (Obj::CCollisionComponent): an object's own movable collision object, made from its node's
// CollisionMode and its model's (or its level sector's) collision, placed where the object is.

#include "game/obj/collision_component.h"
#include "game/gfx/d3dx.h"
#include "game/gfx/model.h"
#include "game/gfx/engine.h"

#include "game/ass/assman.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/mth/matrix.h"
#include "game/net/buddy_status.h"
#include "game/nx/coll_cache.h"
#include "game/nx/coll_obj.h"
#include "game/nx/coll_static.h"
#include "game/nx/coll_world.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/gfx/weather.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

namespace {

extern void** const kVtable;

constexpr uint32_t kModelComponent = 0x286A8D26; // Model
constexpr uint32_t kCollisionMode = 0x2D7E583B;  // CollisionMode
constexpr uint32_t kClass = 0x12B4E660;          // Class
constexpr uint32_t kLevelObject = 0xB7B3BD86;    // LevelObject
constexpr uint32_t kName = 0xA1DC81F9;           // Name

// The model component's model (+0x208).
uint8_t* ModelOf(CBaseComponent* model) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0x208); }

// A level sector by name in any scene (0x0048C510) and its collision object (0x005480F0: +0x18).
uint8_t* GetSector(uint32_t name) { return static_cast<uint8_t*>(Nx::Engine::GetSector(name)); }
Nx::CCollObj* SectorCollision(uint8_t* sector)
{
    return reinterpret_cast<Nx::CCollObj*>(reinterpret_cast<GameNet::FoldedGetters*>(sector)->Word18());
}

// The model's bone matrix (0x00493040: a single-bone model's, from the asset manager; null otherwise): its rotation at
// +0x10, its translation at +0x40 (the matrix's fourth row).
const uint8_t* BoneOf(uint8_t* model) { return reinterpret_cast<const uint8_t*>(reinterpret_cast<Nx::CModel*>(model)->GetAssetHierarchy()); }

void D3dxMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// The collision object's slots 5 / 6: its world position, its orientation.
void SetWorldPosition(Nx::CCollObj* collision, const Mth::Vector* position) { collision->Call<void>(5, position); }
void SetOrientation(Nx::CCollObj* collision, const Mth::Matrix* orientation) { collision->Call<void>(6, orientation); }

} // namespace

// 0x0041C920
CBaseComponent* __cdecl CCollisionComponent::Create()
{
    auto* component = static_cast<CCollisionComponent*>(Mem::Calloc(sizeof(CCollisionComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x0041C710
void CCollisionComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_collision) {
        Nx::UnregisterMovable(mp_collision);
        if (mp_collision)
            mp_collision->Call<void*>(0, 1); // (its scalar deleting destructor)
    }
    CBaseComponent::DestroyBody();
}

// 0x0041C780
CCollisionComponent* CCollisionComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041C7B0
void CCollisionComponent::InitFromStructure(Script::CStruct* params)
{
    uint32_t mode = 0;
    params->GetChecksum(kCollisionMode, &mode, 0);
    int type = Nx::ShapeTypeFromChecksum(mode);
    if (!type)
        return;
    using InitCollisionSlot = void(__thiscall*)(CCollisionComponent*, int, Nx::CCollObjTriData*);
    auto initCollision = reinterpret_cast<InitCollisionSlot>(m_vtable[13]);
    uint32_t cls = 0;
    params->GetChecksum(kClass, &cls, 0);
    if (cls != kLevelObject) {
        initCollision(this, type, nullptr);
        return;
    }
    // (the original passes on whatever was in the name's slot when there's no Name - an assertion's case; ours 0)
    uint32_t name = 0;
    params->GetChecksum(kName, &name, 1);
    uint8_t* sector = GetSector(name); // (no such sector: a crash, as in the original)
    if (SectorCollision(sector))
        initCollision(this, type, SectorCollision(sector)->m_geometry);
    else
        initCollision(this, type, nullptr);
}

// 0x0041C890
void CCollisionComponent::InitCollision(int type, Nx::CCollObjTriData* geometry)
{
    if (geometry) {
        mp_collision = Nx::CreateMovableCollision(type, geometry, 1, reinterpret_cast<int>(m_object));
        return;
    }
    uint8_t* model = ModelOf(m_object->GetComponent(kModelComponent));
    auto* data = static_cast<uint8_t*>(Ass::AssetManager()->GetAssetData(*reinterpret_cast<uint32_t*>(model + 0x30), 0));
    if (data) {
        mp_collision = Nx::CreateMovableCollision(type, *reinterpret_cast<Nx::CCollObjTriData**>(data + 0x8C),
                                                  *reinterpret_cast<int*>(data + 0x88), reinterpret_cast<int>(m_object));
    } else {
        mp_collision = Nx::CreateMovableCollision(type, nullptr, 0, reinterpret_cast<int>(m_object));
    }
}

// 0x0041C980
void CCollisionComponent::Update()
{
    Nx::CCollObj* collision = mp_collision;
    if (!collision)
        return;
    CBaseComponent* model = m_object->GetComponent(kModelComponent);
    const uint8_t* bone = model ? BoneOf(ModelOf(model)) : nullptr;
    if (!bone) {
        SetWorldPosition(collision, &m_object->m_pos);
        SetOrientation(collision, &m_object->m_displayMatrix);
        return;
    }
    const auto* translation = reinterpret_cast<const float*>(bone + 0x40);
    const Mth::Vector& pos = m_object->m_pos;
    Mth::Vector position = {translation[0] + pos.x, translation[1] + pos.y, translation[2] + pos.z, pos.w};
    SetWorldPosition(collision, &position);
    Mth::Matrix orientation;
    D3dxMultiply(&orientation, reinterpret_cast<const Mth::Matrix*>(bone + 0x10), &m_object->m_displayMatrix);
    SetOrientation(collision, &orientation);
}

} // namespace Obj

REPLACE(0x0041C920, Obj::CCollisionComponent::Create);
REPLACE(0x0041C710, &Obj::CCollisionComponent::DestroyBody);
REPLACE(0x0041C780, &Obj::CCollisionComponent::Destroy);
REPLACE(0x0041C7B0, &Obj::CCollisionComponent::InitFromStructure);
REPLACE(0x0041C890, &Obj::CCollisionComponent::InitCollision);
REPLACE(0x0041C980, &Obj::CCollisionComponent::Update);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00645EEC,
    &Obj::CCollisionComponent::Destroy,
    &Obj::CCollisionComponent::Update,
    &Obj::CCollisionComponent::InitFromStructure,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Nx::VirtualSlot1Jump,
    Stubs::IgnoreArgument,
    static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction,
    &Obj::CCollisionComponent::InitCollision);

} // namespace
} // namespace Obj
