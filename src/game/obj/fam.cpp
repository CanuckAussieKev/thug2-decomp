// The fam component (Obj::CFamComponent): see fam.h. From the disassembly of 0x0041D170-0x0041D6D4.

#include "game/obj/fam.h"

#include <cstdint>
#include <cstdio>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/obj/anim_blender.h"
#include "game/pcm/music_api.h"
#include "game/pcm/stream_commands.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "hook/game_code.h"
#include "game/obj/camera_lookaround.h"

namespace Obj {

GAME_GLOBAL(uint32_t[18], g_famMemberFunctions, 0x006836E8,
            0xE5894C80, 0x0301D67C, 0, 0, 0, 0,
            0x48AC2139, 0x0301D67C, 0, 0, 0, 0,
            0, 0x0301D67C, 0, 0, 0, 0);

namespace {

extern void** const kVtable;

constexpr uint32_t kFamScript = 0x074253BB;      // fam_play_stream
constexpr uint32_t kStreamChecksum = 0x0572EA28; // stream_checksum
constexpr uint32_t kVol = 0xF6A36814;            // vol
constexpr uint32_t kPriority = 0x9D5923D8;       // priority
constexpr uint32_t kUsePosInfo = 0xC9B0DEB7;     // use_pos_info
constexpr uint32_t kStreamId = 0x8A68AB90;       // streamId
constexpr uint32_t kPlayAnim = 0x93516575;       // play_anim
constexpr uint32_t kTypeParam = 0x7321A8D6;      // type
constexpr uint32_t kId = 0x40C698AF;             // id
constexpr uint32_t kPartialAnim = 0x659BF355;    // partialAnim
constexpr uint32_t kJawRotation = 0xC0BA0665;    // jawRotation
constexpr uint32_t kRemoveAnimController = 0x986D274E; // RemoveAnimController
constexpr int kDefaultPriority = 0x66;

Ass::CAssMan* AssetManager() { return Ass::g_assetManager; }

Script::CStruct* NewParams()
{
    Script::CStruct* params = Script::NewStruct();
    return params ? params->Init() : nullptr;
}
void DeleteParams(Script::CStruct* params)
{
    if (params) {
        params->Clear(); // (the destructor)
        Script::DeleteStruct(params);
    }
}

} // namespace

// 0x0041D200
CBaseComponent* __cdecl CFamComponent::Create()
{
    auto* component = static_cast<CFamComponent*>(Mem::Calloc(sizeof(CFamComponent)));
    if (!component)
        return nullptr;
    component->CBaseComponent::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->m_fam = 0;
    component->m_stream = 0;
    return component;
}

// 0x0041D170
void __cdecl CFamComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_famMemberFunctions,
                                        0x18);
}

// 0x0041D190
void CFamComponent::DestroyBody()
{
    m_vtable = kVtable;
    CBaseComponent::DestroyBody();
}

// 0x0041D1A0
CFamComponent* CFamComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x0041D2C0
void CFamComponent::UnloadFam()
{
    if (!m_fam)
        return;
    Script::StopScriptsUsingObjectThatReferTo(m_object, kFamScript);
    Script::CStruct* params = NewParams();
    params->AddChecksum(kTypeParam, kPartialAnim);
    params->AddChecksum(kId, kJawRotation);
    if (m_object)
        m_object->GetComponent(CAnimBlenderComponent::kType)->CallMemberFunction(kRemoveAnimController, params, nullptr);
    DeleteParams(params);
    Ass::CAssMan* assets = AssetManager();
    if (Ass::CAsset* asset = assets->GetAsset(m_fam, 0))
        assets->UnloadAsset(asset);
    m_fam = 0;
}

// 0x0041D3E0
void CFamComponent::LoadFam(uint32_t name, uint32_t animSet)
{
    UnloadFam();
    char fileName[32];
    std::snprintf(fileName, sizeof(fileName), "fam\\%08x.fam", name); // (the original: the game's sprintf, 0x006234FE)
    Dbg::Printf("loading %s\n", fileName);
    Ass::CAssMan* assets = AssetManager();
    int saved = assets->m_unknown18;
    if (assets->LoadAnim(fileName, name, animSet, false, false)) {
        Dbg::Printf("loaded\n");
        m_fam = Crc::GenerateCRCFromString(fileName);
    }
    assets->SetUnknown18(saved);
}

// 0x0041D480
bool CFamComponent::PlayFam(uint32_t name, bool streamOnly, int volume, int priority)
{
    if (!Pcm::StreamExists(name))
        return false;
    if (m_stream) {
        Script::StopScriptsUsingObjectThatReferTo(m_object, kFamScript);
        Pcm::StopStream(m_stream);
        m_stream = 0;
    }
    Script::CStruct* params = NewParams();
    params->AddChecksum(kStreamChecksum, name);
    if (volume)
        params->AddInteger(kVol, volume);
    if (!streamOnly) {
        params->AddInteger(kUsePosInfo, 0);
        auto* blender = static_cast<CAnimBlenderComponent*>(m_object->GetComponent(CAnimBlenderComponent::kType));
        uint32_t animSet = blender->GetAnimSet();
        LoadFam(name, animSet);
        uint32_t stream = Pcm::PreLoadStream(name, priority);
        params->AddChecksum(kStreamId, stream);
        m_stream = stream;
        if (AssetManager()->GetAssetData(animSet + name, 0))
            params->AddChecksum(0u, kPlayAnim);
    }
    Script::CScript* script = Script::SpawnScript(kFamScript, params, 0, nullptr, 0, 0, false, false, false, false);
    script->m_object.Set(m_object);
    DeleteParams(params);
    return true;
}

// 0x0041D660
int CFamComponent::Obj_PlayFam(Script::CStruct* params, Script::CScript*)
{
    uint32_t name;
    params->GetChecksum(0u, &name, 1);
    int volume = 0;
    params->GetInteger(kVol, &volume, 0);
    int priority = kDefaultPriority;
    params->GetInteger(kPriority, &priority, 0);
    return PlayFam(name, false, volume, priority) ? 1 : 0;
}

// 0x0041D280
int CFamComponent::Obj_StopLastFam(Script::CStruct*, Script::CScript*)
{
    if (m_stream) {
        Script::StopScriptsUsingObjectThatReferTo(m_object, kFamScript);
        Pcm::StopStream(m_stream);
        m_stream = 0;
    }
    return 1;
}

} // namespace Obj

REPLACE(0x0041D200, Obj::CFamComponent::Create);
REPLACE(0x0041D170, Obj::CFamComponent::RegisterMemberFunctions);
REPLACE(0x0041D190, &Obj::CFamComponent::DestroyBody);
REPLACE(0x0041D1A0, &Obj::CFamComponent::Destroy);
REPLACE(0x0041D2C0, &Obj::CFamComponent::UnloadFam);
REPLACE(0x0041D3E0, &Obj::CFamComponent::LoadFam);
REPLACE(0x0041D480, &Obj::CFamComponent::PlayFam);
REPLACE(0x0041D660, &Obj::CFamComponent::Obj_PlayFam);
REPLACE(0x0041D280, &Obj::CFamComponent::Obj_StopLastFam);

namespace Obj {
namespace {

GAME_VTABLE(kVtable, 0x00645F8C,
    &Obj::CFamComponent::Destroy,
    &Obj::ComponentWithoutUpdate::Update,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::DoNothing,
    Obj::RefreshFromStructure,
    Stubs::IgnoreArguments2,
    &Obj::CBaseComponent::Suspend,
    Stubs::IgnoreArgument,
    Stubs::DoNothing,
    Stubs::IgnoreArgument,
    &Obj::CBaseComponent::RunMemberFunction,
    &Obj::ComponentWithoutMemberFunctions::CallMemberFunction);

} // namespace
} // namespace Obj
