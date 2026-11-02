// Script commands (CFunctions) of the skate game, 0x0059F610-0x0059FE50: the viewer, controllers, node arrays,
// preferences, some network flags (see game_cfuncs.h).

#include "engine/script/game_cfuncs.h"
#include "engine/objects/camera_component.h"

#include "engine/core/str.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <iterator>

#include "engine/assets/resource_registry.h"
#include "engine/assets/resource_entry.h"
#include "engine/core/checksums.h"
#include "engine/file/file_load.h"
#include "engine/file/packs.h"
#include "engine/gfx/tod_manager.h"
#include "engine/maths/vec4.h"
#include "engine/objects/rail_manager.h"
#include "engine/script/nodes.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/input_component.h"
#include "engine/pad_input/input_hub.h"
#include "game/rider/trick_component.h"
#include "engine/sys/mlp.h"
#include "platform/stopwatch.h"
#include "engine/file/async.h"
#include "frontend/front_end.h"
#include "engine/gfx/view_cam.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/model_component.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/objects/entity_hub.h"
#include "engine/script/cscript.h"
#include "engine/script/node_manager.h"
#include "engine/script/preferences.h"
#include "engine/script/skate_cfuncs.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/device.h"
#include "game/skate/profile_manager.h"
#include "game/rider/rider.h"
#include "engine/sys/model_viewer.h"
#include "engine/sys/viewer_object.h"
#include "game/skate/skate_module.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/face_texture.h"
#include "platform/winmain.h"
#include "engine/gfx/movie.h"
#include "platform/crt.h"
#include "platform/compiler.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace commands {

bool g_trying_to_join;
std::array<char, 128> g_node_list_name;
namespace {
gsl::owner<tasks::Task<void>*> g_second_controller_check;
int g_view_mode_cfunc;
std::array<char, 32> g_password_stars;
} // namespace
bool g_got_extra_memory;

namespace {

constexpr uint32_t kModel = 0x286A8D26;              // "model" (the component)
constexpr uint32_t kCamera = 0xC4E311FA;             // "Camera" (an object's camera component)
constexpr uint32_t kController = 0xB30D9965;         // "controller"
constexpr uint32_t kLeftX = 0x303067F1;              // "leftx"
constexpr uint32_t kLeftY = 0x47375767;              // "lefty"
constexpr uint32_t kRightX = 0x694DF774;             // "rightx"
constexpr uint32_t kRightY = 0x1E4AC7E2;             // "righty"
constexpr uint32_t kPrefType = 0x17DAE143;           // "pref_type"
constexpr uint32_t kUiString = 0x96875C0F;           // "ui_string"
constexpr uint32_t kChecksum = 0x21902065;           // "checksum"
constexpr uint32_t kCustomSkaterFile = 0xF36C1878;   // (name unknown) the profile's custom skater file name
constexpr uint32_t kBothControllersIn = 0x07C28B45;  // (name unknown) the script run while both are plugged in
constexpr uint32_t kControllerMissing = 0x9C9DFCF8;  // (name unknown) ... and while one isn't
constexpr uint32_t kViewerCameraObject = 0xEB17151B; // (name unknown) the object whose camera a view mode uses
constexpr uint32_t kSkaterCamObject = 0x967C138C;    // (name unknown) the object whose camera is used otherwise

// The skate module (modules::Skate, 0x007CE478) and its code we call through the game.
modules::Skate* get_skate() { return modules::g_skate_module; }
// The current profile: the skate module's overload taking the params, with none (the original passes a 0 index in
// the params' place - 0x00564F80; every caller passes 0).
objects::RiderIdentity* current_account()
{
    return get_skate()->fetch_account(static_cast<script::Struct*>(nullptr));
}
objects::Rider* get_rider_at(int index)
{
    return core::down_cast<objects::Rider>(get_skate()->get_rider_at(index));
}

// The object manager (0x006F0FB8).
objects::ObjectHubBase* object_edit()
{
    return static_cast<objects::ObjectHubBase*>(objects::g_composite_registry.get());
}

// A viewport's camera (0x004A09C0) and making `camera` a viewport's (0x004A0970).
gfx::ViewCam* viewport_camera(int viewport) { return render::fetch_live_camera(viewport); }
void set_viewport_camera(int viewport, gfx::ViewCam* camera) { render::choose_camera(viewport, camera); }
// A camera component's camera.
gfx::ViewCam* viewpoint_camera(objects::ObjectModule* camera)
{
    return core::down_cast<objects::ViewpointModule>(camera)->view_camera_.get();
}

// The preferences of a type (0x00565680: the network's, the taunts', the skate module's; null for others). The
// original passes a second argument (1) that isn't read.
prefs::Preferences* get_preferences_of_type(uint32_t type) { return modules::Skate::get_player_options(type); }

// The network game flags (online).
bool& internet_game_host() { return sys::g_internet_game_host; }
bool& joining_internet_game() { return sys::g_joining_internet_game; }
bool& trying_to_join() { return g_trying_to_join; }
bool& online_mode_flag() { return online::g_online_active; }
bool& join_server_ip_succeeded() { return online::g_join_server_ip_succeeded; }

// The second controller check's task (0x007CEBC4) and the view mode (0x007CEBCC).
int& view_mode() { return g_view_mode_cfunc; }

// GetPreferencePassword's row of stars (0x007CEBD0).
char* password_stars() { return g_password_stars.data(); }

objects::PreviewEntity* viewer_target(modules::Viewer* viewer)
{
    return viewer->get_viewer_entity();
}

} // namespace

// 0x0059F610: SetViewerAnim <anim>: the viewer's object plays the animation (if there's a viewer and an object).
bool cfunc_set_viewer_anim(script::Struct* params, script::Script*)
{
    if (modules::Viewer* viewer = modules::get_live_viewer()) {
        uint32_t anim = 0; // (unset in the original when missing - an asserted parameter)
        params->read_hash_value(0u, &anim, 1);
        if (objects::PreviewEntity* object = viewer_target(viewer))
            object->play_animation(anim);
    }
    return true;
}

// 0x0059F650: SetViewerLODDist (x, y): the viewer's object's model component gets colour value `x` (an index,
// truncated) set to `y`. False without the pair (with a viewer); true otherwise.
bool cfunc_set_viewer_lod_dist(script::Struct* params, script::Script*)
{
    modules::Viewer* viewer = modules::get_live_viewer();
    if (!viewer)
        return true;
    script::Pair pair;
    if (!params->fetch_pair(0u, &pair, false))
        return false;
    if (objects::PreviewEntity* object = viewer_target(viewer)) {
        // (the model component isn't checked for null, as in the original)
        auto* model = core::down_cast<objects::MeshInstanceModule>(object->fetch_module(kModel));
        model->set_color_value(static_cast<int>(pair.x), pair.y);
    }
    return true;
}

// 0x0059F6C0: ReloadViewerAnim <anim> "<file>": the viewer's object reloads the animation from the file.
bool cfunc_reload_viewer_anim(script::Struct* params, script::Script*)
{
    modules::Viewer* viewer = modules::get_live_viewer();
    if (!viewer)
        return true;
    objects::PreviewEntity* object = viewer_target(viewer);
    if (!object)
        return true;
    uint32_t anim = 0;
    const char* file = nullptr;
    if (params->read_hash_value(0u, &anim, 0) && params->fetch_text(0u, &file, 0))
        object->reload_animation(file, anim);
    return true;
}

// 0x0059F730: CheckForHoles (0x0048C8F0).
bool cfunc_check_for_holes(script::Struct*, script::Script*)
{
    render::print_coll_statics();
    return true;
}

// 0x0059F740: StopSecondControllerCheck: the check's task (if any) deleted.
bool cfunc_stop_second_controller_check(script::Struct*, script::Script*)
{
    delete g_second_controller_check;
    g_second_controller_check = nullptr;
    return true;
}

// 0x0059F760: QuitGame: the game ends (0x007D6A2C).
bool cfunc_quit_game(script::Struct*, script::Script*)
{
    render::g_quit_requested = true;
    return true;
}

// 0x0059F770: IsInternetGameHost.
bool cfunc_is_internet_game_host(script::Struct*, script::Script*) { return internet_game_host(); }

// 0x0059F780: IsJoiningInternetGame (logged).
bool cfunc_is_joining_internet_game(script::Struct*, script::Script*)
{
    debug::printf("ScriptIsJoiningInternetGame %d\n", joining_internet_game()); // "ScriptIsJoiningInternetGame %d\n"
    return joining_internet_game();
}

// 0x0059F7A0: TryingToJoinGame.
bool cfunc_trying_to_join_game(script::Struct*, script::Script*) { return trying_to_join(); }

// 0x0059F7B0: TryJoinServerIPSuccess: whether joining by IP worked (forgotten once no join is being tried; logged).
bool cfunc_try_join_server_ip_success(script::Struct*, script::Script*)
{
    if (!trying_to_join())
        join_server_ip_succeeded() = false;
    debug::printf("ScriptTryJoinServerIPSuccess %d\n",
                  join_server_ip_succeeded()); // "ScriptTryJoinServerIPSuccess %d\n"
    return join_server_ip_succeeded();
}

// 0x0059F7E0 / 0x0059F7F0: GSEnableNet / GSDisableNet.
bool cfunc_gs_enable_net(script::Struct*, script::Script*)
{
    online_mode_flag() = true;
    return true;
}

bool cfunc_gs_disable_net(script::Struct*, script::Script*)
{
    online_mode_flag() = false;
    return true;
}

// 0x0059F800: WaitForAsyncLoadToFinish: waits for every asynchronous file read.
bool cfunc_wait_for_async_load_to_finish(script::Struct*, script::Script*)
{
    if (file::async_busy())
        file::async_wait_for_io(true);
    return true;
}

namespace {
// 0x0059F820
void second_pad_watch_tick(tasks::Task<void>*)
{
    sio::Hub* devices = sio::manager_instance().get();
    const sio::InputPort* first = devices->get_pad_at_slot(0);
    const sio::InputPort* second = devices->get_pad_at_slot(1);
    if (second->is_attached() && first->is_attached())
        script::launch_script(kBothControllersIn, nullptr, nullptr, false, nullptr);
    else
        script::launch_script(kControllerMissing, nullptr, nullptr, false, nullptr);
}
} // namespace

// 0x0059F890
bool make_camera_active(uint32_t id, int viewport, bool take_current_view)
{
    auto* object = core::down_cast<objects::GameEntity>(object_edit()->look_up_entity(id));
    if (!object)
        return false;
    objects::ObjectModule* camera = object->fetch_module(kCamera);
    if (!camera)
        return true;
    if (take_current_view) {
        object->basis_frame_ = *viewport_camera(viewport)->fetch_mat();
        object->at_point_ = *viewport_camera(viewport)->fetch_location();
        camera->tick_frame(); // (vtable slot 1)
    }
    set_viewport_camera(viewport, viewpoint_camera(camera)); // the component's camera
    return true;
}

// 0x0059F930: GetCustomSkaterFilename: the first profile's custom skater file name ("Unimplemented" if none) as
// the script's 0xF36C1878.
bool cfunc_get_custom_skater_filename(script::Struct*, script::Script* script)
{
    objects::RiderIdentity* profile = current_account();
    script->args_->append_word(kCustomSkaterFile, profile->get_string_f_36c_1878());
    return true;
}

// 0x0059F960: SetCustomSkaterFilename ["<name>" ("Unimplemented")]: the first profile's custom skater file name.
bool cfunc_set_custom_skater_filename(script::Struct* params, script::Script*)
{
    objects::RiderIdentity* profile = current_account();
    const char* name = "Unimplemented"; // "Unimplemented"
    params->fetch_text(0u, &name, 0);
    profile->set_string_f_36c_1878(name);
    return true;
}

// 0x0059F9A0: ControllerPressed <button> [controller]: whether the button is held on that controller (the front
// end's handler) - without a number, on either. False without a button.
bool cfunc_controller_pressed(script::Struct* params, script::Script*)
{
    const modules::FrontEnd* front_end = modules::front_end_instance().get();
    uint32_t button = 0;
    if (!params->read_hash_value(0u, &button, 0))
        return false;
    int controller = 0;
    if (params->fetch_int_value(0u, &controller, 0))
        return controller_pressed(core::at(front_end->pad_listeners_, controller).get(), button);
    for (const auto& pad_listener : front_end->pad_listeners_) {
        if (controller_pressed(pad_listener.get(), button))
            return true;
    }
    return false;
}

// 0x0059FA40: GetAnalogueInfo [controller=n (0)]: the controller's sticks as leftx / lefty / rightx / righty in
// the script's parameters - false (and none) if it isn't plugged in and valid.
bool cfunc_get_analogue_info(script::Struct* params, script::Script* script)
{
    const modules::FrontEnd* front_end = modules::front_end_instance().get();
    int controller = 0;
    params->fetch_int_value(kController, &controller, 0);
    const modules::FrontEndInputHandler* handler = core::at(front_end->pad_listeners_, controller).get();
    objects::PadSnapshot pad;
    pad.tick_frame(handler->control_data_);
    auto* device = handler->dev_;
    if (!device->is_attached() || !device->is_usable_)
        return false;
    script->args_->push_float(kLeftX, core::at(pad.scaled_stick_, 2));
    script->args_->push_float(kLeftY, core::at(pad.scaled_stick_, 3));
    script->args_->push_float(kRightX, core::at(pad.scaled_stick_, 0));
    script->args_->push_float(kRightY, core::at(pad.scaled_stick_, 1));
    return true;
}

// 0x0059FB10
void destroy_object_with_id(objects::Entity* object, void* id)
{
    if (object->ident_ == *static_cast<uint32_t*>(id))
        object->dispose_when_free();
}

// 0x0059FB30: NodeArrayManLoad "<file>": the node array's name noted (0x007CE6D8, copied unchecked) and the file
// loaded as an asset of type 11 (the node array manager's).
bool cfunc_node_array_man_load(script::Struct* params, script::Script*)
{
    const char* file = nullptr;
    params->fetch_text(0u, &file, 1);
    text::copy_unbounded(g_node_list_name.data(), file);
    assets::asset_manager()->load_asset(file, 11, 0, 0, false, false, false, nullptr, nullptr);
    debug::printf("p_file = %s\n", file); // "p_file = %s\n"
    return true;
}

// 0x0059FB90: NodeArrayManUnload "<file>": the node manager drops the file's node array.
bool cfunc_node_array_man_unload(script::Struct* params, script::Script*)
{
    const char* file = nullptr;
    params->fetch_text(0u, &file, 1);
    level_nodes::node_manager_instance()->remove_node_array_of_file(file);
    return true;
}

// 0x0059FBC0: GetPreferenceString <field> pref_type=<type>: the preference's ui_string, as the script's ui_string.
// Without pref_type the original reads the type from the slot it would have been written to - the `script`
// argument's (kept: a type no set has, so a null set; the call on it crashes, as does a missing field's
// preference).
bool cfunc_get_preference_string(script::Struct* params, script::Script* script)
{
    script::Struct* results = script->args_;
    uint32_t field = 0;
    params->read_hash_value(0u, &field, 0);
    uint32_t type = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value(kPrefType, &type, 0);
    script::Struct* preference = get_preferences_of_type(type)->fetch_setting(field);
    const char* text = nullptr; // (unset in the original when missing)
    preference->fetch_text(kUiString, &text, 0);
    results->append_word(kUiString, text);
    return true;
}

// 0x0059FC40: GetPreferencePassword <field> pref_type=<type>: as many stars as the preference's ui_string has
// characters (in a static buffer, 0x007CEBD0, unchecked), as the script's password_string.
bool cfunc_get_preference_password(script::Struct* params, script::Script* script)
{
    script::Struct* results = script->args_;
    uint32_t field = 0;
    params->read_hash_value(0u, &field, 0);
    uint32_t type = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value("pref_type", &type, 0);     // "pref_type"
    script::Struct* preference = get_preferences_of_type(type)->fetch_setting(field);
    const char* password = nullptr;                    // (unset in the original when missing)
    preference->fetch_text("ui_string", &password, 0); // "ui_string"
    char* stars = password_stars();
    const size_t length = std::strlen(password);
    for (size_t i = 0; i < length; ++i)
        stars[i] = '*';
    stars[length] = '\0';
    results->append_word("password_string", stars); // "password_string"
    return true;
}

// 0x0059FD00: GetPreferenceChecksum <field> pref_type=<type>: the preference's checksum, as the script's checksum.
bool cfunc_get_preference_checksum(script::Struct* params, script::Script* script)
{
    script::Struct* results = script->args_;
    uint32_t field = 0;
    params->read_hash_value(0u, &field, 0);
    uint32_t type = 0; // (missing: 0 - the original read an argument's stack slot, an address no name has)
    params->read_hash_value("pref_type", &type, 0);     // "pref_type"
    script::Struct* preference = get_preferences_of_type(type)->fetch_setting(field);
    uint32_t value = 0; // (unset in the original when missing)
    preference->read_hash_value(kChecksum, &value, 0);
    results->put_name_key(kChecksum, value);
    return true;
}

// 0x0059FD80: ToggleNetMetrics.
bool cfunc_toggle_net_metrics(script::Struct*, script::Script*)
{
    online::g_online_hub->toggle_net_metrics();
    return true;
}

// 0x0059FD90: change_view_mode <mode>: the viewer's view mode (0 = off); in one, the camera of the object 0xEB17151B,
// else viewport 0 gets the camera of 0x967C138C (if there is one) back. Every skater's pause state is set to the
// mode, and the viewer lets go of its object.
namespace {

// The view mode `mode` (0x007CEBCC and the viewer's): in one, the camera of the object 0xEB17151B; else viewport 0
// gets the camera of 0x967C138C (if there is one) back. Every skater's pause state is set to the mode.
void apply_view_mode(int mode)
{
    view_mode() = mode;
    modules::change_view_mode(mode);
    if (view_mode()) {
        make_camera_active(kViewerCameraObject, 0, true);
    } else if (objects::Entity* object = object_edit()->look_up_entity(kSkaterCamObject)) {
        if (objects::ObjectModule* camera = core::down_cast<objects::GameEntity>(object)->fetch_module(kCamera))
            set_viewport_camera(0, viewpoint_camera(camera));
    }
    for (int i = 0; i < 8; ++i) {
        if (objects::Rider* skater = get_rider_at(i))
            skater->set_pause_state(view_mode());
    }
}

} // namespace

// 0x0059FD90: change_view_mode <mode>: the view mode (apply_view_mode), and the viewer lets go of its object.
bool cfunc_set_view_mode(script::Struct* params, script::Script*)
{
    int mode = 0; // (unset in the original when missing - an asserted parameter)
    params->fetch_int_value(0u, &mode, 1);
    apply_view_mode(mode);
    if (modules::Viewer* viewer = modules::get_live_viewer())
        viewer->drop_viewer_entity();
    return true;
}

// 0x005A1C80: SetViewerModel <...>: view mode 1; the viewer (if any) places its camera and makes a viewer object
// from the parameters.
bool cfunc_set_viewer_model(script::Struct* params, script::Script*)
{
    modules::Viewer* viewer = modules::get_live_viewer();
    apply_view_mode(1);
    if (viewer) {
        viewer->place_camera(params);
        script::dump_to_log(params, 0);
        viewer->create_viewer_object(params);
    }
    return true;
}

// 0x005A1D40: GetViewerObjectID: the viewer object's id as the script's 0x0830ECAF and 0x52280066 (the object isn't
// checked for null).
bool cfunc_get_viewer_object_id(script::Struct*, script::Script* script)
{
    if (modules::Viewer* viewer = modules::get_live_viewer()) {
        const objects::PreviewEntity* object = viewer_target(viewer);
        script->args_->put_name_key(0x0830ECAF, object->ident_);
        script->args_->put_name_key(0x52280066, object->ident_);
    }
    return true;
}

// 0x005A1D80: ReloadAnim 0x52A864B0=<anim> 0x98549BA4=<base> 0xC3F4169A="<file>": the animation asset (by base +
// anim, or without a base the file name's checksum) unloaded and loaded again from the file (in the debug heap while
// 0x007CEFAC is set). False if it isn't loaded.
bool cfunc_reload_anim(script::Struct* params, script::Script*)
{
    uint32_t anim = 0;
    params->read_hash_value(0x52A864B0u, &anim, 0);
    uint32_t base = 0;
    params->read_hash_value(0x98549BA4u, &base, 0);
    const char* file = ""; // ""
    params->fetch_text(0xC3F4169Au, &file, 0);
    assets::ResourceRegistry* assets = assets::asset_manager();
    uint32_t name = checksums::hash_from_text(file);
    if (base)
        name = base + anim;
    assets::Resource* asset = assets->obtain_loaded_item(name, 0);
    if (!asset)
        return false;
    assets->discard_loaded_item(asset);
    const bool debug_heap = g_got_extra_memory;
    if (debug_heap) {
        memory::Hub* memory = memory::Hub::get_pull();
        memory->enter_heap_scope(static_cast<memory::MemSource*>(memory->debug_heap_));
    }
    assets->load_anim(file, base, anim, false, false);
    if (g_got_extra_memory)
        memory::Hub::get_pull()->leave_heap_scope();
    return true;
}

// 0x005A1E70: Debounce <button> [time=seconds (1)] [0x1A4E0EF9=n]: every input component locks the button for that
// long (released if n), and every trick component holds it off until the clock (ms) + time * 1000. Nothing without
// a button.
bool cfunc_debounce(script::Struct* params, script::Script*)
{
    uint32_t button = 0;
    params->read_hash_value(0u, &button, 0);
    if (!button)
        return true;
    float time = 1.0f;
    params->fetch_float(0x906B67BAu, &time, 0); // "time"
    int release = 0;
    params->fetch_int_value(0x1A4E0EF9u, &release, 0);
    auto* objects = objects::g_composite_registry.get();
    for (objects::ObjectModule* input = objects->find_first_module_of_kind(0x27D7CD28); input;
         input = input->next_of_type_)
        core::down_cast<objects::PadInputModule>(input)->lock_button(button, time, release != 0);
    // x87: the unsigned clock + time * 1000.0 (0x006453C0), stored as a float
    auto until = static_cast<float>(static_cast<double>(frame_clock::get_time()) +
                                    static_cast<double>(time) * static_cast<double>(gfx::g_thousand));
    for (objects::ObjectModule* trick = objects->find_first_module_of_kind(0x270F56E1); trick;
         trick = trick->next_of_type_)
        core::down_cast<objects::MoveExecutor>(trick)->hold_off_button(pad_input::glyph_for_button_tag(button), until);
    return true;
}

// 0x005A2050: StartKeyboardHandler 0x69E3A1C4=<device>: typed keys reported for it.
bool cfunc_start_keyboard_handler(script::Struct* params, script::Script*)
{
    modules::FrontEnd* front_end = modules::front_end_instance().get();
    int device = 0; // (unset in the original when missing - an asserted parameter)
    params->fetch_int_value(0x69E3A1C4u, &device, true);
    front_end->register_key_listener(device);
    return true;
}

// 0x005A2080: StopKeyboardHandler.
bool cfunc_stop_keyboard_handler(script::Struct*, script::Script*)
{
    modules::front_end_instance()->unregister_key_listener();
    return true;
}

// 0x005A2100: SpawnSecondControllerCheck: unless it's on, the check's task (tasks::Task, second_pad_watch_tick,
// priority 0) made and added to the main loop's logic tasks. (The original gives the task a pointer to one of its
// own locals as its data - the task never reads it; null here.)
bool cfunc_spawn_second_controller_check(script::Struct*, script::Script*)
{
    auto* main_loop = assets::g_main_loop_manager.get();
    if (g_second_controller_check)
        return true;
    const tasks::Task<void>::Code code = &second_pad_watch_tick;
    auto task = memory::make_owned<tasks::Task<void>>(code, nullptr, 0);
    g_second_controller_check = task;
    main_loop->logic_tasks_.queue_job(task);
    return true;
}

namespace {

constexpr uint32_t kPos = 0x7F261953;      // "pos"
constexpr uint32_t kPosition = 0xB9D31B0A; // "position"

// The node classes MoveNode knows.
constexpr uint32_t kClassClimbingNode = 0x30C19600; // "ClimbingNode"
constexpr uint32_t kClassRailNode = 0x8E6B02AD;     // "RailNode"

bool fetch_place(script::Struct* structure, script::VectorValue* out)
{
    return structure->fetch_vec(kPosition, out, 0) || structure->fetch_vec(kPos, out, 0);
}

// The game's C runtime (library code, called through the game).
gsl::owner<void*> game_fopen(const char* name, const char* mode) { return crt::get_fopen(name, mode); }
int game_fseek(void* file, long offset, int origin) { return crt::get_fseek(static_cast<FILE*>(file), offset, origin); }
long game_ftell(void* file) { return crt::get_ftell(static_cast<FILE*>(file)); }
gsl::owner<void*> game_malloc(size_t size) { return crt::alloc_block(size); }
size_t game_fread(void* buffer, size_t size, size_t count, void* file)
{
    return crt::get_fread(buffer, size, count, static_cast<FILE*>(file));
}
int game_fclose(gsl::owner<void*> file) { return crt::get_fclose(memory::adopt<FILE*>(file)); }
void game_free(gsl::owner<void*> memory) { crt::dealloc(memory); }
double game_atof(const char* text) { return crt::get_atof(text); }

// ShowTracking's debug line drawing - compiled out of this build (an empty call, 0x005480A0).
inline void draw_debug_line(const maths::Vec4* /*from*/, const maths::Vec4* /*to*/, uint32_t /*colour1*/,
                            uint32_t /*colour2*/, int /*unused*/)
{}
// ...and the empty call at its start (no arguments).
inline void compiled_out_debug_call() {}

} // namespace

// 0x005A21C0: MoveNode name=<node> position= / pos=<vector>: the node's pos (the node needs a position already) and
// its object's position (if there is one) set. Rail and climbing nodes are moved in the rail manager too; the other
// known classes (Pedestrian, ProximNode, Vehicle, LevelObject, LevelGeometry, GameObject, 0x061A741E) need nothing
// more; any other class: false. Also false (with a message) without the name or a position.
bool cfunc_move_node(script::Struct* params, script::Script*)
{
    uint32_t name = 0;
    if (!params->read_hash_value(0xA1DC81F9u, &name, 0)) { // "name"
        debug::printf("Missing Name\n");                   // "Missing Name\n"
        return false;
    }
    script::Struct* node = level_nodes::fetch_vertex(name, false);
    if (!node)
        return false;
    script::VectorValue position{};
    if (!fetch_place(params, &position)) {
        debug::printf("Missing position parameter in MoveNode\n"); // "Missing position parameter in MoveNode\n"
        return false;
    }
    script::VectorValue old{};
    if (!fetch_place(node, &old)) {
        debug::printf("node is missing position\n"); // "node is missing position\n"
        return false;
    }
    node->put_vec3(kPos, position.x, position.y, position.z);
    if (objects::Entity* object = object_edit()->look_up_entity(name))
        std::memcpy(&core::down_cast<objects::GameEntity>(object)->at_point_, &position, sizeof(position));
    uint32_t node_class = 0;
    node->read_hash_value(0x12B4E660u, &node_class, 0); // "class"
    switch (node_class) {
    case kClassClimbingNode:
    case kClassRailNode:
        modules::g_skate_module->rail_manager_->move_node(name, &position);
        return true;
    case 0xA0DFAC98: // Pedestrian
    case 0x061A741E: // (name unknown)
    case 0x08470F2E: // ProximNode
    case 0xE47F1B79: // Vehicle
    case 0xB7B3BD86: // LevelObject
    case 0xBF4D3536: // LevelGeometry
    case 0xEF59C100: // GameObject
        return true;
    default:
        return false;
    }
}

// 0x005A2370: SetActiveCamera id=<object> [0x9FD29151=viewport (0)] [0x10C1C887]: make_camera_active (taking the
// current view with the flag); its answer.
bool cfunc_set_active_camera(script::Struct* params, script::Script*)
{
    uint32_t id = 0;
    params->read_hash_value(0x40C698AFu, &id, 0); // "id"
    int viewport = 0;
    params->fetch_int_value(0x9FD29151u, &viewport, 0);
    const bool take_current_view = params->has_bare_flag(0x10C1C887u);
    return make_camera_active(id, viewport, take_current_view);
}

// 0x005A23E0: ShowTracking "<file>" [0xB426BF5A] [0x14240297] [0x969D3AF6] [0xB0FE7369] (none: all four): the
// tracking log (its drive name stripped) read in the debug heap; each "\nTracking<type>" line's six numbers make a
// line from one point to another - types 1 to 4 counted, each shown (compiled out) if its flag is on, the line
// lengthened 10 / 1 / 2 / 4 times for 1, 3, 4; other types shown always. The counts are logged. False if the file
// isn't there.
bool cfunc_show_tracking(script::Struct* params, script::Script*)
{
    compiled_out_debug_call();
    memory::Hub* memory = memory::Hub::get_pull();
    memory->enter_heap_scope(static_cast<memory::MemSource*>(memory->debug_heap_));
    bool show1 = params->has_bare_flag(0xB426BF5Au);
    bool show2 = params->has_bare_flag(0x14240297u);
    bool show3 = params->has_bare_flag(0x969D3AF6u);
    bool show4 = params->has_bare_flag(0xB0FE7369u);
    if (!show1 && !show2 && !show3 && !show4)
        show1 = show2 = show3 = show4 = true;
    const char* file = ""; // ""
    params->fetch_text(0u, &file, 0);
    if (file[1] == ':') {
        debug::printf("WARNING: stripped drive name off file\n"); // "WARNING: stripped drive name off file\n"
        file += 2;
    }
    int size = 0;
    if (!file::is_present(file)) {
        debug::printf("Can't find file: %s\n", file); // "Can't find file: %s\n"
        return false;                                 // (as in the original, the debug heap stays pushed)
    }
    void* loaded = packs::load_file_into(file, &size, nullptr);
    const auto* data = static_cast<const char*>(loaded); // (a text file)
    debug::printf("Loaded %s, size = %d, at %p\n", file, size, data); // "Loaded %s, size = %d, at %p\n"
    memory::Hub::get_pull()->leave_heap_scope();
    int count1 = 0;
    int count2 = 0;
    int count3 = 0;
    int count4 = 0;
    for (const char* at = data; at < data + size; ++at) {
        static const auto kTracking = std::to_array("\nTracking");
        int matched = 0; // (compared a byte at a time: never past the first difference)
        while (matched < 9 && at[matched] == core::at(kTracking, matched))
            ++matched;
        if (matched < 9)
            continue;
        const int type = at[9] - '0';
        at += 11;
        std::array<float, 6> values{};
        for (float& value : values) {
            value = static_cast<float>(game_atof(at));
            while (*at == '-' || *at == '.' || (*at >= '0' && *at <= '9'))
                ++at;
            ++at;
        }
        maths::Vec4 from = {core::at(values, 0), core::at(values, 1), core::at(values, 2), 1.0f};
        const maths::Vec4 to = {core::at(values, 3), core::at(values, 4), core::at(values, 5), 1.0f};
        --at;
        uint32_t colour = 0;
        switch (type) {
        case 1:
            ++count1;
            if (!show1)
                continue;
            colour = 0xFFFFFF;
            from -= (to - from) * 10.0f;
            break;
        case 2:
            ++count2;
            if (!show2)
                continue;
            colour = 0xFF;
            break;
        case 3:
            ++count3;
            if (!show3)
                continue;
            colour = 0xFFFF00;
            from -= (to - from) * 2.0f;
            break;
        case 4:
            ++count4;
            if (!show4)
                continue;
            colour = 0x8000;
            from -= (to - from) * 4.0f;
            break;
        default:
            colour = 0xFFFF;
            break;
        }
        draw_debug_line(&from, &to, colour, colour, 0);
    }
    debug::printf("Bonks = %d,  Grinds = %d, verts= %d, lines = %d\n", count1, count2, count3,
                  count4); // "Bonks = %d, ..."
    memory::free_malloc(loaded);
    return true;
}

// 0x005A2790: ChooseFaceForFacemap string="<file>": the first profile's face texture (+0x54) loaded from
// faces\<file> (read with the C runtime; the script 0x808008EA then runs). False if the file can't be opened.
bool cfunc_choose_face_for_facemap(script::Struct* params, script::Script*)
{
    // (the profile's face texture - the original read the word at +0x54 through a getter folded with the viewer's)
    gfx::FaceSkin* face = current_account()->appearance()->face_skin_;
    const char* name = nullptr;             // (unset in the original when missing - an asserted parameter)
    params->fetch_text("string", &name, 1); // "string"
    std::array<char, 0x80> path{};
    THUG2_MSVC_WARNING(suppress : 4996)    // (unbounded: as in the original)
    std::sprintf(path.data(), "faces\\%s", name); // "faces\%s"
    gsl::owner<void*> file = game_fopen(path.data(), "rb"); // "rb"
    if (!file)
        return false;
    game_fseek(file, 0, SEEK_END);
    const long size = game_ftell(file);
    game_fseek(file, 0, SEEK_SET);
    gsl::owner<void*> data = game_malloc(static_cast<size_t>(size));
    game_fread(data, 1, static_cast<size_t>(size), file);
    game_fclose(file);
    face->set_image(static_cast<const uint8_t*>(data), 0);
    game_free(data);
    face->mark_usable(true);
    script::launch_script(0x808008EA, nullptr, nullptr, false, nullptr);
    return true;
}

// 0x005A29A0: SkaterIsCustom 0x67E6859A=<n>: whether the skate module's profile n (0x00564FC0) has a face texture.
bool cfunc_skater_is_custom(script::Struct* params, script::Script*)
{
    int index = 0; // (unset in the original when missing - an asserted parameter)
    params->fetch_int_value(0x67E6859Au, &index, 1);
    return get_skate()->fetch_account(index)->appearance()->face_skin_ != nullptr; // (its face texture: +0x54)
}

// 0x005A29E0: SetSceneColor <...>: the time of day manager's.
bool cfunc_set_scene_color(script::Struct* params, script::Script*)
{
    g_tod_manager->set_scene_color(params);
    return true;
}

namespace {

constexpr uint32_t kMenuText = 0xC4745838;   // (names unknown) a menu item's text,
constexpr uint32_t kMenuId = 0x40C698AF;     // "id"
constexpr uint32_t kMenuParams = 0xB8437568; // its pad_choose parameters
constexpr uint32_t kMenuScript = 0x2B0CFCA1; // its pad_choose script
constexpr uint32_t kFirstRestartId = 123456; // the restart items' ids, from here

// A new, empty script structure (from the structures' pool) and its deletion.

// A restart menu item's fixed parts.
void add_restart_item_flags(script::Struct* item)
{
    item->put_name_key(0x9F67B2C8, 0x9F67B2C8); // (flags, names unknown)
    item->put_name_key(0x2A434D05, 0x2A434D05);
    item->put_name_key(kMenuScript, 0x506562D9);
}

} // namespace

// 0x005A4260: AddRestartsToMenu 0x1B9E8272=<scale>: an item (script 0xC16BF7BD) per restart of the level (its
// RestartName, else "Unnamed restart"; ids from 123456; the restart's node in its parameters), or "No restarts in
// level".
bool cfunc_add_restarts_to_menu(script::Struct* params, script::Script*)
{
    int count = 0;
    uint32_t restart = 0;
    while ((restart = objects::get_spawn_node(0, 0, restart)) != 0) {
        script::Struct* node = level_nodes::fetch_vertex(restart, true);
        const char* name = nullptr;
        if (!node->fetch_text("RestartName", &name, 0)) // "RestartName"
            name = "Unnamed restart";                   // "Unnamed restart"
        script::StructPtr item = script::make_struct();
        item->append_word(kMenuText, name);
        item->put_name_key(kMenuId, kFirstRestartId + count);
        add_restart_item_flags(item.get());
        script::StructPtr choose = script::make_struct();
        choose->put_name_key(0x66780C6F, restart);
        item->put_sub_struct(kMenuParams, choose.get());
        float scale = 0.0f; // (unset in the original when missing - an asserted parameter)
        params->fetch_float(0x1B9E8272u, &scale, true);
        item->push_float(0x13B9DA7B, scale); // "scale"
        script::launch_script(0xC16BF7BD, item.get(), nullptr, false, nullptr);
        item.reset();
        choose.reset();
        ++count;
    }
    if (!count) {
        const script::StructPtr item = script::make_struct();
        item->append_word(kMenuText, "No restarts in level"); // "No restarts in level"
        item->put_name_key(kMenuId, kFirstRestartId);
        add_restart_item_flags(item.get());
        script::launch_script(0xC16BF7BD, item.get(), nullptr, false, nullptr);
    }
    return true;
}

// 0x005A4560: AddWarpPointsToMenu 0xE2C2FA03=[nodes]: an item (script 0xBEED50B6) per node (its 0xFCD206E1 name,
// else "Unnamed restart"; the node in its parameters). The parameters' own structure isn't freed (a leak, kept). The
// array isn't checked for null.
bool cfunc_add_warp_points_to_menu(script::Struct* params, script::Script*)
{
    script::Array* nodes = nullptr;
    params->fetch_list(0xE2C2FA03u, &nodes, 0);
    for (uint32_t i = 0; i < nodes->extent_; ++i) {
        const script::StructPtr item = script::make_struct();
        script::Struct* node = level_nodes::fetch_vertex(nodes->fetch_part(i), true);
        const char* name = nullptr;
        if (!node->fetch_text(0xFCD206E1u, &name, 0))
            name = "Unnamed restart"; // "Unnamed restart"
        item->append_word(kMenuText, name);
        item->put_name_key(kMenuScript, 0x54523C62);
        gsl::owner<script::Struct*> choose = script::new_struct();
        choose->put_name_key(0x9F92BA78, nodes->fetch_part(i));
        item->put_sub_struct(kMenuParams, choose);
        script::launch_script(0xBEED50B6, item.get(), nullptr, false, nullptr);
    }
    return true;
}

// 0x005A4730: AddFacesToMenu: an add_item_to_face_menu (name= without its extension, file=) for each of the first 11
// Faces\*.img files with names up to 90 characters. False if there are none.
bool cfunc_add_faces_to_menu(script::Struct*, script::Script*)
{
    WIN32_FIND_DATAA found;
    HANDLE search = FindFirstFileA("Faces\\*.img", &found); // "Faces\*.img"
    if (search == INVALID_HANDLE_VALUE)
        return false;
    int count = 0;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && std::strlen(std::data(found.cFileName)) <= 90) {
            std::array<char, 0x110> file{};
            std::array<char, 0xC8> name{};
            std::memcpy(file.data(), std::data(found.cFileName), std::strlen(std::data(found.cFileName)) + 1);
            std::memcpy(name.data(), std::data(found.cFileName), std::strlen(std::data(found.cFileName)) + 1);
            if (std::strlen(name.data()) > 4)
                core::at(name, std::strlen(name.data()) - 4) = '\0';
            const script::StructPtr item = script::make_struct();
            item->append_word("name", name.data());                                      // "name"
            item->append_word("file", file.data());                                      // "file"
            script::launch_script("add_item_to_face_menu", item.get(), nullptr, false); // "add_item_to_face_menu"
            ++count;
        }
    } while (count < 11 && FindNextFileA(search, &found));
    FindClose(search);
    return true;
}

} // namespace commands
