// The out-of-line LazyInstance<T> instances (single_instance.h): one destructor and one scalar deleting destructor per
// singleton, at the addresses the linker gave them, and the one constructor that wasn't inlined.

#include "game/core/single_instance.h"

#include <cstdint>

#include "game/assets/resource_registry.h"
#include "game/file/async.h"
#include "game/park_editing/park_editor.h"
#include "game/file/archive.h"
#include "game/front/front_end.h"
#include "game/front/screen_element_manager.h"
#include "game/gfx/tod_manager.h"
#include "game/save_cards/memcard.h"
#include "game/net/gamenet_manager.h"
#include "game/net/net_manager.h"
#include "game/obj/compositemgr.h"
#include "game/obj/entity_hub.h"
#include "game/obj/trick_objects.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/script/node_manager.h"
#include "game/sound/sfx.h"
#include "game/pad_input/device.h"
#include "game/pad_input/input_hub.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "game/sys/mlp.h"
#include "game/sys/game_module.h"
#include "game/core/code_address.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"

namespace Singletons {

// 0x004E1970
PointerMember* PointerMember::MakeNew(void* pointer)
{
    m_pointer = pointer;
    return this;
}

// 0x00469490
void* PointerMember::Pull()
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
    using KindTag = Pre::ArchiveManager;
    static void** Vtable() { return kPreManagerVtable; }
    static KindTag* Fabricate(bool create) { return Pre::CreateManager(create); }
    static void Release() { Pre::DestroyManager(); }
};
struct CardSlotStub {
    using KindTag = MemCard::Hub;
    static void** Vtable() { return kMcManagerVtable; }
    static KindTag* Fabricate(bool create) { return MemCard::CreateManager(create); }
    static void Release() { MemCard::ReleaseManager(); }
};
struct SerialSlotStub {
    using KindTag = Sio::Hub;
    static void** Vtable() { return kSioManagerVtable; }
    static KindTag* Fabricate(bool create) { return Sio::CreateManager(create); }
    static void Release() { Sio::ReleaseManager(); }
};
struct AsyncLoader {
    using KindTag = FileLoad::AsyncLoaderModule;
    static void** Vtable() { return kAsyncLoaderVtable; }
    static KindTag* Fabricate(bool create) { return FileLoad::CreateAsyncLoaderModule(create); }
    static void Release() { FileLoad::DestroyAsyncLoaderModule(); }
};
struct ScriptCache {
    using KindTag = Scripts::CScriptStore;
    static void** Vtable() { return kScriptCacheVtable; }
    static KindTag* Fabricate(bool create) { return Scripts::CreateScriptCache(create); }
    static void Release() { Scripts::ReleaseScriptCache(); }
};
struct MainLoopSlotStub {
    using KindTag = FrameLoop::Hub;
    static void** Vtable() { return kMlpManagerVtable; }
    static KindTag* Fabricate(bool create) { return FrameLoop::CreateManager(create); }
    static void Release() { FrameLoop::ReleaseManager(); }
};

// Named by their Create functions where there's no better name yet (SystemCore::Main makes them all).
struct ModelSlotStub {
    using KindTag = GameModules::Hub;
    static void** Vtable() { return kMdlManagerVtable; }
    static KindTag* Fabricate(bool create) { return GameModules::CreateManager(create); }
    static void Release() { GameModules::ReleaseManager(); }
};
struct SoundManager { // the sound effects manager (its Create returns nothing: no LazyInstance<SoundManager> is constructed)
    using KindTag = void;
    static void** Vtable() { return kSoundManagerVtable; }
    static void Fabricate(bool create) { SoundFx::CreateManager(create); }
    static void Release() { SoundFx::ReleaseManager(); }
};
struct FrontEnd {
    using KindTag = GameModules::FrontEnd;
    static void** Vtable() { return kFrontEndVtable; }
    static KindTag* Fabricate(bool create) { return GameModules::CreateFrontEnd(create); }
    static void Release() { GameModules::ReleaseFrontEnd(); }
};
struct ObjectTracker {
    using KindTag = GameObjects::CEventWaitBoard;
    static void** Vtable() { return kObjectTrackerVtable; }
    static KindTag* Fabricate(bool create) { return GameObjects::CreateTracker(create); }
    static void Release() { GameObjects::ReleaseTracker(); }
};
struct InputManager {
    using KindTag = PadInput::Hub;
    static void** Vtable() { return kInputManagerVtable; }
    static KindTag* Fabricate(bool create) { return PadInput::CreateManager(create); }
    static void Release() { PadInput::ReleaseManager(); }
};
struct Singleton4088E0 { // the asset manager
    using KindTag = AssetStore::CResourceRegistry;
    static void** Vtable() { return kSingleton4088E0Vtable; }
    static KindTag* Fabricate(bool create) { return AssetStore::CreateAssetManager(create); }
    static void Release() { AssetStore::ReleaseAssetManager(); }
};
struct NetSessionSlot {
    using KindTag = NetLink::Hub;
    static void** Vtable() { return kNetManagerVtable; }
    static KindTag* Fabricate(bool create) { return NetLink::CreateManager(create); }
    static void Release() { NetLink::ReleaseManager(); }
};
struct ObjectHubSlot {
    using KindTag = GameObjects::CEntityHub;
    static void** Vtable() { return kCompositeObjectManagerVtable; }
    static KindTag* Fabricate(bool create) { return GameObjects::CreateCompositeObjectManager(create); }
    static void Release() { GameObjects::ReleaseCompositeObjectManager(); }
};
struct TrickPropSlotStub {
    using KindTag = GameObjects::CTrickSpotHub;
    static void** Vtable() { return kTrickObjectManagerVtable; }
    static KindTag* Fabricate(bool create) { return GameObjects::AcquireTrickObjectManager(create); }
    static void Release() { GameObjects::ReleaseTrickObjectManager(); }
};
struct Singleton553DB0 { // the game mode
    using KindTag = GoalSystem::CMatchRules;
    static void** Vtable() { return kSingleton553DB0Vtable; }
    static KindTag* Fabricate(bool create) { return GoalSystem::CMatchRules::Sole(create); }
    static void Release() { GoalSystem::CMatchRules::Release(); }
};
struct Singleton57C0D0 { // the player profile manager
    using KindTag = GameObjects::CProfileRoster;
    static void** Vtable() { return kSingleton57C0D0Vtable; }
    static KindTag* Fabricate(bool create) { return GameObjects::CProfileRoster::Fabricate(create); }
    static void Release() { GameObjects::CProfileRoster::Release(); }
};
struct Singleton57EED0 { // the game records
    using KindTag = Highscores::CBestResults;
    static void** Vtable() { return kSingleton57EED0Vtable; }
    static KindTag* Fabricate(bool create) { return Highscores::CBestResults::Fabricate(create); }
    static void Release() { Highscores::CBestResults::Release(); }
};
struct Singleton5691A0 { // the skate module
    using KindTag = GameModules::Skate;
    static void** Vtable() { return kSingleton5691A0Vtable; }
    static KindTag* Fabricate(bool create) { return GameModules::Skate::SpawnOccurrence(create); }
    static void Release() { GameModules::Skate::DiscardOccurrence(); }
};
struct Singleton53F520 { // the online game manager (OnlinePlay::Hub)
    using KindTag = OnlinePlay::Hub;
    static void** Vtable() { return kSingleton53F520Vtable; }
    static KindTag* Fabricate(bool create) { return OnlinePlay::Hub::Fabricate(create); }
    static void Release() { OnlinePlay::ReleaseManager(); }
};
struct ParkEditor {
    using KindTag = ParkEditing::CParkBuilderMode;
    static void** Vtable() { return kParkEditorVtable; }
    static KindTag* Fabricate(bool create) { return ParkEditing::CParkBuilderMode::Fabricate(create); }
    static void Release() { ParkEditing::CParkBuilderMode::Release(); }
};
struct Singleton4ACAF0 { // the screen element manager (its Create returns nothing, as SoundManager's)
    using KindTag = void;
    static void** Vtable() { return kSingleton4ACAF0Vtable; }
    static void Fabricate(bool create) { MenuSystem::CreateScreenElementManager(create); }
    static void Release() { MenuSystem::DeleteScreenElementManager(); }
};
struct NodeManager {
    using KindTag = LevelNodes::CNodeManager;
    static void** Vtable() { return kNodeManagerVtable; }
    static KindTag* Fabricate(bool create) { return LevelNodes::CreateNodeManager(create); }
    static void Release() { LevelNodes::ReleaseNodeManager(); }
};
using Singleton45D690 = NodeManager;
struct Singleton574540 { // the time of day manager
    using KindTag = Gfx2D::CTODManager;
    static void** Vtable() { return kSingleton574540Vtable; }
    static KindTag* Fabricate(bool create) { return Gfx2D::CreateTODManager(create); }
    static void Release() { Gfx2D::ReleaseTODManager(); }
};

static VtableSlot kPreManagerVtable_slots[] = {&LazyInstance<PreManager>::Teardown};
void** const kPreManagerVtable = reinterpret_cast<void**>(kPreManagerVtable_slots);
static VtableSlot kMcManagerVtable_slots[] = {&LazyInstance<CardSlotStub>::Teardown};
void** const kMcManagerVtable = reinterpret_cast<void**>(kMcManagerVtable_slots);
static VtableSlot kSioManagerVtable_slots[] = {&LazyInstance<SerialSlotStub>::Teardown};
void** const kSioManagerVtable = reinterpret_cast<void**>(kSioManagerVtable_slots);
static VtableSlot kAsyncLoaderVtable_slots[] = {&LazyInstance<AsyncLoader>::Teardown};
void** const kAsyncLoaderVtable = reinterpret_cast<void**>(kAsyncLoaderVtable_slots);
static VtableSlot kScriptCacheVtable_slots[] = {&LazyInstance<ScriptCache>::Teardown};
void** const kScriptCacheVtable = reinterpret_cast<void**>(kScriptCacheVtable_slots);
static VtableSlot kMlpManagerVtable_slots[] = {&LazyInstance<MainLoopSlotStub>::Teardown};
void** const kMlpManagerVtable = reinterpret_cast<void**>(kMlpManagerVtable_slots);
static VtableSlot kMdlManagerVtable_slots[] = {&LazyInstance<ModelSlotStub>::Teardown};
void** const kMdlManagerVtable = reinterpret_cast<void**>(kMdlManagerVtable_slots);
static VtableSlot kSoundManagerVtable_slots[] = {&LazyInstance<SoundManager>::Teardown};
void** const kSoundManagerVtable = reinterpret_cast<void**>(kSoundManagerVtable_slots);
static VtableSlot kFrontEndVtable_slots[] = {&LazyInstance<FrontEnd>::Teardown};
void** const kFrontEndVtable = reinterpret_cast<void**>(kFrontEndVtable_slots);
static VtableSlot kObjectTrackerVtable_slots[] = {&LazyInstance<ObjectTracker>::Teardown};
void** const kObjectTrackerVtable = reinterpret_cast<void**>(kObjectTrackerVtable_slots);
static VtableSlot kInputManagerVtable_slots[] = {&LazyInstance<InputManager>::Teardown};
void** const kInputManagerVtable = reinterpret_cast<void**>(kInputManagerVtable_slots);
static VtableSlot kSingleton4088E0Vtable_slots[] = {&LazyInstance<Singleton4088E0>::Teardown};
void** const kSingleton4088E0Vtable = reinterpret_cast<void**>(kSingleton4088E0Vtable_slots);
static VtableSlot kNetManagerVtable_slots[] = {&LazyInstance<NetSessionSlot>::Teardown};
void** const kNetManagerVtable = reinterpret_cast<void**>(kNetManagerVtable_slots);
static VtableSlot kCompositeObjectManagerVtable_slots[] = {&LazyInstance<ObjectHubSlot>::Teardown};
void** const kCompositeObjectManagerVtable = reinterpret_cast<void**>(kCompositeObjectManagerVtable_slots);
static VtableSlot kTrickObjectManagerVtable_slots[] = {&LazyInstance<TrickPropSlotStub>::Teardown};
void** const kTrickObjectManagerVtable = reinterpret_cast<void**>(kTrickObjectManagerVtable_slots);
static VtableSlot kSingleton553DB0Vtable_slots[] = {&LazyInstance<Singleton553DB0>::Teardown};
void** const kSingleton553DB0Vtable = reinterpret_cast<void**>(kSingleton553DB0Vtable_slots);
static VtableSlot kSingleton57C0D0Vtable_slots[] = {&LazyInstance<Singleton57C0D0>::Teardown};
void** const kSingleton57C0D0Vtable = reinterpret_cast<void**>(kSingleton57C0D0Vtable_slots);
static VtableSlot kSingleton57EED0Vtable_slots[] = {&LazyInstance<Singleton57EED0>::Teardown};
void** const kSingleton57EED0Vtable = reinterpret_cast<void**>(kSingleton57EED0Vtable_slots);
static VtableSlot kSingleton5691A0Vtable_slots[] = {&LazyInstance<Singleton5691A0>::Teardown};
void** const kSingleton5691A0Vtable = reinterpret_cast<void**>(kSingleton5691A0Vtable_slots);
static VtableSlot kSingleton53F520Vtable_slots[] = {&LazyInstance<Singleton53F520>::Teardown};
void** const kSingleton53F520Vtable = reinterpret_cast<void**>(kSingleton53F520Vtable_slots);
static VtableSlot kParkEditorVtable_slots[] = {&LazyInstance<ParkEditor>::Teardown};
void** const kParkEditorVtable = reinterpret_cast<void**>(kParkEditorVtable_slots);
static VtableSlot kSingleton4ACAF0Vtable_slots[] = {&LazyInstance<Singleton4ACAF0>::Teardown};
void** const kSingleton4ACAF0Vtable = reinterpret_cast<void**>(kSingleton4ACAF0Vtable_slots);
static VtableSlot kNodeManagerVtable_slots[] = {&LazyInstance<NodeManager>::Teardown};
void** const kNodeManagerVtable = reinterpret_cast<void**>(kNodeManagerVtable_slots);
static VtableSlot kSingleton574540Vtable_slots[] = {&LazyInstance<Singleton574540>::Teardown};
void** const kSingleton574540Vtable = reinterpret_cast<void**>(kSingleton574540Vtable_slots);

} // namespace

} // namespace Spt
