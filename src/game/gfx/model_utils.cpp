// Helpers of the models and animations code (see model_utils.h). From the disassembly of 0x00487770-0x00487D54.

#include "game/gfx/model_utils.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/string.h"
#include "game/gfx/model.h"
#include "game/mth/vector.h"
#include "game/obj/compositemgr.h"
#include "game/obj/model_component.h"
#include "game/obj/object.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Gfx {

namespace {

using Script::CStruct;

constexpr uint32_t kCycle = 0x4F792E6C;    // cycle
constexpr uint32_t kPingPong = 0x3153E314; // pingpong
constexpr uint32_t kWobble = 0x6D941203;   // wobble
constexpr uint32_t kFrom = 0x46E55E8F;     // from
constexpr uint32_t kTo = 0x28782D3B;       // to
constexpr uint32_t kStart = 0x6086AA70;    // start
constexpr uint32_t kMiddle = 0x617FE530;   // middle
constexpr uint32_t kCurrent = 0x230CCBF4;  // current
constexpr uint32_t kEnd = 0xFF03CC4E;      // end
constexpr uint32_t kSeconds = 0xD029F619;  // seconds
constexpr uint32_t kBackwards = 0xF8CFD515; // backwards
constexpr uint32_t kScale = 0x13B9DA7B;    // scale
constexpr uint32_t kX = 0x7323E97C;        // x
constexpr uint32_t kY = 0x0424D9EA;        // y
constexpr uint32_t kZ = 0x9D2D8850;        // z
constexpr float kSixtieth = 0x1.111112p-6f; // 0x006463B8

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int Ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A time given by name (others leave `time` as it is).
void NamedTime(CStruct* params, uint32_t name, float current, float duration, float* time)
{
    uint32_t which = 0;
    if (!params->GetChecksum(name, &which, 0))
        return;
    if (which == kMiddle)
        *time = duration * 0.5f;
    else if (which == kCurrent)
        *time = current;
    else if (which == kStart)
        *time = 0.0f;
    else if (which == kEnd)
        *time = duration;
}

// A time given as a number: seconds, or frames (whole ones, at 60 a second).
void NumberTime(CStruct* params, uint32_t name, float* time)
{
    float value = 0.0f;
    if (!params->GetFloat(name, &value, 0))
        return;
    if (params->ContainsFlag(kSeconds))
        *time = value;
    else
        *time = static_cast<float>(Ftol(value) * static_cast<double>(kSixtieth));
}

// A model component's object's id and its model.
uint32_t ObjectId(Obj::CModelComponent* component)
{
    return reinterpret_cast<Obj::CObject*>(component->m_object)->m_id;
}
bool Hideable(Obj::CModelComponent* component)
{
    return ObjectId(component) && !(component->mp_model->m_flags & Nx::kModelHidden);
}

} // namespace

// 0x00487770
bool __cdecl GetAnimLoopType(int* loopType, CStruct* params)
{
    *loopType = 0;
    if (params->ContainsFlag(kCycle)) {
        *loopType = 1;
        return true;
    }
    if (params->ContainsFlag(kPingPong)) {
        *loopType = 2;
        return true;
    }
    if (params->ContainsFlag(kWobble))
        *loopType = 3;
    return true;
}

// 0x004877E0
bool __cdecl GetAnimTimes(float* start, float* end, float current, float duration, CStruct* params)
{
    float from = 0.0f;
    float to = duration;
    NamedTime(params, kFrom, current, duration, &from);
    NamedTime(params, kTo, current, duration, &to);
    NumberTime(params, kFrom, &from);
    NumberTime(params, kTo, &to);
    if (params->ContainsFlag(kBackwards)) {
        *start = to;
        *end = from;
    } else {
        *start = from;
        *end = to;
    }
    return true;
}

// 0x004879D0
Str::String* __cdecl MakeModelPath(Str::String* out, const char* name, const char* extension)
{
    char path[512];
    MakeModelFileName(name, extension, path);
    return new (out) Str::String(path);
}

// 0x00487A70
void __cdecl MakeModelFileName(const char* name, const char* extension, char* out)
{
#pragma warning(push)
#pragma warning(disable : 4996)
    if (std::strstr(name, "/") || std::strstr(name, "\\"))
        std::sprintf(out, "models\\%s", name);
    else
        std::sprintf(out, "models\\%s\\%s%s", name, name, extension);
#pragma warning(pop)
}

// 0x00487AD0
float __cdecl LeastOf(float a, float b, float c)
{
    float least = b < c ? b : c;
    return a < least ? a : least;
}

// 0x00487B00
bool __cdecl GetScaleFromParams(Mth::Vector* scale, CStruct* params)
{
    if (params->ContainsComponentNamed(kScale)) {
        float uniform;
        if (params->GetFloat(kScale, &uniform, 0)) {
            scale->x = uniform;
            scale->y = uniform;
            scale->z = uniform;
            scale->w = 1.0f;
            return true;
        }
        if (params->GetVector(kScale, reinterpret_cast<Script::VectorValue*>(scale), 0))
            return true;
    }
    bool x = params->GetFloat(kX, &scale->x, 0);
    bool y = params->GetFloat(kY, &scale->y, 0);
    bool z = params->GetFloat(kZ, &scale->z, 0);
    return x || y || z;
}

// 0x00487BD0
void SHiddenModels::Hide()
{
    auto* manager = Obj::CompositeObjectManager();
    auto first = [manager] {
        return static_cast<Obj::CModelComponent*>(manager->GetFirstComponentByType(Obj::CModelComponent::kType));
    };
    int count = 0;
    for (auto* component = first(); component; component = static_cast<Obj::CModelComponent*>(component->mp_nextOfType)) {
        if (Hideable(component))
            ++count;
    }
    if (!count)
        return;
    if (!GameVariable<bool>(0x007CC979))
        Mem::EnsureManager();
    mp_ids = static_cast<uint32_t*>(Mem::Manager::Get()->Allocate(count * 4, 1, nullptr));
    m_count = 0;
    for (auto* component = first(); component; component = static_cast<Obj::CModelComponent*>(component->mp_nextOfType)) {
        if (Hideable(component)) {
            mp_ids[m_count] = ObjectId(component);
            component->mp_model->Hide(1);
            ++m_count;
        }
    }
    Dbg::Printf("Hid %d objects\n", m_count);
}

// 0x00487CB0
void SHiddenModels::Show()
{
    if (mp_ids) {
        for (int i = 0; i < m_count; ++i) {
            if (auto* object = static_cast<Obj::CCompositeObject*>(Obj::FindObject(mp_ids[i]))) {
                auto* component = static_cast<Obj::CModelComponent*>(object->GetComponent(Obj::CModelComponent::kType));
                component->mp_model->Hide(0);
            }
        }
        Mem::Manager::Get()->Free(mp_ids);
        mp_ids = nullptr;
    }
    m_count = 0;
}

// 0x00487D20
void __cdecl HideModelsForCutscene() { GameVariable<SHiddenModels>(0x006FA2F0).Hide(); }

// 0x00487D30
void __cdecl ShowModelsAfterCutscene() { GameVariable<SHiddenModels>(0x006FA2F0).Show(); }

// 0x00487D50
__declspec(naked) void VirtualSlot4Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x10]
    }
}

} // namespace Gfx

REPLACE(0x00487770, Gfx::GetAnimLoopType);
REPLACE(0x004877E0, Gfx::GetAnimTimes);
REPLACE(0x004879D0, Gfx::MakeModelPath);
REPLACE(0x00487A70, Gfx::MakeModelFileName);
REPLACE(0x00487AD0, Gfx::LeastOf);
REPLACE(0x00487B00, Gfx::GetScaleFromParams);
REPLACE(0x00487BD0, &Gfx::SHiddenModels::Hide);
REPLACE(0x00487CB0, &Gfx::SHiddenModels::Show);
REPLACE(0x00487D20, Gfx::HideModelsForCutscene);
REPLACE(0x00487D30, Gfx::ShowModelsAfterCutscene);
REPLACE_UNCHECKED(0x00487D50, Gfx::VirtualSlot4Jump); // (a tail jump: the arguments are the slot's)
