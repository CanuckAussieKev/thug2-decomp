// The out-of-line LazyInstance<T> instances (single_instance.h): one destructor and one scalar deleting destructor per
// singleton, at the addresses the linker gave them, and the one constructor that wasn't inlined.

#include "engine/core/single_instance.h"

#include <cstdint>

#include "engine/assets/resource_registry.h"
#include "engine/file/async.h"
#include "game/park_editing/park_editor.h"
#include "engine/file/archive.h"
#include "frontend/front_end.h"
#include "frontend/screen_element_manager.h"
#include "engine/gfx/tod_manager.h"
#include "game/save_cards/memcard.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/trick_objects.h"
#include "engine/objects/tracker.h"
#include "engine/script/cache.h"
#include "engine/script/node_manager.h"
#include "engine/sound/sfx.h"
#include "engine/pad_input/device.h"
#include "engine/pad_input/input_hub.h"
#include "game/skate/profile_manager.h"
#include "game/skate/highscores.h"
#include "engine/sys/mlp.h"
#include "engine/sys/game_module.h"
#include "engine/core/code_address.h"
#include "game/skate/skate_module.h"
#include "game/skate/game_mode.h"

namespace single {

// 0x004E1970
PointerMember* PointerMember::make_new(void* pointer)
{
    pointer_ = pointer;
    return this;
}

// 0x00469490
void* PointerMember::get_pull() { return pointer_; }

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
extern void** const kSingleton4088e0Vtable;
extern void** const kNetManagerVtable;
extern void** const kCompositeObjectManagerVtable;
extern void** const kTrickObjectManagerVtable;
extern void** const kSingleton553dB0Vtable;
extern void** const kSingleton57c0d0Vtable;
extern void** const kSingleton57eEd0Vtable;
extern void** const kSingleton5691a0Vtable;
extern void** const kSingleton53f520Vtable;
extern void** const kParkEditorVtable;
extern void** const kSingleton4aCaf0Vtable;
extern void** const kNodeManagerVtable;
extern void** const kSingleton574540Vtable;

struct PreManager {
    using KindTag = pre::ArchiveManager;
    static void** Vtable() { return kPreManagerVtable; }
    static KindTag* fabricate(bool create) { return pre::create_manager(create); }
    static void Release() { pre::destroy_manager(); }
};
struct CardSlotStub {
    using KindTag = save_cards::Hub;
    static void** Vtable() { return kMcManagerVtable; }
    static KindTag* fabricate(bool create) { return save_cards::create_manager(create); }
    static void Release() { save_cards::release_manager(); }
};
struct SerialSlotStub {
    using KindTag = sio::Hub;
    static void** Vtable() { return kSioManagerVtable; }
    static KindTag* fabricate(bool create) { return sio::create_manager(create); }
    static void Release() { sio::release_manager(); }
};
struct AsyncLoader {
    using KindTag = file::AsyncLoaderModule;
    static void** Vtable() { return kAsyncLoaderVtable; }
    static KindTag* fabricate(bool create) { return file::create_async_loader_module(create); }
    static void Release() { file::destroy_async_loader_module(); }
};
struct ScriptStoreInstance {
    using KindTag = script::ScriptStore;
    static void** Vtable() { return kScriptCacheVtable; }
    static KindTag* fabricate(bool create) { return script::create_script_cache(create); }
    static void Release() { script::release_script_cache(); }
};
struct MainLoopSlotStub {
    using KindTag = frame_loop::Hub;
    static void** Vtable() { return kMlpManagerVtable; }
    static KindTag* fabricate(bool create) { return frame_loop::create_manager(create); }
    static void Release() { frame_loop::release_manager(); }
};

// Named by their Create functions where there's no better name yet (sys::Main makes them all).
struct ModelSlotStub {
    using KindTag = modules::Hub;
    static void** Vtable() { return kMdlManagerVtable; }
    static KindTag* fabricate(bool create) { return modules::create_manager(create); }
    static void Release() { modules::release_manager(); }
};
struct
    SoundManager { // the sound effects manager (its Create returns nothing: no LazyInstance<SoundManager> is constructed)
    using KindTag = void;
    static void** Vtable() { return kSoundManagerVtable; }
    static void fabricate(bool create) { sound::create_manager(create); }
    static void Release() { sound::release_manager(); }
};
struct FrontEnd {
    using KindTag = modules::FrontEnd;
    static void** Vtable() { return kFrontEndVtable; }
    static KindTag* fabricate(bool create) { return modules::create_front_end(create); }
    static void Release() { modules::release_front_end(); }
};
struct ObjectTracker {
    using KindTag = objects::EventWaitBoard;
    static void** Vtable() { return kObjectTrackerVtable; }
    static KindTag* fabricate(bool create) { return objects::create_tracker(create); }
    static void Release() { objects::release_tracker(); }
};
struct InputManager {
    using KindTag = pad_input::Hub;
    static void** Vtable() { return kInputManagerVtable; }
    static KindTag* fabricate(bool create) { return pad_input::create_manager(create); }
    static void Release() { pad_input::release_manager(); }
};
struct Singleton4088e0 { // the asset manager
    using KindTag = assets::ResourceRegistry;
    static void** Vtable() { return kSingleton4088e0Vtable; }
    static KindTag* fabricate(bool create) { return assets::create_asset_manager(create); }
    static void Release() { assets::release_asset_manager(); }
};
struct NetSessionSlot {
    using KindTag = net::Hub;
    static void** Vtable() { return kNetManagerVtable; }
    static KindTag* fabricate(bool create) { return net::create_manager(create); }
    static void Release() { net::release_manager(); }
};
struct ObjectHubSlot {
    using KindTag = objects::EntityHub;
    static void** Vtable() { return kCompositeObjectManagerVtable; }
    static KindTag* fabricate(bool create) { return objects::create_composite_object_manager(create); }
    static void Release() { objects::release_composite_object_manager(); }
};
struct TrickPropSlotStub {
    using KindTag = objects::TrickSpotHub;
    static void** Vtable() { return kTrickObjectManagerVtable; }
    static KindTag* fabricate(bool create) { return objects::acquire_trick_object_manager(create); }
    static void Release() { objects::release_trick_object_manager(); }
};
struct Singleton553dB0 { // the game mode
    using KindTag = goals::MatchRules;
    static void** Vtable() { return kSingleton553dB0Vtable; }
    static KindTag* fabricate(bool create) { return goals::MatchRules::sole(create); }
    static void Release() { goals::MatchRules::get_release(); }
};
struct Singleton57c0d0 { // the player profile manager
    using KindTag = objects::ProfileRoster;
    static void** Vtable() { return kSingleton57c0d0Vtable; }
    static KindTag* fabricate(bool create) { return objects::ProfileRoster::fabricate(create); }
    static void Release() { objects::ProfileRoster::get_release(); }
};
struct Singleton57eEd0 { // the game records
    using KindTag = highscores::BestResults;
    static void** Vtable() { return kSingleton57eEd0Vtable; }
    static KindTag* fabricate(bool create) { return highscores::BestResults::fabricate(create); }
    static void Release() { highscores::BestResults::get_release(); }
};
struct Singleton5691a0 { // the skate module
    using KindTag = modules::Skate;
    static void** Vtable() { return kSingleton5691a0Vtable; }
    static KindTag* fabricate(bool create) { return modules::Skate::spawn_occurrence(create); }
    static void Release() { modules::Skate::discard_occurrence(); }
};
struct Singleton53f520 { // the online game manager (online::Hub)
    using KindTag = online::Hub;
    static void** Vtable() { return kSingleton53f520Vtable; }
    static KindTag* fabricate(bool create) { return online::Hub::fabricate(create); }
    static void Release() { online::release_manager(); }
};
struct ParkEditor {
    using KindTag = park_editing::ParkBuilderMode;
    static void** Vtable() { return kParkEditorVtable; }
    static KindTag* fabricate(bool create) { return park_editing::ParkBuilderMode::fabricate(create); }
    static void Release() { park_editing::ParkBuilderMode::get_release(); }
};
struct Singleton4aCaf0 { // the screen element manager (its Create returns nothing, as SoundManager's)
    using KindTag = void;
    static void** Vtable() { return kSingleton4aCaf0Vtable; }
    static void fabricate(bool create) { frontend::create_screen_element_manager(create); }
    static void Release() { frontend::delete_screen_element_manager(); }
};
struct NodeManager {
    using KindTag = level_nodes::NodeManager;
    static void** Vtable() { return kNodeManagerVtable; }
    static KindTag* fabricate(bool create) { return level_nodes::create_node_manager(create); }
    static void Release() { level_nodes::release_node_manager(); }
};
using Singleton45d690 = NodeManager;
struct Singleton574540 { // the time of day manager
    using KindTag = gfx::CtodManager;
    static void** Vtable() { return kSingleton574540Vtable; }
    static KindTag* fabricate(bool create) { return gfx::create_tod_manager(create); }
    static void Release() { gfx::release_tod_manager(); }
};

static VtableSlot g_pre_manager_vtable_slots[] = {&LazyInstance<PreManager>::teardown};
void** const kPreManagerVtable = reinterpret_cast<void**>(g_pre_manager_vtable_slots);
static VtableSlot g_mc_manager_vtable_slots[] = {&LazyInstance<CardSlotStub>::teardown};
void** const kMcManagerVtable = reinterpret_cast<void**>(g_mc_manager_vtable_slots);
static VtableSlot g_sio_manager_vtable_slots[] = {&LazyInstance<SerialSlotStub>::teardown};
void** const kSioManagerVtable = reinterpret_cast<void**>(g_sio_manager_vtable_slots);
static VtableSlot g_async_loader_vtable_slots[] = {&LazyInstance<AsyncLoader>::teardown};
void** const kAsyncLoaderVtable = reinterpret_cast<void**>(g_async_loader_vtable_slots);
static VtableSlot g_script_cache_vtable_slots[] = {&LazyInstance<ScriptStoreInstance>::teardown};
void** const kScriptCacheVtable = reinterpret_cast<void**>(g_script_cache_vtable_slots);
static VtableSlot g_mlp_manager_vtable_slots[] = {&LazyInstance<MainLoopSlotStub>::teardown};
void** const kMlpManagerVtable = reinterpret_cast<void**>(g_mlp_manager_vtable_slots);
static VtableSlot g_mdl_manager_vtable_slots[] = {&LazyInstance<ModelSlotStub>::teardown};
void** const kMdlManagerVtable = reinterpret_cast<void**>(g_mdl_manager_vtable_slots);
static VtableSlot g_sound_manager_vtable_slots[] = {&LazyInstance<SoundManager>::teardown};
void** const kSoundManagerVtable = reinterpret_cast<void**>(g_sound_manager_vtable_slots);
static VtableSlot g_front_end_vtable_slots[] = {&LazyInstance<FrontEnd>::teardown};
void** const kFrontEndVtable = reinterpret_cast<void**>(g_front_end_vtable_slots);
static VtableSlot g_object_tracker_vtable_slots[] = {&LazyInstance<ObjectTracker>::teardown};
void** const kObjectTrackerVtable = reinterpret_cast<void**>(g_object_tracker_vtable_slots);
static VtableSlot g_input_manager_vtable_slots[] = {&LazyInstance<InputManager>::teardown};
void** const kInputManagerVtable = reinterpret_cast<void**>(g_input_manager_vtable_slots);
static VtableSlot g_singleton_4088e_0_vtable_slots[] = {&LazyInstance<Singleton4088e0>::teardown};
void** const kSingleton4088e0Vtable = reinterpret_cast<void**>(g_singleton_4088e_0_vtable_slots);
static VtableSlot g_net_manager_vtable_slots[] = {&LazyInstance<NetSessionSlot>::teardown};
void** const kNetManagerVtable = reinterpret_cast<void**>(g_net_manager_vtable_slots);
static VtableSlot g_composite_object_manager_vtable_slots[] = {&LazyInstance<ObjectHubSlot>::teardown};
void** const kCompositeObjectManagerVtable = reinterpret_cast<void**>(g_composite_object_manager_vtable_slots);
static VtableSlot g_trick_object_manager_vtable_slots[] = {&LazyInstance<TrickPropSlotStub>::teardown};
void** const kTrickObjectManagerVtable = reinterpret_cast<void**>(g_trick_object_manager_vtable_slots);
static VtableSlot g_singleton_553d_b0_vtable_slots[] = {&LazyInstance<Singleton553dB0>::teardown};
void** const kSingleton553dB0Vtable = reinterpret_cast<void**>(g_singleton_553d_b0_vtable_slots);
static VtableSlot g_singleton_57c_0d_0_vtable_slots[] = {&LazyInstance<Singleton57c0d0>::teardown};
void** const kSingleton57c0d0Vtable = reinterpret_cast<void**>(g_singleton_57c_0d_0_vtable_slots);
static VtableSlot g_singleton_57e_ed0_vtable_slots[] = {&LazyInstance<Singleton57eEd0>::teardown};
void** const kSingleton57eEd0Vtable = reinterpret_cast<void**>(g_singleton_57e_ed0_vtable_slots);
static VtableSlot g_singleton_5691a_0_vtable_slots[] = {&LazyInstance<Singleton5691a0>::teardown};
void** const kSingleton5691a0Vtable = reinterpret_cast<void**>(g_singleton_5691a_0_vtable_slots);
static VtableSlot g_singleton_53f_520_vtable_slots[] = {&LazyInstance<Singleton53f520>::teardown};
void** const kSingleton53f520Vtable = reinterpret_cast<void**>(g_singleton_53f_520_vtable_slots);
static VtableSlot g_park_editor_vtable_slots[] = {&LazyInstance<ParkEditor>::teardown};
void** const kParkEditorVtable = reinterpret_cast<void**>(g_park_editor_vtable_slots);
static VtableSlot g_singleton_4a_caf0_vtable_slots[] = {&LazyInstance<Singleton4aCaf0>::teardown};
void** const kSingleton4aCaf0Vtable = reinterpret_cast<void**>(g_singleton_4a_caf0_vtable_slots);
static VtableSlot g_node_manager_vtable_slots[] = {&LazyInstance<NodeManager>::teardown};
void** const kNodeManagerVtable = reinterpret_cast<void**>(g_node_manager_vtable_slots);
static VtableSlot g_singleton574540_vtable_slots[] = {&LazyInstance<Singleton574540>::teardown};
void** const kSingleton574540Vtable = reinterpret_cast<void**>(g_singleton574540_vtable_slots);

} // namespace

} // namespace single
