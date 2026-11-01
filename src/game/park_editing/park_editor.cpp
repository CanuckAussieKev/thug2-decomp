// The park editor module (park_editing::ParkBuilderMode, park_editor.h), 0x0058D290-0x0058FBD0 but the script commands: the
// module's destructor, start and stop, its pass-throughs to the park manager, the input handler and the menu
// events, the camera and the controls (Update), the state, and the logic task's work.

#include "game/park_editing/park_editor.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/task_list.h"
#include "game/park_editing/piece_menu.h"
#include "frontend/front_end.h"
#include "engine/maths/mat4.h"
#include "engine/objects/entity.h"
#include "engine/objects/entity_hub.h"
#include "engine/objects/tracker.h"
#include "game/park/park_memory.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/pad_input/input_hub.h"
#include "platform/stopwatch.h"
#include "game/park_editing/rail_editor.h"
#include "game/park_editing/goal_editor.h"
#include "engine/script/game_cfuncs.h"
#include "engine/script/world_cfuncs.h"
#include "game/skate/skate_module.h"
#include "engine/assets/resource_registry.h"
#include "engine/objects/compositemgr.h"
#include "engine/sys/mlp.h"

namespace park_editing {

namespace {

// Scripts and names.
constexpr uint32_t kParkedCam = 0x64716BEE;            // parked_cam (the editor's camera object)
constexpr uint32_t kRailEditorCam = 0x5B509AD3;        // Raileditor
constexpr uint32_t kRootWindow = 0x56A1EAE3;           // root_window
constexpr uint32_t kMenuEvent = 0x36B2EE74;            // the menus' pad events' source (name unknown)
constexpr uint32_t kParkedMenuUp = 0xBB9B790B;         // parked_menu_up
constexpr uint32_t kParkedMenuDown = 0xFF86DC11;       // parked_menu_down
constexpr uint32_t kParkedMenuLeft = 0x991EAB42;       // parked_menu_left
constexpr uint32_t kParkedMenuRight = 0xBD902A03;      // parked_menu_right
constexpr uint32_t kSetMenuContainer = 0xE25EE8C3;     // set_menu_container
constexpr uint32_t kPieceMenuContainer = 0x3F391840;   // piece_menu_container
constexpr uint32_t kPadUp = 0x30A4F836;                // pad_up
constexpr uint32_t kPadDown = 0x0FD1AC26;              // pad_down
constexpr uint32_t kPadLeft = 0x6949DB75;              // pad_left
constexpr uint32_t kPadRight = 0x05DDD87C;             // pad_right
constexpr uint32_t kLevel = 0x651533EC;                // level
constexpr uint32_t kLoadSk5ed = 0xE8B4B836;            // load_sk5ed
constexpr uint32_t kChangeLevel = 0x39C58EA1;          // change_level
constexpr uint32_t kHide = 0x5B6634D4;                 // Hide
constexpr uint32_t kPlayRotatePieceSound = 0x9366CECE; // PlayRotatePieceSound
constexpr uint32_t kPlayEdPlaceSound = 0x3B32A620;     // PlayEdPlaceSound
constexpr uint32_t kPlayEdBuzzSound = 0x5179BDF3;      // PlayEdBuzzSound
constexpr uint32_t kPlayEdEraseSound = 0xF969E9A1;     // PlayEdEraseSound
constexpr uint32_t kPlayRaiseGroundSound = 0x1508BBB6; // PlayRaiseGroundSound
constexpr uint32_t kPlayLowerGroundSound = 0xFF957F6E; // PlayLowerGroundSound
constexpr uint32_t kGapPlaced = 0x169F244F;            // (after a gap's half is placed; name unknown)
constexpr uint32_t kAreaSelected = 0xC3E9871D;         // (after an area selection is finished; name unknown)
constexpr uint32_t kSwitchOnRailEditor = 0x2C3B17EA;   // SwitchOnRailEditor
constexpr uint32_t kSwitchOffRailEditor = 0x32428A49;  // SwitchOffRailEditor
constexpr uint32_t kPauseTrickText = 0x93109A21;       // pause_trick_text
constexpr uint32_t kEditorViewOn = 0x6CBDF252;         // (with pause_trick_text; name unknown)
constexpr uint32_t kHideGoalPoints = 0xFB9AF75E;       // GoalManager_HideGoalPoints
constexpr uint32_t kEditorViewOn2 = 0x4EFFB0AC;        // (name unknown)
constexpr uint32_t kUnpauseTrickText = 0x07E3440E;     // unpause_trick_text
constexpr uint32_t kShowPoints = 0x09A30DBF;           // GoalManager_ShowPoints
constexpr uint32_t kShowGoalPoints = 0x888EF05E;       // GoalManager_ShowGoalPoints

// The camera's constants (the exe's).
constexpr float kAngleScaleToRad = 0.0174532924f; // 0x00645464
constexpr float kCameraHeight = 300.0f;           // 0x0064F224
constexpr float kStickDeadZone = 50.0f;           // 0x006463CC (and -50, 0x0064EBE8)
constexpr float kStickScale = 0.0128205130f;      // 0x0064EBEC (1 / 78)
constexpr float kMillisecond = 0.001f;            // 0x006461D8
constexpr float kZoomStep = 16.0f;                // 0x0064F220
constexpr float kMaxDistance = 1500.0f;           // 0x0064F218
constexpr float kMinDistance = 500.0f;            // 0x0064F21C
constexpr float kMinTilt = 10.0f;                 // 0x00646710
constexpr double kMaxTilt = 90.0;                 // 0x0064F340
constexpr double kFullTurn = 360.0;               // 0x0064F348 (and 360.0f, 0x00645EA8)

uint8_t* skate_module() { return reinterpret_cast<uint8_t*>(modules::g_skate_module); }
frame_loop::Hub* get_main_loop() { return assets::g_main_loop_manager; }
pad_input::Hub* input_manager() { return pad_input::g_input_manager; }

ParkLayout* hub() { return ParkLayout::singleton(); }
BuildPointer* get_cursor() { return BuildPointer::singleton(); }
bool have_cursor() { return BuildPointer::singleton() != nullptr; } // (the original asks the getter with 0)
bool is_gap_tool(BuildPointer* cursor)
{
    return cursor->mode_sel_ == BuildPointer::kModeGap || cursor->mode_sel_ == BuildPointer::kModeGapAdjust;
}

void launch_script(uint32_t name) { script::launch_script(name, nullptr, nullptr, false, nullptr); }
GapRegistry* gap_registry() { return GapRegistry::singleton(); }

// The rail editor (0x004F4970 looks it up) and the goal editor (0x004E8470).
RailAuthoring* get_rail_editor() { return RailAuthoring::sole(); }
ObjectiveAuthoring* get_goal_editor() { return ObjectiveAuthoring::get_pull(); }

// The level changed to load_sk5ed again (the park's shell changed).
void reload_park_level()
{
    script::Struct params;
    params.put_name_key(kLevel, kLoadSk5ed);
    script::launch_script(kChangeLevel, &params, nullptr, false, nullptr);
    params.get_clear();
}

// The park rebuilt after a load: the cursor's piece again, the gap pieces' highlights off.
void rebuild_after_load(ParkLayout* manager)
{
    manager->rebuild_park(0);
    get_cursor()->step_piece_choice(0);
    manager->highlight_gap_pieces(false, nullptr);
}

// The stick's dead zone: +-50 taken off, then 1/78 of the rest (the original keeps the result in a register).
double dead_zone(float value)
{
    if (value > kStickDeadZone)
        return (static_cast<double>(value) - kStickDeadZone) * kStickScale;
    if (value < -kStickDeadZone)
        return (static_cast<double>(value) + kStickDeadZone) * kStickScale;
    return 0.0;
}

// The left stick's move turned by the camera's angle (radians): x = cos * b + sin * a, z = cos * a - sin * b,
// the sine rounded to float first - as the original computes them.
void turn_move(double angle, double a, double b, float* x, float* z)
{
    const double sine = static_cast<float>(std::sin(angle));
    const double cosine = std::cos(angle);
    *x = static_cast<float>(cosine * b + sine * a);
    *z = static_cast<float>(cosine * a - sine * b);
}

// D3DXMatrixMultiply (the statically linked library's, 0x005BFA8C).
void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

} // namespace

// --- the module ----------------------------------------------------------------------------------------------

// 0x0058D290
void ParkBuilderMode::get_release()
{
    if (ref_tally() > 1) {
        --ref_tally();
        return;
    }
    delete sole();
    sole() = nullptr;
    ref_tally() = 0;
}

// 0x0058D2D0
ParkBuilderMode::~ParkBuilderMode() { ParkLayout::discard_occurrence(); }

// 0x0058DAD0
void ParkBuilderMode::start_callback()
{
    frame_loop::Hub* main_loop = get_main_loop();
    main_loop->logic_tasks_.queue_job(editor_update_job_.get());
    ParkEditorInputHandler* handler = editor_pad_listener_.get();
    pad_input::HostEnd& server = input_manager()->hosts_[handler->ordinal_];
    server.handlers_.queue_job(handler);
    handler->control_data_ = &input_manager()->hosts_[handler->ordinal_].content_;
    handler->dev_ = input_manager()->hosts_[handler->ordinal_].dev_;
    main_loop->display_tasks_.queue_job(draw_job_.get());
}

// 0x0058D340
void ParkBuilderMode::stop_callback()
{
    editor_update_job_->drop_out();
    editor_pad_listener_->drop_out();
}

// 0x0058DB30
void ParkBuilderMode::get_leave()
{
    cur_phase_ = kStateOff;
    BuildPointer::teardown();
    TopBarMenus::discard_occurrence();
    ParkLayout* manager = hub();
    manager->teardown(0);
    manager->destroy_gap_manager_copy();
    ParkAssembler::remove_node_array(); // the park's node array taken away
}

// 0x0058D360
void ParkBuilderMode::transfer_park_file(bool save, int slot, bool defer_regen)
{
    ParkLayout* manager = hub();
    if (save) {
        manager->transfer_park_file(true, slot);
        return;
    }
    int shell_size = hub()->get_shell_size();
    if (cur_phase_ != kStateOff && !defer_regen && have_cursor())
        get_cursor()->destroy_cursor_meta();
    if (have_cursor())
        get_cursor()->hide_clipboards();
    manager->transfer_park_file(false, slot);
    if (cur_phase_ == kStateOff || defer_regen)
        return;
    if (manager->get_shell_size() != shell_size) {
        reload_park_level();
        return;
    }
    rebuild_after_load(manager);
}

// 0x0058D4E0
void ParkBuilderMode::apply_map(const CompressedMap* map, int shell_size)
{
    bool editing = cur_phase_ == kStateEdit || cur_phase_ == kStateTestPlay;
    if (editing && have_cursor())
        get_cursor()->destroy_cursor_meta();
    ParkLayout* manager = hub();
    manager->set_packed_park_data(map, false);
    if (!editing)
        return;
    if (hub()->get_shell_size() != shell_size) {
        reload_park_level();
        return;
    }
    rebuild_after_load(manager);
}

// 0x0058D600 - 0x0058D720: the park manager's (and the cursor's, the park memory's) own.
void ParkBuilderMode::export_to_struct(script::Struct* params) { hub()->export_to_struct(params); }
void ParkBuilderMode::decode_from_struct(script::Struct* params, bool apply_to_editor)
{
    hub()->decode_from_struct(params, apply_to_editor);
}
void ParkBuilderMode::get_map_info(const CompressedMap* map, int* num_gaps, int* num_metas, uint16_t* shell_size,
                                   uint32_t* theme, int* width, int* length)
{
    hub()->get_map_info(map, num_gaps, num_metas, shell_size, theme, width, length);
}
void ParkBuilderMode::write_compressed_map_if_built() { hub()->write_compressed_map_if_built(); }
CompressedMap* ParkBuilderMode::get_packed_park_data(bool flag_clean)
{
    return hub()->get_packed_park_data(flag_clean);
}
void ParkBuilderMode::set_packed_park_data(const CompressedMap* map, bool flag_dirty)
{
    hub()->set_packed_park_data(map, flag_dirty);
}
bool ParkBuilderMode::main_heap_has_room(int count, int size) { return park_editing::main_heap_has_room(count, size); }
maths::Vec4* ParkBuilderMode::get_cursor_pos() { return &get_cursor()->at_point_; }
void ParkBuilderMode::set_daylight_script(uint32_t script) { lighting_script_ = script; }
uint32_t ParkBuilderMode::get_shell_checksum() { return hub()->get_shell_checksum(); }
const char* ParkBuilderMode::get_park_name() { return hub()->get_park_name(); }

// 0x0058DCB0
void ParkBuilderMode::show_editor_view(bool on)
{
    hub()->builder_->glow_every_piece(false);
    hub()->builder_->set_pieces_highlighted(on);
    hub()->builder_->set_restart_pieces_active(on);
    if (on) {
        launch_script(kPauseTrickText);
        launch_script(kEditorViewOn);
        launch_script(kHideGoalPoints);
        launch_script(kEditorViewOn2);
    } else {
        launch_script(kUnpauseTrickText);
        launch_script(kShowPoints);
        launch_script(kShowGoalPoints);
    }
}

// 0x0058DD70
void ParkBuilderMode::switch_camera()
{
    // 0x0059F890: the active camera (by its object's name)
    auto set_camera = [](uint32_t id, int viewport, int take_current_view) {
        commands::make_camera_active(id, viewport, take_current_view != 0);
    };
    if (have_cursor() && get_cursor()->mode_sel_ == BuildPointer::kModeRailPlacement) {
        set_camera(kRailEditorCam, 0, 0);
        return;
    }
    set_camera(kParkedCam, 0, 0);
}

// 0x0058DDC0
int ParkBuilderMode::is_park_full() { return get_attributes().max_usage_ >= 1.0f ? 1 : 0; }

// 0x0058DDE0
void ParkBuilderMode::refresh_piece_menu()
{
    if (!have_cursor())
        return;
    BuildPointer* cursor = get_cursor();
    int menu_index = cursor->menu_index_;
    int set = cursor->paste_set_index_;
    PieceFamily* pieces = hub()->get_piece_family(set, nullptr);
    TopBarMenus::singleton()->select_piece_family(pieces, set, menu_index);
    get_cursor()->on_mode_changed();
}

// 0x0058DE40
void ParkBuilderMode::Listener::receive(objects::Signal* event)
{
    ParkBuilderMode* editor = get_editor();
    if (editor->cur_phase_ != kStateEdit || !have_cursor() || (editor->flag_bits_ & kFlagPaused) ||
        event->aim_point_ != kRootWindow)
        return;
    // NOLINTNEXTLINE(bugprone-switch-missing-default-case): the other values do nothing, as the original's
    switch (event->kind_tag_) {
    case kPadUp:
        editor->actions_ |= kCommandPreviousSet;
        break;
    case kPadRight:
        editor->actions_ |= kCommandNextPiece;
        break;
    case kPadDown:
        editor->actions_ |= kCommandNextSet;
        break;
    case kPadLeft:
        editor->actions_ |= kCommandPreviousPiece;
        break;
    }
}

// 0x0058DEC0
bool ParkBuilderMode::is_inside_park(maths::Vec4 pos, float margin)
{
    maths::Vec4 size;
    ParkLayout* manager = hub();
    CellBox bounds = manager->build_area_;
    maths::Vec4 corner = hub()->cell_to_world_pos(bounds, &size);
    double x = pos.x;
    double z = pos.z;
    return static_cast<double>(corner.x) + margin < x && x < static_cast<double>(size.x) + corner.x - margin &&
           static_cast<double>(corner.z) + margin < z && z < static_cast<double>(size.z) + corner.z - margin;
}

// 0x0058DF80
bool ParkBuilderMode::find_restart(void* a, void* b, void* c)
{
    return hub()->builder_->find_nearest_rail_point(static_cast<const maths::Vec4*>(a), static_cast<maths::Vec4*>(b),
                                                    static_cast<float*>(c));
}

// 0x0058DFA0
int ParkBuilderMode::get_player_limit() { return hub()->builder_->get_player_limit(); }

// 0x0058DB70
void process_pad_input(ParkEditorInputHandler* handler)
{
    ParkBuilderMode* editor = handler->content_;
    editor->actions_ = 0;
    const pad_input::Payload* data = handler->control_data_;
    auto stick = [data](int event) { return static_cast<float>(static_cast<int>(data->incident_[event]) - 0x80); };
    editor->pad_rx_ = stick(pad_input::Payload::kRightX);
    editor->pad_ry_ = stick(pad_input::Payload::kRightY);
    editor->pad_lx_ = stick(pad_input::Payload::kLeftX);
    editor->pad_ly_ = stick(pad_input::Payload::kLeftY);
    uint32_t& commands = editor->actions_;
    auto made = [handler]() { return handler->control_data_->makes_; };
    auto held = [handler]() { return handler->control_data_->buttons_; };
    if (made() & 0x80) // Square
        commands |= ParkBuilderMode::kCommandTurnRight;
    if (made() & 0x20) // Circle
        commands |= ParkBuilderMode::kCommandTurnLeft;
    if (made() & 0x40) // X
        commands |= ParkBuilderMode::kCommandPlace;
    if (made() & 0x10) // Triangle
        commands |= ParkBuilderMode::kCommandRemove;
    if (made() & 0x04) // L1
        commands |= ParkBuilderMode::kCommandRaise;
    if (made() & 0x01) // L2
        commands |= ParkBuilderMode::kCommandLower;
    if (made() & 0x04)
        commands |= ParkBuilderMode::kCommandWidenGapRight;
    if (made() & 0x01)
        commands |= ParkBuilderMode::kCommandNarrowGapRight;
    if (made() & 0x10000)
        commands |= ParkBuilderMode::kCommandWidenGapLeft;
    if (made() & 0x20000)
        commands |= ParkBuilderMode::kCommandNarrowGapLeft;
    if (held() & 0x10000)
        commands |= ParkBuilderMode::kCommandZoomOut;
    if (held() & 0x20000)
        commands |= ParkBuilderMode::kCommandZoomIn;
}

// 0x0058EBE0
void ParkBuilderMode::move_selection_cursor()
{
    if (actions_ & kCommandPreviousSet) {
        get_cursor()->step_piece_family(-1);
        refresh_piece_menu();
        objects::get_tracker()->launch_event(kParkedMenuUp, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (actions_ & kCommandNextSet) {
        get_cursor()->step_piece_family(1);
        refresh_piece_menu();
        objects::get_tracker()->launch_event(kParkedMenuDown, kSetMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (actions_ & kCommandPreviousPiece) {
        get_cursor()->step_piece_choice(-1);
        refresh_piece_menu();
        objects::get_tracker()->launch_event(kParkedMenuLeft, kPieceMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
    if (actions_ & kCommandNextPiece) {
        get_cursor()->step_piece_choice(1);
        refresh_piece_menu();
        objects::get_tracker()->launch_event(kParkedMenuRight, kPieceMenuContainer, kMenuEvent, nullptr, false, -1.0f);
    }
}

// 0x0058ED50
void ParkBuilderMode::tick_frame()
{
    // The camera: (0, 0, distance) tilted down, turned round y, put over the cursor 300 up.
    maths::Mat4 camera;
    camera.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    camera.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    camera.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    camera.row[3] = {0.0f, 0.0f, orbit_radius_, 1.0f};
    maths::Mat4 rotation;
    matrix_multiply(
        &camera, &camera,
        &maths::make_pitch_matrix(rotation, static_cast<float>(camera_tilt_ * -static_cast<double>(kAngleScaleToRad))));
    matrix_multiply(
        &camera, &camera,
        &maths::make_yaw_matrix(rotation, static_cast<float>(camera_angle_ * static_cast<double>(kAngleScaleToRad))));
    const maths::Vec4& cursor_pos = get_cursor()->at_point_;
    camera.row[3].x = static_cast<float>(static_cast<double>(camera.row[3].x) + get_cursor()->at_point_.x);
    camera.row[3].y = static_cast<float>(static_cast<double>(camera.row[3].y) + kCameraHeight);
    camera.row[3].z = static_cast<float>(static_cast<double>(cursor_pos.z) + camera.row[3].z);
    auto* objects = reinterpret_cast<objects::ObjectHubBase*>(objects::g_composite_registry);
    if (auto* cam = reinterpret_cast<objects::GameEntity*>(objects->look_up_entity(kParkedCam))) {
        cam->at_point_ = camera.row[3];
        cam->basis_frame_ = camera;
    }
    if (flag_bits_ & kFlagPaused)
        return;

    if (get_cursor()->mode_sel_ != BuildPointer::kModeAreaSelect)
        get_cursor()->glow_overlapping_pieces(0);
    uint32_t now = frame_clock::get_time();
    int elapsed = static_cast<int>(now - static_cast<uint32_t>(last_time_));
    last_time_ = static_cast<int>(now);
    double seconds = static_cast<double>(elapsed) * kMillisecond;

    // The left stick moves the cursor (turned by the camera's angle).
    double move_y = dead_zone(pad_ly_);
    pad_ly_ = static_cast<float>(move_y);
    double move_x = dead_zone(pad_lx_);
    pad_lx_ = static_cast<float>(move_x);
    double b = move_x * move_speed_ * seconds;
    double a = move_y * move_speed_ * seconds;
    float offset_cell_x;
    float offset_cell_z;
    turn_move(static_cast<double>(camera_angle_) * kAngleScaleToRad, a, b, &offset_cell_x, &offset_cell_z);

    // The right stick turns (0-360) and tilts (10-90) the camera.
    double turn_y = dead_zone(pad_ry_);
    pad_ry_ = static_cast<float>(turn_y);
    double turn_x = dead_zone(pad_rx_);
    pad_rx_ = static_cast<float>(turn_x);
    double angle = turn_x * turn_speed_ * seconds + camera_angle_;
    camera_angle_ = static_cast<float>(angle);
    if (angle < 0.0)
        camera_angle_ = static_cast<float>(angle + kFullTurn);
    else if (angle >= kFullTurn)
        camera_angle_ = static_cast<float>(angle - kFullTurn);
    double tilt = camera_tilt_ - seconds * (turn_y * turn_speed_);
    camera_tilt_ = static_cast<float>(tilt);
    if (tilt < kMinTilt)
        camera_tilt_ = kMinTilt;
    else if (tilt > kMaxTilt)
        camera_tilt_ = static_cast<float>(kMaxTilt);

    // Turning the piece, or the gap.
    int rotation90 = 0;
    if (actions_ & kCommandTurnLeft) {
        launch_script(kPlayRotatePieceSound);
        if (is_gap_tool(get_cursor()))
            get_cursor()->change_gap(-1, 0, 0);
        else
            rotation90 = -1;
    }
    if (actions_ & kCommandTurnRight) {
        launch_script(kPlayRotatePieceSound);
        if (is_gap_tool(get_cursor()))
            get_cursor()->change_gap(1, 0, 0);
        else
            rotation90 = 1;
    }

    // Placing: a gap's half, the area selection's corners, the paste, or the piece.
    if (actions_ & kCommandPlace) {
        if (is_gap_tool(get_cursor())) {
            if ((get_cursor()->add_gap_half() || get_cursor()->is_over_gap()) &&
                get_cursor()->gap_.halves_placed_ != 1) {
                launch_script(kPlayEdPlaceSound);
                launch_script(kGapPlaced);
            } else {
                launch_script(kPlayEdBuzzSound);
            }
            action_finished_ = true;
        } else if (get_cursor()->mode_sel_ == BuildPointer::kModeAreaSelect) {
            switch (get_cursor()->advance_area_selection()) {
            case 1:
                launch_script(kPlayEdPlaceSound);
                break;
            case 2:
                launch_script(kPlayEdPlaceSound);
                launch_script(kAreaSelected);
                break;
            default:
                launch_script(kPlayEdBuzzSound);
                break;
            }
        } else if (get_cursor()->mode_sel_ == BuildPointer::kModePaste) {
            get_cursor()->show_clipboard();
            action_finished_ = true;
        } else {
            launch_script(get_cursor()->commit_tile_placement() ? kPlayEdPlaceSound : kPlayEdBuzzSound);
            action_finished_ = true;
        }
    }

    // Removing: a gap's half (the command isn't counted), the area selection, or what's under the cursor.
    if (actions_ & kCommandRemove) {
        if (is_gap_tool(get_cursor())) {
            launch_script(get_cursor()->remove_gap_half() ? kPlayEdEraseSound : kPlayEdBuzzSound);
        } else {
            if (get_cursor()->mode_sel_ == BuildPointer::kModeAreaSelect)
                get_cursor()->drop_area_marquee();
            else
                launch_script(get_cursor()->destroy_under_cursor() ? kPlayEdEraseSound : kPlayEdBuzzSound);
            action_finished_ = true;
        }
        gap_registry()->update_checklist();
    }

    move_selection_cursor();

    // Raising and lowering the floor (not with a gap).
    if (actions_ & kCommandRaise) {
        if (!is_gap_tool(get_cursor()))
            launch_script(get_cursor()->raise_lower(1) ? kPlayRaiseGroundSound : kPlayEdBuzzSound);
        action_finished_ = true;
    }
    if (actions_ & kCommandLower) {
        if (!is_gap_tool(get_cursor()))
            launch_script(get_cursor()->raise_lower(-1) ? kPlayLowerGroundSound : kPlayEdBuzzSound);
        action_finished_ = true;
    }

    // Zooming (not with a gap).
    if ((actions_ & kCommandZoomOut) && !is_gap_tool(get_cursor())) {
        double distance = static_cast<double>(orbit_radius_) + kZoomStep;
        orbit_radius_ = static_cast<float>(distance);
        if (distance > kMaxDistance)
            orbit_radius_ = kMaxDistance;
    }
    if ((actions_ & kCommandZoomIn) && !is_gap_tool(get_cursor())) {
        double distance = static_cast<double>(orbit_radius_) - kZoomStep;
        orbit_radius_ = static_cast<float>(distance);
        if (distance < kMinDistance)
            orbit_radius_ = kMinDistance;
    }

    // A gap's extents.
    if ((actions_ & kCommandWidenGapLeft) && is_gap_tool(get_cursor()))
        get_cursor()->change_gap(0, 1, 0);
    if ((actions_ & kCommandWidenGapRight) && is_gap_tool(get_cursor()))
        get_cursor()->change_gap(0, 0, 1);
    if ((actions_ & kCommandNarrowGapLeft) && is_gap_tool(get_cursor()))
        get_cursor()->change_gap(0, -1, 0);
    if ((actions_ & kCommandNarrowGapRight) && is_gap_tool(get_cursor()))
        get_cursor()->change_gap(0, 0, -1);

    // After a change to the park: the rails and the goals follow the floor.
    if ((actions_ & (kCommandPlace | kCommandRemove | kCommandRaise)) || (actions_ & kCommandLower)) {
        RailAuthoring* rails = get_rail_editor();
        rails->restore_heights();                        // the rails' points onto the floor again
        rails->draw_all_segments();                      // their segments updated
        rails->draw_all_posts();                         // their points updated
        get_goal_editor()->place_park_goals_on_ground(); // the park's goals placed again
    }

    get_cursor()->tick_frame(offset_cell_x, offset_cell_z, rotation90);
    if (get_cursor()->mode_sel_ == BuildPointer::kModeAreaSelect)
        get_cursor()->update_area_selection_highlight();
    else if (get_cursor()->mode_sel_ == BuildPointer::kModeRailPlacement)
        get_cursor()->destroy_cursor_meta();
    else
        get_cursor()->glow_overlapping_pieces(1);
}

// 0x0058F640
void ParkBuilderMode::update_menu_stick()
{
    modules::FrontEnd* front_end = modules::create_front_end(false); // (a single::LazyInstance<modules::FrontEnd>)
    front_end->set_analog_stick_active_for_menus(!(cur_phase_ == kStateEdit && !(flag_bits_ & kFlagPaused)));
    modules::release_front_end();
}

// 0x0058F6C0
void ParkBuilderMode::link_to_pad(int controller)
{
    pad_input::Hub* input = pad_input::create_manager(false); // (a single::LazyInstance<pad_input::Hub>)
    ParkEditorInputHandler* handler = editor_pad_listener_.get();
    handler->drop_out();
    pad_input::HostEnd& server = input->hosts_[controller];
    server.handlers_.queue_job(handler);
    handler->control_data_ = &server.content_;
    handler->dev_ = server.dev_;
    pad_input::release_manager();
}

// 0x0058F8A0 (reached from the logic task 0x0058FFC0)
void ParkBuilderMode::logic_update()
{
    if (!(flag_bits_ & kFlagPaused)) {
        if (cur_phase_ == kStateEdit) {
            script::Struct params;
            params.put_name_key(0u, kHide);
            commands::cfunc_pause_skaters(&params, nullptr);
            params.get_clear();
        } else if (cur_phase_ == kStateTestPlay) {
            commands::cfunc_un_pause_skaters(nullptr, nullptr);
        }
    }
    if (cur_phase_ != kStateEdit || !have_cursor()) {
        actions_ = 0;
        return;
    }
    if (flag_bits_ & kFlagPaused)
        return;
    // The rail editor's flags (+0x08): bit 0.
    auto rail_bit = []() { return (reinterpret_cast<uint8_t*>(get_rail_editor())[8] & 1) != 0; };
    if (get_cursor()->mode_sel_ == BuildPointer::kModeRailPlacement) {
        if (rail_bit())
            launch_script(kSwitchOnRailEditor);
        if (!(flag_bits_ & kFlagPaused))
            move_selection_cursor();
        if (get_cursor()->mode_sel_ != BuildPointer::kModeRailPlacement)
            launch_script(kSwitchOffRailEditor);
    } else {
        if (!rail_bit())
            launch_script(kSwitchOffRailEditor);
        tick_frame();
        if (get_cursor()->mode_sel_ == BuildPointer::kModeRailPlacement)
            launch_script(kSwitchOnRailEditor);
    }
    ParkLayout::update_percent_bar();
}

// 0x0058FA50
void ParkBuilderMode::change_phase(uint32_t state)
{
    if (state)
        cur_phase_ = state;
    actions_ = 0;
    if (have_cursor())
        get_cursor()->toggle_shown(cur_phase_ == kStateEdit);
    if (state == kStateTestPlay || state == kStateRegularPlay) {
        show_editor_view(false);
        gap_registry()->update_checklist();
    } else {
        show_editor_view(true);
    }
    modules::FrontEnd* front_end = modules::create_front_end(false); // (a single::LazyInstance<modules::FrontEnd>)
    if (state == kStateEdit) {
        reinterpret_cast<modules::Skate*>(skate_module())->tidy_before_park_build();
        front_end->hold_repeat_ms_ = 320;
        front_end->auto_repeat_rate_ = 100;
        hub()->highlight_gap_pieces(false, nullptr);
    } else {
        front_end->hold_repeat_ms_ = 320;
        front_end->auto_repeat_rate_ = 50;
    }
    update_menu_stick();
    modules::release_front_end();
}

} // namespace park_editing

