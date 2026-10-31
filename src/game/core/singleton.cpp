// The out-of-line LazyInstance<T> instances (singleton.h): one destructor and one scalar deleting destructor per
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

namespace Singletons {

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
    using Type = Pre::ArchiveManager;
    static void** Vtable() { return kPreManagerVtable; }
    static Type* Create(bool create) { return Pre::CreateManager(create); }
    static void Release() { Pre::DestroyManager(); }
};
struct McManager {
    using Type = MemCard::Manager;
    static void** Vtable() { return kMcManagerVtable; }
    static Type* Create(bool create) { return MemCard::CreateManager(create); }
    static void Release() { MemCard::ReleaseManager(); }
};
struct SioManager {
    using Type = Sio::Manager;
    static void** Vtable() { return kSioManagerVtable; }
    static Type* Create(bool create) { return Sio::CreateManager(create); }
    static void Release() { Sio::ReleaseManager(); }
};
struct AsyncLoader {
    using Type = FileLoad::AsyncLoaderModule;
    static void** Vtable() { return kAsyncLoaderVtable; }
    static Type* Create(bool create) { return FileLoad::CreateAsyncLoaderModule(create); }
    static void Release() { FileLoad::DestroyAsyncLoaderModule(); }
};
struct ScriptCache {
    using Type = Scripts::CScriptStore;
    static void** Vtable() { return kScriptCacheVtable; }
    static Type* Create(bool create) { return Scripts::CreateScriptCache(create); }
    static void Release() { Scripts::ReleaseScriptCache(); }
};
struct MlpManager {
    using Type = FrameLoop::Manager;
    static void** Vtable() { return kMlpManagerVtable; }
    static Type* Create(bool create) { return FrameLoop::CreateManager(create); }
    static void Release() { FrameLoop::ReleaseManager(); }
};

// Named by their Create functions where there's no better name yet (SystemCore::Main makes them all).
struct MdlManager {
    using Type = GameModules::Manager;
    static void** Vtable() { return kMdlManagerVtable; }
    static Type* Create(bool create) { return GameModules::CreateManager(create); }
    static void Release() { GameModules::ReleaseManager(); }
};
struct SoundManager { // the sound effects manager (its Create returns nothing: no LazyInstance<SoundManager> is constructed)
    using Type = void;
    static void** Vtable() { return kSoundManagerVtable; }
    static void Create(bool create) { SoundFx::CreateManager(create); }
    static void Release() { SoundFx::ReleaseManager(); }
};
struct FrontEnd {
    using Type = GameModules::FrontEnd;
    static void** Vtable() { return kFrontEndVtable; }
    static Type* Create(bool create) { return GameModules::CreateFrontEnd(create); }
    static void Release() { GameModules::ReleaseFrontEnd(); }
};
struct ObjectTracker {
    using Type = GameObjects::CEventWaitBoard;
    static void** Vtable() { return kObjectTrackerVtable; }
    static Type* Create(bool create) { return GameObjects::CreateTracker(create); }
    static void Release() { GameObjects::ReleaseTracker(); }
};
struct InputManager {
    using Type = PadInput::Manager;
    static void** Vtable() { return kInputManagerVtable; }
    static Type* Create(bool create) { return PadInput::CreateManager(create); }
    static void Release() { PadInput::ReleaseManager(); }
};
struct Singleton4088E0 { // the asset manager
    using Type = AssetStore::CResourceRegistry;
    static void** Vtable() { return kSingleton4088E0Vtable; }
    static Type* Create(bool create) { return AssetStore::CreateAssetManager(create); }
    static void Release() { AssetStore::ReleaseAssetManager(); }
};
struct NetManager {
    using Type = NetLink::Manager;
    static void** Vtable() { return kNetManagerVtable; }
    static Type* Create(bool create) { return NetLink::CreateManager(create); }
    static void Release() { NetLink::ReleaseManager(); }
};
struct CompositeObjectManager {
    using Type = GameObjects::CEntityHub;
    static void** Vtable() { return kCompositeObjectManagerVtable; }
    static Type* Create(bool create) { return GameObjects::CreateCompositeObjectManager(create); }
    static void Release() { GameObjects::ReleaseCompositeObjectManager(); }
};
struct TrickObjectManager {
    using Type = GameObjects::CTrickSpotHub;
    static void** Vtable() { return kTrickObjectManagerVtable; }
    static Type* Create(bool create) { return GameObjects::AcquireTrickObjectManager(create); }
    static void Release() { GameObjects::ReleaseTrickObjectManager(); }
};
struct Singleton553DB0 { // the game mode
    using Type = GoalSystem::CMatchRules;
    static void** Vtable() { return kSingleton553DB0Vtable; }
    static Type* Create(bool create) { return GoalSystem::CMatchRules::Instance(create); }
    static void Release() { GoalSystem::CMatchRules::Release(); }
};
struct Singleton57C0D0 { // the player profile manager
    using Type = GameObjects::CProfileRoster;
    static void** Vtable() { return kSingleton57C0D0Vtable; }
    static Type* Create(bool create) { return GameObjects::CProfileRoster::Create(create); }
    static void Release() { GameObjects::CProfileRoster::Release(); }
};
struct Singleton57EED0 { // the game records
    using Type = Highscores::CBestResults;
    static void** Vtable() { return kSingleton57EED0Vtable; }
    static Type* Create(bool create) { return Highscores::CBestResults::Create(create); }
    static void Release() { Highscores::CBestResults::Release(); }
};
struct Singleton5691A0 { // the skate module
    using Type = GameModules::Skate;
    static void** Vtable() { return kSingleton5691A0Vtable; }
    static Type* Create(bool create) { return GameModules::Skate::CreateInstance(create); }
    static void Release() { GameModules::Skate::DeleteInstance(); }
};
struct Singleton53F520 { // the online game manager (OnlinePlay::Manager)
    using Type = OnlinePlay::Manager;
    static void** Vtable() { return kSingleton53F520Vtable; }
    static Type* Create(bool create) { return OnlinePlay::Manager::Create(create); }
    static void Release() { OnlinePlay::ReleaseManager(); }
};
struct ParkEditor {
    using Type = ParkEditing::CParkBuilderMode;
    static void** Vtable() { return kParkEditorVtable; }
    static Type* Create(bool create) { return ParkEditing::CParkBuilderMode::Create(create); }
    static void Release() { ParkEditing::CParkBuilderMode::Release(); }
};
struct Singleton4ACAF0 { // the screen element manager (its Create returns nothing, as SoundManager's)
    using Type = void;
    static void** Vtable() { return kSingleton4ACAF0Vtable; }
    static void Create(bool create) { MenuSystem::CreateScreenElementManager(create); }
    static void Release() { MenuSystem::DeleteScreenElementManager(); }
};
struct NodeManager {
    using Type = LevelNodes::CNodeManager;
    static void** Vtable() { return kNodeManagerVtable; }
    static Type* Create(bool create) { return LevelNodes::CreateNodeManager(create); }
    static void Release() { LevelNodes::ReleaseNodeManager(); }
};
using Singleton45D690 = NodeManager;
struct Singleton574540 { // the time of day manager
    using Type = Gfx2D::CTODManager;
    static void** Vtable() { return kSingleton574540Vtable; }
    static Type* Create(bool create) { return Gfx2D::CreateTODManager(create); }
    static void Release() { Gfx2D::ReleaseTODManager(); }
};

static VtableSlot kPreManagerVtable_slots[] = {&LazyInstance<PreManager>::Destroy};
void** const kPreManagerVtable = reinterpret_cast<void**>(kPreManagerVtable_slots);
static VtableSlot kMcManagerVtable_slots[] = {&LazyInstance<McManager>::Destroy};
void** const kMcManagerVtable = reinterpret_cast<void**>(kMcManagerVtable_slots);
static VtableSlot kSioManagerVtable_slots[] = {&LazyInstance<SioManager>::Destroy};
void** const kSioManagerVtable = reinterpret_cast<void**>(kSioManagerVtable_slots);
static VtableSlot kAsyncLoaderVtable_slots[] = {&LazyInstance<AsyncLoader>::Destroy};
void** const kAsyncLoaderVtable = reinterpret_cast<void**>(kAsyncLoaderVtable_slots);
static VtableSlot kScriptCacheVtable_slots[] = {&LazyInstance<ScriptCache>::Destroy};
void** const kScriptCacheVtable = reinterpret_cast<void**>(kScriptCacheVtable_slots);
static VtableSlot kMlpManagerVtable_slots[] = {&LazyInstance<MlpManager>::Destroy};
void** const kMlpManagerVtable = reinterpret_cast<void**>(kMlpManagerVtable_slots);
static VtableSlot kMdlManagerVtable_slots[] = {&LazyInstance<MdlManager>::Destroy};
void** const kMdlManagerVtable = reinterpret_cast<void**>(kMdlManagerVtable_slots);
static VtableSlot kSoundManagerVtable_slots[] = {&LazyInstance<SoundManager>::Destroy};
void** const kSoundManagerVtable = reinterpret_cast<void**>(kSoundManagerVtable_slots);
static VtableSlot kFrontEndVtable_slots[] = {&LazyInstance<FrontEnd>::Destroy};
void** const kFrontEndVtable = reinterpret_cast<void**>(kFrontEndVtable_slots);
static VtableSlot kObjectTrackerVtable_slots[] = {&LazyInstance<ObjectTracker>::Destroy};
void** const kObjectTrackerVtable = reinterpret_cast<void**>(kObjectTrackerVtable_slots);
static VtableSlot kInputManagerVtable_slots[] = {&LazyInstance<InputManager>::Destroy};
void** const kInputManagerVtable = reinterpret_cast<void**>(kInputManagerVtable_slots);
static VtableSlot kSingleton4088E0Vtable_slots[] = {&LazyInstance<Singleton4088E0>::Destroy};
void** const kSingleton4088E0Vtable = reinterpret_cast<void**>(kSingleton4088E0Vtable_slots);
static VtableSlot kNetManagerVtable_slots[] = {&LazyInstance<NetManager>::Destroy};
void** const kNetManagerVtable = reinterpret_cast<void**>(kNetManagerVtable_slots);
static VtableSlot kCompositeObjectManagerVtable_slots[] = {&LazyInstance<CompositeObjectManager>::Destroy};
void** const kCompositeObjectManagerVtable = reinterpret_cast<void**>(kCompositeObjectManagerVtable_slots);
static VtableSlot kTrickObjectManagerVtable_slots[] = {&LazyInstance<TrickObjectManager>::Destroy};
void** const kTrickObjectManagerVtable = reinterpret_cast<void**>(kTrickObjectManagerVtable_slots);
static VtableSlot kSingleton553DB0Vtable_slots[] = {&LazyInstance<Singleton553DB0>::Destroy};
void** const kSingleton553DB0Vtable = reinterpret_cast<void**>(kSingleton553DB0Vtable_slots);
static VtableSlot kSingleton57C0D0Vtable_slots[] = {&LazyInstance<Singleton57C0D0>::Destroy};
void** const kSingleton57C0D0Vtable = reinterpret_cast<void**>(kSingleton57C0D0Vtable_slots);
static VtableSlot kSingleton57EED0Vtable_slots[] = {&LazyInstance<Singleton57EED0>::Destroy};
void** const kSingleton57EED0Vtable = reinterpret_cast<void**>(kSingleton57EED0Vtable_slots);
static VtableSlot kSingleton5691A0Vtable_slots[] = {&LazyInstance<Singleton5691A0>::Destroy};
void** const kSingleton5691A0Vtable = reinterpret_cast<void**>(kSingleton5691A0Vtable_slots);
static VtableSlot kSingleton53F520Vtable_slots[] = {&LazyInstance<Singleton53F520>::Destroy};
void** const kSingleton53F520Vtable = reinterpret_cast<void**>(kSingleton53F520Vtable_slots);
static VtableSlot kParkEditorVtable_slots[] = {&LazyInstance<ParkEditor>::Destroy};
void** const kParkEditorVtable = reinterpret_cast<void**>(kParkEditorVtable_slots);
static VtableSlot kSingleton4ACAF0Vtable_slots[] = {&LazyInstance<Singleton4ACAF0>::Destroy};
void** const kSingleton4ACAF0Vtable = reinterpret_cast<void**>(kSingleton4ACAF0Vtable_slots);
static VtableSlot kNodeManagerVtable_slots[] = {&LazyInstance<NodeManager>::Destroy};
void** const kNodeManagerVtable = reinterpret_cast<void**>(kNodeManagerVtable_slots);
static VtableSlot kSingleton574540Vtable_slots[] = {&LazyInstance<Singleton574540>::Destroy};
void** const kSingleton574540Vtable = reinterpret_cast<void**>(kSingleton574540Vtable_slots);

} // namespace

} // namespace Spt
