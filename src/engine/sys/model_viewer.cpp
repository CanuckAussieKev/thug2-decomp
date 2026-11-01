// The viewer module, 0x00576CB0-0x00577F20. See model_viewer.h.

// NOLINTNEXTLINE(bugprone-reserved-identifier, readability-identifier-naming): Winsock's own macro
#define _WINSOCK_DEPRECATED_NO_WARNINGS // inet_addr: what the game uses
#include <memory>
#include <winsock2.h>

#include "engine/sys/model_viewer.h"

#include <cstdint>
#include <cstring>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "engine/core/task_list.h"
#include "frontend/front_end.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_handlers.h"
#include "engine/net/net_app.h"
#include "engine/net/net_server.h"
#include "engine/net/net_dispatcher.h"
#include "engine/net/net_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
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
#include "engine/core/raw_view.h"
#include "platform/code_address.h"

namespace modules {

namespace {

// Names.
constexpr uint32_t kViewerButtonsEnabled = 0x89BEF647; // viewer_buttons_enabled (a script global)
constexpr uint32_t kSelectShift = 0xF3E055E1;          // select_shift (a script global)
constexpr uint32_t kUserViewerSquare = 0xAFB309AD;     // UserViewerSquare (scripts)
constexpr uint32_t kUserViewerCircle = 0xB6E7047D;     // UserViewerCircle
constexpr uint32_t kUserViewerTriangle = 0xC09789E2;   // UserViewerTriangle
constexpr uint32_t kUserViewerX = 0xF440EC15;          // UserViewerX

int& view_mode_value() { return objects::g_view_mode; }

float g_rotate_velocity;
float g_movement_velocity;
Viewer* g_viewer;
uint32_t g_last_time;

Viewer*& sole() { return g_viewer; }
bool& run_now_loaded() { return g_pending_script; }

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

float& movement_velocity() { return g_movement_velocity; }
float& rotate_velocity() { return g_rotate_velocity; }
uint32_t& last_time() { return g_last_time; }
bool debug_checks() { return game_config::g_from_cd; }
pad_input::Hub* input_manager() { return pad_input::g_input_manager; }
frame_loop::Hub* main_loop_manager() { return assets::g_main_loop_manager; }

void append_vertex(tasks::PileUp& list, tasks::SchedulerTask* node) { list.queue_job(node); }

script::Struct* spawn_param_table()
{
    script::Struct* memory = script::spawn_param_bag();
    return memory ? memory::construct_in<script::Struct>(memory) : nullptr;
}
void delete_structure(script::Struct* structure)
{
    if (structure) {
        structure->get_clear();
        script::delete_struct(structure);
    }
}

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
    const maths::Vec4 axis = {0.0f, 0.0f, 1.0f, 1.0f};
    maths::Mat4 rotation{};
    maths::build_axis_angle_matrix(rotation, axis, angle);
    maths::get_multiply(object->basis_frame_, rotation, object->basis_frame_);
}

void add_input_handler(ViewerInputHandler* handler)
{
    pad_input::HostEnd& server = input_manager()->hosts_[handler->ordinal_];
    append_vertex(server.handlers_, handler);
    handler->control_data_ = &server.content_;
    handler->dev_ = server.dev_;
}

} // namespace

// 0x00576CB0
script::Script* spawn_script_if_exists(uint32_t name)
{
    if (!script::script_exists(name)) // (a script of that name)
        return nullptr;
    return script::spawn_script(name, nullptr, 0, nullptr, 0, 0, false, false, false, false);
}

// 0x00576CF0
int reload_script_file(const char** name)
{
    const char* file = *name;
    if (std::strcmp(file, "..\\runnow.qb") == 0 || std::strcmp(file, "runnow.qb") == 0)
        run_now_loaded() = true;
    script::load_qb_file(file, 0);
    return 1;
}

// 0x00576D40
void Viewer::drop_viewer_entity()
{
    if (!entity_)
        return;
    shut_down_object(entity_);
    delete entity_;
    entity_ = nullptr;
}

// 0x00576D70
void Viewer::stop_callback()
{
    delete entity_;
    entity_ = nullptr;
    remove_node(pad_listener_.get());
    remove_node(task_1c_.get());
    remove_node(task20_.get());
    remove_node(shift_handler_.get());
    net_handler_->dispatcher_.tear_down_state();
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
void* Viewer::get_viewer_entity() { return entity_; }

// 0x00576E70
void change_view_mode(int mode) { view_mode_value() = mode; }
// 0x00576E80
void set_movement_velocity(float velocity) { g_movement_velocity = velocity; }
// 0x00576E90
void set_rotate_velocity(float velocity) { g_rotate_velocity = velocity; }
// 0x00576EA0
Viewer* get_live_viewer() { return sole(); }

// 0x00576EB0
void handle_viewer_shift(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->content_;
    viewer->shift_makes_ = 0;
    const pad_input::Payload* data = handler->control_data_;
    if (!(data->buttons_ & 0x100)) // select
        return;
    viewer->shift_makes_ |= data->makes_;
    if (script::fetch_int_value(kSelectShift)) {
        handler->control_data_->block_button_input(1);
        handler->control_data_->block_stick_input(1);
    }
}

// 0x00576F00
void handle_viewer_input(ViewerInputHandler* handler)
{
    Viewer* viewer = handler->content_;
    if (!view_mode_value())
        return;
    auto stick = [handler](int event) {
        return static_cast<float>(static_cast<int>(handler->control_data_->incident_[event]) - 0x80);
    };
    viewer->pad_rx_ = stick(pad_input::Payload::kRawRightX);
    viewer->pad_ry_ = stick(pad_input::Payload::kRawRightY);
    viewer->pad_lx_ = stick(pad_input::Payload::kRawLeftX);
    viewer->pad_ly_ = stick(pad_input::Payload::kRawLeftY);
    // Not original - debug mode's keyboard flying (engine/sys/debug_mode.h): held keys over the sticks, and up / down.
    sys::debug_mode::keyboard_viewer(&viewer->pad_lx_, &viewer->pad_ly_, &viewer->pad_rx_, &viewer->pad_ry_,
                                     &viewer->actions_);
    if (!script::fetch_int_value(kViewerButtonsEnabled))
        return;
    auto held = [handler]() { return handler->control_data_->buttons_; };
    auto made = [handler]() { return handler->control_data_->makes_; };
    if (held() & 0x8) {
        if (!(made() & 0x2) && !(held() & 0x2))
            viewer->actions_ |= 0x2;
    } else if (held() & 0x2) {
        viewer->actions_ |= 0x4;
    }
    if (made() & 0x800)
        viewer->actions_ |= 0x800;
    if (viewer->mode_sel_ == 0) {
        if (made() & 0x10)
            viewer->actions_ |= 0x400;
        if (made() & 0x80)
            viewer->actions_ |= 0x100;
        if (made() & 0x20)
            viewer->actions_ |= 0x200;
        if (made() & 0x40)
            viewer->actions_ |= 0x80;
    } else if (viewer->mode_sel_ == 1) {
        if (made() & 0x40)
            viewer->actions_ |= 0x8000;
    }
    if ((held() & 0x1000) && (made() & 0x1000))
        viewer->actions_ |= 0x10;
    if ((held() & 0x4000) && (made() & 0x4000))
        viewer->actions_ |= 0x8;
    if (held() & 0x2000)
        viewer->actions_ |= 0x40;
    if (held() & 0x8000)
        viewer->actions_ |= 0x20;
    if (made() & 0x200)
        viewer->actions_ |= 0x4000;
    if (held() & 0x4) {
        if (!(held() & 0x1))
            viewer->actions_ |= 0x10000;
    } else if (held() & 0x1) {
        viewer->actions_ |= 0x20000;
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
int handle_remove_viewer_object(void** context)
{
    static_cast<Viewer*>(*context)->drop_viewer_entity();
    return 1;
}

// 0x00577170
void Viewer::create_viewer_object(void* data)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(commands::g_got_extra_memory ? static_cast<memory::MemSource*>(manager->debug_heap_)
                                                           : static_cast<memory::MemSource*>(manager->bottom_up_heap_));
    drop_viewer_entity();
    entity_ = new objects::PreviewEntity(net_handler_.get());
    entity_->read_in(static_cast<script::Struct*>(data)); // (no memory: a crash, as the original)
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x00577260
void Viewer::add_commands(uint32_t commands) { actions_ |= commands; }

// 0x005772E0
void move_object_local(objects::GameEntity* object, const maths::Vec4* offset)
{
    maths::Mat4 m{};
    m = object->basis_frame_;
    const maths::Vec4& v = *offset;
    auto along = [&](int c) {
        return static_cast<double>(m.row[2][c]) * v.z + static_cast<double>(m.row[0][c]) * v.x +
               static_cast<double>(m.row[1][c]) * v.y;
    };
    const double x = along(0) + object->at_point_.x;
    const double y = along(1) + object->at_point_.y;
    const double z = along(2) + object->at_point_.z;
    object->at_point_.z = static_cast<float>(z);
    object->at_point_.x = static_cast<float>(x);
    object->at_point_.y = static_cast<float>(y);
}

// 0x00577380
void select_combo_task(ViewerTask* task)
{
    const Viewer* viewer = task->content_;
    if (!script::fetch_int_value(kSelectShift))
        return;
    // The buttons pressed with Select held, and their scripts - the first one in this order.
    static const struct {
        uint32_t button;
        uint32_t script;
    } kCombos[] = {
        {0x10, 0xCBB91022},   // UserSelectTriangle
        {0x80, 0xE69691FA},   // UserSelectSquare
        {0x20, 0xFFC29C2A},   // UserSelectCircle
        {0x800, 0x51B0E413},  // UserSelectStart
        {0x40, 0x746D2598},   // UserSelectX
        {0x4, 0x81D0A13C},    // UserSelectL1
        {0x1, 0x18D9F086},    // UserSelectL2
        {0x8, 0x55919EE3},    // UserSelectR1
        {0x2, 0xCC98CF59},    // UserSelectR2
        {0x1000, 0x1B0B7922}, // UserSelectUp
        {0x4000, 0xBBC0E36A}, // UserSelectDown
        {0x8000, 0xDD589439}, // UserSelectLeft
        {0x2000, 0x7A03C488}, // UserSelectRight
    };
    const uint32_t makes = viewer->shift_makes_;
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
    const char* message = context->body_;
    script::Struct* params = spawn_param_table();
    params->append_word(kScene, script::kQbvalString, message);
    params->append_word(kAdd, script::kQbvalString, message + 0x80);
    script::launch_script(kAddToScene, params, nullptr, false, nullptr);
    delete_structure(params);
    return 1;
}

// 0x005775A0
int handle_create_viewer_object(net::InboundMsgCtx* context)
{
    auto* viewer = raw::as<Viewer>(context->body_); // (sic: see model_viewer.h)
    script::Struct* params = spawn_param_table();
    const char* message = context->body_;
    params->append_word(kModelName, message);
    params->put_name_key(kSkeletonName, *raw::field_at<const uint32_t>(message, 0x84));
    viewer->create_viewer_object(params);
    delete_structure(params);
    return 1;
}

// 0x00577670
int on_remote_script_msg(net::InboundMsgCtx* context)
{
    const char* message = context->body_;
    const uint32_t name = *raw::as<const uint32_t>(message);
    script::Struct* params = spawn_param_table();
    script::decode_from_bytes(params, raw::bytes(message + 4)); // the script reader
    script::launch_script(name, params, nullptr, false, nullptr);
    delete_structure(params);
    return 1;
}

// 0x00577740
bool Viewer::place_camera(script::Struct* params)
{
    if (!script::fetch_int_value(kResetCameraToViewerObject))
        return true;
    auto* camera = static_cast<objects::GameEntity*>(objects::object_hub_slot()->look_up_entity(kViewerCam));
    if (!camera)
        return true;
    maths::Mat4 identity{};
    identity.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    identity.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    identity.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    identity.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    camera->basis_frame_ = identity;
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
    camera->at_point_.y = y;
    camera->at_point_.x = x;
    camera->at_point_.w = 1.0f;
    camera->at_point_.z = z;
    return true;
}

// 0x00577870
void free_camera_task(ViewerTask* task)
{
    Viewer* viewer = task->content_;
    const uint32_t last = last_time();
    const uint32_t elapsed = frame_clock::get_time() - last;
    last_time() += elapsed;
    auto seconds = static_cast<float>(static_cast<double>(static_cast<int>(elapsed)) * 0.001f);
    if (viewer->actions_ & 0x4000)
        viewer->mode_sel_ = viewer->entity_ && viewer->mode_sel_ == 0 ? 1 : 0;
    viewer->pad_ry_ = dead_zone(viewer->pad_ry_);
    viewer->pad_rx_ = dead_zone(viewer->pad_rx_);
    viewer->pad_ly_ = dead_zone(viewer->pad_ly_);
    viewer->pad_lx_ = dead_zone(viewer->pad_lx_);

    auto* camera = static_cast<objects::GameEntity*>(objects::object_hub_slot()->look_up_entity(kViewerCam));
    constexpr float kDegrees = 0.017453292f; // 0x00645464 (pi / 180)
    if (camera) {
        if (viewer->mode_sel_ == 0) {
            if (viewer->pad_ly_ != 0.0f) {
                // Up / down: about its own x axis.
                const maths::Vec4 axis = {1.0f, 0.0f, 0.0f, 1.0f};
                auto angle =
                    static_cast<float>(static_cast<double>(rotate_velocity()) * viewer->pad_ly_ * seconds * kDegrees);
                camera->basis_frame_.turn_in_own_frame(axis, angle);
            }
            if (viewer->pad_lx_ != 0.0f) {
                // Left / right: about the world's y axis.
                maths::Mat4 matrix{};
                matrix = camera->basis_frame_;
                auto angle =
                    static_cast<float>(static_cast<double>(rotate_velocity()) * viewer->pad_lx_ * seconds * -kDegrees);
                maths::Mat4 rotation{};
                maths::get_multiply(matrix, matrix, maths::make_yaw_matrix(rotation, angle));
                camera->basis_frame_ = matrix;
            }
        } else if (viewer->mode_sel_ == 1) {
            viewer->entity_->change_spin(viewer->pad_lx_, viewer->pad_ly_);
        }
        if (viewer->pad_rx_ != 0.0f || viewer->pad_ry_ != 0.0f) {
            const double step = static_cast<double>(movement_velocity()) * seconds;
            const maths::Vec4 offset = {static_cast<float>(step * viewer->pad_rx_), 0.0f,
                                        static_cast<float>(step * viewer->pad_ry_), 1.0f};
            move_object_local(camera, &offset);
        }
    }
    // (from here on the original doesn't check for the camera object)
    if (viewer->actions_ & Viewer::kCommandShoulderA) {
        const maths::Vec4 offset = {0.0f, static_cast<float>(static_cast<double>(movement_velocity()) * seconds), 0.0f,
                                    1.0f};
        move_object_local(camera, &offset);
    }
    if (viewer->actions_ & Viewer::kCommandShoulderB) {
        const maths::Vec4 offset = {0.0f, static_cast<float>(-(static_cast<double>(movement_velocity()) * seconds)),
                                    0.0f, 1.0f};
        move_object_local(camera, &offset);
    }
    if (!script::fetch_int_value(kNoRoll)) {
        constexpr float kHalfDegrees = 0.008726646f; // 0x00648800 (pi / 360)
        if (viewer->actions_ & 0x10000)
            get_roll(camera, static_cast<float>(static_cast<double>(rotate_velocity()) * seconds * -kHalfDegrees));
        if (viewer->actions_ & 0x20000)
            get_roll(camera, static_cast<float>(static_cast<double>(rotate_velocity()) * seconds * kHalfDegrees));
    }
    camera->composite_flags_ &= ~objects::kCompositeFlagPaused;
    if (viewer->actions_ & 0x1000) {
        const int next = (render::get_view_layout() + 1) % 6;
        render::set_screen_mode(next);
    }
    viewer->actions_ = 0;
}

// 0x00577CD0
int handle_reload_scene(net::InboundMsgCtx* context)
{
    const char* message = context->body_;
    script::Struct* params = spawn_param_table();
    params->append_word(kScene, script::kQbvalString, message);
    script::launch_script(kReloadScene, params, nullptr, false, nullptr);
    delete_structure(params);
    return 1;
}

// 0x00577DA0
void Viewer::start_callback()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->front_end_heap_);
    if (!debug_checks()) {
        net::Router& dispatcher = net_handler_->dispatcher_;
        dispatcher.append_listener(0x20, &handle_reload_scene, 3, nullptr, 0x80);
        dispatcher.append_listener(0x21, &online::handle_nothing, 3, nullptr, 0x80);
        dispatcher.append_listener(0x22, function_at<net::HandlerSlotValue>(code_address(&reload_script_file)), 3, nullptr,
                                   0x80);
        dispatcher.append_listener(0x2A, &handle_add_to_scene, 3, nullptr, 0x80);
        dispatcher.append_listener(0x23, &handle_create_viewer_object, 3, this, 0x80);
        dispatcher.append_listener(0x24, function_at<net::HandlerSlotValue>(code_address(&handle_remove_viewer_object)), 3, this,
                                   0x80);
        dispatcher.append_listener(0x2B, &on_remote_script_msg, 3, this, 0x80);
    }
    append_vertex(main_loop_manager()->logic_tasks_, task_1c_.get());
    add_input_handler(pad_listener_.get());
    debug::printf("Adding shift input handlers\n)");
    add_input_handler(shift_handler_.get());
    append_vertex(main_loop_manager()->logic_tasks_, task20_.get());
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
        net_handler_.reset(net->make_server_app(0, "THUG2 Viewer", 4, 10000, address, 0x10));
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
