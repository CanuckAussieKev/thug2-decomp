// The viewer module, 0x00576CB0-0x00577F20. See model_viewer.h.

// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr: what the game uses
#include <memory>
#include <winsock2.h>

#include "engine/sys/model_viewer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <cstring>

#include "engine/core/arena_alloc.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "engine/net/net_server.h" // IWYU pragma: keep - the complete HostEnd type (converted to AppHost* here)
#include "frontend/front_end.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/net_app.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/script_startup.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "engine/pad_input/input_hub.h"
#include "engine/sys/debug_mode.h"
#include "engine/sys/game_config.h"
#include "engine/sys/mlp.h"
#include "platform/stopwatch.h"
#include "engine/sys/viewer_object.h"
#include "engine/objects/movie_manager.h"
#include "engine/assets/resource_registry.h"
#include "engine/script/cfuncs_scene.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "wire/net_wire.h"
#include "engine/core/no_destroy.h"
#include "engine/core/checksums.h"
#include <gsl/pointers>

namespace modules {

namespace {

// Names.
constexpr uint32_t kViewerButtonsEnabled = 0x89BEF647; // viewer_buttons_enabled (a script global)
constexpr uint32_t kSelectShift = 0xF3E055E1;          // select_shift (a script global)
constexpr uint32_t kUserViewerSquare = 0xAFB309AD;     // UserViewerSquare (scripts)
constexpr uint32_t kUserViewerCircle = 0xB6E7047D;     // UserViewerCircle
constexpr uint32_t kUserViewerTriangle = 0xC09789E2;   // UserViewerTriangle
constexpr uint32_t kUserViewerX = 0xF440EC15;          // UserViewerX

// NOT ORIGINAL grouping - the model viewer's speeds, viewer and last time. Made on first use, never destroyed
// (model_viewer_state()).
struct ModelViewerState {
    float rotate_velocity_{};
    float movement_velocity_{};
    Viewer* viewer_{};
    uint32_t last_time_{};
};

ModelViewerState& model_viewer_state()
{
    static core::NoDestroy<ModelViewerState> state;
    return state.get();
}

Viewer*& sole() { return model_viewer_state().viewer_; }

// A task or input handler off its list (0x004068F0).
void remove_node(tasks::SchedulerTask* node) { node->drop_out(); }
void shut_down_object(objects::PreviewEntity* object) { object->power_down(); }

void launch_script(uint32_t name) { script::launch_script(name, nullptr, nullptr, false, nullptr); }

// More names.
constexpr uint32_t kViewerPadKey = 0x702247A5; // the controller the handlers read (a script integer)
constexpr uint32_t kViewerCam = 0xEB17151B;    // viewer_cam (the camera object's id)
constexpr uint32_t kResetCameraToViewerObject = 0xC965E06F;
constexpr uint32_t kNoRoll = 0xD8C91578;       // (a script integer: the roll commands ignored)
constexpr uint32_t kScene = 0x26861025;        // scene
constexpr uint32_t kAdd = 0x02E58C18;          // Add
constexpr uint32_t kAddToScene = 0x89A0D75E;   // AddToScene (a script)
constexpr uint32_t kReloadScene = 0x179BB868;  // ReloadScene (a script)
constexpr uint32_t kModelName = 0xB974F2FC;    // modelname
constexpr uint32_t kSkeletonName = 0x09794932; // skeletonName

float& movement_velocity() { return model_viewer_state().movement_velocity_; }
float& rotate_velocity() { return model_viewer_state().rotate_velocity_; }
uint32_t& last_time() { return model_viewer_state().last_time_; }
bool debug_checks() { return game_config::from_cd(); }
pad_input::Hub* input_manager() { return pad_input::get_input_manager(); }
frame_loop::Hub* main_loop_manager() { return assets::get_main_loop_manager(); }

void append_vertex(tasks::PileUp& list, tasks::SchedulerTask* node) { list.queue_job(node); }

// A raw stick value through the dead zone: 0 within +-50, else scaled from there.
float dead_zone(float value)
{
    constexpr float kScale = 0.012820513f; // 0x0064EBEC (1 / 78)
    if (value > 50.0f)
        return static_cast<float>((static_cast<double>(value) - 50.0) * kScale);
    if (value < -50.0f)
        return static_cast<float>((static_cast<double>(value) + 50.0) * kScale);
    return 0.0f;
}

// `object` rolled by `angle` about the world's z axis (the rotation before its matrix).
void get_roll(objects::GameEntity* object, float angle)
{
    const maths::Vec4 axis = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 1.0f};
    maths::Mat4 rotation{};
    maths::build_axis_angle_matrix(rotation, axis, angle);
    maths::get_multiply(object->basis_frame(), rotation, object->basis_frame());
}

void add_input_handler(ViewerInputHandler* handler)
{
    pad_input::HostEnd& server = core::at(input_manager()->hosts(), handler->ordinal());
    append_vertex(server.handlers(), handler);
    handler->set_control_data(&server.content());
    handler->set_dev(server.dev());
}

} // namespace

// 0x00576CB0
script::Script* spawn_script_if_exists(uint32_t name)
{
    if (!script::script_exists(name)) // (a script of that name)
        return nullptr;
    return script::spawn_script(name, nullptr);
}

// 0x00576CF0
int reload_script_file(net::InboundMsgCtx* context)
{
    const char* file = net::wire::text_of(context->body());
    if (std::strcmp(file, "..\\runnow.qb") == 0 || std::strcmp(file, "runnow.qb") == 0)
        set_pending_script(true);
    script::load_qb_file(file, 0);
    return 1;
}

// 0x00576D40
void Viewer::drop_viewer_entity()
{
    if (!entity_)
        return;
    shut_down_object(entity_.get());
    memory::destroy_then_clear(entity_);
}

// 0x00576D70
void Viewer::stop_callback()
{
    memory::destroy_then_clear(entity_);
    remove_node(pad_listener_.get());
    remove_node(task_1c_.get());
    remove_node(task20_.get());
    remove_node(shift_handler_.get());
    net_handler_->dispatcher().tear_down_state();
}

// 0x00576DC0
Viewer::~Viewer()
{
    net_handler_.reset(); // (deleted directly, not released through the network manager - as the original)
    pad_listener_.reset();
    task_1c_.reset();
    task20_.reset();
    shift_handler_.reset();
    sole() = nullptr;
}

// 0x00576E60
objects::PreviewEntity* Viewer::get_viewer_entity() { return entity_.get(); }

// 0x00576E70
void change_view_mode(int mode) { objects::set_view_mode(mode); }
// 0x00576E80
void set_movement_velocity(float velocity) { model_viewer_state().movement_velocity_ = velocity; }
// 0x00576E90
void set_rotate_velocity(float velocity) { model_viewer_state().rotate_velocity_ = velocity; }
// 0x00576EA0
Viewer* get_live_viewer() { return sole(); }

// 0x00576EB0
void handle_viewer_shift(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->content();
    viewer->shift_makes() = 0;
    const pad_input::Payload* data = handler->control_data();
    if (!(data->buttons() & 0x100)) // select
        return;
    viewer->shift_makes() |= data->makes();
    if (script::fetch_int_value(kSelectShift)) {
        handler->control_data()->block_button_input(1);
        handler->control_data()->block_stick_input(1);
    }
}

// 0x00576F00
void handle_viewer_input(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->content();
    if (!objects::view_mode())
        return;
    auto stick = [handler](int event) {
        return static_cast<float>(static_cast<int>(core::at(handler->control_data()->incident(), event)) - 0x80);
    };
    viewer->pad_rx() = stick(pad_input::Payload::kRawRightX);
    viewer->pad_ry() = stick(pad_input::Payload::kRawRightY);
    viewer->pad_lx() = stick(pad_input::Payload::kRawLeftX);
    viewer->pad_ly() = stick(pad_input::Payload::kRawLeftY);
    // Not original - debug mode's keyboard flying (engine/sys/debug_mode.h): held keys over the sticks, and up / down.
    sys::debug_mode::keyboard_viewer(&viewer->pad_lx(), &viewer->pad_ly(), &viewer->pad_rx(), &viewer->pad_ry(),
                                     &viewer->actions());
    if (!script::fetch_int_value(kViewerButtonsEnabled))
        return;
    auto held = [handler]() { return handler->control_data()->buttons(); };
    auto made = [handler]() { return handler->control_data()->makes(); };
    if (held() & 0x8) {
        if (!(made() & 0x2) && !(held() & 0x2))
            viewer->actions() |= 0x2;
    } else if (held() & 0x2) {
        viewer->actions() |= 0x4;
    }
    if (made() & 0x800)
        viewer->actions() |= 0x800;
    if (viewer->mode_sel() == 0) {
        if (made() & 0x10)
            viewer->actions() |= 0x400;
        if (made() & 0x80)
            viewer->actions() |= 0x100;
        if (made() & 0x20)
            viewer->actions() |= 0x200;
        if (made() & 0x40)
            viewer->actions() |= 0x80;
    } else if (viewer->mode_sel() == 1) {
        if (made() & 0x40)
            viewer->actions() |= 0x8000;
    }
    if ((held() & 0x1000) && (made() & 0x1000))
        viewer->actions() |= 0x10;
    if ((held() & 0x4000) && (made() & 0x4000))
        viewer->actions() |= 0x8;
    if (held() & 0x2000)
        viewer->actions() |= 0x40;
    if (held() & 0x8000)
        viewer->actions() |= 0x20;
    if (made() & 0x200)
        viewer->actions() |= 0x4000;
    if (held() & 0x4) {
        if (!(held() & 0x1))
            viewer->actions() |= 0x10000;
    } else if (held() & 0x1) {
        viewer->actions() |= 0x20000;
    }
    if (held() & 0x100) // (select: the shift handler's)
        return;
    if (made() & 0x80)
        launch_script(kUserViewerSquare);
    if (made() & 0x20)
        launch_script(kUserViewerCircle);
    if (made() & 0x10)
        launch_script(kUserViewerTriangle);
    if (made() & 0x40)
        launch_script(kUserViewerX);
}

// 0x00577140
int handle_remove_viewer_object(net::InboundMsgCtx* context)
{
    // (the original calls it on the message's body - the first word of the context; it does nothing either way)
    static_cast<Viewer*>(context->content())->drop_viewer_entity();
    return 1;
}

// 0x00577170
void Viewer::create_viewer_object(void* data)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(game_config::get_extra_memory() ? static_cast<memory::MemSource*>(manager->debug_heap())
                                                           : static_cast<memory::MemSource*>(manager->bottom_up_heap()));
    drop_viewer_entity();
    entity_ = std::make_unique<objects::PreviewEntity>(net_handler_.get());
    entity_->read_in(static_cast<script::Struct*>(data)); // (no memory: a crash, as the original)
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00577260
void Viewer::add_commands(uint32_t commands) { actions_ |= commands; }

// 0x005772E0
void move_object_local(objects::GameEntity* object, const maths::Vec4* offset)
{
    maths::Mat4 m{};
    m = object->basis_frame();
    const maths::Vec4& v = *offset;
    auto along = [&](int c) {
        return static_cast<double>(core::at(m.row, 2)[c]) * v.z + static_cast<double>(core::at(m.row, 0)[c]) * v.x +
               static_cast<double>(core::at(m.row, 1)[c]) * v.y;
    };
    const double x = along(0) + object->at_point().x;
    const double y = along(1) + object->at_point().y;
    const double z = along(2) + object->at_point().z;
    object->at_point().z = static_cast<float>(z);
    object->at_point().x = static_cast<float>(x);
    object->at_point().y = static_cast<float>(y);
}

// 0x00577380
void select_combo_task(ViewerTask* task)
{
    const Viewer* viewer = task->content();
    if (!script::fetch_int_value(kSelectShift))
        return;
    // The buttons pressed with Select held, and their scripts - the first one in this order.
    struct SelectCombo {
        uint32_t button;
        uint32_t script;
    };
    static const auto kCombos = std::to_array<SelectCombo>({
        {.button = 0x10, .script = 0xCBB91022},   // UserSelectTriangle
        {.button = 0x80, .script = 0xE69691FA},   // UserSelectSquare
        {.button = 0x20, .script = 0xFFC29C2A},   // UserSelectCircle
        {.button = 0x800, .script = 0x51B0E413},  // UserSelectStart
        {.button = 0x40, .script = 0x746D2598},   // UserSelectX
        {.button = 0x4, .script = 0x81D0A13C},    // UserSelectL1
        {.button = 0x1, .script = 0x18D9F086},    // UserSelectL2
        {.button = 0x8, .script = 0x55919EE3},    // UserSelectR1
        {.button = 0x2, .script = 0xCC98CF59},    // UserSelectR2
        {.button = 0x1000, .script = 0x1B0B7922}, // UserSelectUp
        {.button = 0x4000, .script = 0xBBC0E36A}, // UserSelectDown
        {.button = 0x8000, .script = 0xDD589439}, // UserSelectLeft
        {.button = 0x2000, .script = 0x7A03C488}, // UserSelectRight
    });
    const uint32_t makes = viewer->shift_makes();
    for (const auto& combo : kCombos) {
        if (makes & combo.button) {
            spawn_script_if_exists(combo.script);
            return;
        }
    }
}

// 0x005774C0
int handle_add_to_scene(net::InboundMsgCtx* context)
{
    const char* message = net::wire::text_of(context->body());
    script::StructPtr params = script::make_struct();
    params->append_word(kScene, script::kQbvalString, message);
    params->append_word(kAdd, script::kQbvalString, net::wire::text_at(context->body(), 0x80));
    script::launch_script(kAddToScene, params.get(), nullptr, false, nullptr);
    params.reset();
    return 1;
}

// 0x005775A0
int handle_create_viewer_object(net::InboundMsgCtx* context)
{
    // NOT ORIGINAL: the viewer the handler was registered with (its data). The original called create_viewer_object
    // on the message's body as if it were the viewer - writing the new object's pointer into the packet buffer.
    auto* viewer = static_cast<Viewer*>(context->content());
    script::StructPtr params = script::make_struct();
    params->append_word(kModelName, net::wire::text_of(context->body()));
    params->put_name_key(checksums::Name{kSkeletonName}, net::wire::value_at<uint32_t>(context->body(), 0x84));
    viewer->create_viewer_object(params.get());
    params.reset();
    return 1;
}

// 0x00577670
int on_remote_script_msg(net::InboundMsgCtx* context)
{
    const auto name = net::wire::value_at<uint32_t>(context->body());
    script::StructPtr params = script::make_struct();
    // (the script reader; a message without room for the name decodes nothing)
    const std::span<const uint8_t> body(context->body(), context->body_size());
    const size_t skip = std::min<size_t>(4, body.size());
    script::decode_from_bytes(params.get(), body.subspan(skip).data(), body.size() - skip);
    script::launch_script(name, params.get(), nullptr, false, nullptr);
    params.reset();
    return 1;
}

// 0x00577740
bool Viewer::place_camera(script::Struct* params)
{
    if (!script::fetch_int_value(kResetCameraToViewerObject))
        return true;
    auto* camera = core::down_cast<objects::GameEntity>(objects::object_hub_slot()->look_up_entity(kViewerCam));
    if (!camera)
        return true;
    maths::Mat4 identity{};
    core::at(identity.row, 0) = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    core::at(identity.row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
    core::at(identity.row, 2) = {.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    core::at(identity.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    camera->basis_frame() = identity;
    script::VectorValue at{};
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (params->fetch_vec("ViewerObjPos", &at, 0)) {
        x = at.x;
        y = static_cast<float>(static_cast<double>(at.y) + 35.0);
        z = static_cast<float>(static_cast<double>(at.z) + 100.0);
    } else {
        x = 0.0f;
        y = 35.0f;
        z = 100.0f;
    }
    camera->at_point().y = y;
    camera->at_point().x = x;
    camera->at_point().w = 1.0f;
    camera->at_point().z = z;
    return true;
}

// 0x00577870
void free_camera_task(ViewerTask* task)
{
    Viewer* viewer = task->content();
    const uint32_t last = last_time();
    const uint32_t elapsed = frame_clock::get_time() - last;
    last_time() += elapsed;
    auto seconds = static_cast<float>(static_cast<double>(static_cast<int>(elapsed)) * 0.001f);
    if (viewer->actions() & 0x4000)
        viewer->set_mode_sel(viewer->entity() && viewer->mode_sel() == 0 ? 1 : 0);
    viewer->pad_ry() = dead_zone(viewer->pad_ry());
    viewer->pad_rx() = dead_zone(viewer->pad_rx());
    viewer->pad_ly() = dead_zone(viewer->pad_ly());
    viewer->pad_lx() = dead_zone(viewer->pad_lx());

    auto* camera = core::down_cast<objects::GameEntity>(objects::object_hub_slot()->look_up_entity(kViewerCam));
    constexpr float kDegrees = 0.017453292f; // 0x00645464 (pi / 180)
    if (camera) {
        if (viewer->mode_sel() == 0) {
            if (viewer->pad_ly() != 0.0f) {
                // Up / down: about its own x axis.
                const maths::Vec4 axis = {.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
                auto angle =
                    static_cast<float>(static_cast<double>(rotate_velocity()) * viewer->pad_ly() * seconds * kDegrees);
                camera->basis_frame().turn_in_own_frame(axis, angle);
            }
            if (viewer->pad_lx() != 0.0f) {
                // Left / right: about the world's y axis.
                maths::Mat4 matrix{};
                matrix = camera->basis_frame();
                auto angle =
                    static_cast<float>(static_cast<double>(rotate_velocity()) * viewer->pad_lx() * seconds * -kDegrees);
                maths::Mat4 rotation{};
                maths::get_multiply(matrix, matrix, maths::make_yaw_matrix(rotation, angle));
                camera->basis_frame() = matrix;
            }
        } else if (viewer->mode_sel() == 1) {
            viewer->entity()->change_spin({.z = viewer->pad_lx(), .x = viewer->pad_ly()});
        }
        if (viewer->pad_rx() != 0.0f || viewer->pad_ry() != 0.0f) {
            const double step = static_cast<double>(movement_velocity()) * seconds;
            const maths::Vec4 offset = {.x = static_cast<float>(step * viewer->pad_rx()), .y = 0.0f,
                                        .z = static_cast<float>(step * viewer->pad_ry()), .w = 1.0f};
            move_object_local(camera, &offset);
        }
    }
    // (from here on the original doesn't check for the camera object)
    if (viewer->actions() & Viewer::kCommandShoulderA) {
        const maths::Vec4 offset = {.x = 0.0f, .y = static_cast<float>(static_cast<double>(movement_velocity()) * seconds), .z = 0.0f,
                                    .w = 1.0f};
        move_object_local(camera, &offset);
    }
    if (viewer->actions() & Viewer::kCommandShoulderB) {
        const maths::Vec4 offset = {.x = 0.0f, .y = static_cast<float>(-(static_cast<double>(movement_velocity()) * seconds)),
                                    .z = 0.0f, .w = 1.0f};
        move_object_local(camera, &offset);
    }
    if (!script::fetch_int_value(kNoRoll)) {
        constexpr float kHalfDegrees = 0.008726646f; // 0x00648800 (pi / 360)
        if (viewer->actions() & 0x10000)
            get_roll(camera, static_cast<float>(static_cast<double>(rotate_velocity()) * seconds * -kHalfDegrees));
        if (viewer->actions() & 0x20000)
            get_roll(camera, static_cast<float>(static_cast<double>(rotate_velocity()) * seconds * kHalfDegrees));
    }
    camera->composite_flags() &= ~objects::kCompositeFlagPaused;
    if (viewer->actions() & 0x1000) {
        const int next = (render::get_view_layout() + 1) % 6;
        render::set_screen_mode(next);
    }
    viewer->actions() = 0;
}

// 0x00577CD0
int handle_reload_scene(net::InboundMsgCtx* context)
{
    const char* message = net::wire::text_of(context->body());
    script::StructPtr params = script::make_struct();
    params->append_word(kScene, script::kQbvalString, message);
    script::launch_script(kReloadScene, params.get(), nullptr, false, nullptr);
    params.reset();
    return 1;
}

// 0x00577DA0
void Viewer::start_callback()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->front_end_heap());
    if (!debug_checks()) {
        net::Router& dispatcher = net_handler_->dispatcher();
        dispatcher.append_listener(0x20, &handle_reload_scene, 3, nullptr, 0x80);
        dispatcher.append_listener(0x21, &online::handle_nothing, 3, nullptr, 0x80);
        dispatcher.append_listener(0x22, &reload_script_file, 3, nullptr,
                                   0x80);
        dispatcher.append_listener(0x2A, &handle_add_to_scene, 3, nullptr, 0x80);
        dispatcher.append_listener(0x23, &handle_create_viewer_object, 3, this, 0x80);
        dispatcher.append_listener(0x24, &handle_remove_viewer_object, 3, this,
                                   0x80);
        dispatcher.append_listener(0x2B, &on_remote_script_msg, 3, this, 0x80);
    }
    append_vertex(main_loop_manager()->logic_tasks(), task_1c_.get());
    add_input_handler(pad_listener_.get());
    debug::printf("Adding shift input handlers\n)");
    add_input_handler(shift_handler_.get());
    append_vertex(main_loop_manager()->logic_tasks(), task20_.get());
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00577F20
Viewer::Viewer()
{
    if (!debug_checks()) {
        net::Hub* net = net::Hub::sole();
        // NOLINTNEXTLINE(bugprone-unused-return-value): the original's call; its result unused, as there
        static_cast<void>(inet_addr(net->fetch_own_address())); // (unused)
        stubs::return_true();                // (0x005B9760 on the net manager: a check compiled to "true")
        const int address = static_cast<int>(inet_addr(net->fetch_own_address()));
        net_handler_ = memory::take_unique<net::AppHost>(net->make_server_app(0, "THUG2 Viewer", 4, 10000, {.address = address, .flags = 0x10}));
    }
    const int controller = script::fetch_int_value(kViewerPadKey);

    task_1c_ = std::make_unique<ViewerTask>(&free_camera_task, this, 0);
    pad_listener_ = std::make_unique<ViewerInputHandler>(&handle_viewer_input, this, 0, controller);
    set_movement_velocity(400.0f);
    set_rotate_velocity(120.0f);
    debug::printf("Creating shift input handlers\n)");
    task20_ = std::make_unique<ViewerTask>(&select_combo_task, this, 0);
    shift_handler_ = std::make_unique<ViewerInputHandler>(&handle_viewer_shift, this, 1000, controller);
    sole() = this;
}

} // namespace modules
