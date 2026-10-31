// The static vehicle component (game/obj/static_vehicle.h).

#include "game/obj/static_vehicle.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/model.h"
#include "game/gfx/skeleton.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;

// The model component's model (+0x208) and that model's hierarchy (0x00493040, not rewritten yet): objects of 0x50
// bytes from +0x50, each with its matrix at +0x10.
const uint8_t* HierarchyOf(CObjectModule* model)
{
    uint8_t* m = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0x208);
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<Engine3D::CRenderModel*>(m)->GetAssetHierarchy());
}

// The skeleton component's skeleton (+0x18), its bone matrices (+0x04) and bone count (0x004A2BA0, not rewritten).
uint8_t* SkeletonOf(CObjectModule* skeleton) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skeleton) + 0x18); }
Maths::Matrix* BoneMatrices(uint8_t* skeleton) { return *reinterpret_cast<Maths::Matrix**>(skeleton + 0x04); }
int BoneCount(uint8_t* skeleton) { return reinterpret_cast<int(__thiscall*)(uint8_t*)>(CodeAddress(&Gfx2D::CRig::GetNumBones))(skeleton); }

// A model position in the game's axes: x and the model's y negated, y and z swapped.
Maths::Vector FromModel(const float* p) { return {-p[0], p[2], -p[1], 1.0f}; }

} // namespace

// 0x00433690
CObjectModule* __cdecl CParkedVehicleModule::Create()
{
    void* memory = MemSys::AllocZeroed(sizeof(CParkedVehicleModule));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CParkedVehicleModule*>(memory);
    component->CObjectModule::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x00433650
void CParkedVehicleModule::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x00433660
CParkedVehicleModule* CParkedVehicleModule::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x004336F0
void CParkedVehicleModule::Finalize()
{
    const uint8_t* hierarchy = HierarchyOf(m_object->GetComponent(kModel));
    // The wheels: the hierarchy's objects 1-4 (their matrices' translations).
    Maths::Vector wheels[4];
    for (int k = 0; k < 4; ++k)
        wheels[k] = FromModel(reinterpret_cast<const float*>(hierarchy + 0xE0 + 0x50 * k));
    // The body: the root's (object 0's) translation.
    Maths::Vector body = FromModel(reinterpret_cast<const float*>(hierarchy + 0x90));

    CObjectModule* skeletonComponent = m_object->GetComponent(kSkeleton);
    Maths::Matrix* bones = BoneMatrices(SkeletonOf(skeletonComponent));
    for (int i = 0; i < BoneCount(SkeletonOf(skeletonComponent)); ++i) {
        Maths::Matrix& bone = bones[i];
        std::memset(&bone, 0, sizeof(bone));
        if (i == 0)
            continue;
        if (i == 1) {
            bone.row[0].x = 1.0f;
            bone.row[1].z = -1.0f;
            bone.row[2].y = 1.0f;
            bone.row[3] = body;
        } else {
            bone.row[0].x = -1.0f;
            bone.row[1].z = 1.0f;
            bone.row[2].y = 1.0f;
            // Not original - a seventh bone or more would be a fifth wheel: the original reads it from past the end of
            // its four (its own stack - garbage); ours stands it at the origin.
            bone.row[3] = i - 2 < 4 ? wheels[i - 2] : Maths::Vector{0.0f, 0.0f, 0.0f, 0.0f};
        }
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CParkedVehicleModule::Destroy,
         &GameObjects::ComponentWithoutUpdate::Update,
         Stubs::IgnoreArgument,
         &GameObjects::CParkedVehicleModule::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
