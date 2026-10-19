// The FlipRotate component (Obj::CFlipRotateComponent): construction, destruction and its parameters.

#include "game/obj/flip_rotate.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064B1D4);

// The base component (no source yet).
void ConstructBase(CFlipRotateComponent* component, void* object)
{
    GameFunction<void*(__thiscall*)(CFlipRotateComponent*, void*)>(0x00481710)(component, object);
}
void DestroyBase(CFlipRotateComponent* component)
{
    GameFunction<void(__thiscall*)(CFlipRotateComponent*)>(0x00481730)(component);
}
void InitBaseFromStructure(CFlipRotateComponent* component, Script::CStruct* params)
{
    GameFunction<void(__thiscall*)(CFlipRotateComponent*, Script::CStruct*)>(0x00481740)(component, params);
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
    uint8_t* skeleton = GameFunction<uint8_t*(__thiscall*)(void*)>(0x00481770)(this);
    void* bones = *reinterpret_cast<void**>(skeleton + 8);
    auto* object = GameFunction<CCompositeObject*(__thiscall*)(void*)>(0x00481760)(this);
    CBaseComponent* flipAndRotate = object->GetComponent(0x488AF07A);
    if (flipAndRotate && reinterpret_cast<uint8_t*>(flipAndRotate)[0x18]) {
        int bone = GameFunction<int(__thiscall*)(void*, uint32_t)>(0x004A2B60)(skeleton, 0x98971FAF);
        if (bone != -1) {
            auto* rotation = reinterpret_cast<float*>(pose + 0x10 * bone);
            const float turn[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            float product[4];
            const float* result = GameFunction<const float*(__cdecl*)(float*, const float*, const float*)>(0x004354C0)(
                product, turn, rotation);
            std::memcpy(rotation, result, 16);
            GameFunction<void(__thiscall*)(float*, float)>(0x0042E310)(rotation, 1.0f); // (to length 1)
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
    int count = GameFunction<int(__thiscall*)(void*)>(0x004A2BA0)(skeleton);
    std::memcpy(rotations, pose, 0x10 * (count > 0 ? count : 0));
    std::memcpy(translations, pose + 0x400, 0x10 * (count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) {
        int mirror = GameFunction<int(__thiscall*)(void*, int)>(0x004A2C60)(bones, i);
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

REPLACE(0x004E2C50, &Obj::CFlipRotateComponent::Construct);
REPLACE(0x004E2C80, &Obj::CFlipRotateComponent::DestroyBody);
REPLACE(0x004E2CF0, &Obj::CFlipRotateComponent::Destroy);
REPLACE(0x004E2C90, &Obj::CFlipRotateComponent::InitFromStructure);
REPLACE(0x004E2D20, &Obj::CFlipRotateComponent::GetPose);
