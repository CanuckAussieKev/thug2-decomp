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
#include "game/core/code_address.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"

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

// Each instance's vtable (defined below, after its Destroy).
extern void** const kPreManagerVtable;
extern void** const kMcManagerVtable;
extern void** const kSioManagerVtable;
extern void** const kAsyncLoaderVtable;
extern void** const kScriptCacheVtable;
extern void** const kMlpManagerVtable;
extern void** const kMdlManagerVtable;
extern void** const kSoundManagerVtable;
extern void** const kFrontEndVtable;
extern void** const kObjectTrackerVtable;
extern void** const kInputManagerVtable;
extern void** const kSingleton4088E0Vtable;
extern void** const kNetManagerVtable;
extern void** const kCompositeObjectManagerVtable;
extern void** const kTrickObjectManagerVtable;
extern void** const kSingleton553DB0Vtable;
extern void** const kSingleton57C0D0Vtable;
extern void** const kSingleton57EED0Vtable;
extern void** const kSingleton5691A0Vtable;
extern void** const kSingleton53F520Vtable;
extern void** const kParkEditorVtable;
extern void** const kSingleton4ACAF0Vtable;
extern void** const kNodeManagerVtable;
extern void** const kSingleton574540Vtable;

struct PreManager {
    using Type = Pre::PreMgr;
    static void** Vtable() { return kPreManagerVtable; }
    static Type* Create(bool create) { return Pre::CreateManager(create); }
    static void Release() { Pre::DestroyManager(); }
};
struct McManager {
    using Type = Mc::Manager;
    static void** Vtable() { return kMcManagerVtable; }
    static Type* Create(bool create) { return Mc::CreateManager(create); }
    static void Release() { Mc::ReleaseManager(); }
};
struct SioManager {
    using Type = Sio::Manager;
    static void** Vtable() { return kSioManagerVtable; }
    static Type* Create(bool create) { return Sio::CreateManager(create); }
    static void Release() { Sio::ReleaseManager(); }
};
struct AsyncLoader {
    using Type = File::AsyncLoaderModule;
    static void** Vtable() { return kAsyncLoaderVtable; }
    static Type* Create(bool create) { return File::CreateAsyncLoaderModule(create); }
    static void Release() { File::DestroyAsyncLoaderModule(); }
};
struct ScriptCache {
    using Type = Script::CScriptCache;
    static void** Vtable() { return kScriptCacheVtable; }
    static Type* Create(bool create) { return Script::CreateScriptCache(create); }
    static void Release() { Script::ReleaseScriptCache(); }
};
struct MlpManager {
    using Type = Mlp::Manager;
    static void** Vtable() { return kMlpManagerVtable; }
    static Type* Create(bool create) { return Mlp::CreateManager(create); }
    static void Release() { Mlp::ReleaseManager(); }
};

// Named by their Create functions where there's no better name yet (Sys::Main makes them all).
struct MdlManager {
    using Type = Mdl::Manager;
    static void** Vtable() { return kMdlManagerVtable; }
    static Type* Create(bool create) { return Mdl::CreateManager(create); }
    static void Release() { Mdl::ReleaseManager(); }
};
struct SoundManager { // the sound effects manager (its Create returns nothing: no SingletonPtr<SoundManager> is constructed)
    using Type = void;
    static void** Vtable() { return kSoundManagerVtable; }
    static void Create(bool create) { Sfx::CreateManager(create); }
    static void Release() { Sfx::ReleaseManager(); }
};
struct FrontEnd {
    using Type = Mdl::FrontEnd;
    static void** Vtable() { return kFrontEndVtable; }
    static Type* Create(bool create) { return Mdl::CreateFrontEnd(create); }
    static void Release() { Mdl::ReleaseFrontEnd(); }
};
struct ObjectTracker {
    using Type = Obj::CTracker;
    static void** Vtable() { return kObjectTrackerVtable; }
    static Type* Create(bool create) { return Obj::CreateTracker(create); }
    static void Release() { Obj::ReleaseTracker(); }
};
struct InputManager {
    using Type = Inp::Manager;
    static void** Vtable() { return kInputManagerVtable; }
    static Type* Create(bool create) { return Inp::CreateManager(create); }
    static void Release() { Inp::ReleaseManager(); }
};
struct Singleton4088E0 { // the asset manager
    using Type = Ass::CAssMan;
    static void** Vtable() { return kSingleton4088E0Vtable; }
    static Type* Create(bool create) { return Ass::CreateAssetManager(create); }
    static void Release() { Ass::ReleaseAssetManager(); }
};
struct NetManager {
    using Type = Net::Manager;
    static void** Vtable() { return kNetManagerVtable; }
    static Type* Create(bool create) { return Net::CreateManager(create); }
    static void Release() { Net::ReleaseManager(); }
};
struct CompositeObjectManager {
    using Type = Obj::CCompositeObjectManager;
    static void** Vtable() { return kCompositeObjectManagerVtable; }
    static Type* Create(bool create) { return Obj::CreateCompositeObjectManager(create); }
    static void Release() { Obj::ReleaseCompositeObjectManager(); }
};
struct TrickObjectManager {
    using Type = Obj::CTrickObjectManager;
    static void** Vtable() { return kTrickObjectManagerVtable; }
    static Type* Create(bool create) { return Obj::AcquireTrickObjectManager(create); }
    static void Release() { Obj::ReleaseTrickObjectManager(); }
};
struct Singleton553DB0 { // the game mode
    using Type = Game::CGameMode;
    static void** Vtable() { return kSingleton553DB0Vtable; }
    static Type* Create(bool create) { return Game::CGameMode::Instance(create); }
    static void Release() { Game::CGameMode::Release(); }
};
struct Singleton57C0D0 { // the player profile manager
    using Type = Obj::CPlayerProfileManager;
    static void** Vtable() { return kSingleton57C0D0Vtable; }
    static Type* Create(bool create) { return Obj::CPlayerProfileManager::Create(create); }
    static void Release() { Obj::CPlayerProfileManager::Release(); }
};
struct Singleton57EED0 { // the game records
    using Type = Records::CGameRecords;
    static void** Vtable() { return kSingleton57EED0Vtable; }
    static Type* Create(bool create) { return Records::CGameRecords::Create(create); }
    static void Release() { Records::CGameRecords::Release(); }
};
struct Singleton5691A0 { // the skate module
    using Type = Mdl::Skate;
    static void** Vtable() { return kSingleton5691A0Vtable; }
    static Type* Create(bool create) { return Mdl::Skate::CreateInstance(create); }
    static void Release() { Mdl::Skate::DeleteInstance(); }
};
struct Singleton53F520 { // the online game manager (GameNet::Manager)
    using Type = GameNet::Manager;
    static void** Vtable() { return kSingleton53F520Vtable; }
    static Type* Create(bool create) { return GameNet::Manager::Create(create); }
    static void Release() { GameNet::ReleaseManager(); }
};
struct ParkEditor {
    using Type = Ed::CParkEditor;
    static void** Vtable() { return kParkEditorVtable; }
    static Type* Create(bool create) { return Ed::CParkEditor::Create(create); }
    static void Release() { Ed::CParkEditor::Release(); }
};
struct Singleton4ACAF0 { // the screen element manager (its Create returns nothing, as SoundManager's)
    using Type = void;
    static void** Vtable() { return kSingleton4ACAF0Vtable; }
    static void Create(bool create) { Front::CreateScreenElementManager(create); }
    static void Release() { Front::DeleteScreenElementManager(); }
};
struct NodeManager {
    using Type = SkateScript::CNodeManager;
    static void** Vtable() { return kNodeManagerVtable; }
    static Type* Create(bool create) { return SkateScript::CreateNodeManager(create); }
    static void Release() { SkateScript::ReleaseNodeManager(); }
};
using Singleton45D690 = NodeManager;
struct Singleton574540 { // the time of day manager
    using Type = Gfx::CTODManager;
    static void** Vtable() { return kSingleton574540Vtable; }
    static Type* Create(bool create) { return Gfx::CreateTODManager(create); }
    static void Release() { Gfx::ReleaseTODManager(); }
};

static VtableSlot kPreManagerVtable_slots[] = {&SingletonPtr<PreManager>::Destroy};
void** const kPreManagerVtable = reinterpret_cast<void**>(kPreManagerVtable_slots);
static VtableSlot kMcManagerVtable_slots[] = {&SingletonPtr<McManager>::Destroy};
void** const kMcManagerVtable = reinterpret_cast<void**>(kMcManagerVtable_slots);
static VtableSlot kSioManagerVtable_slots[] = {&SingletonPtr<SioManager>::Destroy};
void** const kSioManagerVtable = reinterpret_cast<void**>(kSioManagerVtable_slots);
static VtableSlot kAsyncLoaderVtable_slots[] = {&SingletonPtr<AsyncLoader>::Destroy};
void** const kAsyncLoaderVtable = reinterpret_cast<void**>(kAsyncLoaderVtable_slots);
static VtableSlot kScriptCacheVtable_slots[] = {&SingletonPtr<ScriptCache>::Destroy};
void** const kScriptCacheVtable = reinterpret_cast<void**>(kScriptCacheVtable_slots);
static VtableSlot kMlpManagerVtable_slots[] = {&SingletonPtr<MlpManager>::Destroy};
void** const kMlpManagerVtable = reinterpret_cast<void**>(kMlpManagerVtable_slots);
static VtableSlot kMdlManagerVtable_slots[] = {&SingletonPtr<MdlManager>::Destroy};
void** const kMdlManagerVtable = reinterpret_cast<void**>(kMdlManagerVtable_slots);
static VtableSlot kSoundManagerVtable_slots[] = {&SingletonPtr<SoundManager>::Destroy};
void** const kSoundManagerVtable = reinterpret_cast<void**>(kSoundManagerVtable_slots);
static VtableSlot kFrontEndVtable_slots[] = {&SingletonPtr<FrontEnd>::Destroy};
void** const kFrontEndVtable = reinterpret_cast<void**>(kFrontEndVtable_slots);
static VtableSlot kObjectTrackerVtable_slots[] = {&SingletonPtr<ObjectTracker>::Destroy};
void** const kObjectTrackerVtable = reinterpret_cast<void**>(kObjectTrackerVtable_slots);
static VtableSlot kInputManagerVtable_slots[] = {&SingletonPtr<InputManager>::Destroy};
void** const kInputManagerVtable = reinterpret_cast<void**>(kInputManagerVtable_slots);
static VtableSlot kSingleton4088E0Vtable_slots[] = {&SingletonPtr<Singleton4088E0>::Destroy};
void** const kSingleton4088E0Vtable = reinterpret_cast<void**>(kSingleton4088E0Vtable_slots);
static VtableSlot kNetManagerVtable_slots[] = {&SingletonPtr<NetManager>::Destroy};
void** const kNetManagerVtable = reinterpret_cast<void**>(kNetManagerVtable_slots);
static VtableSlot kCompositeObjectManagerVtable_slots[] = {&SingletonPtr<CompositeObjectManager>::Destroy};
void** const kCompositeObjectManagerVtable = reinterpret_cast<void**>(kCompositeObjectManagerVtable_slots);
static VtableSlot kTrickObjectManagerVtable_slots[] = {&SingletonPtr<TrickObjectManager>::Destroy};
void** const kTrickObjectManagerVtable = reinterpret_cast<void**>(kTrickObjectManagerVtable_slots);
static VtableSlot kSingleton553DB0Vtable_slots[] = {&SingletonPtr<Singleton553DB0>::Destroy};
void** const kSingleton553DB0Vtable = reinterpret_cast<void**>(kSingleton553DB0Vtable_slots);
static VtableSlot kSingleton57C0D0Vtable_slots[] = {&SingletonPtr<Singleton57C0D0>::Destroy};
void** const kSingleton57C0D0Vtable = reinterpret_cast<void**>(kSingleton57C0D0Vtable_slots);
static VtableSlot kSingleton57EED0Vtable_slots[] = {&SingletonPtr<Singleton57EED0>::Destroy};
void** const kSingleton57EED0Vtable = reinterpret_cast<void**>(kSingleton57EED0Vtable_slots);
static VtableSlot kSingleton5691A0Vtable_slots[] = {&SingletonPtr<Singleton5691A0>::Destroy};
void** const kSingleton5691A0Vtable = reinterpret_cast<void**>(kSingleton5691A0Vtable_slots);
static VtableSlot kSingleton53F520Vtable_slots[] = {&SingletonPtr<Singleton53F520>::Destroy};
void** const kSingleton53F520Vtable = reinterpret_cast<void**>(kSingleton53F520Vtable_slots);
static VtableSlot kParkEditorVtable_slots[] = {&SingletonPtr<ParkEditor>::Destroy};
void** const kParkEditorVtable = reinterpret_cast<void**>(kParkEditorVtable_slots);
static VtableSlot kSingleton4ACAF0Vtable_slots[] = {&SingletonPtr<Singleton4ACAF0>::Destroy};
void** const kSingleton4ACAF0Vtable = reinterpret_cast<void**>(kSingleton4ACAF0Vtable_slots);
static VtableSlot kNodeManagerVtable_slots[] = {&SingletonPtr<NodeManager>::Destroy};
void** const kNodeManagerVtable = reinterpret_cast<void**>(kNodeManagerVtable_slots);
static VtableSlot kSingleton574540Vtable_slots[] = {&SingletonPtr<Singleton574540>::Destroy};
void** const kSingleton574540Vtable = reinterpret_cast<void**>(kSingleton574540Vtable_slots);

} // namespace

} // namespace Spt
