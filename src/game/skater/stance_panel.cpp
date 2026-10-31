// The skater's stance panel component (GameObjects::CRiderStanceDisplay): see stance_panel.h.

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

namespace GameObjects {

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
CObjectModule* __cdecl CRiderStanceDisplay::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderStanceDisplay));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderStanceDisplay*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterStancePanel;
    component->m_corePhysics = nullptr;
    return component;
}

// 0x00519130
void CRiderStanceDisplay::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x005191E0
CRiderStanceDisplay* CRiderStanceDisplay::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00519140
void CRiderStanceDisplay::ConfigureFromData(Scripts::CStruct* /*params*/) { m_shown = -1; }

// 0x00519210
void CRiderStanceDisplay::Seal()
{
    m_corePhysics = reinterpret_cast<uint8_t*>(m_entity->FetchModule(kSkaterCorePhysics));
}

// 0x00519150
int CRiderStanceDisplay::GetStance()
{
    auto* core = reinterpret_cast<CObjectModule*>(m_corePhysics);
    uint8_t flip = *(*reinterpret_cast<uint8_t**>(m_corePhysics + 0x1528) + 0x28);
    bool reversed = reinterpret_cast<uint8_t*>(core->m_entity)[0x110] != 0;
    bool flipped = reversed ? flip != 0 : flip == 0;
    if (m_pressAmount)
        return flipped ? 3 : 4;
    if (flipped)
        return m_nollie ? 1 : 2;
    return m_nollie ? 0 : kHidden;
}

// 0x00519230
void CRiderStanceDisplay::TickFrame()
{
    MenuSystem::CUiWidget* panel;
    {
        MenuSystem::ElementPtr found;
        uint32_t id = static_cast<uint32_t>(static_cast<CRider*>(m_entity)->GetSkaterNumber()) - kPanelIdOffset;
        MenuSystem::CUiTree::Sole()->FetchPart(&found, id, 0);
        panel = found.Pull();
    }
    int stance = GetStance();
    if (stance == m_shown)
        return;
    m_shown = stance;
    if (stance == kHidden) {
        panel->ChangeOpacity(0.0f, 1);
        return;
    }
    static_cast<MenuSystem::CImageWidget*>(panel)->SetTexture(StanceTextures()[stance]);
    panel->ChangeOpacity(1.0f, 1);
}

// 0x005192C0
int CRiderStanceDisplay::InvokeMethodByName(uint32_t name, Scripts::CStruct* /*params*/,
                                                    Scripts::CScript* /*script*/)
{
    switch (name) {
    case kNollieOn:
        if (!m_nollie) {
            m_nollie = 1;
            m_entity->Announce(kSkaterEnterNollie, nullptr, 0.0f);
        }
        return 1;
    case kNollieOff:
        if (m_nollie == 1) {
            m_nollie = 0;
            m_entity->Announce(kSkaterExitNollie, nullptr, 0.0f);
        }
        return 1;
    case kInNollie:
        return m_nollie ? 1 : 0;
    case kPressureOn:
        if (!m_pressAmount) {
            m_pressAmount = 1;
            m_entity->Announce(kSkaterEnterPressure, nullptr, 0.0f);
        }
        return 1;
    case kPressureOff:
        if (m_pressAmount == 1) {
            m_pressAmount = 0;
            m_entity->Announce(kSkaterExitPressure, nullptr, 0.0f);
        }
        return 1;
    case kInPressure:
        return m_pressAmount ? 1 : 0;
    default:
        return 2;
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderStanceDisplay::Teardown,
         &GameObjects::CRiderStanceDisplay::TickFrame,
         &GameObjects::CRiderStanceDisplay::ConfigureFromData,
         &GameObjects::CRiderStanceDisplay::Seal,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::CRiderStanceDisplay::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
