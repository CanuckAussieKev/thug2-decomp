// The FlipRotate component (Obj::CFlipRotateComponent): construction, destruction and its parameters.

#include "game/obj/flip_rotate.h"

#include "game/core/manager.h"
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

} // namespace Obj

REPLACE(0x004E2C50, &Obj::CFlipRotateComponent::Construct);
REPLACE(0x004E2C80, &Obj::CFlipRotateComponent::DestroyBody);
REPLACE(0x004E2CF0, &Obj::CFlipRotateComponent::Destroy);
REPLACE(0x004E2C90, &Obj::CFlipRotateComponent::InitFromStructure);
