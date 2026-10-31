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
#include "game/script/param_struct.h"
#include "game/sys/game_clock.h"
#include "game/obj/control_pad.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/obj/camera_lookaround.h"

namespace MenuSystem {

// 0x0041E2A0
CUiTree* __cdecl GetScreenElementManager() { return CUiTree::Sole(); }

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
    return params ? params->PrepareAll() : nullptr;
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
CObjectModule* __cdecl CFloatingTextComponent::Fabricate()
{
    auto* component = static_cast<CFloatingTextComponent*>(MemSys::AllocZeroed(sizeof(CFloatingTextComponent)));
    if (!component)
        return nullptr;
    component->CObjectModule::MakeNew();
    component->m_text[0] = 0;
    component->m_ident = 0;
    component->m_endTime = 0;
    component->m_vtable = kVtable;
    component->m_kindTag = kType;
    component->m_tint = 2;
    component->m_yOffset = 120.0f;
    return component;
}

// 0x0041E5E0
void CFloatingTextComponent::DestroyBody()
{
    m_vtable = kVtable;
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kId, m_ident);
    Scripts::LaunchScript(kDestroyScript, params, nullptr, false, nullptr);
    DeleteParams(params);
    CObjectModule::DestroyBody();
}

// 0x0041E6C0
CFloatingTextComponent* CFloatingTextComponent::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x0041E2B0
void CFloatingTextComponent::ConfigureFromData(Scripts::CStruct* params)
{
    if (!m_ident)
        m_ident = m_entity->m_ident - kIdBase;
    const char* text;
    if (params->FetchText(kString, &text, 0)) {
        if (!text[0]) {
            m_endTime = static_cast<uint32_t>(FrameClock::GameClockElapsed() + g_clockOffset);
        } else {
            CopyText(m_text, text, sizeof(m_text)); // (the original: the game's strncpy, 0x006239E0)
            m_endTime = 0;
        }
    }
    params->FetchIntValue(kColor, &m_tint, 0);
    params->FetchFloat(kYOffset, &m_yOffset, 0);
    params->ReadHashValue(kElementId, &m_ident, 0);
    if (params->HasEntryNamed(kDuration)) {
        float duration;
        params->FetchFloat(kDuration, &duration, 0);
        int elapsed = FrameClock::GameClockElapsed();
        int milliseconds = Truncated(static_cast<double>(duration) * kMinusMilliseconds);
        m_endTime = static_cast<uint32_t>(elapsed + (g_clockOffset - milliseconds));
    }
}

// 0x0041E3A0
void CFloatingTextComponent::Seal() { m_meshInstance = m_entity->FetchModule(kModel); }

// 0x0041E450
void CFloatingTextComponent::TickFrame()
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
    params->PutNameKey(kId, m_ident);
    // (the original: the game's sprintf, 0x006234FE, into 64 bytes - a text of more than about 60 characters overran
    // them into the stack cookie, ending the game; here it's cut short)
    char text[0x40];
    std::snprintf(text, sizeof(text), "\\c%d%s", m_tint, m_text);
    params->AppendWord(kText, text);
    const Maths::Vec4& pos = m_entity->m_atPoint;
    params->PutVec3(kPos3D, pos.x, pos.y + m_yOffset, pos.z);
    MenuSystem::ElementPtr found;
    MenuSystem::CUiTree::Sole()->FetchPart(&found, m_ident, 0);
    MenuSystem::CUiWidget* element = found.Pull();
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

static VtableSlot kVtable_slots[] = {&GameObjects::CFloatingTextComponent::Teardown,
         &GameObjects::CFloatingTextComponent::TickFrame,
         &GameObjects::CFloatingTextComponent::ConfigureFromData,
         &GameObjects::CFloatingTextComponent::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CFloatingTextComponent::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
