// The out-of-line SingletonPtr<T> instances (singleton.h): one destructor and one scalar deleting destructor per
// singleton, at the addresses the linker gave them, and the one constructor that wasn't inlined.

#include "game/core/singleton.h"

#include <cstdint>

#include "game/ass/assman.h"
#include "game/file/async.h"
#include "game/ed/park_editor.h"
#include "game/file/pre.h"
#include "game/front/front_end.h"
#include "game/front/screen_element_manager.h"
#include "game/gfx/tod_manager.h"
#include "game/mc/memcard.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/trick_objects.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/script/node_manager.h"
#include "game/sfx/sfx.h"
#include "game/sio/device.h"
#include "game/sio/inpman.h"
#include "game/skate/profile_manager.h"
#include "game/skate/records.h"
#include "game/sys/mlp.h"
#include "game/sys/module.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Spt {

// 0x004E1970
PointerMember* PointerMember::Construct(void* pointer)
{
    m_pointer = pointer;
    return this;
}

// 0x00469490
void* PointerMember::Get()
{
    return m_pointer;
}

namespace {

// A singleton without source yet: its Create (cdecl, bool) and Release (cdecl) in the game.
template <uint32_t kCreate, uint32_t kRelease, uint32_t kVtableAddress>
struct GameSingleton {
    using Type = void;
    static constexpr uint32_t kVtable = kVtableAddress;
    static void* Create(bool create) { return GameFunction<void*(__cdecl*)(bool)>(kCreate)(create); }
    static void Release() { GameFunction<void(__cdecl*)()>(kRelease)(); }
};

struct PreManager {
    using Type = Pre::PreMgr;
    static constexpr uint32_t kVtable = 0x0064AFB0;
    static Type* Create(bool create) { return Pre::CreateManager(create); }
    static void Release() { Pre::DestroyManager(); }
};
struct McManager {
    using Type = Mc::Manager;
    static constexpr uint32_t kVtable = 0x0064AFBC;
    static Type* Create(bool create) { return Mc::CreateManager(create); }
    static void Release() { Mc::ReleaseManager(); }
};
struct SioManager {
    using Type = Sio::Manager;
    static constexpr uint32_t kVtable = 0x006472B8;
    static Type* Create(bool create) { return Sio::CreateManager(create); }
    static void Release() { Sio::ReleaseManager(); }
};
struct AsyncLoader {
    using Type = File::AsyncLoaderModule;
    static constexpr uint32_t kVtable = 0x0064AFC4;
    static Type* Create(bool create) { return File::CreateAsyncLoaderModule(create); }
    static void Release() { File::DestroyAsyncLoaderModule(); }
};
struct ScriptCache {
    using Type = Script::CScriptCache;
    static constexpr uint32_t kVtable = 0x0064AFC8;
    static Type* Create(bool create) { return Script::CreateScriptCache(create); }
    static void Release() { Script::ReleaseScriptCache(); }
};
struct MlpManager {
    using Type = Mlp::Manager;
    static constexpr uint32_t kVtable = 0x0064A2EC;
    static Type* Create(bool create) { return Mlp::CreateManager(create); }
    static void Release() { Mlp::ReleaseManager(); }
};

// Named by their Create functions where there's no better name yet (Sys::Main makes them all).
struct MdlManager {
    using Type = Mdl::Manager;
    static constexpr uint32_t kVtable = 0x0064AFB8;
    static Type* Create(bool create) { return Mdl::CreateManager(create); }
    static void Release() { Mdl::ReleaseManager(); }
};
struct SoundManager { // the sound effects manager (its Create returns nothing: no SingletonPtr<SoundManager> is constructed)
    using Type = void;
    static constexpr uint32_t kVtable = 0x006472B4;
    static void Create(bool create) { Sfx::CreateManager(create); }
    static void Release() { Sfx::ReleaseManager(); }
};
struct FrontEnd {
    using Type = Mdl::FrontEnd;
    static constexpr uint32_t kVtable = 0x0064AFEC;
    static Type* Create(bool create) { return Mdl::CreateFrontEnd(create); }
    static void Release() { Mdl::ReleaseFrontEnd(); }
};
struct ObjectTracker {
    using Type = Obj::CTracker;
    static constexpr uint32_t kVtable = 0x0064AFB4;
    static Type* Create(bool create) { return Obj::CreateTracker(create); }
    static void Release() { Obj::ReleaseTracker(); }
};
struct InputManager {
    using Type = Inp::Manager;
    static constexpr uint32_t kVtable = 0x0064AFC0;
    static Type* Create(bool create) { return Inp::CreateManager(create); }
    static void Release() { Inp::ReleaseManager(); }
};
struct Singleton4088E0 { // the asset manager
    using Type = Ass::CAssMan;
    static constexpr uint32_t kVtable = 0x0064AFCC;
    static Type* Create(bool create) { return Ass::CreateAssetManager(create); }
    static void Release() { Ass::ReleaseAssetManager(); }
};
struct NetManager {
    using Type = Net::Manager;
    static constexpr uint32_t kVtable = 0x0064AFD0;
    static Type* Create(bool create) { return Net::CreateManager(create); }
    static void Release() { Net::ReleaseManager(); }
};
struct CompositeObjectManager {
    using Type = Obj::CCompositeObjectManager;
    static constexpr uint32_t kVtable = 0x0064AFD4;
    static Type* Create(bool create) { return Obj::CreateCompositeObjectManager(create); }
    static void Release() { Obj::ReleaseCompositeObjectManager(); }
};
struct TrickObjectManager {
    using Type = Obj::CTrickObjectManager;
    static constexpr uint32_t kVtable = 0x0064AFD8;
    static Type* Create(bool create) { return Obj::AcquireTrickObjectManager(create); }
    static void Release() { Obj::ReleaseTrickObjectManager(); }
};
using Singleton553DB0 = GameSingleton<0x00553DB0, 0x00552F70, 0x0064AFDC>;
struct Singleton57C0D0 { // the player profile manager
    using Type = Obj::CPlayerProfileManager;
    static constexpr uint32_t kVtable = 0x0064AFE0;
    static Type* Create(bool create) { return Obj::CPlayerProfileManager::Create(create); }
    static void Release() { Obj::CPlayerProfileManager::Release(); }
};
struct Singleton57EED0 { // the game records
    using Type = Records::CGameRecords;
    static constexpr uint32_t kVtable = 0x0064AFE4;
    static Type* Create(bool create) { return Records::CGameRecords::Create(create); }
    static void Release() { Records::CGameRecords::Release(); }
};
using Singleton5691A0 = GameSingleton<0x005691A0, 0x00564C10, 0x0064AFE8>;
struct Singleton53F520 { // the online game manager (GameNet::Manager)
    using Type = GameNet::Manager;
    static constexpr uint32_t kVtable = 0x0064AFF0;
    static Type* Create(bool create) { return GameNet::Manager::Create(create); }
    static void Release() { GameNet::ReleaseManager(); }
};
struct ParkEditor {
    using Type = Ed::CParkEditor;
    static constexpr uint32_t kVtable = 0x0064AFF4;
    static Type* Create(bool create) { return Ed::CParkEditor::Create(create); }
    static void Release() { Ed::CParkEditor::Release(); }
};
struct Singleton4ACAF0 { // the screen element manager (its Create returns nothing, as SoundManager's)
    using Type = void;
    static constexpr uint32_t kVtable = 0x0064AFF8;
    static void Create(bool create) { Front::CreateScreenElementManager(create); }
    static void Release() { Front::DeleteScreenElementManager(); }
};
struct NodeManager {
    using Type = SkateScript::CNodeManager;
    static constexpr uint32_t kVtable = 0x0064AFFC;
    static Type* Create(bool create) { return SkateScript::CreateNodeManager(create); }
    static void Release() { SkateScript::ReleaseNodeManager(); }
};
using Singleton45D690 = NodeManager;
struct Singleton574540 { // the time of day manager
    using Type = Gfx::CTODManager;
    static constexpr uint32_t kVtable = 0x0064B000;
    static Type* Create(bool create) { return Gfx::CreateTODManager(create); }
    static void Release() { Gfx::ReleaseTODManager(); }
};

} // namespace

} // namespace Spt

// Each instance's destructor (11 bytes: the vtable, then a jump to Release) and scalar deleting destructor.
REPLACE(0x0044FD40, &Spt::SingletonPtr<Spt::SoundManager>::DestroyBody);
REPLACE(0x0044FD60, &Spt::SingletonPtr<Spt::SoundManager>::Destroy);
REPLACE(0x0044FD50, &Spt::SingletonPtr<Spt::SioManager>::DestroyBody);
REPLACE(0x0044FD90, &Spt::SingletonPtr<Spt::SioManager>::Destroy);
REPLACE(0x004B2230, &Spt::SingletonPtr<Spt::MlpManager>::DestroyBody);
REPLACE(0x004B2240, &Spt::SingletonPtr<Spt::MlpManager>::Destroy);
REPLACE(0x004E1980, &Spt::SingletonPtr<Spt::PreManager>::DestroyBody);
REPLACE(0x004E1B00, &Spt::SingletonPtr<Spt::PreManager>::Destroy);
REPLACE(0x004E1990, &Spt::SingletonPtr<Spt::ObjectTracker>::DestroyBody);
REPLACE(0x004E1B30, &Spt::SingletonPtr<Spt::ObjectTracker>::Destroy);
REPLACE(0x004E19A0, &Spt::SingletonPtr<Spt::MdlManager>::DestroyBody);
REPLACE(0x004E1B60, &Spt::SingletonPtr<Spt::MdlManager>::Destroy);
REPLACE(0x004E19B0, &Spt::SingletonPtr<Spt::McManager>::DestroyBody);
REPLACE(0x004E1B90, &Spt::SingletonPtr<Spt::McManager>::Destroy);
REPLACE(0x004E19C0, &Spt::SingletonPtr<Spt::InputManager>::DestroyBody);
REPLACE(0x004E1BC0, &Spt::SingletonPtr<Spt::InputManager>::Destroy);
REPLACE(0x004E19D0, &Spt::SingletonPtr<Spt::AsyncLoader>::DestroyBody);
REPLACE(0x004E1BF0, &Spt::SingletonPtr<Spt::AsyncLoader>::Destroy);
REPLACE(0x004E19E0, &Spt::SingletonPtr<Spt::ScriptCache>::DestroyBody);
REPLACE(0x004E1C20, &Spt::SingletonPtr<Spt::ScriptCache>::Destroy);
REPLACE(0x004E19F0, &Spt::SingletonPtr<Spt::Singleton4088E0>::DestroyBody);
REPLACE(0x004E1C50, &Spt::SingletonPtr<Spt::Singleton4088E0>::Destroy);
REPLACE(0x004E1A00, &Spt::SingletonPtr<Spt::NetManager>::DestroyBody);
REPLACE(0x004E1C80, &Spt::SingletonPtr<Spt::NetManager>::Destroy);
REPLACE(0x004E1A10, &Spt::SingletonPtr<Spt::CompositeObjectManager>::DestroyBody);
REPLACE(0x004E1CB0, &Spt::SingletonPtr<Spt::CompositeObjectManager>::Destroy);
REPLACE(0x004E1A20, &Spt::SingletonPtr<Spt::TrickObjectManager>::DestroyBody);
REPLACE(0x004E1CE0, &Spt::SingletonPtr<Spt::TrickObjectManager>::Destroy);
REPLACE(0x004E1A30, &Spt::SingletonPtr<Spt::Singleton553DB0>::DestroyBody);
REPLACE(0x004E1D10, &Spt::SingletonPtr<Spt::Singleton553DB0>::Destroy);
REPLACE(0x004E1A40, &Spt::SingletonPtr<Spt::Singleton57C0D0>::DestroyBody);
REPLACE(0x004E1D40, &Spt::SingletonPtr<Spt::Singleton57C0D0>::Destroy);
REPLACE(0x004E1A50, &Spt::SingletonPtr<Spt::Singleton57EED0>::DestroyBody);
REPLACE(0x004E1D70, &Spt::SingletonPtr<Spt::Singleton57EED0>::Destroy);
REPLACE(0x004E1A60, &Spt::SingletonPtr<Spt::Singleton5691A0>::DestroyBody);
REPLACE(0x004E1DA0, &Spt::SingletonPtr<Spt::Singleton5691A0>::Destroy);
REPLACE(0x004E1A70, &Spt::SingletonPtr<Spt::FrontEnd>::DestroyBody);
REPLACE(0x004E1DD0, &Spt::SingletonPtr<Spt::FrontEnd>::Destroy);
REPLACE(0x004E1A80, &Spt::SingletonPtr<Spt::Singleton53F520>::DestroyBody);
REPLACE(0x004E1E00, &Spt::SingletonPtr<Spt::Singleton53F520>::Destroy);
REPLACE(0x004E1AC0, &Spt::SingletonPtr<Spt::ParkEditor>::DestroyBody);
REPLACE(0x004E1E30, &Spt::SingletonPtr<Spt::ParkEditor>::Destroy);
REPLACE(0x004E1AD0, &Spt::SingletonPtr<Spt::Singleton4ACAF0>::DestroyBody);
REPLACE(0x004E1E60, &Spt::SingletonPtr<Spt::Singleton4ACAF0>::Destroy);
REPLACE(0x004E1AE0, &Spt::SingletonPtr<Spt::Singleton45D690>::DestroyBody);
REPLACE(0x004E1E90, &Spt::SingletonPtr<Spt::Singleton45D690>::Destroy);
REPLACE(0x004E1AF0, &Spt::SingletonPtr<Spt::Singleton574540>::DestroyBody);
REPLACE(0x004E1EC0, &Spt::SingletonPtr<Spt::Singleton574540>::Destroy);

// The one constructor out of line (called by 0x004F75F0; it builds the instance pointer through 0x004E1970).
REPLACE(0x004E1A90, &Spt::SingletonPtr<Spt::ParkEditor>::Construct);
REPLACE(0x004E1970, &Spt::PointerMember::Construct);
REPLACE_SOURCE_ONLY(0x00469490, &Spt::PointerMember::Get); // 3 bytes
