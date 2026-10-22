// The model viewer's object (0x005897B0-0x0058ADB0). See viewer_object.h.

#include "game/sys/viewer_object.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/ass/assman.h"
#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/string.h"
#include "game/core/task.h"
#include "game/mth/vector.h"
#include "game/obj/anim_blender.h"
#include "game/obj/compositemgr.h"
#include "game/obj/game_objects.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kViewerObjectVtable = reinterpret_cast<void**>(0x0064F138);
constexpr uint32_t kKillViewerObjectPanel = 0x9E65A71E; // kill_viewer_object_panel
constexpr uint32_t kCasArtist = 0x2A648514;              // cas_artist
constexpr uint32_t kModelName = 0xB974F2FC;              // modelname
constexpr uint32_t kModel = 0x286A8D26;                  // model
constexpr uint32_t kProfile = 0x7EA855F0;                // profile
constexpr uint32_t kSkeletonFileName = 0xF679DB13;       // skeletonFileName
constexpr uint32_t kSkeletonName = 0x09794932;           // skeletonName
constexpr uint32_t kViewerObjPos = 0x15670AB7;           // ViewerObjPos
constexpr uint32_t kAddComponents = 0x24C71BC7;          // viewerobj_add_components
constexpr uint32_t kInitModel = 0xBBAB79DE;              // viewerobj_init_model
constexpr uint32_t kIdle = 0x23DB4AEA;                   // idle
constexpr int kSkeletonAsset = 6;                        // (the asset type)

// `path` set to a model's file path: "models\<name>\<name><extension>", or "models\<name>" when the name holds a path
// separator (0x004879D0, in the CAS code - not rewritten yet - makes it as a temporary String).
void SetModelPath(Str::String& path, const char* name, const char* extension)
{
    alignas(Str::String) uint8_t temporary[sizeof(Str::String)];
    GameFunction<Str::String*(__cdecl*)(void*, const char*, const char*)>(0x004879D0)(temporary, name, extension);
    path = *reinterpret_cast<Str::String*>(temporary);
    reinterpret_cast<Str::String*>(temporary)->~String();
}

// `path` without its .skin / .mdl part, with `extension` (".col" / ".tex") on the end (0x0048BFE0 cuts the path).
void SiblingFile(const char* path, const char* extension, char (&out)[256])
{
    GameFunction<bool(__cdecl*)(const char*, char*)>(0x0048BFE0)(path, out);
    strcat_s(out, extension);
}

// A CAS artist's cached copy of `file` dropped, so it's read again from the edited file.
void DropCachedFile(const char* file)
{
    if (!Script::GetInteger(kCasArtist))
        return;
    Ass::CAssMan* assets = Ass::AssetManager();
    if (Ass::CAsset* asset = assets->GetAsset(Crc::GenerateCRCFromString(file), 0))
        assets->UnloadAsset(asset);
}

// Model `name`'s .col, .skin, .mdl and .tex files dropped (in that order).
void DropCachedModel(const char* name)
{
    Str::String path;
    SetModelPath(path, name, ".skin");
    char sibling[256];
    SiblingFile(path.c_str(), ".col", sibling);
    DropCachedFile(sibling);
    DropCachedFile(path.c_str());
    SetModelPath(path, name, ".mdl");
    DropCachedFile(path.c_str());
    SiblingFile(path.c_str(), ".tex", sibling);
    DropCachedFile(sibling);
}

CAnimBlenderComponent* Animation(CCompositeObject* object)
{
    return static_cast<CAnimBlenderComponent*>(object->GetComponent(CAnimBlenderComponent::kType));
}

} // namespace

// 0x005897B0
void CViewerObject::ShutDown() { Script::RunScript(kKillViewerObjectPanel, nullptr, nullptr, false, nullptr); }

// 0x005897D0
void CViewerObject::PlayAnimation(uint32_t anim)
{
    // (The original looks the component up again for each call.)
    if (!Animation(this) || !Animation(this)->HasValue(anim))
        return;
    const float length = Animation(this)->GetValue(anim);
    Animation(this)->PlayAnimationEx(anim, 0, 0, std::bit_cast<uint32_t>(length), 1, std::bit_cast<uint32_t>(0.3f),
                                     std::bit_cast<uint32_t>(1.0f));
}

// 0x00589860
void CViewerObject::DestroyBody()
{
    m_vtable = kViewerObjectVtable;
    m_task->Remove(); // (not null-checked: as the original)
    if (m_asset) {
        Ass::CAssMan* assets = Ass::AssetManager();
        if (Ass::CAsset* asset = assets->GetAsset(m_asset, 0)) {
            assets->UnloadAsset(asset);
            assets->GetAsset(m_asset, 0);
        }
        m_asset = 0;
    }
    if (m_task)
        reinterpret_cast<void(__thiscall*)(void*, unsigned int)>((*reinterpret_cast<void***>(m_task))[2])(m_task, 1);
    CCompositeObject::DestroyBody();
}

// 0x0058A2B0
CViewerObject* CViewerObject::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00589910
void CViewerObject::Load(Script::CStruct* params)
{
    CompositeObjectManager()->VirtualRegisterObject(this);
    AddMotionComponents(this);
    AddNodeComponents(this, params);
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(GameVariable<bool>(0x007CEFAC) ? static_cast<Mem::Allocator*>(memory->m_debugHeap)
                                                        : static_cast<Mem::Allocator*>(memory->m_bottomUpHeap));
    const char* name;
    for (int i = 0; i < 4; ++i) {
        char key[260];
        std::snprintf(key, sizeof(key), "model%d", i);
        if (params->GetString(key, &name, 0))
            DropCachedModel(name);
    }
    if (params->GetString(kModelName, &name, 0) || params->GetString(kModel, &name, 0)) {
        DropCachedModel(name);
    } else {
        Script::CStruct* profile;
        params->GetStructure(kProfile, &profile, 0); // (not used)
    }
    const char* skeletonFile;
    if (params->GetString(kSkeletonFileName, &skeletonFile, 0)) {
        uint32_t skeleton; // (left unset if skeletonName is missing: as the original)
        params->GetChecksum(kSkeletonName, &skeleton, 1);
        Ass::CAssMan* assets = Ass::AssetManager();
        if (!assets->GetAsset(skeleton, 0)) {
            assets->LoadAsset(skeletonFile, kSkeletonAsset, skeleton, 0, false, false, false, nullptr, 0);
            m_asset = skeleton;
        }
    }
    Script::VectorValue position;
    if (params->GetVector(kViewerObjPos, &position, 0)) {
        m_pos.x = position.x;
        m_pos.y = position.y;
        m_pos.z = position.z;
        m_pos.w = position.w;
    } else {
        m_pos = GameVariable<Mth::Vector>(0x0067F388); // (0, 0, 0, 1)
    }
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(&m_matrix, kIdentity, sizeof(kIdentity));
    Script::RunScript(kAddComponents, params, this, false, nullptr);
    reinterpret_cast<void(__thiscall*)(CViewerObject*)>(m_vtable[0x20 / 4])(this); // Finalize
    Script::RunScript(kInitModel, params, this, false, nullptr);
    PlayAnimation(kIdle);
    Mem::Manager::Get()->PopContext();
}

} // namespace Obj

REPLACE(0x005897B0, &Obj::CViewerObject::ShutDown);
REPLACE(0x005897D0, &Obj::CViewerObject::PlayAnimation);
REPLACE(0x00589860, &Obj::CViewerObject::DestroyBody);
REPLACE(0x0058A2B0, &Obj::CViewerObject::Destroy);
REPLACE(0x00589910, &Obj::CViewerObject::Load);
