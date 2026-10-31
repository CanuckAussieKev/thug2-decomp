// Helpers of the models and animations code (see model_utils.h). From the disassembly of 0x00487770-0x00487D54.

#include "game/gfx/model_utils.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/text_store.h"
#include "game/gfx/model.h"
#include "game/maths/vec4.h"
#include "game/obj/compositemgr.h"
#include "game/obj/model_component.h"
#include "game/obj/entity.h"
#include "game/script/param_struct.h"

namespace Gfx2D {

namespace {

using Scripts::CStruct;

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
    if (!params->ReadHashValue(name, &which, 0))
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
    if (!params->FetchFloat(name, &value, 0))
        return;
    if (params->HasBareFlag(kSeconds))
        *time = value;
    else
        *time = static_cast<float>(Ftol(value) * static_cast<double>(kSixtieth));
}

// A model component's object's id and its model.
uint32_t ObjectId(GameObjects::CMeshInstanceModule* component)
{
    return reinterpret_cast<GameObjects::CEntity*>(component->m_entity)->m_ident;
}
bool Hideable(GameObjects::CMeshInstanceModule* component)
{
    return ObjectId(component) && !(component->m_meshInstance->m_flagBits & Engine3D::kModelHidden);
}

} // namespace

// 0x00487770
bool __cdecl GetAnimLoopType(int* loopType, CStruct* params)
{
    *loopType = 0;
    if (params->HasBareFlag(kCycle)) {
        *loopType = 1;
        return true;
    }
    if (params->HasBareFlag(kPingPong)) {
        *loopType = 2;
        return true;
    }
    if (params->HasBareFlag(kWobble))
        *loopType = 3;
    return true;
}

// 0x004877E0
bool __cdecl FetchAnimDurations(float* start, float* end, float current, float duration, CStruct* params)
{
    float from = 0.0f;
    float to = duration;
    NamedTime(params, kFrom, current, duration, &from);
    NamedTime(params, kTo, current, duration, &to);
    NumberTime(params, kFrom, &from);
    NumberTime(params, kTo, &to);
    if (params->HasBareFlag(kBackwards)) {
        *start = to;
        *end = from;
    } else {
        *start = from;
        *end = to;
    }
    return true;
}

// 0x004879D0
TextUtil::Chars* __cdecl MakeModelPath(TextUtil::Chars* out, const char* name, const char* extension)
{
    char path[512];
    MakeModelFileName(name, extension, path);
    return new (out) TextUtil::Chars(path);
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
bool __cdecl ScaleFromArgs(Maths::Vec4* scale, CStruct* params)
{
    if (params->HasEntryNamed(kScale)) {
        float uniform;
        if (params->FetchFloat(kScale, &uniform, 0)) {
            scale->x = uniform;
            scale->y = uniform;
            scale->z = uniform;
            scale->w = 1.0f;
            return true;
        }
        if (params->FetchVec(kScale, reinterpret_cast<Scripts::VectorValue*>(scale), 0))
            return true;
    }
    bool x = params->FetchFloat(kX, &scale->x, 0);
    bool y = params->FetchFloat(kY, &scale->y, 0);
    bool z = params->FetchFloat(kZ, &scale->z, 0);
    return x || y || z;
}

// 0x00487BD0
void SHiddenModels::Conceal()
{
    auto* manager = GameObjects::CompositeObjectManager();
    auto first = [manager] {
        return static_cast<GameObjects::CMeshInstanceModule*>(manager->FindFirstModuleOfKind(GameObjects::CMeshInstanceModule::kType));
    };
    int count = 0;
    for (auto* component = first(); component; component = static_cast<GameObjects::CMeshInstanceModule*>(component->mp_nextOfType)) {
        if (Hideable(component))
            ++count;
    }
    if (!count)
        return;
    if (!MemSys::g_managerReady)
        MemSys::EnsureManager();
    mp_ids = static_cast<uint32_t*>(MemSys::Hub::Pull()->Carve(count * 4, 1, nullptr));
    m_num = 0;
    for (auto* component = first(); component; component = static_cast<GameObjects::CMeshInstanceModule*>(component->mp_nextOfType)) {
        if (Hideable(component)) {
            mp_ids[m_num] = ObjectId(component);
            component->m_meshInstance->Conceal(1);
            ++m_num;
        }
    }
    DebugOut::Printf("Hid %d objects\n", m_num);
}

// 0x00487CB0
void SHiddenModels::Show()
{
    if (mp_ids) {
        for (int i = 0; i < m_num; ++i) {
            if (auto* object = static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(mp_ids[i]))) {
                auto* component = static_cast<GameObjects::CMeshInstanceModule*>(object->FetchModule(GameObjects::CMeshInstanceModule::kType));
                component->m_meshInstance->Conceal(0);
            }
        }
        MemSys::Hub::Pull()->Dealloc(mp_ids);
        mp_ids = nullptr;
    }
    m_num = 0;
}

// The models hidden for a cutscene (the original's one instance).
SHiddenModels g_hiddenModels;

// 0x00487D20
void __cdecl HideModelsForCutscene() { g_hiddenModels.Conceal(); }

// 0x00487D30
void __cdecl ShowModelsAfterCutscene() { g_hiddenModels.Show(); }

// 0x00487D50
__declspec(naked) void VirtualSlot4Jump()
{
    __asm {
        mov eax, dword ptr [ecx]
        jmp dword ptr [eax + 0x10]
    }
}

} // namespace Gfx
