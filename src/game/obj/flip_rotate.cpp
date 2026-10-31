// The FlipRotate component (Obj::CFlipRotateComponent): construction, destruction and its parameters.

#include "game/obj/flip_rotate.h"
#include "game/gfx/anim_controller.h"
#include "game/gfx/skeleton.h"

#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/mth/quat.h"
#include "game/obj/object.h"
#include "game/obj/vehicle_component.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/car_physics.h"

namespace Obj {

namespace {

extern void** const kVtable;

// The base component (no source yet).
void ConstructBase(CFlipRotateComponent* component, void* object)
{
    reinterpret_cast<Gfx::CAnimController*>(component)->Construct(static_cast<Gfx::CAnimTimeline*>(object));
}
void DestroyBase(CFlipRotateComponent* component)
{
    reinterpret_cast<Gfx::CAnimController*>(component)->DestroyBody();
}
void InitBaseFromStructure(CFlipRotateComponent* component, Script::CStruct* params)
{
    reinterpret_cast<Gfx::CAnimController*>(component)->InitFromStructure(params);
}

constexpr uint32_t kFlipped = 0x0C7A712C; // "flipped"
constexpr uint32_t kRotated = 0x6EA3704A; // "rotated"

} // namespace

// 0x004E2C50
CFlipRotateComponent* CFlipRotateComponent::Construct(void* object)
{
    ConstructBase(this, object);
    m_flipped = false;
    m_rotated = false;
    m_vtable = kVtable;
    return this;
}

// 0x004E2C80
void CFlipRotateComponent::DestroyBody()
{
    m_vtable = kVtable;
    DestroyBase(this);
}

// 0x004E2CF0
CFlipRotateComponent* CFlipRotateComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x004E2C90
void CFlipRotateComponent::InitFromStructure(Script::CStruct* params)
{
    InitBaseFromStructure(this, params);
    int value;
    if (params->GetInteger(kFlipped, &value, 0))
        m_flipped = value != 0;
    if (params->GetInteger(kRotated, &value, 0))
        m_rotated = value != 0;
}

// 0x004E2D20
bool CFlipRotateComponent::GetPose(uint8_t* pose)
{
    // The controller's skeleton and object (the base controller's; not rewritten yet).
    uint8_t* skeleton = reinterpret_cast<Gfx::CAnimController*>(this)->GetSkeleton();
    void* bones = *reinterpret_cast<void**>(skeleton + 8);
    auto* object = reinterpret_cast<Gfx::CAnimController*>(this)->GetObject();
    CBaseComponent* flipAndRotate = object->GetComponent(0x488AF07A);
    if (flipAndRotate && reinterpret_cast<uint8_t*>(flipAndRotate)[0x18]) {
        int bone = reinterpret_cast<Gfx::CSkeleton*>(skeleton)->GetBoneIndex(0x98971FAF);
        if (bone != -1) {
            auto* rotation = reinterpret_cast<float*>(pose + 0x10 * bone);
            const float turn[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            float product[4];
            const float* result = reinterpret_cast<const float*>(QuatProduct(reinterpret_cast<Mth::Vector*>(product),
                                                                             reinterpret_cast<const Mth::Vector*>(turn),
                                                                             reinterpret_cast<const Mth::Vector*>(rotation)));
            std::memcpy(rotation, result, 16);
            reinterpret_cast<Mth::Quat*>(rotation)->Normalize(1.0f); // (to length 1)
        }
    }
    if (!m_flipped)
        return true;
    // The pose as it was (rotations past the bone count: no turn), then each bone from its mirror image.
    float rotations[64][4], translations[64][4];
    for (auto& r : rotations) {
        r[0] = r[1] = r[2] = 0.0f;
        r[3] = 1.0f;
    }
    int count = reinterpret_cast<int(__thiscall*)(void*)>(CodeAddress(&Gfx::CSkeleton::GetNumBones))(skeleton);
    std::memcpy(rotations, pose, 0x10 * (count > 0 ? count : 0));
    std::memcpy(translations, pose + 0x400, 0x10 * (count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) {
        int mirror = static_cast<Gfx::CSkeletonData*>(bones)->GetFlipIndex(i);
        int from = mirror == -1 ? i : mirror;
        auto* rotation = reinterpret_cast<float*>(pose + 0x10 * i);
        auto* translation = reinterpret_cast<float*>(pose + 0x400 + 0x10 * i);
        std::memcpy(rotation, rotations[from], 16);
        std::memcpy(translation, translations[from], 16);
        rotation[0] = -rotation[0];
        rotation[3] = -rotation[3];
        translation[0] = -translation[0];
    }
    return true;
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CFlipRotateComponent::Destroy,
         &Obj::CFlipRotateComponent::GetPose,
         Stubs::IgnoreArgument,
         &Obj::ComponentWithoutMemberFunctions::CallMemberFunction,
         &Obj::CFlipRotateComponent::InitFromStructure,
         Stubs::DoNothing};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
