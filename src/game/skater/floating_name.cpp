// The skater's floating name component (Obj::CSkaterFloatingNameComponent): see floating_name.h.

#include "game/skater/floating_name.h"

#include <cstdint>
#include <cstdio>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/screen_element_manager.h"
#include "game/net/gamenet_manager.h"
#include "game/obj/smtptr.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/skater/skater.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x0064BE54);
constexpr uint32_t kSkaterFloatingName = 0x125044E2;
constexpr uint32_t kId = 0x40C698AF, kText = 0xC4745838, kPos3D = 0x4B491900;
constexpr uint32_t kCreateObjectLabel = 0x6A060CF0, kDestroyObjectLabel = 0x2575B406;

// skater_name_0 ... skater_name_7
constexpr uint32_t kLabelIds[8] = {0xE797A186, 0x90909110, 0x0999C0AA, 0x7E9EF03C,
                                   0xE0FA659F, 0x97FD5509, 0x0EF404B3, 0x79F33425};

constexpr float kLocalHeight = 96.0f;  // above this machine's player's skater
constexpr float kOtherHeight = 120.0f; // above the others'

// The network manager (0x007CCE14): a network game on, its show_names preference on, the player with an object id,
// this machine's player (GetCamPlayer: its +0x1CC).
GameNet::Manager* NetManager() { return GameVariable<GameNet::Manager*>(0x007CCE14); }
bool InNetGame() { return NetManager()->InNetGame(); }
bool ShowNames() { return NetManager()->ShouldDrawPlayerNames(); }
uint8_t* PlayerByObjectId(uint32_t id) { return NetManager()->PlayerByObjectId(static_cast<uint16_t>(id)); }
uint8_t* LocalPlayer() { return static_cast<uint8_t*>(NetManager()->GetCamPlayer()); }
// The game mode (0x007CE470)'s 0x005535F0: a team game. Not rewritten yet.
bool IsTeamGame() { return GameFunction<bool(__thiscall*)(void*)>(0x005535F0)(GameVariable<void*>(0x007CE470)); }

// `name` run with a structure holding only id = `labelId`.
void RunWithLabelId(uint32_t name, uint32_t labelId)
{
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params->Init();
    params->AddChecksum(kId, labelId);
    Script::RunScript(name, params, nullptr, false, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace

// 0x00511810
CBaseComponent* __cdecl CSkaterFloatingNameComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CSkaterFloatingNameComponent));
    if (!memory)
        return nullptr;
    auto* component = static_cast<CSkaterFloatingNameComponent*>(memory);
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kSkaterFloatingName;
    return component;
}

// 0x00511AA0
void CSkaterFloatingNameComponent::DestroyBody()
{
    m_vtable = kVtable;
    RunWithLabelId(kDestroyObjectLabel, m_labelId);
    CBaseComponent::DestroyBody();
}

// 0x00511B80
CSkaterFloatingNameComponent* CSkaterFloatingNameComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00511780
void CSkaterFloatingNameComponent::InitFromStructure(Script::CStruct* /*params*/)
{
    uint32_t id = m_object->m_id;
    if (id < 8)
        m_labelId = kLabelIds[id];
}

// 0x00511870
void CSkaterFloatingNameComponent::Update()
{
    if (!InNetGame() || !ShowNames()) {
        VirtualSuspend(true);
        return;
    }
    Front::CScreenElement* label;
    {
        Front::ElementPtr found;
        Front::CScreenElementManager::Instance()->GetElement(&found, m_labelId, 0);
        label = found.Get();
    }
    uint8_t* player = PlayerByObjectId(m_object->m_id);
    auto* skater = static_cast<CSkater*>(m_object);
    if (!skater->GetFlag2C4() || (m_object->m_compositeFlags & COMPOSITE_FLAG_HIDDEN)) {
        if (label)
            RunWithLabelId(kDestroyObjectLabel, m_labelId);
        return;
    }
    float height = LocalPlayer() == player ? kLocalHeight : kOtherHeight;
    Script::CStruct* params = Script::NewStruct();
    if (params)
        params->Init();
    params->AddChecksum(kId, m_labelId);
    // The text's colour: the team's (+0x30 of the player) in a team game, else the skater's own.
    uint32_t colour = IsTeamGame() ? *reinterpret_cast<uint32_t*>(player + 0x30) : m_object->m_id;
    char text[64];
    std::snprintf(text, sizeof(text), "\\c%d%s", static_cast<int>(colour + 2), skater->GetDisplayName());
    params->AddString(kText, text);
    const Mth::Vector& pos = m_object->m_pos;
    params->AddVector(kPos3D, pos.x, height + pos.y, pos.z);
    if (label) // (its vtable's SetProperties: whatever object has the id)
        reinterpret_cast<void(__thiscall*)(Front::CScreenElement*, Script::CStruct*)>(label->m_vtable[1])(label, params);
    else
        Script::RunScript(kCreateObjectLabel, params, nullptr, false, nullptr);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

} // namespace Obj

REPLACE(0x00511810, Obj::CSkaterFloatingNameComponent::Create);
REPLACE(0x00511AA0, &Obj::CSkaterFloatingNameComponent::DestroyBody);
REPLACE(0x00511B80, &Obj::CSkaterFloatingNameComponent::Destroy);
REPLACE(0x00511780, &Obj::CSkaterFloatingNameComponent::InitFromStructure);
REPLACE(0x00511870, &Obj::CSkaterFloatingNameComponent::Update);
