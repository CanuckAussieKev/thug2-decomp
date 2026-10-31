// The floating text component (GameObjects::CFloatingTextComponent): see floating_text.h. From the disassembly of
// 0x0041E270-0x0041E6DE.

#include "game/obj/floating_text.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element.h"
#include "game/front/screen_element_manager.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace MenuSystem {

// 0x0041E2A0
CUiTree* __cdecl GetScreenElementManager() { return CUiTree::Instance(); }

} // namespace Front

namespace GameObjects {

namespace {

extern void** const kVtable;

constexpr uint32_t kId = 0x40C698AF;        // id
constexpr uint32_t kString = 0x61414D56;    // string
constexpr uint32_t kColor = 0x99A9B716;     // color
constexpr uint32_t kYOffset = 0x14975800;   // y_offset
constexpr uint32_t kElementId = 0x727F9552; // (the element's id)
constexpr uint32_t kDuration = 0x79A07F3F;  // duration
constexpr uint32_t kText = 0xC4745838;      // text
constexpr uint32_t kPos3D = 0x4B491900;     // pos3D
constexpr uint32_t kModel = 0x286A8D26;
constexpr uint32_t kCreateScript = 0x6A060CF0;  // makes the element
constexpr uint32_t kDestroyScript = 0x2575B406; // destroys it
constexpr uint32_t kReinit = 0xE357E3CB;
constexpr uint32_t kIdBase = 0x1525E9DF;    // (subtracted from the object's id)
constexpr float kMinusMilliseconds = -1000.0f; // 0x0064604C

// strncpy: at most `size` characters, the rest of `out` zeroed (not terminated when `in` is that long or longer).
void CopyText(char* out, const char* in, size_t size)
{
    size_t i = 0;
    for (; i < size && in[i]; ++i)
        out[i] = in[i];
    for (; i < size; ++i)
        out[i] = 0;
}

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits of the 64-bit result.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

Scripts::CStruct* NewParams()
{
    Scripts::CStruct* params = Scripts::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(Scripts::CStruct* params)
{
    if (params) {
        params->Clear(); // (the destructor)
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x0041E3C0
CObjectModule* __cdecl CFloatingTextComponent::Create()
{
    auto* component = static_cast<CFloatingTextComponent*>(MemSys::AllocZeroed(sizeof(CFloatingTextComponent)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_text[0] = 0;
    component->m_id = 0;
    component->m_endTime = 0;
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->m_tint = 2;
    component->m_yOffset = 120.0f;
    return component;
}

// 0x0041E5E0
void CFloatingTextComponent::DestroyBody()
{
    m_vtable = kVtable;
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kId, m_id);
    Scripts::LaunchScript(kDestroyScript, params, nullptr, false, nullptr);
    DeleteParams(params);
    CObjectModule::DestroyBody();
}

// 0x0041E6C0
CFloatingTextComponent* CFloatingTextComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0041E2B0
void CFloatingTextComponent::ConfigureFromData(Scripts::CStruct* params)
{
    if (!m_id)
        m_id = m_object->m_id - kIdBase;
    const char* text;
    if (params->GetString(kString, &text, 0)) {
        if (!text[0]) {
            m_endTime = static_cast<uint32_t>(FrameClock::GameClockElapsed() + g_clockOffset);
        } else {
            CopyText(m_text, text, sizeof(m_text)); // (the original: the game's strncpy, 0x006239E0)
            m_endTime = 0;
        }
    }
    params->FetchIntValue(kColor, &m_tint, 0);
    params->GetFloat(kYOffset, &m_yOffset, 0);
    params->ReadHashValue(kElementId, &m_id, 0);
    if (params->HasEntryNamed(kDuration)) {
        float duration;
        params->GetFloat(kDuration, &duration, 0);
        int elapsed = FrameClock::GameClockElapsed();
        int milliseconds = Truncated(static_cast<double>(duration) * kMinusMilliseconds);
        m_endTime = static_cast<uint32_t>(elapsed + (g_clockOffset - milliseconds));
    }
}

// 0x0041E3A0
void CFloatingTextComponent::Finalize() { m_meshInstance = m_object->GetComponent(kModel); }

// 0x0041E450
void CFloatingTextComponent::Update()
{
    uint8_t* model = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(m_meshInstance) + 0x208);
    if (!model || !(model[0x44] & 2) || (model[0x44] & 4))
        return;
    if (!m_text[0])
        return;
    if (m_endTime && m_endTime < static_cast<uint32_t>(FrameClock::GameClockElapsed() + g_clockOffset)) {
        m_text[0] = 0;
        m_endTime = 0;
    }
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kId, m_id);
    // (the original: the game's sprintf, 0x006234FE, into 64 bytes - a text of more than about 60 characters overran
    // them into the stack cookie, ending the game; here it's cut short)
    char text[0x40];
    std::snprintf(text, sizeof(text), "\\c%d%s", m_tint, m_text);
    params->AddString(kText, text);
    const Maths::Vector& pos = m_object->m_pos;
    params->PutVec3(kPos3D, pos.x, pos.y + m_yOffset, pos.z);
    MenuSystem::ElementPtr found;
    MenuSystem::CUiTree::Instance()->GetElement(&found, m_id, 0);
    MenuSystem::CUiWidget* element = found.Get();
    found.Release();
    if (element)
        reinterpret_cast<void(__thiscall*)(MenuSystem::CUiWidget*, Scripts::CStruct*)>(element->m_vtable[1])(element, params);
    else
        Scripts::LaunchScript(kCreateScript, params, nullptr, false, nullptr);
    DeleteParams(params);
}

// 0x0041E270
int CFloatingTextComponent::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    if (name != kReinit)
        return 2;
    reinterpret_cast<void(__thiscall*)(CObjectModule*, Scripts::CStruct*)>(m_vtable[5])(this, params);
    return 1;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CFloatingTextComponent::Destroy,
         &GameObjects::CFloatingTextComponent::Update,
         &GameObjects::CFloatingTextComponent::ConfigureFromData,
         &GameObjects::CFloatingTextComponent::Finalize,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CFloatingTextComponent::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
