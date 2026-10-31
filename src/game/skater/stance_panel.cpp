// The skater's stance panel component (Obj::CSkaterStancePanelComponent): see stance_panel.h.

#include "game/skater/stance_panel.h"

#include <cstdint>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element_manager.h"
#include "game/front/sprite_element.h"
#include "game/obj/smtptr.h"
#include "game/skater/skater.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

uint32_t g_stanceTextures[5]{0x33A15296, 0xBE91F3B6, 0x9F9D3907, 0xB793EF40, 0x4E304FA1};

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterStancePanel = 0x21FC3301, kSkaterCorePhysics = 0x5BD63B29;
constexpr uint32_t kNollieOn = 0x41EB710B, kInNollie = 0x1EB61DCE, kPressureOn = 0x0A23D710,
                   kPressureOff = 0x71B57DD6, kNollieOff = 0xFB9B7C9C, kInPressure = 0x9FAB9D0B;
constexpr uint32_t kSkaterEnterNollie = 0x8157AB31, kSkaterExitNollie = 0x3F70881A,
                   kSkaterEnterPressure = 0x2E8DE921, kSkaterExitPressure = 0xFA9ADB1D;
constexpr uint32_t kPanelIdOffset = 0x69751A23; // the panel's element id: below the skater number
constexpr int kHidden = 5;

// The stances' textures (0x0064C12C).
const uint32_t* StanceTextures() { return g_stanceTextures; }

} // namespace

// 0x005193B0
CBaseComponent* __cdecl CSkaterStancePanelComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterStancePanelComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterStancePanelComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterStancePanel;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x00519130
void CSkaterStancePanelComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x005191E0
CSkaterStancePanelComponent* CSkaterStancePanelComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00519140
void CSkaterStancePanelComponent::InitFromStructure(Script::CStruct* /*params*/) { m_shown = -1; }

// 0x00519210
void CSkaterStancePanelComponent::Finalize()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_object->GetComponent(kSkaterCorePhysics));
}

// 0x00519150
int CSkaterStancePanelComponent::GetStance()
{
    auto* core = reinterpret_cast<CBaseComponent*>(m_corePhysics);
    uint8_t flip = *(*reinterpret_cast<uint8_t**>(m_corePhysics + 0x1528) + 0x28);
    bool reversed = reinterpret_cast<uint8_t*>(core->m_object)[0x110] != 0;
    bool flipped = reversed ? flip != 0 : flip == 0;
    if (m_pressure)
        return flipped ? 3 : 4;
    if (flipped)
        return m_nollie ? 1 : 2;
    return m_nollie ? 0 : kHidden;
}

// 0x00519230
void CSkaterStancePanelComponent::Update()
{
    Front::CScreenElement* panel;
    {
        Front::ElementPtr found;
        uint32_t id = static_cast<uint32_t>(static_cast<CSkater*>(m_object)->GetSkaterNumber()) - kPanelIdOffset;
        Front::CScreenElementManager::Instance()->GetElement(&found, id, 0);
        panel = found.Get();
    }
    int stance = GetStance();
    if (stance == m_shown)
        return;
    m_shown = stance;
    if (stance == kHidden) {
        panel->SetAlpha(0.0f, 1);
        return;
    }
    static_cast<Front::CSpriteElement*>(panel)->SetTexture(StanceTextures()[stance]);
    panel->SetAlpha(1.0f, 1);
}

// 0x005192C0
int CSkaterStancePanelComponent::CallMemberFunction(uint32_t name, Script::CStruct* /*params*/,
                                                    Script::CScript* /*script*/)
{
    switch (name) {
    case kNollieOn:
        if (!m_nollie) {
            m_nollie = 1;
            m_object->BroadcastEvent(kSkaterEnterNollie, nullptr, 0.0f);
        }
        return 1;
    case kNollieOff:
        if (m_nollie == 1) {
            m_nollie = 0;
            m_object->BroadcastEvent(kSkaterExitNollie, nullptr, 0.0f);
        }
        return 1;
    case kInNollie:
        return m_nollie ? 1 : 0;
    case kPressureOn:
        if (!m_pressure) {
            m_pressure = 1;
            m_object->BroadcastEvent(kSkaterEnterPressure, nullptr, 0.0f);
        }
        return 1;
    case kPressureOff:
        if (m_pressure == 1) {
            m_pressure = 0;
            m_object->BroadcastEvent(kSkaterExitPressure, nullptr, 0.0f);
        }
        return 1;
    case kInPressure:
        return m_pressure ? 1 : 0;
    default:
        return 2;
    }
}

} // namespace Obj

namespace Obj {
namespace {

static VtableSlot kVtable_slots[] = {&Obj::CSkaterStancePanelComponent::Destroy,
         &Obj::CSkaterStancePanelComponent::Update,
         &Obj::CSkaterStancePanelComponent::InitFromStructure,
         &Obj::CSkaterStancePanelComponent::Finalize,
         Stubs::DoNothing,
         Obj::RefreshFromStructure,
         Stubs::IgnoreArguments2,
         &Obj::CBaseComponent::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (Obj::CBaseComponent::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CBaseComponent::CallMemberFunction),
         &Obj::CSkaterStancePanelComponent::CallMemberFunction};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
