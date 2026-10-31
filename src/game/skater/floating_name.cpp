// The skater's floating name component (GameObjects::CRiderOverheadTag): see floating_name.h.

#include "game/skater/floating_name.h"

#include <cstdint>
#include <cstdio>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skater/rider.h"
#include "game/skate/game_mode.h"
#include "game/core/code_address.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/entity.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

namespace {

extern void** const kVtable;
constexpr uint32_t kSkaterFloatingName = 0x125044E2;
constexpr uint32_t kId = 0x40C698AF, kText = 0xC4745838, kPos3D = 0x4B491900;
constexpr uint32_t kCreateObjectLabel = 0x6A060CF0, kDestroyObjectLabel = 0x2575B406;

// skater_name_0 ... skater_name_7
constexpr uint32_t kLabelIds[8] = {0xE797A186, 0x90909110, 0x0999C0AA, 0x7E9EF03C,
                                   0xE0FA659F, 0x97FD5509, 0x0EF404B3, 0x79F33425};

constexpr float kLocalHeight = 96.0f;  // above this machine's player's skater
constexpr float kOtherHeight = 120.0f; // above the others'

// The network manager (0x007CCE14): a network game on, its show_names preference on, the player with an object id,
// this machine's player (FetchViewedPlayer: its +0x1CC).
OnlinePlay::Hub* NetSessionSlot() { return OnlinePlay::g_onlineHub; }
bool InNetGame() { return NetSessionSlot()->InNetGame(); }
bool ShowNames() { return NetSessionSlot()->WantsNameTags(); }
uint8_t* FindPlayerSlotByObj(uint32_t id) { return NetSessionSlot()->FindPlayerSlotByObj(static_cast<uint16_t>(id)); }
uint8_t* FindOwnPlayerRecord() { return static_cast<uint8_t*>(NetSessionSlot()->FetchViewedPlayer()); }
// The game mode (0x007CE470)'s 0x005535F0: a team game. Not rewritten yet.
bool IsSquadMatch() { return static_cast<GoalSystem::CMatchRules*>(static_cast<void*>(GoalSystem::g_gameMode))->IsSquadMatch() != 0; }

// `name` run with a structure holding only id = `labelId`.
void RunWithLabelId(uint32_t name, uint32_t labelId)
{
    Scripts::CStruct* params = Scripts::SpawnParamBag();
    if (params)
        params->PrepareAll();
    params->PutNameKey(kId, labelId);
    Scripts::LaunchScript(name, params, nullptr, false, nullptr);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace

// 0x00511810
CObjectModule* __cdecl CRiderOverheadTag::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CRiderOverheadTag));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CRiderOverheadTag*>(memory);
    component->CObjectModule::MakeNew();
    component->m_vtable = kVtable;
    component->m_kindTag = kSkaterFloatingName;
    return component;
}

// 0x00511AA0
void CRiderOverheadTag::DestroyBody()
{
    m_vtable = kVtable;
    RunWithLabelId(kDestroyObjectLabel, m_labelId);
    CObjectModule::DestroyBody();
}

// 0x00511B80
CRiderOverheadTag* CRiderOverheadTag::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00511780
void CRiderOverheadTag::ConfigureFromData(Scripts::CStruct* /*params*/)
{
    uint32_t id = m_entity->m_ident;
    if (id < 8)
        m_labelId = kLabelIds[id];
}

// 0x00511870
void CRiderOverheadTag::TickFrame()
{
    if (!InNetGame() || !ShowNames()) {
        VirtualSuspend(true);
        return;
    }
    MenuSystem::CUiWidget* label;
    {
        MenuSystem::ElementPtr found;
        MenuSystem::CUiTree::Sole()->FetchPart(&found, m_labelId, 0);
        label = found.Pull();
    }
    uint8_t* player = FindPlayerSlotByObj(m_entity->m_ident);
    auto* skater = static_cast<CRider*>(m_entity);
    if (!skater->GetFlag2C4() || (m_entity->m_compositeFlags & COMPOSITE_FLAG_HIDDEN)) {
        if (label)
            RunWithLabelId(kDestroyObjectLabel, m_labelId);
        return;
    }
    float height = FindOwnPlayerRecord() == player ? kLocalHeight : kOtherHeight;
    Scripts::CStruct* params = Scripts::SpawnParamBag();
    if (params)
        params->PrepareAll();
    params->PutNameKey(kId, m_labelId);
    // The text's colour: the team's (+0x30 of the player) in a team game, else the skater's own.
    uint32_t colour = IsSquadMatch() ? *reinterpret_cast<uint32_t*>(player + 0x30) : m_entity->m_ident;
    char text[64];
    std::snprintf(text, sizeof(text), "\\c%d%s", static_cast<int>(colour + 2), skater->FetchShownName());
    params->AppendWord(kText, text);
    const Maths::Vec4& pos = m_entity->m_atPoint;
    params->PutVec3(kPos3D, pos.x, height + pos.y, pos.z);
    if (label) // (its vtable's ChangeTraits: whatever object has the id)
        reinterpret_cast<void(__thiscall*)(MenuSystem::CUiWidget*, Scripts::CStruct*)>(label->m_vtable[1])(label, params);
    else
        Scripts::LaunchScript(kCreateObjectLabel, params, nullptr, false, nullptr);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CRiderOverheadTag::Teardown,
         &GameObjects::CRiderOverheadTag::TickFrame,
         &GameObjects::CRiderOverheadTag::ConfigureFromData,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         static_cast<int (GameObjects::CObjectModule::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CObjectModule::InvokeMethodByName),
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
