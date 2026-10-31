// The fam component (GameObjects::CFamComponent): see fam.h. From the disassembly of 0x0041D170-0x0041D6D4.

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
#include "game/core/code_address.h"
#include "game/skater/skitch.h"
#include "game/core/stubs.h"
#include "game/obj/car_physics.h"
#include "game/obj/node_components.h"
#include "game/obj/object.h"
#include "game/skater/vehicle_sound.h"
#include "game/obj/camera_lookaround.h"

namespace GameObjects {

uint32_t g_famMemberFunctions[18]{
            0xE5894C80, 0x0301D67C, 0, 0, 0, 0,
            0x48AC2139, 0x0301D67C, 0, 0, 0, 0,
            0, 0x0301D67C, 0, 0, 0, 0};

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

AssetStore::CResourceRegistry* AssetManager() { return AssetStore::g_assetManager; }

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

// 0x0041D200
CObjectModule* __cdecl CFamComponent::Create()
{
    auto* component = static_cast<CFamComponent*>(MemSys::AllocZeroed(sizeof(CFamComponent)));
    if (!component)
        return nullptr;
    component->CObjectModule::Construct();
    component->m_vtable = kVtable;
    component->m_type = kType;
    component->m_fam = 0;
    component->m_feed = 0;
    return component;
}

// 0x0041D170
void __cdecl CFamComponent::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_famMemberFunctions,
                                        0x18);
}

// 0x0041D190
void CFamComponent::DestroyBody()
{
    m_vtable = kVtable;
    CObjectModule::DestroyBody();
}

// 0x0041D1A0
CFamComponent* CFamComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Manager::Get()->Free(this);
    return this;
}

// 0x0041D2C0
void CFamComponent::UnloadFam()
{
    if (!m_fam)
        return;
    Scripts::StopScriptsUsingObjectThatReferTo(m_object, kFamScript);
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kTypeParam, kPartialAnim);
    params->PutNameKey(kId, kJawRotation);
    if (m_object)
        m_object->GetComponent(CAnimBlenderComponent::kType)->InvokeMethodByName(kRemoveAnimController, params, nullptr);
    DeleteParams(params);
    AssetStore::CResourceRegistry* assets = AssetManager();
    if (AssetStore::CResource* asset = assets->ObtainLoadedItem(m_fam, 0))
        assets->DiscardLoadedItem(asset);
    m_fam = 0;
}

// 0x0041D3E0
void CFamComponent::LoadAnimBank(uint32_t name, uint32_t animSet)
{
    UnloadFam();
    char fileName[32];
    std::snprintf(fileName, sizeof(fileName), "fam\\%08x.fam", name); // (the original: the game's sprintf, 0x006234FE)
    DebugOut::Printf("loading %s\n", fileName);
    AssetStore::CResourceRegistry* assets = AssetManager();
    int saved = assets->m_unknown18;
    if (assets->LoadAnim(fileName, name, animSet, false, false)) {
        DebugOut::Printf("loaded\n");
        m_fam = Checksums::HashFromText(fileName);
    }
    assets->SetUnknown18(saved);
}

// 0x0041D480
bool CFamComponent::PlayFam(uint32_t name, bool streamOnly, int volume, int priority)
{
    if (!AudioStream::StreamExists(name))
        return false;
    if (m_feed) {
        Scripts::StopScriptsUsingObjectThatReferTo(m_object, kFamScript);
        AudioStream::StopStream(m_feed);
        m_feed = 0;
    }
    Scripts::CStruct* params = NewParams();
    params->PutNameKey(kStreamChecksum, name);
    if (volume)
        params->PutInt(kVol, volume);
    if (!streamOnly) {
        params->PutInt(kUsePosInfo, 0);
        auto* blender = static_cast<CAnimBlenderComponent*>(m_object->GetComponent(CAnimBlenderComponent::kType));
        uint32_t animSet = blender->GetAnimSet();
        LoadAnimBank(name, animSet);
        uint32_t stream = AudioStream::PrimeStream(name, priority);
        params->PutNameKey(kStreamId, stream);
        m_feed = stream;
        if (AssetManager()->GetAssetData(animSet + name, 0))
            params->PutNameKey(0u, kPlayAnim);
    }
    Scripts::CScript* script = Scripts::SpawnScript(kFamScript, params, 0, nullptr, 0, 0, false, false, false, false);
    script->m_object.Set(m_object);
    DeleteParams(params);
    return true;
}

// 0x0041D660
int CFamComponent::Obj_PlayFam(Scripts::CStruct* params, Scripts::CScript*)
{
    uint32_t name;
    params->ReadHashValue(0u, &name, 1);
    int volume = 0;
    params->FetchIntValue(kVol, &volume, 0);
    int priority = kDefaultPriority;
    params->FetchIntValue(kPriority, &priority, 0);
    return PlayFam(name, false, volume, priority) ? 1 : 0;
}

// 0x0041D280
int CFamComponent::Obj_StopLastFam(Scripts::CStruct*, Scripts::CScript*)
{
    if (m_feed) {
        Scripts::StopScriptsUsingObjectThatReferTo(m_object, kFamScript);
        AudioStream::StopStream(m_feed);
        m_feed = 0;
    }
    return 1;
}

} // namespace Obj

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&GameObjects::CFamComponent::Destroy,
         &GameObjects::ComponentWithoutUpdate::Update,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::DoNothing,
         GameObjects::ReapplyFromData,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::Suspend,
         Stubs::IgnoreArgument,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::RunMemberFunction,
         &GameObjects::ComponentWithoutMemberFunctions::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
