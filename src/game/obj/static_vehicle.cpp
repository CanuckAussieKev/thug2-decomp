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
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646560);
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kSkeleton = 0x222756D5;

// The model component's model (+0x208) and that model's hierarchy (0x00493040, not rewritten yet): objects of 0x50
// bytes from +0x50, each with its matrix at +0x10.
const uint8_t* HierarchyOf(CBaseComponent* model)
{
    uint8_t* m = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(model) + 0x208);
    return reinterpret_cast<const uint8_t*>(reinterpret_cast<Nx::CModel*>(m)->GetAssetHierarchy());
}

// The skeleton component's skeleton (+0x18), its bone matrices (+0x04) and bone count (0x004A2BA0, not rewritten).
uint8_t* SkeletonOf(CBaseComponent* skeleton) { return *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(skeleton) + 0x18); }
Mth::Matrix* BoneMatrices(uint8_t* skeleton) { return *reinterpret_cast<Mth::Matrix**>(skeleton + 0x04); }
int BoneCount(uint8_t* skeleton) { return GameFunction<int(__thiscall*)(uint8_t*)>(0x004A2BA0)(skeleton); }

// A model position in the game's axes: x and the model's y negated, y and z swapped.
Mth::Vector FromModel(const float* p) { return {-p[0], p[2], -p[1], 1.0f}; }

} // namespace

// 0x00433690
CBaseComponent* __cdecl CStaticVehicleComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CStaticVehicleComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CStaticVehicleComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    return component;
}

// 0x00433650
void CStaticVehicleComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x00433660
CStaticVehicleComponent* CStaticVehicleComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004336F0
void CStaticVehicleComponent::Finalize()
{
    const uint8_t* hierarchy = HierarchyOf(m_object->GetComponent(kModel));
    // The wheels: the hierarchy's objects 1-4 (their matrices' translations).
    Mth::Vector wheels[4];
    for (int k = 0; k < 4; ++k)
        wheels[k] = FromModel(reinterpret_cast<const float*>(hierarchy + 0xE0 + 0x50 * k));
    // The body: the root's (object 0's) translation.
    Mth::Vector body = FromModel(reinterpret_cast<const float*>(hierarchy + 0x90));

    CBaseComponent* skeletonComponent = m_object->GetComponent(kSkeleton);
    Mth::Matrix* bones = BoneMatrices(SkeletonOf(skeletonComponent));
    for (int i = 0; i < BoneCount(SkeletonOf(skeletonComponent)); ++i) {
        Mth::Matrix& bone = bones[i];
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
            bone.row[3] = i - 2 < 4 ? wheels[i - 2] : Mth::Vector{0.0f, 0.0f, 0.0f, 0.0f};
        }
    }
}

} // namespace Obj

REPLACE(0x00433690, Obj::CStaticVehicleComponent::Create);
REPLACE(0x00433650, &Obj::CStaticVehicleComponent::DestroyBody);
REPLACE(0x00433660, &Obj::CStaticVehicleComponent::Destroy);
REPLACE(0x004336F0, &Obj::CStaticVehicleComponent::Finalize);
